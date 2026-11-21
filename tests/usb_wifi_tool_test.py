#!/usr/bin/env python3
"""Real MONITOR IPC/Improv integration over a private pseudo-terminal, NOT a PCB."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import pty
import select
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import improv_serial as protocol
import kv_test

PUBLIC_KEY = ("04" "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
              "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5")
FINGERPRINT = hashlib.sha256(bytes.fromhex(PUBLIC_KEY)).hexdigest()


def rpc_result(number, strings):
    data = b"".join(bytes((len(s.encode()),)) + s.encode() for s in strings)
    return protocol.frame(4, bytes((number, len(data))) + data)


def codec_checks():
    query = protocol.command(3)
    assert query == b"IMPROV\x01\x03\x02\x03\x00" + bytes((sum(query[:-2]) & 255,)) + b"\n"
    decoder = protocol.Decoder()
    valid = rpc_result(3, ["KeyValet", "test", "ESP32-C6", "keyvalet-test"])
    events = []
    for byte in b"boot\n" + valid:
        parsed, _ = decoder.feed(bytes((byte,)), now=0)
        events += parsed
    assert events[-1]["values"][0] == "KeyValet"
    echo = protocol.command(1, "hidden", "private-password")
    events, logs = decoder.feed(echo, now=0)
    assert not events and b"private-password" not in logs
    damaged = bytearray(echo); damaged[-2] ^= 1
    events, logs = decoder.feed(damaged + valid, now=0)
    assert len(events) == 1 and b"private-password" not in logs
    events, logs = decoder.feed(b"IMPROV\x01\x04\xffpartial" + valid, now=0)
    assert len(events) == 1 and b"partial" not in logs
    for ssid, password in (("", ""), ("s" * 33, ""), ("ok", "short"),
                           ("bad\x1b", ""), ("ok", "p" * 65), ("ok", "x" * 64)):
        try:
            protocol.command(1, ssid, password)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid credentials accepted")
    protocol.command(1, "s" * 32, "a" * 64)
    staged = protocol.command(protocol.KEYVALET_STAGE_ENROLLMENT_KEY_RPC, public_key=PUBLIC_KEY)
    assert staged[9:12] == bytes((129, 131, 130)) and staged[12:142] == PUBLIC_KEY.encode()
    for key in ("04", "02" + "a" * 128, PUBLIC_KEY.upper(), "private-key", None):
        try:
            protocol.command(129, public_key=key)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid public-key encoding accepted")


def integration():
    master, slave = pty.openpty()
    port = os.ttyname(slave)
    failures, received = [], []
    stop = threading.Event()

    def device():
        pending = bytearray()
        try:
            while not stop.is_set():
                if not select.select([master], [], [], .05)[0]:
                    continue
                pending.extend(os.read(master, 4096))
                while True:
                    start = pending.find(b"IMPROV")
                    if start < 0 or len(pending) < start + 9:
                        break
                    size = pending[start + 8] + 10
                    if len(pending) < start + size:
                        break
                    packet = bytes(pending[start:start + size]); del pending[:start + size]
                    assert sum(packet[:-1]) & 255 == packet[-1]
                    number = packet[9]; received.append(number)
                    if number == 3:
                        reply = rpc_result(3, ["KeyValet", "test", "ESP32-C6", "keyvalet-test"])
                    elif number == 7:
                        reply = rpc_result(7, ["3", "http://192.168.1.27/api/v1/status"])
                    elif number == protocol.KEYVALET_CURRENT_NETWORK_RPC:
                        reply = rpc_result(number, ["connected", "example-network"])
                    elif number == protocol.KEYVALET_STAGE_ENROLLMENT_KEY_RPC:
                        assert packet[12:142] == PUBLIC_KEY.encode()
                        reply = rpc_result(number, ["STAGED", FINGERPRINT, "300"])
                    elif number == 4:
                        reply = rpc_result(4, ["nearby", "-40", "YES", "WPA2-Personal"]) + b"radio log\n" + rpc_result(4, [])
                    elif number == 1:
                        assert b"private-password" in packet
                        # Even an accidentally echoed credential frame must never print.
                        if b"bad-network" in packet:
                            reply = packet + protocol.frame(1, b"\x03") + protocol.frame(2, b"\x03")
                        elif b"storage-failure" in packet:
                            reply = protocol.frame(2, b"\xff")
                        else:
                            reply = packet + protocol.frame(1, b"\x03") + protocol.frame(1, b"\x04") + rpc_result(1, ["http://192.168.1.42/api/v1/status"])
                    else:
                        raise AssertionError("unexpected command")
                    os.write(master, b"device log\n" + reply[:7]);time.sleep(.005);os.write(master, reply[7:])
        except OSError:
            if not stop.is_set():
                failures.append("pseudo-terminal I/O failed")
        except BaseException as error:
            failures.append(type(error).__name__)

    with tempfile.TemporaryDirectory(prefix="keyvalet-usb-wifi-") as directory:
        ipc = str(Path(directory) / "monitor.sock")
        process = subprocess.Popen([sys.executable, "-B", str(ROOT / "tools/kv_test.py"),
                                    "monitor", "--port", port, "--socket", ipc],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        thread = threading.Thread(target=device);thread.start()
        try:
            deadline = time.monotonic() + 5
            while not Path(ipc).exists():
                assert process.poll() is None, process.stderr.read().decode()
                if time.monotonic() > deadline:
                    raise AssertionError("monitor did not start")
                time.sleep(.01)
            identity = kv_test.improv_request(ipc, 3)
            assert any(e.get("values", [None])[0] == "KeyValet" for e in identity if e["event"] == "result")
            assert kv_test.improv_request(ipc, 7)[0]["values"] == ["3", "http://192.168.1.27/api/v1/status"]
            # A fresh client has no remembered SSID; the device supplies it.
            assert kv_test.improv_request(ipc, protocol.KEYVALET_CURRENT_NETWORK_RPC)[0]["values"] == ["connected", "example-network"]
            assert kv_test.improv_request(ipc, 129, public_key=PUBLIC_KEY)[0]["values"] == ["STAGED", FINGERPRINT, "300"]
            rows = kv_test.improv_request(ipc, 4)
            assert rows[-1]["values"] == [] and rows[0]["values"][0] == "nearby"
            result = kv_test.improv_request(ipc, 1, ssid="hidden", password="private-password")
            assert result[-1]["values"][0].startswith("http://192.168.1.42")
            for name in ("bad-network", "storage-failure"):
                try:
                    kv_test.improv_request(ipc,1,ssid=name,password="private-password")
                except RuntimeError as error:
                    assert "private-password" not in str(error)
                else:
                    raise AssertionError("device failure presented as success")
            # Invalid secret-bearing IPC requests get a generic error, never an echo.
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.connect(ipc);client.sendall(b'{"improv":1,"password":"private-password"}\n')
                assert b"private-password" not in client.recv(4096)
        finally:
            process.send_signal(signal.SIGTERM)
            stdout, stderr = process.communicate(timeout=5)
            stop.set();thread.join(timeout=5)
            os.close(master);os.close(slave)
        assert process.returncode == 0 and not failures
        assert b"private-password" not in stdout + stderr
        assert b"radio log" in stdout and received == [3, 7, 128, 129, 4, 1, 1, 1]


def control_checks():
    for fingerprint, accepted in ((FINGERPRINT, True), ("0" * 64, False)):
        output = io.StringIO()
        with patch.object(kv_test, "improv_request", return_value=[{"event": "result", "command": 129,
                "values": ["STAGED", fingerprint, "300"]}]) as request_mock, \
             patch("builtins.input", return_value=PUBLIC_KEY), contextlib.redirect_stdout(output):
            try:
                kv_test.stage_enrollment_key("unused")
            except RuntimeError:
                assert not accepted
            else:
                assert accepted and "Not enrolled yet" in output.getvalue()
            request_mock.assert_called_once_with("unused", 129, public_key=PUBLIC_KEY)
    def request(path, number, **credentials):
        if number == 3:
            return [{"event": "result", "values": ["KeyValet", "test", "ESP32-C6", "keyvalet-test"]}]
        if number == protocol.KEYVALET_CURRENT_NETWORK_RPC:
            return [{"event": "result", "command": number, "values": ["unconfigured", ""]}]
        if number == 4:
            return [{"event": "result", "values": ["nearby", "-40", "YES", "WPA2-Personal"]}]
        assert credentials == {"ssid": "hidden", "password": "private-password"}
        return [{"event": "state", "value": 4}, {"event": "result", "command": 1,
                "values": ["http://192.168.1.42/api/v1/status"]}]
    output = io.StringIO()
    with patch.object(kv_test, "improv_request", side_effect=request), \
         patch("builtins.input", side_effect=["M", "hidden"]), \
         patch.object(kv_test.sys.stdin, "isatty", return_value=True), \
         patch.object(kv_test.getpass, "getpass", return_value="private-password"), \
         contextlib.redirect_stdout(output):
        kv_test.wifi_setup("unused")
    assert "IP=192.168.1.42" in output.getvalue() and "private-password" not in output.getvalue()

    # Each invocation is an independent session, including after simulated reboot.
    for state, ssid in (("connected", "example-network"), ("disconnected", ""),
                        ("unconfigured", ""), ("provisioning", ""), ("unavailable", ""),
                        ("connected", "After reboot"), ("connected", "Réseau")):
        def current_request(path, number, **credentials):
            if number == protocol.KEYVALET_CURRENT_NETWORK_RPC:
                return [{"event": "result", "command": number, "values": [state, ssid]}]
            return request(path, number, **credentials)
        output = io.StringIO()
        with patch.object(kv_test, "improv_request", side_effect=current_request), \
             patch("builtins.input", return_value="Q"), contextlib.redirect_stdout(output):
            kv_test.wifi_setup("new-client")
        rendered = output.getvalue()
        assert f"Current network: {ssid or state}\n" in rendered
        assert "http://" not in rendered and "private-password" not in rendered

    for values in (["connected", ""], ["disconnected", "stale"], ["connected"],
                   ["unknown", ""], []):
        def invalid_request(path, number, **credentials):
            if number == protocol.KEYVALET_CURRENT_NETWORK_RPC:
                return [{"event": "result", "command": number, "values": values}]
            return request(path, number, **credentials)
        with patch.object(kv_test, "improv_request", side_effect=invalid_request), \
             contextlib.redirect_stdout(io.StringIO()):
            try:
                kv_test.wifi_setup("unused")
            except RuntimeError:
                pass
            else:
                raise AssertionError("invalid device current-network result accepted")


if __name__ == "__main__":
    codec_checks();integration();control_checks()
    print("USB Wi-Fi tool tests passed: real MONITOR/IPC over private PTY, standard frames, scan, durable-result handling, secret suppression, hidden setup")
