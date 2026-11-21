#!/usr/bin/env python3
"""Read-only KeyValet LAN WebSocket acceptance client."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import select
import socket
import ssl
import struct
import time
from urllib.parse import urlparse


WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
PING_INTERVAL_SECONDS = 30.0
PONG_TIMEOUT_SECONDS = 10.0


def _recv_exact(connection: socket.socket, length: int) -> bytes:
    result = bytearray()
    while len(result) < length:
        chunk = connection.recv(length - len(result))
        if not chunk:
            raise ConnectionError("WebSocket connection closed")
        result.extend(chunk)
    return bytes(result)


def _expected_accept(key: str) -> str:
    digest = hashlib.sha1((key + WEBSOCKET_GUID).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


def _open_websocket(url: str, origin: str = "http://localhost:8765", cafile: str | None = None) -> socket.socket:
    parsed = urlparse(url)
    if parsed.scheme != "wss" or not parsed.hostname or parsed.username or parsed.password:
        raise ValueError("URL must use wss:// and include the commissioned hostname")
    if not origin or any(c in origin for c in "\r\n"):
        raise ValueError("An explicitly provisioned Origin is required")
    port = parsed.port or 443
    path = parsed.path or "/"
    if parsed.query:
        path = f"{path}?{parsed.query}"

    raw = socket.create_connection((parsed.hostname, port), timeout=10)
    try:
        connection = ssl.create_default_context(cafile=cafile).wrap_socket(raw, server_hostname=parsed.hostname)
    except Exception:
        raw.close()
        raise
    # select() governs idle liveness. A socket timeout here applies only after
    # a frame has begun, so an incomplete frame becomes a real failure rather
    # than being mistaken for an idle connection.
    connection.settimeout(PONG_TIMEOUT_SECONDS)
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    host = parsed.hostname if port == 443 else f"{parsed.hostname}:{port}"
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        f"Origin: {origin}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    connection.sendall(request.encode("ascii"))

    response = bytearray()
    while not response.endswith(b"\r\n\r\n"):
        response.extend(_recv_exact(connection, 1))
        if len(response) > 8192:
            connection.close()
            raise ConnectionError("WebSocket handshake response was too large")
    header_text = response.decode("iso-8859-1")
    lines = header_text.split("\r\n")
    if not lines[0].startswith("HTTP/1.1 101"):
        connection.close()
        raise ConnectionError(f"WebSocket handshake rejected: {lines[0]}")
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            name, value = line.split(":", 1)
            headers[name.strip().lower()] = value.strip()
    if headers.get("sec-websocket-accept") != _expected_accept(key):
        connection.close()
        raise ConnectionError("WebSocket handshake accept value was invalid")
    return connection


def _send_control_frame(connection: socket.socket, opcode: int, payload: bytes = b"") -> None:
    if len(payload) > 125:
        raise ValueError("control-frame payload exceeds 125 bytes")
    mask = os.urandom(4)
    masked = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    connection.sendall(bytes((0x80 | opcode, 0x80 | len(payload))) + mask + masked)


def _read_frame(connection: socket.socket) -> tuple[int, bytes]:
    first, second = _recv_exact(connection, 2)
    final = bool(first & 0x80)
    opcode = first & 0x0F
    masked = bool(second & 0x80)
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", _recv_exact(connection, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", _recv_exact(connection, 8))[0]
    if length > 65536:
        raise ConnectionError("WebSocket frame exceeds acceptance-client limit")
    mask = _recv_exact(connection, 4) if masked else b""
    payload = _recv_exact(connection, length)
    if masked:
        payload = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    if not final:
        raise ConnectionError("fragmented WebSocket frames are not supported by this tool")
    return opcode, payload


def receive_messages(
    connection: socket.socket,
    *,
    ping_interval: float = PING_INTERVAL_SECONDS,
    pong_timeout: float = PONG_TIMEOUT_SECONDS,
    select_fn=select.select,
    clock=time.monotonic,
    nonce_factory=lambda: os.urandom(8),
) -> None:
    pending_ping = None
    pong_deadline = None
    next_ping = clock() + ping_interval
    while True:
        now = clock()
        deadline = pong_deadline if pending_ping is not None else next_ping
        if getattr(connection, "pending", lambda: 0)() > 0:
            readable = [connection]  # TLS may already hold decrypted frame bytes.
        else:
            readable, _, _ = select_fn(
                [connection], [], [], max(0.0, deadline - now)
            )
        if not readable:
            now = clock()
            if pending_ping is not None:
                if now >= pong_deadline:
                    raise ConnectionError("WebSocket PONG timed out")
                continue
            if now >= next_ping:
                pending_ping = nonce_factory()
                if not pending_ping or len(pending_ping) > 125:
                    raise ValueError("PING nonce must contain 1 to 125 bytes")
                _send_control_frame(connection, 0x9, pending_ping)
                pong_deadline = now + pong_timeout
            continue

        opcode, payload = _read_frame(connection)
        if opcode == 0x1:
            text = payload.decode("utf-8")
            try:
                print(json.dumps(json.loads(text), indent=2, sort_keys=True), flush=True)
            except json.JSONDecodeError:
                print(text, flush=True)
        elif opcode == 0x8:
            _send_control_frame(connection, 0x8, payload[:125])
            raise ConnectionError("server sent a close frame")
        elif opcode == 0x9:
            _send_control_frame(connection, 0xA, payload)
        elif opcode == 0xA:
            if pending_ping is not None and payload == pending_ping:
                pending_ping = None
                pong_deadline = None
                next_ping = clock() + ping_interval
            continue
        else:
            print(f"Ignoring WebSocket opcode {opcode}", flush=True)


def run(url: str, reconnect_delay: float, once: bool, origin: str = "http://localhost:8765", cafile: str | None = None) -> int:
    while True:
        connection = None
        try:
            print(f"Connecting read-only client to {url}", flush=True)
            connection = _open_websocket(url, origin, cafile)
            print("WebSocket connected", flush=True)
            receive_messages(connection)
        except KeyboardInterrupt:
            print("\nClient stopped.", flush=True)
            return 0
        except (ConnectionError, OSError, ValueError, UnicodeError) as error:
            print(f"WebSocket disconnected: {error}", flush=True)
            if once:
                return 2
        finally:
            if connection is not None:
                connection.close()
        print(f"Reconnecting in {reconnect_delay:g} seconds", flush=True)
        time.sleep(reconnect_delay)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="wss://COMMISSIONED-HOSTNAME/api/v1/ws")
    parser.add_argument("--origin", default="http://localhost:8765", help="must be explicitly provisioned on KeyValet")
    parser.add_argument("--cafile", help="explicit test trust anchor; hostname verification stays enabled")
    parser.add_argument("--reconnect-delay", type=float, default=3.0)
    parser.add_argument("--once", action="store_true", help="exit after the first disconnect")
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    if arguments.reconnect_delay < 0:
        raise SystemExit("--reconnect-delay must not be negative")
    return run(arguments.url, arguments.reconnect_delay, arguments.once, arguments.origin, arguments.cafile)


if __name__ == "__main__":
    raise SystemExit(main())
