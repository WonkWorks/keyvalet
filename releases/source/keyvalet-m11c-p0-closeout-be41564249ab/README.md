# P0 corresponding-source preparation

This is source-delivery material, separate from the unchanged nine-file
`releases/keyvalet-m11c-p0-closeout-be41564249ab/` package. It is not a firmware
rebuild, new release, public hosting claim or legal-compliance certification.
Copyright © 2026 Evan W. Cash (WonkWorks); KeyValet-owned material is
GPL-3.0-or-later with the adopted [six-library permission](../../../LICENSE-EXCEPTION.md).
See [GPL text](../../../LICENSE) and [third-party notices](../../../THIRD_PARTY_NOTICES.md).

## What is available

| Required material | Location / evidence |
| --- | --- |
| Matching application sources and headers | Repository `main/`; all 35 project translation units match accepted M11C `be41564249abb3e410a2db4d57d31e7b64dd5724`, including the local HTTPS component source |
| Local HTTPS patch and Apache terms | `components/esp_https_server/`, including its README, source notice and LICENSE |
| Bootloader and SDK sources | ESP-IDF v5.5.5 at `b774170ff46c393eeb5e495ea37936038d3f4f4f`; 738 application and 103 bootloader SDK translation units match pinned Git objects |
| Exact production configuration | [sdkconfig](sdkconfig), copied byte-for-byte from the preserved build input; SHA-256 `9b702bff5a5920f694031ea3b4165adb2ebfa80061a091ff6da836a992129f01` |
| Partition layout and build controls | Repository `partitions.csv`, root/main CMakeLists, `main/Kconfig.projbuild`, `sdkconfig.defaults`, `version.txt` and `tools/build_identity.py` |
| Packaging / safe transition tools | Repository `tools/package_firmware.py`, `tools/verify_partition_transition.py`; normal packaging records the actual new build, not a relabeled P0 identity |
| SDK/submodule/toolchain identities and input hashes | [source.json](source.json); includes 27 recursive SDK submodule pins and 88 repository-input hashes |
| Installation and modification instructions | [FACTORY_INSTALLATION](../../../docs/FACTORY_INSTALLATION.md), [Wi-Fi](../../../docs/WIFI_PROVISIONING.md), [integration](../../../docs/TOWNECORE_INTEGRATION_V1.md) and hardware/protocol references |
| Release provenance and bytes | Original P0 `release.json`/`SHA256SUMS`; artifact checkpoint `c5df4bcc5a49599e25da67f4aad990de40666c1e`; honest historical dirty descriptor retained |

## Configuration and build procedure

Activate the pinned SDK/toolchain using the portable factory guide. To build your
own production image with the preserved configuration, copy `sdkconfig` from
this directory to a **new**, unused `sdkconfig.source-production.local` in the
repository root. Then use the guide's `build-source-production` command, or point
its `-DSDKCONFIG` option at a new writable copy of this configuration. Do not
build against this tracked reference file in place, overwrite preserved build
directories or copy new binaries into the immutable P0 directory.

All 1,249 application SDK JSON values match this configuration. The bootloader
uses the same input; its derived SDK configuration has two extra defaults,
`SECURE_ENABLE_TEE=false` and `SECURE_TEE_LOG_LEVEL=0`. The SDK recreates these
and generated project descriptor/CA-bundle assembly sources. The default public
CA bundle is pinned SDK `components/mbedtls/esp_crt_bundle/cacrt_all.pem`, not
device-specific commissioning material.

Clean source builds have new timestamps, ELF digests and honest Git/dirty build
identities. Byte-identical P0 reproduction is not promised. Hardware remains PCB
v0.4.2; software remains `0.5.0-m5`; neither is the long distribution identifier.

## External source and toolchain retrieval

- [ESP-IDF source at the pinned commit](https://github.com/espressif/esp-idf/tree/b774170ff46c393eeb5e495ea37936038d3f4f4f)
  and its recursive submodules supply the SDK and bootloader; keep their licenses
  and per-file notices. All revision pins are in `source.json`.
- [Espressif toolchain release](https://github.com/espressif/crosstool-NG/releases/tag/esp-14.2.0_20260121)
  and [build recipe commit](https://github.com/espressif/crosstool-NG/tree/83e0002aad6dff93c69707a7b85c8d152f1b17c9)
  identify GCC 14.2.0, newlib 4.3.0.20230120 and binutils 2.43.1. The public recipe
  references the corresponding Espressif GCC/newlib/binutils source revisions.
  The accepted Linux package digest is recorded from the pinned SDK's tools manifest.
- esptool 4.12.0 is an external build/install tool, not embedded application code.
  Do not assume every separately installed tool's sources must be vendored here.
- The six pinned radio objects are source-unavailable. Only the adopted permission
  supplies the narrow waiver under the holder's KeyValet GPL conditions; other
  components' GPL/source obligations remain unchanged.

Do not redistribute local compiler configuration or the entire SDK/toolchain
installation blindly. Installed internal build configuration contains credential-
bearing URLs; none is included here. Use public upstream source recipes and
nonsecret identities. Operational credentials, dumps and provisioning state are
never corresponding-source publication inputs.

## Before publication

No required local project source/configuration input was found missing. However,
complete the public-tree, source-download and release-notice review before
distribution. The publisher must provide equally accessible, maintained source
access, including required SDK/runtime sources and notices, for as long as binary
downloads are offered. A pinned reference is not evidence that a public source
download has already been tested or hosted by KeyValet.

Attach or clearly provide this source/configuration material, the source snapshot,
GPL text, adopted permission, complete applicable third-party notices and
installation information as separate release materials. Do not alter the nine
approved assets. Modified-firmware installation must remain possible without a
publisher-only key or central provisioning service; do not disclose operational
private keys. Physical Chrome/Web Serial flashing acceptance remains pending.
