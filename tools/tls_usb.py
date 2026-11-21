#!/usr/bin/env python3
"""Protected-USB TLS commissioning. Never generates or receives a device private key.

Opening a port is an operator-controlled hardware operation. Stop other monitors
first. Importing this module does not open serial, network, or output files.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import struct
import time

from improv_serial import Decoder, frame
from kv_test import open_serial, write_serial

RPC = 0x82


def packet(body: bytes) -> bytes:
    if not body or len(body) > 253:
        raise ValueError("Invalid TLS vendor operation size")
    return frame(3, bytes((RPC, len(body))) + body)


class Device:
    def __init__(self, descriptor):
        self.descriptor, self.decoder = descriptor, Decoder()

    def rpc(self, body):
        write_serial(self.descriptor, packet(body))
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if not select.select([self.descriptor], [], [], 1)[0]:
                continue
            data = os.read(self.descriptor, 4096)
            if not data:
                raise RuntimeError("USB disconnected; query state before retrying")
            events, _ = self.decoder.feed(data)
            for event in events:
                if event["event"] == "error" and event["value"]:
                    raise RuntimeError("TLS operation rejected; inspect status/clock/candidate")
                if event["event"] == "result" and event["command"] == RPC:
                    return event["values"]
        raise RuntimeError("USB timeout; operation outcome is unknown; query state")

    def transfer(self, kind, data):
        if not data or len(data) > (1024 if kind == 1 else 6144) or b"\0" in data:
            raise ValueError("Object exceeds TLS transfer bounds or contains NUL")
        self.rpc(bytes((1, kind)) + struct.pack("<H", len(data)) + hashlib.sha256(data).digest())
        for offset in range(0, len(data), 220):
            self.rpc(b"\x02" + struct.pack("<H", offset) + data[offset:offset + 220])
        return self.rpc(b"\x03")

    def read(self, kind):
        data = bytearray()
        expected = None
        while expected is None or len(data) < expected:
            values = self.rpc(bytes((4, kind)) + struct.pack("<H", len(data)))
            if len(values) != 3:
                raise RuntimeError("Invalid object response")
            total, offset = map(int, values[:2])
            chunk = bytes.fromhex(values[2])
            if (not 0 <= total < 2048 or offset != len(data) or
                    (expected is not None and total != expected) or not chunk or len(data) + len(chunk) > total):
                raise RuntimeError("Inconsistent TLS export")
            expected = total
            data.extend(chunk)
        return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="explicit stable USB path; no autodiscovery")
    commands = parser.add_subparsers(dest="operation", required=True)
    commands.add_parser("status")
    prepare = commands.add_parser("prepare")
    prepare.add_argument("--hostname", required=True)
    prepare.add_argument("--origin", action="append", required=True)
    prepare.add_argument("--rotate-key", action="store_true")
    csr = commands.add_parser("csr")
    csr.add_argument("--output", type=Path, required=True)
    csr.add_argument("--active", action="store_true")
    install = commands.add_parser("install")
    install.add_argument("--chain", type=Path, required=True, help="leaf first, followed by intermediate CA(s)")
    commands.add_parser("activate")
    commands.add_parser("abort")
    commands.add_parser("discard-candidate")
    clock = commands.add_parser("set-time")
    clock.add_argument("--utc", type=int, help="UTC seconds; defaults to this operator computer's clock")
    args = parser.parse_args()
    # Validate local inputs before opening the appliance.
    data = None
    if args.operation == "prepare":
        if not 1 <= len(args.origin) <= 3:
            parser.error("provision one to three exact Origins")
        data = json.dumps({"hostname": args.hostname, "origins": args.origin,
                           "rotate_key": args.rotate_key}, separators=(",", ":")).encode()
    if args.operation == "install":
        data = args.chain.read_bytes()
        if b"PRIVATE KEY" in data or not data or len(data) > 6144 or b"\0" in data:
            parser.error("provide only a bounded public certificate chain, never a private key")
    if args.operation == "csr" and args.output.exists():
        parser.error("CSR output already exists; choose a new file")
    descriptor = open_serial(args.port)
    try:
        device = Device(descriptor)
        if args.operation == "status":
            print(device.read(3).decode())
        elif args.operation == "prepare":
            print(device.transfer(1, data))
        elif args.operation == "install":
            print(device.transfer(2, data))
        elif args.operation == "csr":
            with args.output.open("xb") as output:
                output.write(device.read(4 if args.active else 1))
        elif args.operation == "set-time":
            print(device.rpc(b"\x08" + struct.pack("<Q", args.utc if args.utc is not None else int(time.time()))))
        else:
            operation = {"activate": 5, "abort": 6, "discard-candidate": 7}[args.operation]
            print(device.rpc(bytes((operation,))))
    finally:
        os.close(descriptor)


if __name__ == "__main__":
    main()
