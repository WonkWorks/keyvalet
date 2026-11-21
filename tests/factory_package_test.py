#!/usr/bin/env python3
"""Real esptool packaging and NOR-sector write simulation; no device I/O.

Use IDF Python: tests/factory_package_test.py --production BUILD --diagnostic BUILD
"""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import package_firmware as release


def flash_parts(flash, parts, directory):
    for part in parts:
        data = (directory / part["path"]).read_bytes()
        start = part["offset"]
        end = (start + len(data) + 4095) & ~4095
        assert start % 4096 == 0
        flash[start:end] = b"\xff" * (end - start)
        flash[start:start + len(data)] = data


def check(build, root, variant, preserved=False):
    first, second = root / (variant + "-a"), root / (variant + "-b")
    metadata = release.package(build, first, preserved_m11c_closeout=preserved)
    release.package(build, second, preserved_m11c_closeout=preserved)
    assert (first / "SHA256SUMS").read_bytes() == (second / "SHA256SUMS").read_bytes()
    assert metadata["variant"] == variant
    for line in (first / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split("  ")
        assert hashlib.sha256((first / name).read_bytes()).hexdigest() == digest
    for name, record in metadata["files"].items():
        data = (first / name).read_bytes()
        assert len(data) == record["bytes"] and release.sha(data) == record["sha256"]
    for purpose in ("factory", "replace"):
        manifest = json.loads((first / f"manifest.{purpose}.json").read_text())
        assert manifest["name"] == "KeyValet" and manifest["new_install_prompt_erase"] is True
        assert manifest["version"] == metadata["firmware_version"]
        assert len(manifest["builds"]) == 1 and manifest["builds"][0]["chipFamily"] == "ESP32-C6"
        parts = manifest["builds"][0]["parts"]
        assert all(set(part) == {"path", "offset"} and type(part["offset"]) is int for part in parts)
        assert parts == ([{"path": "keyvalet-factory.bin", "offset": 0}]
                         if purpose == "factory" else metadata["replacement_parts"])
    # Factory: blank N8 receives bootloader/table/erased otadata/application.
    factory_flash = bytearray(b"\xff" * release.FLASH_BYTES)
    flash_parts(factory_flash, [{"path": "keyvalet-factory.bin", "offset": 0}], first)
    for part in metadata["factory_parts"]:
        data = (first / part["path"]).read_bytes()
        assert factory_flash[part["offset"]:part["offset"] + len(data)] == data
    assert factory_flash[0xd000:0xf000] == b"\xff" * 8192
    app = (first / "keyvalet-app.bin").read_bytes()
    assert factory_flash[0x10000:0x10000 + len(app)] == app
    # Data-filled compatible device: both selected-slot parities, high OTA
    # sequence numbers and erased factory selection. No otadata rewrite at all.
    for seq in (None, 1, 2, 3, 65534, 65535, 0xfffffffe):
        flash = bytearray(b"\x5a" * release.FLASH_BYTES)
        flash[0xd000:0xf000] = b"\xff" * 8192
        if seq is not None:
            for sector, value in ((0xd000, seq), (0xe000, max(seq - 1, 1))):
                struct.pack_into("<I", flash, sector, value)
                struct.pack_into("<I", flash, sector + 24, 2)  # ESP_OTA_IMG_VALID
                struct.pack_into("<I", flash, sector + 28, zlib.crc32(struct.pack("<I", value), 0xffffffff))
        before = bytes(flash)
        flash_parts(flash, metadata["replacement_parts"], first)
        for part in metadata["replacement_parts"]:
            offset = part["offset"]
            assert flash[offset:offset + len(app)] == app
        allowed = bytearray(release.FLASH_BYTES)
        for part in metadata["replacement_parts"]:
            start = part["offset"]; end = (start + len(app) + 4095) & ~4095
            allowed[start:end] = b"\x01" * (end - start)
        assert all(allowed[i] or original == flash[i] for i, original in enumerate(before))
        assert flash[0x9000:0x10000] == before[0x9000:0x10000]  # NVS, otadata, PHY
        assert flash[0x510000:] == before[0x510000:]  # journal, replay, tail
    # The merged factory image is NOT a data-preserving replacement.
    assert (first / "keyvalet-factory.bin").read_bytes()[0x9000:0xd000] == b"\xff" * 0x4000
    try:
        release.package(build, first, preserved_m11c_closeout=preserved)
    except FileExistsError:
        pass
    else:
        raise AssertionError("existing package overwritten")
    damaged = root / f"{variant}-damaged.bin"
    data = bytearray(app); data[-1] ^= 1; damaged.write_bytes(data)
    try:
        release.checked_image(damaged)
    except ValueError:
        pass
    else:
        raise AssertionError("corrupt image accepted")
    if preserved:
        assert metadata["git_revision"] == release.M11C_CHECKPOINT
        assert metadata["provenance"]["embedded_build_prefix"] == "f7eaa18daedb-dirty-production"
        assert metadata["release_id"] == release.M11C_RELEASE_ID
        assert release.sha(app) == release.M11C_CLOSEOUT_INPUTS["keyvalet.bin"]
        try:
            release.package(build, root / "unapproved-default")
        except ValueError as error:
            assert "Build prefix" in str(error)
        else:
            raise AssertionError("default build identity protection bypassed")
        # Exercise the real pin check against substituted bytes, without touching
        # preserved inputs. All validation must precede output creation.
        copied = root / "substituted-build"
        for relative in release.M11C_CLOSEOUT_INPUTS:
            target = copied / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(build / relative, target)
        target = copied / "flasher_args.json"
        target.write_bytes(target.read_bytes() + b"\n")
        try:
            release.package(copied, root / "unapproved-substitution", preserved_m11c_closeout=True)
        except ValueError as error:
            assert "input hash mismatch" in str(error)
        else:
            raise AssertionError("substituted preserved input accepted")
        assert not (root / "unapproved-substitution").exists()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production", type=Path, required=True)
    parser.add_argument("--diagnostic", type=Path)
    parser.add_argument("--preserved-m11c-closeout", action="store_true")
    args = parser.parse_args()
    if not args.diagnostic and not args.preserved_m11c_closeout:
        parser.error("--diagnostic is required unless testing the preserved M11C closeout")
    for size in (0, -1, release.SLOT_BYTES + 1):
        try:
            release.replacement_parts(size)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid slot size accepted")
    with tempfile.TemporaryDirectory(prefix="keyvalet-factory-test-") as temporary:
        for variant, build in (("production", args.production), ("diagnostic", args.diagnostic)):
            if build:
                check(build, Path(temporary), variant, args.preserved_m11c_closeout and variant == "production")
    print("Factory packaging passed: deterministic output, real esptool merge, manifests, image integrity, both-slot NOR replacement/data preservation, size and overwrite refusal")
