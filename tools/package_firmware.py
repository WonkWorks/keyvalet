#!/usr/bin/env python3
"""Package an existing canonical IDF build. Offline only: never opens a device.

Run with the ESP-IDF v5.5.5 Python environment after idf.py build. Output must
be a new directory; no release artifacts or source files are overwritten.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

from esptool import __version__ as ESPTOOL_VERSION
from esptool.bin_image import LoadFirmwareImage

ROOT = Path(__file__).resolve().parents[1]
PARTITION_SHA256 = "ad5968bd80275039f52efe875ee6ce7d765ab27b74b79bae8406de9383c5dfad"
FLASH_BYTES = 0x800000
SLOT_BYTES = 0x280000
APP_OFFSETS = (0x10000, 0x290000)
# Fixed product layout, not a configurable board/profile abstraction.
PARTITIONS = (
    ("nvs", 1, 2, 0x9000, 0x4000),
    ("otadata", 1, 0, 0xd000, 0x2000),
    ("phy_init", 1, 1, 0xf000, 0x1000),
    ("ota_0", 0, 16, APP_OFFSETS[0], SLOT_BYTES),
    ("ota_1", 0, 17, APP_OFFSETS[1], SLOT_BYTES),
    ("journal", 1, 0x40, 0x510000, 0x280000),
    ("replay_nvs", 1, 2, 0x790000, 0x10000),
    ("tls_nvs", 1, 2, 0x7a0000, 0x10000),
)

# Audited P0 exception for one preserved pre-checkpoint build, not a caller-
# supplied revision or hash override. Keep the embedded dirty identity honest.
M11C_CHECKPOINT = "be41564249abb3e410a2db4d57d31e7b64dd5724"
M11C_RELEASE_ID = "keyvalet-m11c-p0-closeout-be41564249ab"
M11C_CLOSEOUT_INPUTS = {
    "keyvalet.bin": "52a230412760157ffcea5b1f6bc91d44fef624e545dc00263070cbdac16b69df",
    "keyvalet.elf": "01ab67178a2f3b441895353f08818f3b252dad222197c6e27d8062a8aee760d1",
    "bootloader/bootloader.bin": "1865d77fe7c2d6c989b815d6211f2af3fff2c7f5559ae2caced24b44dd221f5f",
    "partition_table/partition-table.bin": PARTITION_SHA256,
    "ota_data_initial.bin": "7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f",
    "config/sdkconfig.json": "efc6c5d33429f51d9d679e8cfe6ede19bfa09ff969720ee1e0acc72c63b1e562",
    "flasher_args.json": "9ee8be5fd935accfc787fc2ef399d8d4dd9e849188eb24e81f9ac6653790c9cb",
    "project_description.json": "ae00f9010b331e9fab924d3d2208df64706e583589f6a7b2f4b0d64465065229",
    "esp-idf/main/keyvalet_build_identity.h": "668ac6151747486c91d88c98ddb3423c4a16c876dd1bfdcf03e1b1e1a26e46d5",
}


def preserved_m11c_provenance(build: Path, prefix: str, variant: str) -> dict:
    require(prefix == "f7eaa18daedb-dirty-production" and variant == "production",
            "Preserved M11C closeout identity/variant mismatch")
    for relative, expected in M11C_CLOSEOUT_INPUTS.items():
        require(sha((build / relative).read_bytes()) == expected,
                f"Preserved M11C input hash mismatch: {relative}")
    subprocess.run(["git", "-C", str(ROOT), "merge-base", "--is-ancestor", M11C_CHECKPOINT, "HEAD"], check=True)
    # Only firmware/build inputs must remain at the accepted checkpoint;
    # this packaging-only change and its documentation/tests are permitted.
    subprocess.run(["git", "-C", str(ROOT), "diff", "--quiet", M11C_CHECKPOINT, "--",
                    "main", "components", "partitions.csv", "sdkconfig.defaults",
                    "sdkconfig.diagnostics", "CMakeLists.txt", "version.txt",
                    "tools/build_identity.py"], check=True)
    return {
        "accepted_source_checkpoint": M11C_CHECKPOINT,
        "embedded_build_prefix": prefix,
        "embedded_build_git_revision": "f7eaa18daedb1c2ae098b900af7f39cdea07eacb",
        "preserved_inputs_sha256": M11C_CLOSEOUT_INPUTS,
        "physically_accepted_application_sha256": "97de381156fb6dc7294f64a12e28546c095e94e2fa27b5d87f47f9c4d12be1d2",
        "application_equivalence": "All six loaded segments identical except descriptor time and ELF SHA-256; image integrity verified independently.",
        "physical_acceptance": "Repair application accepted; closeout application/factory package require separate flashing qualification.",
    }


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def checked_image(path: Path):
    image = LoadFirmwareImage("esp32c6", str(path))
    require(image.chip_id == 13, "Image is not ESP32-C6")
    require(image.flash_mode == 2 and image.flash_size_freq == (
        image.ROM_LOADER.FLASH_SIZES["8MB"] | image.ROM_LOADER.FLASH_FREQUENCY["80m"]),
        "Image flash configuration must be DIO / 80 MHz / 8 MB")
    require(image.checksum == image.calculate_checksum(), "Invalid image checksum")
    require(image.append_digest and image.stored_digest == image.calc_digest,
            "Invalid/missing image validation hash")
    return image


def replacement_parts(app_size: int) -> list[dict]:
    require(0 < app_size <= SLOT_BYTES, "Application does not fit both OTA slots")
    # Both slots get the same verified image. Preserve otadata, including a
    # previously selected ota_1; no proprietary OTA selector or slot decoder.
    return [{"path": "keyvalet-app.bin", "offset": offset} for offset in APP_OFFSETS]


def package(build: Path, output: Path, *, preserved_m11c_closeout: bool = False) -> dict:
    build = build.resolve()
    project = json.loads((build / "project_description.json").read_text())
    config = json.loads((build / "config/sdkconfig.json").read_text())
    flash = json.loads((build / "flasher_args.json").read_text())
    require(Path(project["project_path"]).resolve() == ROOT and
            project["project_name"] == "keyvalet" and project["target"] == "esp32c6",
            "Not a build of this KeyValet ESP32-C6 project")
    require(project["git_revision"] == "v5.5.5" and ESPTOOL_VERSION == "4.12.0",
            "Use the accepted ESP-IDF v5.5.5 / esptool 4.12.0 toolchain")
    require(flash["flash_settings"] == {"flash_mode": "dio", "flash_size": "8MB", "flash_freq": "80m"},
            "Unexpected flash settings")
    inputs = {int(offset, 0): build / name for offset, name in flash["flash_files"].items()}
    require(set(inputs) == {0, 0x8000, 0xd000, 0x10000}, "Unexpected factory write regions")
    require(not config.get("SECURE_BOOT") and not config.get("SECURE_FLASH_ENC_ENABLED") and
            not config.get("BOOTLOADER_APP_ROLLBACK_ENABLE"),
            "Changed security/rollback configuration requires installation review")
    diagnostic = bool(config.get("KEYVALET_DIAGNOSTICS"))
    require(not config.get("KEYVALET_DEVELOPMENT_AUTHORITY"), "Retired root configuration; rebuild M10 first")
    raw = {offset: path.read_bytes() for offset, path in inputs.items()}
    require(sha(raw[0x8000]) == PARTITION_SHA256, "Partition table differs from accepted layout")
    require(raw[0xd000] == b"\xff" * 0x2000, "Factory otadata must be wholly erased")
    require(0 < len(raw[0]) <= 0x8000, "Bootloader overlaps partition table")
    checked_image(inputs[0])
    image = checked_image(inputs[0x10000])
    app = raw[0x10000]
    parts = replacement_parts(len(app))
    desc = image.segments[0].data
    require(struct.unpack_from("<I", desc)[0] == 0xabcd5432, "Missing application descriptor")
    text = lambda offset: desc[offset:offset + 32].split(b"\0", 1)[0].decode()
    require(text(16) == project["project_version"] and text(48) == "keyvalet" and
            text(112) == "v5.5.5", "Build metadata and image descriptor disagree")
    elf_hash = sha((build / project["app_elf"]).read_bytes())
    require(desc[144:176].hex() == elf_hash, "Application/ELF mismatch")
    header = (build / "esp-idf/main/keyvalet_build_identity.h").read_text()
    prefix = re.search(r'^#define KEYVALET_BUILD_PREFIX "([^"]+)"$', header, re.M)
    require(prefix is not None, "Missing build identity")
    prefix = prefix.group(1)
    variant = "diagnostic" if diagnostic else "production"
    revision = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
    provenance = None
    if preserved_m11c_closeout:
        provenance = preserved_m11c_provenance(build, prefix, variant)
        revision = M11C_CHECKPOINT
    else:
        require(prefix.startswith(revision[:12] + "-") and prefix.endswith("-" + variant),
                "Build prefix does not match current revision/variant; build first")
    require(prefix.encode() + b"\0" in app and b"keyvalet-v0.4.2\0" in app,
            "Image lacks selected build/hardware identity")
    require(b"STAGED\0" in app, "Image lacks M10 protected-USB key staging; rebuild first")
    require(diagnostic or b"KVTEST " not in app, "Production image contains diagnostic commands")
    # mbedTLS's parser necessarily contains PEM delimiter constants. Reject an
    # embedded PEM object, not the delimiter strings required by TLS parsing.
    require(not re.search(rb"-----BEGIN (?:EC |RSA )?PRIVATE KEY-----\r?\n[A-Za-z0-9+/=\r\n]{32,}-----END (?:EC |RSA )?PRIVATE KEY-----", app),
            "Embedded private key found in application")

    # Validate everything before creating output. Never overwrite an earlier package.
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    names = {0: "bootloader.bin", 0x8000: "partition-table.bin",
             0xd000: "ota-data-initial.bin", 0x10000: "keyvalet-app.bin"}
    for offset, name in names.items():
        (output / name).write_bytes(raw[offset])
    merged = output / "keyvalet-factory.bin"
    command = [sys.executable, "-B", "-m", "esptool", "--chip", "esp32c6", "merge_bin",
               "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "8MB", "-o", str(merged)]
    for offset in sorted(names):
        command += [hex(offset), str(output / names[offset])]
    subprocess.run(command, check=True)
    factory = merged.read_bytes()
    expected = bytearray(b"\xff" * (APP_OFFSETS[0] + len(app)))
    for offset, data in raw.items():
        expected[offset:offset + len(data)] = data
    require(factory == expected, "Merged image composition/padding differs from inputs")

    manifest_base = {"name": "KeyValet", "version": project["project_version"],
                     # True asks rather than implicitly erasing a non-Improv device.
                     # This is NOT an erase prohibition; the future host must enforce it.
                     "new_install_prompt_erase": True, "new_install_improv_wait_time": 10}
    for purpose, write_parts in (("factory", [{"path": merged.name, "offset": 0}]), ("replace", parts)):
        manifest = {**manifest_base, "builds": [{"chipFamily": "ESP32-C6", "parts": write_parts}]}
        if provenance:
            manifest["keyvalet_release_id"] = M11C_RELEASE_ID
            manifest["keyvalet_installation"] = "virgin-factory-only" if purpose == "factory" else "compatible-app-replacement-only"
        (output / f"manifest.{purpose}.json").write_bytes(json_bytes(manifest))
    files = {}
    for path in sorted(output.iterdir()):
        data = path.read_bytes()
        files[path.name] = {"bytes": len(data), "sha256": sha(data)}
    metadata = {
        "schema": "keyvalet-release-v1", "product": "KeyValet", "hardware": "keyvalet-v0.4.2",
        "module": "ESP32-C6-WROOM-1-N8", "chip": "esp32c6", "flash_bytes": FLASH_BYTES,
        "flash_mode": "dio", "flash_frequency_mhz": 80, "firmware_version": text(16),
        "esp_idf_version": text(112), "esptool_version": ESPTOOL_VERSION, "variant": variant,
        "git_revision": revision, "build_id": prefix + "-" + elf_hash, "elf_sha256": elf_hash,
        "ota_slot_bytes": SLOT_BYTES, "ota_slot_headroom_bytes": SLOT_BYTES - len(app),
        "partitions": [{"name": name, "type": typ, "subtype": sub, "offset": offset, "bytes": size}
                       for name, typ, sub, offset, size in PARTITIONS],
        "factory_parts": [{"path": name, "offset": offset} for offset, name in sorted(names.items())],
        "replacement_parts": parts, "files": files,
        "factory_image_is_destructive": True, "replacement_preserves_otadata": True,
        "initial_production_enrollment": "protected-usb-public-key-staging-button-and-enroll-v1-proof",
    }
    if provenance:
        metadata.update(release_id=M11C_RELEASE_ID, provenance=provenance,
                        factory_installation="virgin-only; destructive on an installed device; not upgrade or recovery",
                        browser_qualification="required; no browser/hardware flashing performed for P0")
    (output / "release.json").write_bytes(json_bytes(metadata))
    sums = "".join(f"{sha(path.read_bytes())}  {path.name}\n" for path in sorted(output.iterdir()))
    (output / "SHA256SUMS").write_text(sums)
    print(f"Verified {variant} package: {output}\nBuild: {metadata['build_id']}")
    return metadata


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new output directory")
    parser.add_argument("--preserved-m11c-closeout", action="store_true",
                        help="package only the hash-pinned, audited P0 closeout build; never relabel its embedded identity")
    args = parser.parse_args()
    try:
        package(args.build, args.output, preserved_m11c_closeout=args.preserved_m11c_closeout)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"Packaging refused: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
