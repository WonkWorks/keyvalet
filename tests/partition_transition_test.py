#!/usr/bin/env python3
"""Offline actual-table transition simulation. Never contacts a PCB."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from verify_partition_transition import verify, FLASH_BYTES


def rejected(before, table, after=None, app=None):
    try:
        verify(before, table, after, app)
    except ValueError:
        return
    raise AssertionError("unsafe transition accepted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old-table", type=Path, required=True)
    parser.add_argument("--new-table", type=Path, required=True)
    args = parser.parse_args()
    old, new = args.old_table.read_bytes(), args.new_table.read_bytes()
    before = bytearray(b"\x5a" * FLASH_BYTES)
    before[0x8000:0x9000] = old + b"\xff" * 0x400
    before[0x7a0000:0x7b0000] = b"\xff" * 0x10000
    assert verify(before, new)
    after = bytearray(before)
    after[0x8000:0x9000] = new + b"\xff" * 0x400
    app = b"\x42" * 8321
    for start in (0x10000, 0x290000):
        end = (start + len(app) + 4095) & ~4095
        after[start:end] = b"\xff" * (end - start)
        after[start:start + len(app)] = app
    assert verify(before, new, after, app)
    for offset in (0, 0x9000, 0xd000, 0xf000, 0x510000, 0x790000, 0x7a0000, 0x7b0000, 0x7fffff):
        damaged = bytearray(after); damaged[offset] ^= 1
        rejected(before, new, damaged, app)
    for offset in (0x8000, 0x8d00, 0x7a0000):
        damaged = bytearray(before); damaged[offset] ^= 1
        rejected(damaged, new)
    rejected(before, old)
    rejected(before[:-1], new)
    rejected(before, new, after, b"x" * (0x280000 + 1))
    print("PASS: actual old/new tables; exact sector-rounded transition; all operational data, OTA selector and TLS region preserved; unknown/occupied/corrupt inputs rejected")


if __name__ == "__main__":
    main()
