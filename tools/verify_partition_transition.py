#!/usr/bin/env python3
"""Read-only offline pre/postflight for the one approved M10 -> M11C map change.

Reads operator-created ROM-mode dumps; never opens a port or writes any file.
An application-only manifest must NOT be used to perform this transition.
"""
import argparse
import hashlib
from pathlib import Path

OLD_TABLE_SHA256 = "877f5e0f991bb1ecddfbc04d308f52628b5b2b9621104353ee4bd9c5eb12e177"
NEW_TABLE_SHA256 = "ad5968bd80275039f52efe875ee6ce7d765ab27b74b79bae8406de9383c5dfad"
FLASH_BYTES, SLOT_BYTES = 0x800000, 0x280000


def verify(before: bytes, table: bytes, after: bytes | None = None, app: bytes | None = None):
    if len(before) != FLASH_BYTES or len(table) != 0xc00:
        raise ValueError("Require a complete 8 MiB before-dump and 3072-byte new table")
    if hashlib.sha256(before[0x8000:0x8c00]).hexdigest() != OLD_TABLE_SHA256:
        raise ValueError("Before-dump is not the accepted M10 partition map; stop")
    if hashlib.sha256(table).hexdigest() != NEW_TABLE_SHA256:
        raise ValueError("New table is not the approved M11C map; stop")
    if before[0x8c00:0x9000] != b"\xff" * 0x400:
        raise ValueError("Unexpected occupied bytes in the partition-table erase sector")
    if before[0x7a0000:0x7b0000] != b"\xff" * 0x10000:
        raise ValueError("New TLS region is not erased; investigate ownership; do not erase automatically")
    if table[:7 * 32] != before[0x8000:0x8000 + 7 * 32]:
        raise ValueError("An existing partition entry changed")
    if after is not None:
        if len(after) != FLASH_BYTES or not app or len(app) > SLOT_BYTES:
            raise ValueError("Postflight requires a complete dump and verified fitting application")
        expected = bytearray(before)
        expected[0x8000:0x9000] = table + b"\xff" * 0x400
        for start in (0x10000, 0x290000):
            end = (start + len(app) + 4095) & ~4095
            expected[start:end] = b"\xff" * (end - start)
            expected[start:start + len(app)] = app
        if after != expected:
            raise ValueError("Postflight differs outside the exact table/app write plan, or artifact verification failed")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--new-table", type=Path, required=True)
    parser.add_argument("--after", type=Path)
    parser.add_argument("--application", type=Path)
    args = parser.parse_args()
    if bool(args.after) != bool(args.application):
        parser.error("--after and --application are a paired postflight check")
    try:
        verify(args.before.read_bytes(), args.new_table.read_bytes(),
               args.after.read_bytes() if args.after else None,
               args.application.read_bytes() if args.application else None)
    except ValueError as error:
        parser.exit(1, str(error) + "\n")
    print("PASS: " + ("exact data-preserving table + both-app transition" if args.after else "M10 layout, unchanged entries, erased TLS tail; transition preflight only"))


if __name__ == "__main__":
    main()
