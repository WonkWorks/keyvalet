#!/usr/bin/env python3
"""Two-terminal KeyValet hardware acceptance-test harness."""

from __future__ import annotations

import argparse
import errno
import fcntl
import glob
import getpass
import hashlib
import json
import os
import selectors
import select
import signal
import socket
import stat
import sys
import termios
import time
import tty
import warnings
from pathlib import Path
from urllib.parse import urlsplit

from improv_serial import (Decoder, KEYVALET_CURRENT_NETWORK_RPC,
                           KEYVALET_STAGE_ENROLLMENT_KEY_RPC, command as improv_command)


COMMANDS = {
    "PULSE": b"KVTEST PULSE\n",
    "PULSE_LOCK_1": b"KVTEST PULSE LOCK_1\n",
    "PULSE_LOCK_2": b"KVTEST PULSE LOCK_2\n",
    "CHIRP": b"KVTEST CHIRP\n",
    "LED": b"KVTEST LED\n",
    "STATUS": b"KVTEST STATUS\n",
    "REBOOT": b"KVTEST REBOOT\n",
    "JOURNAL_RECENT": b"KVTEST JOURNAL RECENT\n",
    "JOURNAL_STATS": b"KVTEST JOURNAL STATS\n",
}

MENU_ACTIONS = {
    "1": "PULSE_LOCK_1",
    "2": "PULSE_LOCK_2",
    "3": "CHIRP",
    "4": "LED",
    "5": "STATUS",
    "6": "REBOOT",
    "7": "JOURNAL_RECENT",
    "8": "JOURNAL_STATS",
    "9": "WIFI_SETUP",
    "10": "ENROLLMENT_KEY",
}


class MonitorStopped(Exception):
    """Raised for an expected monitor shutdown."""


def default_socket_path() -> str:
    runtime_dir = os.environ.get("XDG_RUNTIME_DIR")
    if runtime_dir:
        return str(Path(runtime_dir) / "keyvalet-test.sock")
    return f"/tmp/keyvalet-{os.getuid()}-test.sock"


def discover_serial_port() -> str:
    stable = sorted(glob.glob("/dev/serial/by-id/*"))
    candidates = stable or sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    if not candidates:
        raise RuntimeError("no serial device found; pass --port explicitly")
    if len(candidates) > 1:
        joined = ", ".join(candidates)
        raise RuntimeError(f"multiple serial devices found ({joined}); pass --port explicitly")
    return candidates[0]


def open_serial(path: str) -> int:
    descriptor = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        fcntl.ioctl(descriptor, termios.TIOCEXCL)  # Do not share provisioning bytes with another terminal.
        tty.setraw(descriptor, termios.TCSANOW)
        attributes = termios.tcgetattr(descriptor)
        attributes[4] = termios.B115200
        attributes[5] = termios.B115200
        attributes[2] |= termios.CLOCAL | termios.CREAD
        termios.tcsetattr(descriptor, termios.TCSANOW, attributes)
        termios.tcflush(descriptor, termios.TCIOFLUSH)
    except Exception:
        os.close(descriptor)
        raise
    return descriptor


def prepare_server(socket_path: str) -> socket.socket:
    path = Path(socket_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() or path.is_socket():
        mode = path.lstat().st_mode
        if not stat.S_ISSOCK(mode):
            raise RuntimeError(f"refusing to replace non-socket path: {path}")
        probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            probe.connect(socket_path)
        except OSError as error:
            if error.errno not in (errno.ECONNREFUSED, errno.ENOENT):
                raise
            path.unlink(missing_ok=True)
        else:
            raise RuntimeError(f"a monitor is already running at {socket_path}")
        finally:
            probe.close()

    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(socket_path)
    os.chmod(socket_path, 0o600)
    server.listen(4)
    server.setblocking(False)
    return server


def write_serial(descriptor: int, data: bytes) -> None:
    view = memoryview(data)
    while view:
        try:
            written = os.write(descriptor, view)
        except BlockingIOError:
            _, writable, _ = select.select([], [descriptor], [], 1.0)
            if not writable:
                raise RuntimeError("serial write timed out")
            continue
        if written <= 0:
            raise RuntimeError("serial device disconnected during write")
        view = view[written:]


def close_client(selector: selectors.BaseSelector, client: socket.socket) -> None:
    try:
        selector.unregister(client)
    except (KeyError, ValueError):
        pass
    client.close()


def monitor(port: str | None, socket_path: str) -> int:
    serial_path = port or discover_serial_port()
    serial_descriptor: int | None = open_serial(serial_path)
    server: socket.socket | None = None
    selector = selectors.DefaultSelector()
    clients: dict[socket.socket, bytearray] = {}
    next_serial_attempt = 0.0
    decoder = Decoder()
    active_client = None
    active_command = 0
    active_deadline = 0.0

    def finish() -> None:
        nonlocal active_client, active_command
        if active_client is not None:
            try:
                active_client.sendall(b'{"done":true}\n')
            except OSError:
                pass
            close_client(selector, active_client)
            clients.pop(active_client, None)
        active_client = None
        active_command = 0

    def deliver(event: dict) -> None:
        if active_client is not None:
            try:
                active_client.sendall(json.dumps(event).encode() + b"\n")
            except OSError:
                pass

    def stop_handler(signum: int, frame: object) -> None:
        del signum, frame
        raise MonitorStopped

    previous_sigterm = signal.signal(signal.SIGTERM, stop_handler)
    try:
        server = prepare_server(socket_path)
        selector.register(serial_descriptor, selectors.EVENT_READ, "serial")
        selector.register(server, selectors.EVENT_READ, "server")
        print(f"KeyValet monitor: serial={serial_path} socket={socket_path}", flush=True)

        while True:
            if active_command and time.monotonic() >= active_deadline:
                deliver({"event": "failure", "message": "Device response timed out; no provisioning success confirmed"})
                finish()
            if serial_descriptor is None and time.monotonic() >= next_serial_attempt:
                try:
                    candidate = serial_path if port or Path(serial_path).exists() else discover_serial_port()
                    serial_descriptor = open_serial(candidate)
                    serial_path = candidate
                    selector.register(serial_descriptor, selectors.EVENT_READ, "serial")
                    print(f"KeyValet monitor: serial reconnected at {serial_path}", flush=True)
                except (OSError, RuntimeError) as error:
                    print(f"KeyValet monitor: waiting for serial device: {error}",
                          file=sys.stderr, flush=True)
                    next_serial_attempt = time.monotonic() + 1.0

            for key, _ in selector.select(timeout=1.0):
                if key.data == "serial":
                    try:
                        data = os.read(serial_descriptor, 4096)
                    except OSError as error:
                        if error.errno in (errno.EIO, errno.ENODEV, errno.EBADF):
                            data = b""
                        else:
                            raise
                    if not data:
                        if active_command:
                            deliver({"event": "failure", "message": "USB disconnected; query state after reconnect; the device may finish independently"})
                            finish()
                        print("KeyValet monitor: serial disconnected; attempting reconnect",
                              file=sys.stderr, flush=True)
                        try:
                            selector.unregister(serial_descriptor)
                        except (KeyError, ValueError):
                            pass
                        os.close(serial_descriptor)
                        serial_descriptor = None
                        next_serial_attempt = 0.0
                        break
                    events, logs = decoder.feed(data)
                    sys.stdout.buffer.write(logs)
                    sys.stdout.buffer.flush()
                    for event in events:
                        if not active_command:
                            continue
                        deliver(event)
                        if event["event"] == "error" and event["value"]:
                            finish()
                        elif event["event"] == "state" and active_command == 2 and event["value"] != 4:
                            finish()
                        elif event["event"] == "result" and event["command"] == active_command:
                            if active_command != 4 or not event["values"]:
                                finish()
                    continue

                if key.data == "server":
                    client, _ = server.accept()
                    client.setblocking(False)
                    clients[client] = bytearray()
                    selector.register(client, selectors.EVENT_READ, "client")
                    continue

                client = key.fileobj
                assert isinstance(client, socket.socket)
                try:
                    request = client.recv(128)
                    if not request:
                        if client is active_client:
                            active_client = None  # Keep the RPC busy until its result/timeout.
                        close_client(selector, client)
                        clients.pop(client, None)
                        continue
                    buffer = clients[client]
                    buffer.extend(request)
                    if len(buffer) > 2048:
                        client.sendall(b"ERR request too long\n")
                        close_client(selector, client)
                        clients.pop(client, None)
                        continue
                    if b"\n" not in buffer:
                        continue
                    token = bytes(buffer).split(b"\n", 1)[0].decode("utf-8", "strict").strip()
                    if token.startswith("{"):
                        if active_command:
                            client.sendall(b'{"event":"failure","message":"Another USB operation is active"}\n')
                        elif serial_descriptor is None:
                            client.sendall(b'{"event":"failure","message":"USB disconnected"}\n')
                        else:
                            try:
                                rpc = json.loads(token)
                                number = rpc.get("improv")
                                allowed = ({"improv", "ssid", "password"} if number == 1 else
                                           {"improv", "public_key"} if number == KEYVALET_STAGE_ENROLLMENT_KEY_RPC else {"improv"})
                                if set(rpc) != allowed:
                                    raise ValueError("Invalid request fields")
                                wire = improv_command(number, rpc.get("ssid", ""), rpc.get("password", ""),
                                                      public_key=rpc.get("public_key", ""))
                            except (TypeError, ValueError, UnicodeError, AttributeError):
                                client.sendall(b'{"event":"failure","message":"Invalid provisioning request"}\n')
                            else:
                                active_client, active_command = client, number
                                active_deadline = time.monotonic() + (45 if number == 1 else 18 if number == 4 else 5)
                                write_serial(serial_descriptor, wire)
                                clients[client].clear()
                                del rpc, wire, token
                                continue
                        close_client(selector, client)
                        clients.pop(client, None)
                        continue
                    command = COMMANDS.get(token)
                    if command is None:
                        client.sendall(b"ERR unknown controller command\n")
                    elif active_command:
                        client.sendall(b"ERR Wi-Fi operation active\n")
                    elif serial_descriptor is None:
                        client.sendall(b"ERR serial device disconnected\n")
                    else:
                        write_serial(serial_descriptor, command)
                        client.sendall(f"OK sent {token}\n".encode("ascii"))
                except (OSError, UnicodeError, RuntimeError):
                    if client is active_client:
                        deliver({"event": "failure", "message": "USB/IPC write failed; no device success confirmed"})
                        finish()
                        continue
                    try:
                        client.sendall(b"ERR USB/IPC operation failed\n")
                    except OSError:
                        pass
                close_client(selector, client)
                clients.pop(client, None)
                if client is active_client:
                    active_client = None
                    active_command = 0
    except (KeyboardInterrupt, MonitorStopped):
        print("\nKeyValet monitor stopped.", flush=True)
        return 0
    except (OSError, RuntimeError) as error:
        print(f"KeyValet monitor error: {error}", file=sys.stderr, flush=True)
        return 2
    finally:
        signal.signal(signal.SIGTERM, previous_sigterm)
        for client in list(clients):
            close_client(selector, client)
        if server is not None:
            try:
                selector.unregister(server)
            except (KeyError, ValueError):
                pass
            server.close()
        if serial_descriptor is not None:
            try:
                selector.unregister(serial_descriptor)
            except (KeyError, ValueError):
                pass
        selector.close()
        if serial_descriptor is not None:
            os.close(serial_descriptor)
        try:
            path = Path(socket_path)
            if path.is_socket():
                path.unlink()
        except OSError:
            pass


def send_controller_command(socket_path: str, command: str) -> str:
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(2.0)
    try:
        client.connect(socket_path)
        client.sendall(f"{command}\n".encode("ascii"))
        response = client.recv(512)
        if not response:
            raise RuntimeError("monitor closed the IPC connection without a response")
        return response.decode("utf-8", "replace").strip()
    finally:
        client.close()


def control(socket_path: str) -> int:
    while True:
        print(
            "\nKEYVALET HARDWARE TEST\n\n"
            "1  Pulse lock_1 (350 ms)\n"
            "2  Pulse lock_2 (350 ms)\n"
            "3  Play test chirp\n"
            "4  Toggle status LED\n"
            "5  Show all sensor/lock states\n"
            "6  Reboot KeyValet\n"
            "7  Show recent journal events\n"
            "8  Show journal statistics\n"
            "9  Wi-Fi Setup (production Improv Serial)\n"
            "10 Stage first-enrollment PUBLIC key (protected USB)\n\n"
            "Q  Quit"
        )
        try:
            selection = input("> ").strip().upper()
        except (EOFError, KeyboardInterrupt):
            print()
            return 0
        if selection == "Q":
            return 0
        command = MENU_ACTIONS.get(selection)
        if command is None:
            print("Unknown selection.")
            continue
        if command in ("WIFI_SETUP", "ENROLLMENT_KEY"):
            try:
                if command == "WIFI_SETUP":
                    wifi_setup(socket_path)
                else:
                    stage_enrollment_key(socket_path)
            except (OSError, RuntimeError, ValueError, EOFError, KeyboardInterrupt) as error:
                print(f"USB setup stopped: {error}")
            continue
        if command == "REBOOT":
            confirmation = input("Type REBOOT to confirm software restart: ").strip()
            if confirmation != "REBOOT":
                print("Reboot cancelled.")
                continue
        try:
            print(send_controller_command(socket_path, command))
        except (OSError, RuntimeError) as error:
            print(f"Controller error: {error}", file=sys.stderr)


def improv_request(socket_path: str, number: int, **credentials) -> list[dict]:
    """Wait for actual device frames, not MONITOR's successful serial write."""
    improv_command(number, credentials.get("ssid", ""), credentials.get("password", ""),
                   public_key=credentials.get("public_key", ""))
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(50)
    events = []
    try:
        client.connect(socket_path)
        client.sendall(json.dumps({"improv": number, **credentials}).encode() + b"\n")
        with client.makefile("rb") as stream:
            while True:
                line = stream.readline(4097)
                if not line or len(line) > 4096:
                    raise RuntimeError("MONITOR ended the operation without a device result")
                event = json.loads(line)
                if event.get("event") == "failure":
                    raise RuntimeError(event["message"])
                if event.get("event") == "error" and event["value"]:
                    errors = {1: "Malformed request or device busy", 2: "Unsupported command",
                              3: "Connection/scan failed or timed out; previous credentials retained",
                              255: "Device internal/persistence failure; durable success NOT confirmed; see MONITOR"}
                    raise RuntimeError(errors.get(event["value"], "Device rejected provisioning"))
                if event.get("event") == "state" and event["value"] == 3:
                    print("Device is attempting Wi-Fi association and DHCP…")
                if event.get("done"):
                    return events
                events.append(event)
    finally:
        client.close()


def stage_enrollment_key(socket_path: str) -> None:
    public_key = input("Building operational PUBLIC key (130 lowercase hex characters): ").strip()
    events = improv_request(socket_path, KEYVALET_STAGE_ENROLLMENT_KEY_RPC, public_key=public_key)
    result = next((event["values"] for event in events if event.get("event") == "result" and
                   event.get("command") == KEYVALET_STAGE_ENROLLMENT_KEY_RPC), [])
    expected = hashlib.sha256(bytes.fromhex(public_key)).hexdigest()
    if (len(result) != 3 or result[:2] != ["STAGED", expected] or
            not result[2].isdecimal() or not 1 <= int(result[2]) <= 300):
        raise RuntimeError("Device did not confirm the intended public-key staging")
    print(f"Public key staged ({result[2]}s remaining); SHA-256={expected}")
    print("Not enrolled yet. Use the five-second physical button hold and complete ENROLL-V1 in the browser.")


def wifi_setup(socket_path: str) -> None:
    identity = next((e["values"] for e in improv_request(socket_path, 3) if e.get("event") == "result"), [])
    if len(identity) < 4 or identity[0] != "KeyValet" or identity[2] != "ESP32-C6":
        raise RuntimeError("The USB device did not identify as ESP32-C6 KeyValet")
    print(f"{identity[3]} — {identity[0]} {identity[1]}")
    network = next((e["values"] for e in improv_request(socket_path, KEYVALET_CURRENT_NETWORK_RPC)
                    if e.get("event") == "result" and e.get("command") == KEYVALET_CURRENT_NETWORK_RPC), [])
    if len(network) != 2 or network[0] not in (
            "connected", "unconfigured", "disconnected", "provisioning", "unavailable") or (
            bool(network[1]) != (network[0] == "connected")):
        raise RuntimeError("Device returned invalid current-network information")
    print("Current network: " + (network[1] if network[0] == "connected" else network[0]))
    print("Scanning nearby 2.4 GHz networks…")
    rows = [e["values"] for e in improv_request(socket_path, 4)
            if e.get("event") == "result" and e["values"]]
    rows.sort(key=lambda row: (-int(row[1]), row[0], row[3] if len(row) > 3 else row[2]))
    for index, row in enumerate(rows, 1):
        auth = row[3] if len(row) > 3 else "secured" if row[2] == "YES" else "open"
        print(f"{index:2}  {row[0]}  {row[1]} dBm  {auth}")
    selection = input("Network number, M for manual/hidden SSID, or Q to cancel: ").strip()
    if selection.upper() == "Q":
        return
    if selection.upper() == "M":
        ssid = input("SSID: ")  # Spaces are meaningful; do not strip the name.
    elif selection.isdecimal() and 1 <= int(selection) <= len(rows):
        ssid = rows[int(selection) - 1][0]
    else:
        raise ValueError("Invalid network selection")
    if not sys.stdin.isatty():
        raise RuntimeError("Password entry requires an interactive terminal (no echo fallback)")
    with warnings.catch_warnings():
        warnings.simplefilter("error", getpass.GetPassWarning)
        try:
            password = getpass.getpass("Password (empty for an open network): ")
        except getpass.GetPassWarning:
            raise RuntimeError("Secure password entry unavailable") from None
    try:
        events = improv_request(socket_path, 1, ssid=ssid, password=password)
    finally:
        del password
    result = next((e["values"] for e in events if e.get("event") == "result" and e.get("command") == 1), [])
    if not result or not result[0] or not any(e.get("event") == "state" and e.get("value") == 4 for e in events):
        raise RuntimeError("Device did not confirm durable provisioning success")
    print(f"Wi-Fi saved successfully: SSID={ssid}; IP={urlsplit(result[0]).hostname}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="mode", required=True)

    monitor_parser = subparsers.add_parser("monitor", help="own and display the USB serial stream")
    monitor_parser.add_argument("--port", help="serial path; prefer /dev/serial/by-id/... when available")
    monitor_parser.add_argument("--socket", default=default_socket_path(), help="Unix IPC socket path")

    control_parser = subparsers.add_parser("control", help="open the interactive controller menu")
    control_parser.add_argument("--socket", default=default_socket_path(), help="Unix IPC socket path")
    command_parser = subparsers.add_parser("command", help="send one allowlisted diagnostic command through MONITOR")
    command_parser.add_argument("action", choices=sorted(COMMANDS))
    command_parser.add_argument("--socket", default=default_socket_path(), help="Unix IPC socket path")
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    if arguments.mode == "monitor":
        try:
            return monitor(arguments.port, arguments.socket)
        except (OSError, RuntimeError) as error:
            print(f"KeyValet monitor error: {error}", file=sys.stderr)
            return 2
    if arguments.mode == "command":
        print(send_controller_command(arguments.socket, arguments.action))
        return 0
    return control(arguments.socket)


if __name__ == "__main__":
    raise SystemExit(main())
