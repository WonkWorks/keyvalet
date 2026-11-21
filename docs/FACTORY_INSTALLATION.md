# Factory installation and USB replacement — M10 baseline, M11C TLS extension

This is the firmware-side installation contract for the sole KeyValet v0.4.2
ESP32-C6-WROOM-1-N8 platform. M10 passed offline verification and operator physical
acceptance using esptool, not a browser installer. It is not a v1.0 release.
M11C partition transition, TLS commissioning and sustained four-client desktop
HTTPS/WSS operation are now physically accepted; see the acceptance record below.
The implementation/closeout agent performed no hardware operations. Exact
artifacts and the authoritative physical record are in [STATUS.md](STATUS.md).

Historical bench identifiers/URLs below are redacted or illustrative, not literal
recorded DNS/SSID/device values. Results, hashes and dates are unchanged. Use your
association's own identity, Staff Origin and issuer configuration; no developer
cloud project or personal DNS zone is required.

## P0 reconciled factory release — November 21, 2026

Use release **`keyvalet-m11c-p0-closeout-be41564249ab`**, in
[`releases/keyvalet-m11c-p0-closeout-be41564249ab/`](../releases/keyvalet-m11c-p0-closeout-be41564249ab/),
as the current factory distribution candidate for TowneCore. This is a **virgin-only,
destructive factory package**, not an upgrade or recovery image. Browser and
hardware flashing qualification remains required; P0 performed no hardware work.

The historical `build-m11c-production/package-m11c-final/` remains untouched but
contains the superseded application SHA-256
`4c642523f47daf89dec6c953531364af859195f5c6616be3612dbfcce4a40db0`.
The reconciled package contains the exact preserved closeout application
`52a230412760157ffcea5b1f6bc91d44fef624e545dc00263070cbdac16b69df`.
Independent esptool inspection of all six loaded segments found only descriptor
time and ELF-hash differences from the physically accepted repair application
`97de381156fb6dc7294f64a12e28546c095e94e2fa27b5d87f47f9c4d12be1d2`.
Bootloader loaded segments differ only in compile-time metadata; partition and
OTA initialization bytes are identical. The 35 project compilation inputs match
accepted checkpoint `be41564249abb3e410a2db4d57d31e7b64dd5724`; their timestamps
precede the preserved closeout objects. This establishes artifact provenance,
not separate physical acceptance of the closeout image.

The binary retains its honest pre-checkpoint identity
`f7eaa18daedb-dirty-production`. Default packaging correctly rejects that prefix
against the later acceptance checkpoint. The explicit preserved-release mode
instead requires nine exact audited input hashes, the accepted checkpoint in
Git ancestry, and unchanged firmware/build sources. It provides no arbitrary
revision/hash override and does not modify or rebuild any image.

Historical maintainer-only packaging procedure: this requires the exact ignored
closeout build inputs, which a fresh source checkout does not contain. It is not
the public source-build recipe and must not overwrite or repackage the accepted
nine-file directory. The new output below is illustrative, not a GH2 action:

```sh
python -B tools/package_firmware.py --build build-m11c-closeout-production \
  --output build-m11c-closeout-production/package-p0-reproduction \
  --preserved-m11c-closeout
python -B tests/factory_package_test.py --production build-m11c-closeout-production \
  --preserved-m11c-closeout
```

The approved factory image SHA-256 is
`b1afc1f8d1d2fce4be37b80d200d6447585c493160fe7f2867300414b8be29ae`.
Its `SHA256SUMS` covers every other package file. `release.json` records pinned
provenance, component sizes/hashes, geometry and write plans; both manifests
identify the release and distinguish factory installation from compatible app
replacement. The factory manifest writes the merged image at zero; component
offsets remain `0x0`, `0x8000`, `0xd000`, and `0x10000`. The package does not
authorize flashing installed boards or eliminate browser-side chip/geometry,
download-hash and readback verification requirements.

The nine approved release files are tracked together in Git, copied byte-for-byte
from `build-m11c-closeout-production/package-p0-m11c-closeout-be41564249ab-virgin/`.
The original build package and all historical artifacts remain untouched. No
device-specific credentials, private keys, certificates or commissioning data
are included; the virgin data regions are erased. The checksum file itself has
SHA-256 `3a01e36b17693dd14d022e9ff74e24dc1e0944f17f395bccc97fe6aa655233e6`.
Verify the preserved package without rebuilding or repackaging:

```sh
cd releases/keyvalet-m11c-p0-closeout-be41564249ab
sha256sum SHA256SUMS
sha256sum --check SHA256SUMS
```

TowneCore consumes these exact bytes as versioned public GitHub Release assets,
retaining relative filenames; it does not permanently store firmware in its Staff
deployment or GCS. The intended repository is `WonkWorks/keyvalet`; it and this
release are not yet published. Do not configure an invented download URL.
Use `release.json` for release identity, hardware/geometry, provenance, hashes
and write plans, and `manifest.factory.json` for the virgin-only merged-image
plan. Validate the bundle against the approved checksum identity above, then
verify each downloaded artifact before flashing and read back written bytes.
Do not substitute the historical package or rebuild the application. This local
checkpoint did not publish the bundle. TowneCore P1 is now implemented,
development-deployed and visually inspected in Chrome, but live public retrieval
and physical browser flashing/virgin installation remain pending. See
[publication, licensing and source-delivery blockers](PUBLICATION.md) and the
[release license notice](../releases/README.md).

## Build and package

Use ESP-IDF **v5.5.5** and its **esptool 4.12.0** Python environment. Linux/macOS
prerequisites include Git, Python 3 with venv/pip, CMake (3.16+), Ninja and native
build tools; install the OS prerequisites from the
[pinned ESP-IDF setup guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/get-started/linux-macos-setup.html).
On macOS install command-line developer tools as directed there. In a new SDK
directory of your choice (do not overwrite an existing installation):

```sh
git clone --branch v5.5.5 --recursive https://github.com/espressif/esp-idf.git esp-idf-v5.5.5
cd esp-idf-v5.5.5
./install.sh esp32c6
. ./export.sh
```

The export sets `IDF_PATH` and activates the toolchain/Python environment; repeat
`. /path/to/esp-idf-v5.5.5/export.sh` in new terminals. Confirm `idf.py --version`
and `python -m esptool version`. The SDK target is already `esp32c6` in
`sdkconfig.defaults`; use separate new build/configuration paths rather than a
destructive `set-target` on an accepted build. From the KeyValet repository root,
these commands build **your own new production/diagnostic images**, not the
approved P0 bytes. Do not run them into preserved historical build directories:

```sh
idf.py -B build-source-production -DSDKCONFIG=sdkconfig.source-production.local \
  -DSDKCONFIG_DEFAULTS=sdkconfig.defaults reconfigure build
idf.py -B build-source-diagnostics -DSDKCONFIG=sdkconfig.source-diagnostics.local \
  -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.diagnostics' reconfigure build
python -B tools/package_firmware.py --build build-source-production \
  --output build-source-production/package
python -B tools/package_firmware.py --build build-source-diagnostics \
  --output build-source-diagnostics/package
```

Choose a **new** output directory for each package. The tool refuses overwrite,
does not build, opens no serial port and performs no network requests. Existing
M7/M9 build directories are not reused. See STATUS for the actual reviewed M10
package locations/identities; the commands are source-build examples. Keep
`version.txt` honest: it currently declares software `0.5.0-m5`, not PCB v0.4.2.
New builds get their actual Git/dirty/variant identity and different image hashes.
For an intentionally modified build, review its newly generated metadata/hashes
and use that new package consistently with the same guarded factory/replacement
plans below; never copy it into the preserved release or label it the P0 image.
Review `main/Kconfig.projbuild` and `sdkconfig.diagnostics` before choosing a
variant. Keep secrets and all device state out of build inputs. This procedure
supports modified firmware; the separate P0 corresponding-source delivery
arrangement is still a [publication blocker](PUBLICATION.md).

The packager consumes actual IDF `flasher_args.json`, generated SDK config,
project description and image descriptors. It rejects changed partition bytes,
chip/flash geometry, rollback/encryption assumptions, oversized apps, invalid
checksums/digests, stale revision prefixes, mismatched ELF/application and
production diagnostic strings. It validates the exact merged bytes, including
padding, against its inputs using the installed esptool, not a second flasher.

Each package contains:

| File | Purpose |
| --- | --- |
| `keyvalet-factory.bin` | Raw merged image at offset 0; virgin installation only |
| `keyvalet-app.bin` | Unmodified application image; both-slot USB replacement or future OTA payload |
| `bootloader.bin`, `partition-table.bin`, `ota-data-initial.bin` | Exact factory components |
| `manifest.factory.json` | ESP Web Tools manifest, one merged part at offset 0 |
| `manifest.replace.json` | ESP Web Tools manifest, same app at both application offsets |
| `release.json` | `keyvalet-release-v1` metadata: product/hardware/toolchain, build identity, ELF hash, sizes, hashes, layout and write plans |
| `SHA256SUMS` | Hashes of all other package files, including metadata/manifests |

Metadata uses relative filenames, stable ordering and no generated wall-clock
timestamp. Packaging the **same build inputs** twice produces identical files.
This is not a claim of bit-for-bit reproducible clean IDF builds across machines:
accepted build configuration still includes compile-time/debug-path inputs.
Dirty-source review builds are labeled honestly; do not relabel them as clean
published releases. ELF debug artifacts stay in their build directories.

## Exact geometry and write semantics

Flash is **8 MiB**, DIO, **80 MHz**, ESP32-C6 (image chip ID 13). Offsets are
byte offsets; sector erase granularity is 4096 bytes. Canonical partition bytes
have SHA-256 `ad5968bd80275039f52efe875ee6ce7d765ab27b74b79bae8406de9383c5dfad`.

| Region | Offset | Size / bound |
| --- | ---: | ---: |
| Bootloader | `0x0` | ends before `0x8000` |
| Partition table | `0x8000` | one 4 KiB sector |
| Default NVS | `0x9000` | `0x4000` |
| OTA selection | `0xd000` | `0x2000` |
| PHY initialization | `0xf000` | `0x1000` |
| `ota_0` | `0x10000` | `0x280000` |
| `ota_1` | `0x290000` | `0x280000` |
| Journal | `0x510000` | `0x280000` |
| Replay NVS | `0x790000` | `0x10000` |
| TLS NVS (M11C) | `0x7a0000` | `0x10000` |

See `partitions.csv` and STORAGE for formats. M10 changed no layout; M11C adds
only TLS NVS in the unused tail. Existing boards need the transition below.

### Virgin factory installation

The factory merge includes bootloader, table, wholly erased OTA metadata and
the app in `ota_0`. No factory app partition exists; IDF boots `ota_0` when
selection is erased. The image ends at the application's end, not at 8 MiB.
It contains **0xff-filled gaps**, including default NVS and PHY. It must only be
used on a virgin/fully erased board: writing it to an installed device can erase
trust/Wi-Fi/settings/checkpoints while leaving old journal/replay behind.
It is neither an ordinary update nor a complete factory reset by itself.

After first boot, current firmware initializes blank storage without an erase,
offers Improv with no credentials and permits protected-USB public-key staging
plus physical-button/WebSocket enrollment. No building keys or credentials are
compiled into a universal production image.

### Compatible, data-preserving USB replacement

Do not use the factory image, `idf.py flash`, `ota_data_initial.bin`, a full
erase or an erase-first browser option. Do not rewrite the bootloader or table.

Write the **same verified application to both `ota_0` and `ota_1`**, leaving
`otadata` byte-identical. This handles a previously selected `ota_1`, either OTA
sequence parity, and erased factory selection without assuming `0x10000` is
selected. Each write's sector-rounded end remains inside its 2.5 MiB slot.
It preserves Wi-Fi, trust, settings, replay, journal/checkpoints/ACK and the
MAC-derived device ID. Normal boot may append `DEVICE_BOOT`/other real events;
that is not an installer rewrite/reset of history.

This is an offline USB replacement, **not** power-loss-atomic OTA. Both old app
copies are replaced; no old-image rollback guarantee remains. Stable power and
completion of both verified writes are required. An interruption may require
repeating this same app-only procedure, not erasing persistent data. The current
bootloader has rollback and Secure Boot/flash encryption disabled; protected or
layout-incompatible devices require separate review, never `--force`.

Before replacement, require the canonical physical PCB/8 MiB chip, a compatible
running storage version, and matching partition bytes read in ROM download mode
(not inferred only from a filename or self-reported profile). Unknown/corrupt
selection, image validation failure, or incompatible storage is a stop condition.
Do not silently repair or reset persistent state. Stock manifests cannot enforce
these preconditions: the eventual TowneCore host must.

### Deliberate destructive recovery

An explicitly authorized **whole-flash erase**, then virgin factory installation,
is the separate recovery procedure. It removes Wi-Fi, trust, policies, replay,
journal and ACK, restarts sequence numbers, and needs provisioning/enrollment.
It does not change the chip's eFuse MAC or delete TowneCore's old history. Do not
automate recovery as a fallback after an ordinary update fails. Cross-reset
journal identity remains a documented dependency in the integration handoff.

## Browser/distribution contract

### Explicit operator CLI equivalents

Replace `<operator-verified-port>` only after separately authorizing hardware
access. From the repository root, these use the approved preserved package:
`releases/keyvalet-m11c-p0-closeout-be41564249ab/`. The operator
accepted these write plans using esptool; they are not instructions for the
closeout agent to repeat hardware operations:

```sh
# Virgin installation ONLY; no implicit whole-flash erase.
python -B -m esptool --chip esp32c6 --port '<operator-verified-port>' \
  --baud 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 releases/keyvalet-m11c-p0-closeout-be41564249ab/keyvalet-factory.bin

# Compatible-board preflight: read only the canonical 3072 partition bytes.
# Leave the application stopped in download mode; compare locally before writes.
python -B -m esptool --chip esp32c6 --port '<operator-verified-port>' \
  --before default_reset --after no_reset read_flash \
  0x8000 0xc00 /tmp/keyvalet-existing-partitions.bin
cmp /tmp/keyvalet-existing-partitions.bin releases/keyvalet-m11c-p0-closeout-be41564249ab/partition-table.bin

# Ordinary replacement ONLY after compatible layout/storage checks succeed.
# No table, bootloader, selection metadata or data partition writes.
python -B -m esptool --chip esp32c6 --port '<operator-verified-port>' \
  --baud 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode keep --flash_freq keep --flash_size keep \
  0x10000 releases/keyvalet-m11c-p0-closeout-be41564249ab/keyvalet-app.bin \
  0x290000 releases/keyvalet-m11c-p0-closeout-be41564249ab/keyvalet-app.bin
```

Do not continue after a failed comparison. Do not use these commands on unknown
storage layouts or enable erase/force options. A separately approved full reset
uses standard `erase_flash` **before** the virgin command; it is destructive and
is not part of ordinary replacement.

Generated manifests have `name: "KeyValet"`, the actual firmware version,
`new_install_prompt_erase: true`, `new_install_improv_wait_time: 10`, and one
`builds` entry with `chipFamily: "ESP32-C6"` and numeric byte offsets. Artifact
paths are relative to each manifest. Example factory part:

```json
{"name":"KeyValet","version":"0.5.0-m5","new_install_prompt_erase":true,"new_install_improv_wait_time":10,"builds":[{"chipFamily":"ESP32-C6","parts":[{"path":"keyvalet-factory.bin","offset":0}]}]}
```

`new_install_prompt_erase` asks; it **does not forbid erasing**. Upstream ESP Web
Tools can also offer an explicit erase-data action and compares firmware name/
version rather than full KeyValet build identity. TowneCore must independently
gate factory/reset operations, prevent erase-first replacement and validate the
full build identity (development builds currently share a version). Do not offer
an unguarded stock install dialog as a data-preservation guarantee.

TowneCore's `KEYVALET_RELEASE_BASE_URL` selects one explicit approved public
GitHub Release asset directory, never a floating `latest`. It remains unset:
the repository/release is not published, and acquisition returns
`publication-required` with factory flashing disabled. The release catalog is
separate from the Web Serial flashing engine so future OTA can reuse acquisition.
Prefer direct browser retrieval of `release.json`, the factory manifest and
approved image; if CORS prevents it, the existing TowneCore route is a bounded,
stateless download proxy. Do not introduce permanent firmware storage, a second
repository, GCS, GitHub credentials, OAuth or service accounts for public downloads.
Association deployments supply their own configuration and authorization.
All permitted sources must retain the same identity, compatibility and checked
write plan; unimplemented custom-source UI is not implied. A local app alone is
not sufficient for virgin installation; a bare arbitrary binary has no safe
inferred layout/identity. Local factory files need matching metadata/hashes.
Resolve relative assets safely (including Blob URLs for local files), enforce
bounded sizes/offsets, reject unexpected parts and path traversal, and obtain
explicit consent for custom firmware. Public release metadata carries no keys.

ESP Web Tools manifest fields do not enforce SHA-256 or authenticity. TowneCore
must verify `SHA256SUMS`/`release.json` and fetched bytes against a trusted release
source before handing them to the flashing library. Hashes from the same
untrusted URL are consistency checks, not publisher authentication. GitHub raw
assets may need a CORS-capable delivery endpoint. Do not embed signing secrets.
Factory and replacement manifests deliberately retain the Improv firmware name
`KeyValet`; their shape/name/version alone does not distinguish diagnostics.
The installer must check `release.json.variant == "production"` by default;
diagnostic packages require explicit workbench authorization, never silent use.

Initial browser provisioning support is desktop Chrome/Web Serial, including
Chrome on macOS. Browser requirements: secure context, supported Web Serial implementation,
user gesture/port permission, OS serial access, exclusive port ownership and
supported ESP32-C6 native USB loader/reset behavior. Stop MONITOR before browser
access. Reacquire re-enumerated USB before Improv. HTTPS hosting/CORS for fetching
artifacts does not commission device TLS. M11C requires device-local WSS,
protected-USB certificate installation, exact Origins, DNS/LAN reachability and
browser local-network permission. No browser/OS acceptance is inferred offline.

Primary references checked against installed IDF/esptool sources:
[ESP Web Tools](https://esphome.github.io/esp-web-tools/),
[manifest types](https://github.com/esphome/esp-web-tools/blob/main/src/const.ts),
[flash implementation](https://github.com/esphome/esp-web-tools/blob/main/src/flash.ts),
[install/erase behavior](https://github.com/esphome/esp-web-tools/blob/main/src/install-dialog.ts),
[IDF v5.5.5 OTA selection](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/api-reference/system/ota.html).
Pin the actual browser-library version and physically qualify it before shipping.

## Acceptance boundary

The operator completed M10 production acceptance on `keyvalet-example`: a
separately authorized full erase and factory installation, Improv commissioning,
USB public-key staging plus button/WebSocket enrollment, then compatible
both-slot app replacement **without** another erase. Partition comparison and
both app write/hash checks passed; connectivity, enrollment and journal retrieval
continued after replacement. See [STATUS.md](STATUS.md#operator-physical-acceptance--m10-production)
for exact artifacts, observed sequences and status. Do not infer preservation
through the initial destructive erase or claim physical byte-level verification
of every data store from the subsequent functional observations.

P1's browser provisioning foundation is implemented/development-deployed and
visually inspected, but browser-native physical flashing remains unvalidated.
Pin and qualify the actual standard
flashing library, browser/OS, native USB re-enumeration, package/provenance checks
and erase-prevention guards before claiming a browser installer. Offline
slot-selection coverage does not qualify M12's future OTA slot transitions,
power-loss recovery or rollback. No new lock/sensor test or destructive reset is
requested by this checkpoint.

## M10 to M11C data-preserving partition transition

**Procedure only; not executed by the implementation agent. Separate physical
authorization is required for each execution.** The operator has accepted this
transition with byte-for-byte preservation verification on the bench PCB; that
does not authorize another execution. Never use a factory merge, whole-flash erase,
`idf.py flash`, `ota_data_initial.bin`, erase-first browser option or ordinary
app-only manifest for this transition. Keep the application stopped in ROM
download mode until table and both applications have been verified.

1. Review the new production/diagnostic package provenance and its hashes. Stop
   browsers/MONITOR; arrange stable USB power and a safe unloaded bench condition
   (12 V solenoid supply disconnected). Explicitly enter ROM download mode using
   the already-qualified operator method. Avoid unintended reset/actuation.
2. Under `umask 077`, save a **complete 8 MiB** ROM flash dump outside Git to a
   new protected backup path. This contains Wi-Fi/authority/history and possibly
   private keys; do not publish it. Verify the backup's size/hash and retain a
   separate copy. Run the offline preflight below.
3. Preflight requires the exact accepted M10 table hash
   `877f5e0f991bb1ecddfbc04d308f52628b5b2b9621104353ee4bd9c5eb12e177`,
   unchanged seven original entries, erased table-sector padding and an erased
   `0x7a0000..0x7affff` TLS region. If the region is occupied, stop and investigate
   ownership; do not automatically erase or reinterpret it.
4. Write **only** the new partition-table binary at `0x8000` and the same verified
   app at both `0x10000` and `0x290000`, without resetting between writes. Do not
   write bootloader, default NVS, otadata, PHY, journal, replay or TLS data. Sector
   erasure of the table is confined to `0x8000..0x8fff`; application tails stay
   inside their original slots.
5. Still in download mode, save a second complete dump to another new protected
   path. Offline postflight compares it byte-for-byte with the exact expected
   changes: one table sector and both sector-rounded application ranges. Every
   other byte must equal the before-dump. On failure keep ROM mode and recover
   using the saved artifacts; do not boot ambiguous storage.
6. Only after successful postflight, explicitly authorize/reset into M11C.
   It initializes the new blank TLS NVS, never erases another partition. Existing
   enrollment/settings/replay/journal/ACK/Wi-Fi remain. No re-enrollment is needed
   on the accepted M10 board. No network listener exists until TLS commissioning.

Illustrative commands, **not authorization to execute**. Replace PORT and the
absolute backup paths deliberately. The example uses the approved P0 application
and M11C table, not the superseded pre-repair build. The
`--before no_reset --after no_reset` flags require the operator to have already
entered ROM mode and leave it there. No command below erases a data partition.

```sh
python -m esptool --chip esp32c6 --port PORT --before no_reset --after no_reset \
  read_flash 0x0 0x800000 /PROTECTED/NEW-before.bin
python -B tools/verify_partition_transition.py \
  --before /PROTECTED/NEW-before.bin \
  --new-table releases/keyvalet-m11c-p0-closeout-be41564249ab/partition-table.bin

python -m esptool --chip esp32c6 --port PORT --before no_reset --after no_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x8000 releases/keyvalet-m11c-p0-closeout-be41564249ab/partition-table.bin \
  0x10000 releases/keyvalet-m11c-p0-closeout-be41564249ab/keyvalet-app.bin \
  0x290000 releases/keyvalet-m11c-p0-closeout-be41564249ab/keyvalet-app.bin

python -m esptool --chip esp32c6 --port PORT --before no_reset --after no_reset \
  read_flash 0x0 0x800000 /PROTECTED/NEW-after.bin
python -B tools/verify_partition_transition.py \
  --before /PROTECTED/NEW-before.bin \
  --new-table releases/keyvalet-m11c-p0-closeout-be41564249ab/partition-table.bin \
  --after /PROTECTED/NEW-after.bin --application releases/keyvalet-m11c-p0-closeout-be41564249ab/keyvalet-app.bin
```

The checker opens no device and writes no files. It rejects unknown layouts or
occupied TLS space. Backups/fixture simulation do not establish brownout safety:
power loss while rewriting a table/app requires protected USB ROM recovery.
After transition, ordinary app-only both-slot replacement additionally preserves
TLS identity. Replacing with old M10 firmware would restore cleartext and is not
an approved downgrade; no firmware anti-rollback/eFuse policy is implemented.

### Minimal M11C operator acceptance after transition

Use `tools/tls_usb.py --port PORT` with exclusive USB ownership: `status`, then
`prepare --hostname ASSIGNED-NAME --origin EXACT-STAFF-HTTPS-ORIGIN` (add an
explicit localhost Origin only for the reference console), `csr --output NEW.csr`,
`install --chain ISSUED-LEAF-AND-INTERMEDIATES.pem`, `activate`, and poll `status`.
Use `set-time` only as physical UTC recovery when SNTP is unavailable. Supply a
real publicly trusted certificate obtained by the authorized issuer process;
the cloud service is not implemented by this repository.

Confirm port 80 is absent; browser TLS/hostname validation succeeds without
warning bypasses; disallowed/null/absent Origins and a fifth WSS client fail.
Connect four approved WSS clients plus short HTTPS settings reads, inspect heap/
largest-block/stack counters and repeated reconnect stability, then exercise
idle PING/PONG and Wi-Fi loss cleanup. Check desktop and mobile/PWA clients.
Verify existing enrollment/configuration/journal continuity read-only; a virgin
unit uses unchanged USB staging/button/ENROLL-V1 over WSS. Test invalid candidate
rejection, interrupted transfer, reboot retention and controlled renewal/re-key.
No repeated lock/sensor actuation is required to establish TLS acceptance.

### Accepted M11C commissioning and operational renewal

The operator accepted the transition and commissioning on `keyvalet-example`
(UID `<bench-device-uid>`) at
`keyvalet-example.devices.example.com`, permitting Staff Origin
`https://staff.example.com`.
Google Public CA / Google Trust Services issued the device-generated CSR using
ACME DNS-01 through the existing Google Cloud DNS delegation for `devices.example.com`.
The private key stayed on the device. USB installation and activation reached
`READY`, generation `1`, `ACTIVATED`; trusted TLS 1.2, hostname/chain validation,
HTTPS and WSS upgrade/welcome passed. Improv and all existing persisted domains
were preserved. This was operator commissioning, not a deployed cloud worker.

After the empty-PONG receive correction, Safari and Chrome maintained four
simultaneous WSS sessions for approximately eight minutes across repeated
heartbeats without the original receive failure. [STATUS](STATUS.md) records
the defect, repair, accepted binary hash and final offline verification.
This observation does not qualify every browser/OS/mobile/PWA, repeated power
failure, peak resource or long-duration endurance case in the checklist above.

The current certificate expires **February 13, 2027**. Renew and install an
authorized certificate before that date using the protected-USB workflow above;
retain normal candidate validation and explicit activation. No expiry bypass or
automatic renewal is present. The cloud maintenance service, fleet issuance,
renewal scheduling and remote lifecycle management remain **`NOT_IMPLEMENTED`**;
the proposed contract in TOWNECORE_INTEGRATION_V1 is future work.
