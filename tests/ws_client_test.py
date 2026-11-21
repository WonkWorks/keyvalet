#!/usr/bin/env python3

import sys
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest.mock import patch
import ssl

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import ws_client


class FakeConnection:
    def __init__(self, data: bytes):
        self.data = bytearray(data)
        self.sent = bytearray()
        self.closed = False

    def settimeout(self, timeout):
        self.timeout = timeout

    def close(self):
        self.closed = True

    def recv(self, length: int) -> bytes:
        result = bytes(self.data[:length])
        del self.data[:length]
        return result

    def sendall(self, data: bytes) -> None:
        self.sent.extend(data)


class FakeClock:
    def __init__(self) -> None:
        self.now = 0.0

    def __call__(self) -> float:
        return self.now


class ScriptedSelect:
    def __init__(self, clock: FakeClock, actions: list[str]):
        self.clock = clock
        self.actions = iter(actions)

    def __call__(self, readers, writers, errors, timeout):
        del writers, errors
        action = next(self.actions)
        if action == "timeout":
            self.clock.now += timeout
            return [], [], []
        return readers, [], []


def server_frame(opcode: int, payload: bytes = b"") -> bytes:
    assert len(payload) <= 125
    return bytes((0x80 | opcode, len(payload))) + payload


def decode_client_control_frame(frame: bytes) -> tuple[int, bytes]:
    opcode = frame[0] & 0x0F
    length = frame[1] & 0x7F
    assert frame[1] & 0x80
    mask = frame[2:6]
    payload = bytes(
        value ^ mask[index % 4]
        for index, value in enumerate(frame[6:6 + length])
    )
    return opcode, payload


def main() -> None:
    nonce = b"0123456789abcdef"
    key = ws_client.base64.b64encode(nonce).decode()
    response = ("HTTP/1.1 101 Switching Protocols\r\nSec-WebSocket-Accept: " +
                ws_client._expected_accept(key) + "\r\n\r\n").encode()
    raw = FakeConnection(b"")
    secure = FakeConnection(response)
    with patch.object(ws_client.socket, "create_connection", return_value=raw) as connect, \
            patch.object(ws_client.ssl, "create_default_context") as context, \
            patch.object(ws_client.os, "urandom", return_value=nonce):
        context.return_value.wrap_socket.return_value = secure
        assert ws_client._open_websocket("wss://kv.example.com/api/v1/ws", "https://staff.example.com") is secure
        connect.assert_called_once_with(("kv.example.com", 443), timeout=10)
        context.assert_called_once_with(cafile=None)
        context.return_value.wrap_socket.assert_called_once_with(raw, server_hostname="kv.example.com")
        assert b"Origin: https://staff.example.com\r\n" in secure.sent
    with patch.object(ws_client.socket, "create_connection", return_value=raw), \
            patch.object(ws_client.ssl, "create_default_context") as context:
        context.return_value.wrap_socket.side_effect = ssl.SSLCertVerificationError("untrusted")
        try:
            ws_client._open_websocket("wss://kv.example.com/api/v1/ws")
        except ssl.SSLCertVerificationError:
            assert raw.closed
        else:
            raise AssertionError("TLS verification failure was bypassed")
    try:
        ws_client._open_websocket("ws://kv.example.com/api/v1/ws")
    except ValueError:
        pass
    else:
        raise AssertionError("cleartext transport accepted")
    assert ws_client._expected_accept("dGhlIHNhbXBsZSBub25jZQ==") == (
        "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
    )

    payload = b'{"type":"welcome"}'
    opcode, decoded = ws_client._read_frame(
        FakeConnection(bytes((0x81, len(payload))) + payload)
    )
    assert opcode == 0x1
    assert decoded == payload

    extended = b"x" * 130
    opcode, decoded = ws_client._read_frame(
        FakeConnection(
            bytes((0x82, 126)) + len(extended).to_bytes(2, "big") + extended
        )
    )
    assert opcode == 0x2
    assert decoded == extended

    nonce = b"ping-001"
    clock = FakeClock()
    connection = FakeConnection(server_frame(0xA, nonce))
    try:
        ws_client.receive_messages(
            connection,
            ping_interval=30,
            pong_timeout=10,
            select_fn=ScriptedSelect(clock, ["timeout", "readable", "readable"]),
            clock=clock,
            nonce_factory=lambda: nonce,
        )
    except ConnectionError as error:
        assert str(error) == "WebSocket connection closed"
    else:
        raise AssertionError("EOF did not end the WebSocket connection")
    assert decode_client_control_frame(connection.sent) == (0x9, nonce)
    assert clock.now == 30

    clock = FakeClock()
    connection = FakeConnection(b"")
    try:
        ws_client.receive_messages(
            connection,
            ping_interval=30,
            pong_timeout=10,
            select_fn=ScriptedSelect(clock, ["timeout", "timeout"]),
            clock=clock,
            nonce_factory=lambda: nonce,
        )
    except ConnectionError as error:
        assert str(error) == "WebSocket PONG timed out"
    else:
        raise AssertionError("missing PONG did not fail the connection")
    assert clock.now == 40

    clock = FakeClock()
    connection = FakeConnection(server_frame(0xA, b"wrong-pong"))
    try:
        ws_client.receive_messages(
            connection,
            ping_interval=30,
            pong_timeout=10,
            select_fn=ScriptedSelect(
                clock, ["timeout", "readable", "timeout"]
            ),
            clock=clock,
            nonce_factory=lambda: nonce,
        )
    except ConnectionError as error:
        assert str(error) == "WebSocket PONG timed out"
    else:
        raise AssertionError("mismatched PONG satisfied the heartbeat")

    connection = FakeConnection(server_frame(0x9, b"server-ping"))
    try:
        ws_client.receive_messages(
            connection,
            select_fn=lambda readers, _w, _e, _t: (readers, [], []),
        )
    except ConnectionError as error:
        assert str(error) == "WebSocket connection closed"
    assert decode_client_control_frame(connection.sent) == (0xA, b"server-ping")

    connection = FakeConnection(server_frame(0x8))
    try:
        ws_client.receive_messages(
            connection,
            select_fn=lambda readers, _w, _e, _t: (readers, [], []),
        )
    except ConnectionError as error:
        assert str(error) == "server sent a close frame"
    else:
        raise AssertionError("CLOSE did not end the WebSocket connection")
    assert decode_client_control_frame(connection.sent) == (0x8, b"")

    event = b'{"type":"event","sequence":44}'
    connection = FakeConnection(server_frame(0x1, event))
    calls = iter(("readable", "readable"))

    def event_select(readers, _writers, _errors, _timeout):
        next(calls)
        return readers, [], []

    output = StringIO()
    try:
        with redirect_stdout(output):
            ws_client.receive_messages(connection, select_fn=event_select)
    except ConnectionError as error:
        assert str(error) == "WebSocket connection closed"
    assert '"sequence": 44' in output.getvalue()
    print("WebSocket client tests passed")


if __name__ == "__main__":
    main()
