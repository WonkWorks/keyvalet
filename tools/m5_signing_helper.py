#!/usr/bin/env python3
"""Serve the KeyValet acceptance console and isolated development signer."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import tempfile
import time
import uuid
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SECRETS = ROOT / "secrets" / "m5-development"
OPERATIONAL_KEY = SECRETS / "operational-key.pem"
CONSOLE = ROOT / "tools" / "m5_console.html"


def run_openssl(*arguments: str, input_data: bytes | None = None) -> bytes:
    return subprocess.run(
        ["openssl", *arguments], input=input_data, check=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout


def ensure_key(path: Path) -> None:
    if path.exists():
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    run_openssl("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(path))
    path.chmod(0o600)


def public_point(key: Path) -> bytes:
    der = run_openssl("pkey", "-in", str(key), "-pubout", "-outform", "DER")
    point = der[-65:]
    if len(point) != 65 or point[0] != 4:
        raise RuntimeError("OpenSSL did not produce an uncompressed P-256 public key")
    return point


def initialize_authority() -> None:
    ensure_key(OPERATIONAL_KEY)


def der_signature_to_raw(signature: bytes) -> bytes:
    if len(signature) < 8 or signature[0] != 0x30:
        raise ValueError("invalid ECDSA DER signature")
    index = 2
    if signature[1] & 0x80:
        length_bytes = signature[1] & 0x7F
        index = 2 + length_bytes
    values = []
    for _ in range(2):
        if signature[index] != 0x02:
            raise ValueError("invalid ECDSA integer")
        length = signature[index + 1]
        value = signature[index + 2:index + 2 + length]
        index += 2 + length
        values.append(value.lstrip(b"\0").rjust(32, b"\0"))
    if any(len(value) != 32 for value in values):
        raise ValueError("ECDSA integer is outside P-256 range")
    return b"".join(values)


def sign(key: Path, message: str) -> str:
    with tempfile.NamedTemporaryFile() as message_file:
        message_file.write(message.encode())
        message_file.flush()
        der = run_openssl("dgst", "-sha256", "-sign", str(key), message_file.name)
    return der_signature_to_raw(der).hex()


def authorization_fields() -> dict[str, object]:
    issued = int(time.time())
    return {
        "operation_id": str(uuid.uuid4()),
        "nonce": os.urandom(32).hex(),
        "issued_at": issued,
        "expires_at": issued + 60,
    }


def signed_response(path: str, request: dict[str, object]) -> dict[str, object]:
    device_value = request.get("device_id")
    if not isinstance(device_value, str) or not device_value:
        raise ValueError("device_id is required")
    device = device_value
    if path == "/api/sign/enrollment":
        challenge = str(request["challenge"])
        lifetime = request.get("expires_in_s")
        if type(lifetime) is not int or not 1 <= lifetime <= 300:
            raise ValueError("Enrollment requires the live challenge's expires_in_s (1–300)")
        expires = int(time.time()) + min(60, lifetime)
        operational_public_key = public_point(OPERATIONAL_KEY).hex()
        message = (
            f"KEYVALET-ENROLL-V1\n{device}\n{challenge}\n"
            f"{operational_public_key}\nENROLL_FIRST\n{expires}\n"
        )
        return {
            "challenge": challenge, "operational_public_key": operational_public_key,
            "expires_at": expires, "signature": sign(OPERATIONAL_KEY, message),
        }
    fields = authorization_fields()
    if path == "/api/sign/unlock":
        lock_id = request.get("lock_id")
        if lock_id not in ("lock_1", "lock_2"):
            raise ValueError("lock_id must be lock_1 or lock_2")
        timeout_present = "door_open_timeout_s" in request
        timeout = request.get("door_open_timeout_s")
        if timeout_present and (
            type(timeout) is not int or (timeout != 0 and (timeout < 5 or timeout > 3600))
        ):
            raise ValueError("door_open_timeout_s must be 0 or an integer from 5 to 3600")
        timeout_token = str(timeout) if timeout_present else "DEFAULT"
        message = (
            f"KEYVALET-UNLOCK-V2\n{device}\nUNLOCK\n{lock_id}\n"
            f"{timeout_token}\n{fields['operation_id']}\n{fields['nonce']}\n"
            f"{fields['issued_at']}\n{fields['expires_at']}\n"
        )
        response: dict[str, object] = {
            "type": "unlock", "protocol_version": 2, "device_id": device,
            "action": "UNLOCK", "lock_id": lock_id, **fields,
            "signature": sign(OPERATIONAL_KEY, message),
        }
        if timeout_present:
            response["door_open_timeout_s"] = timeout
        return response
    if path == "/api/sign/config":
        config = request.get("config")
        required = ("expected_revision", "default_door_open_timeout_s", "locks", "sensors")
        if not isinstance(config, dict) or any(name not in config for name in required):
            raise ValueError("config requires expected_revision and all mutable settings")
        sensors = config["sensors"]
        locks = config["locks"]
        if not isinstance(locks, dict) or set(locks) != {"lock_1", "lock_2"}:
            raise ValueError("config requires exactly lock_1 and lock_2")
        lock_lines = []
        for index in range(1, 3):
            lock_id = f"lock_{index}"
            entry = locks[lock_id]
            if (not isinstance(entry, dict) or
                    type(entry.get("sensor_required")) is not bool):
                raise ValueError(f"{lock_id} requires boolean sensor_required")
            lock_lines.append(f"{lock_id}.sensor_required={1 if entry['sensor_required'] else 0}\n")
        if not isinstance(sensors, dict) or set(sensors) != {
            f"sensor_{index}" for index in range(1, 5)
        }:
            raise ValueError("config requires exactly sensor_1 through sensor_4")
        sensor_lines = []
        for index in range(1, 5):
            sensor_id = f"sensor_{index}"
            entry = sensors[sensor_id]
            if (not isinstance(entry, dict) or
                    type(entry.get("monitoring_enabled")) is not bool):
                raise ValueError(f"{sensor_id} requires boolean monitoring_enabled")
            sensor_lines.append(
                f"{sensor_id}={1 if entry['monitoring_enabled'] else 0}\n"
            )
        message = (
            f"KEYVALET-CONFIG-V3\n{device}\nUPDATE_CONFIG\n"
            f"{fields['operation_id']}\n{fields['nonce']}\n"
            f"{fields['issued_at']}\n{fields['expires_at']}\n"
            f"{int(config['expected_revision'])}\n"
            f"{int(config['default_door_open_timeout_s'])}\n"
            f"{len(lock_lines)}\n{''.join(lock_lines)}"
            f"{len(sensor_lines)}\n{''.join(sensor_lines)}"
        )
        return {
            "protocol_version": 3, "device_id": device,
            "action": "UPDATE_CONFIG", **config, **fields,
            "signature": sign(OPERATIONAL_KEY, message),
        }
    if path == "/api/sign/journal-ack":
        sequence = request.get("highest_contiguous_sequence")
        if type(sequence) is not int or sequence < 0:
            raise ValueError("highest_contiguous_sequence must be a non-negative integer")
        message = (
            f"KEYVALET-JOURNAL-ACK-V1\n{device}\nJOURNAL_ACK\n{sequence}\n"
            f"{fields['operation_id']}\n{fields['nonce']}\n"
            f"{fields['issued_at']}\n{fields['expires_at']}\n"
        )
        return {
            "type": "journal_ack", "protocol_version": 1,
            "device_id": device, "action": "JOURNAL_ACK",
            "highest_contiguous_sequence": sequence, **fields,
            "signature": sign(OPERATIONAL_KEY, message),
        }
    raise ValueError("unknown signing operation")


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self) -> None:
        if self.path in ("/", "/m5_console.html"):
            data = CONSOLE.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        else:
            self.send_error(404)

    def do_POST(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 8192:
                raise ValueError("invalid request size")
            request = json.loads(self.rfile.read(length))
            response = json.dumps(signed_response(self.path, request)).encode()
            self.send_response(200)
        except (KeyError, TypeError, ValueError, subprocess.CalledProcessError) as error:
            response = json.dumps({"error": str(error)}).encode()
            self.send_response(400)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)

    def log_message(self, format_string: str, *arguments: object) -> None:
        print(format_string % arguments)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--init-only", action="store_true")
    arguments = parser.parse_args()
    initialize_authority()
    print("Stage this operational PUBLIC key through CONTROL (USB) before enrollment:")
    print(public_point(OPERATIONAL_KEY).hex())
    if arguments.init_only:
        print("Development authority initialized; private keys remain under ignored secrets/.")
        return 0
    print("Browser console: http://localhost:8765/")
    try:
        ThreadingHTTPServer(("127.0.0.1", 8765), Handler).serve_forever()
    except KeyboardInterrupt:
        print("\nDevelopment signing helper stopped.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
