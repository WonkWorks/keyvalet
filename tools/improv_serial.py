"""Standard Improv Serial framing; no serial port or network is opened here."""
import time

HEADER = b"IMPROV"
# Read-only KeyValet extension, not a standard Improv command.
KEYVALET_CURRENT_NETWORK_RPC = 0x80
KEYVALET_STAGE_ENROLLMENT_KEY_RPC = 0x81


def frame(packet_type: int, payload: bytes) -> bytes:
    if len(payload) > 255:
        raise ValueError("Improv payload too long")
    body = HEADER + bytes((1, packet_type, len(payload))) + payload
    return body + bytes((sum(body) & 255,)) + b"\n"


def text(value: str, maximum: int, *, empty: bool = False) -> bytes:
    if not isinstance(value, str) or any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in value):
        raise ValueError("Invalid Wi-Fi text")
    try:
        encoded = value.encode("utf-8")
    except UnicodeError:
        raise ValueError("Invalid UTF-8 Wi-Fi field") from None
    if len(encoded) > maximum or (not empty and not encoded):
        raise ValueError("Wi-Fi field length is invalid")
    return encoded


def command(number: int, ssid: str = "", password: str = "", *, public_key: str = "") -> bytes:
    if type(number) is not int or number not in (1, 2, 3, 4, 7, KEYVALET_CURRENT_NETWORK_RPC,
                                               KEYVALET_STAGE_ENROLLMENT_KEY_RPC):
        raise ValueError("Unsupported Improv command")
    payload = b""
    if number == 1:
        name, secret = text(ssid, 32), text(password, 64, empty=True)
        if secret and (len(secret) < 8 or (len(secret) == 64 and
                any(c not in b"0123456789abcdefABCDEF" for c in secret))):
            raise ValueError("Password must be empty, 8–63 UTF-8 bytes, or a 64-digit hex PSK")
        payload = bytes((len(name),)) + name + bytes((len(secret),)) + secret
    elif number == KEYVALET_STAGE_ENROLLMENT_KEY_RPC:
        if (not isinstance(public_key, str) or len(public_key) != 130 or
                not public_key.startswith("04") or any(c not in "0123456789abcdef" for c in public_key)):
            raise ValueError("Public key must be a lowercase 65-byte uncompressed P-256 point")
        payload = bytes((130,)) + public_key.encode("ascii")
    return frame(3, bytes((number, len(payload))) + payload)


def response(packet_type: int, payload: bytes) -> dict | None:
    if packet_type in (1, 2) and len(payload) == 1:
        return {"event": "state" if packet_type == 1 else "error", "value": payload[0]}
    if packet_type != 4 or len(payload) < 2 or payload[1] != len(payload) - 2:
        return None  # Never expose echoed RPC commands/credentials to MONITOR.
    values, offset = [], 2
    while offset < len(payload):
        size = payload[offset]
        offset += 1
        if offset + size > len(payload):
            return None
        value = payload[offset:offset + size].decode("utf-8", "strict")
        text(value, 255, empty=True)
        values.append(value)
        offset += size
    return {"event": "result", "command": payload[0], "values": values}


class Decoder:
    """Demultiplex bounded binary frames and text logs; discard corrupt frames.

    Partial binary input expires without being printed. A new complete header
    can resynchronize a truncated frame (valid Wi-Fi text cannot contain the
    header's binary version byte).
    """
    def __init__(self):
        self.pending = bytearray()
        self.last = 0.0

    def feed(self, data: bytes, now: float | None = None) -> tuple[list[dict], bytes]:
        now = time.monotonic() if now is None else now
        if self.pending and now - self.last > 1:
            self.pending.clear()
        if data:
            self.last = now
        self.pending.extend(data)
        events, logs = [], bytearray()
        while self.pending:
            position = self.pending.find(HEADER)
            if position < 0:
                keep = next((n for n in range(min(5, len(self.pending)), 0, -1)
                             if self.pending[-n:] == HEADER[:n]), 0)
                end = len(self.pending) - keep
                logs.extend(self.pending[:end]); del self.pending[:end]
                break
            if position:
                logs.extend(self.pending[:position]); del self.pending[:position]
            if len(self.pending) < 9:
                break
            if self.pending[6] != 1:
                # Do not print any part of malformed binary input.
                self.pending.clear(); break
            total = self.pending[8] + 10
            later = self.pending.find(HEADER + b"\x01", 6)
            if 0 < later < total:
                del self.pending[:later]; continue
            if len(self.pending) < total:
                break
            packet = bytes(self.pending[:total]); del self.pending[:total]
            if sum(packet[:-1]) & 255 != packet[-1]:
                continue
            try:
                parsed = response(packet[7], packet[9:-1])
            except (UnicodeError, ValueError):
                parsed = None
            if parsed is not None:
                events.append(parsed)
        return events, bytes(logs)
