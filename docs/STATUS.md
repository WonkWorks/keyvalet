# Project Status

## Current distribution and publication preparation — GH2

The current factory candidate is
`releases/keyvalet-m11c-p0-closeout-be41564249ab/`, preserved at
`c5df4bcc5a49599e25da67f4aad990de40666c1e` from accepted M11C source
`be41564249abb3e410a2db4d57d31e7b64dd5724`. See
[factory hashes and guards](FACTORY_INSTALLATION.md#p0-reconciled-factory-release--november-21-2026).
Older artifact tables below are historical evidence, not selectable releases;
the pre-repair `4c642523…` application is superseded.

PCB **v0.4.2** is hardware. The unchanged software version is **`0.5.0-m5`**;
M11C/P0 are milestones and the long release ID identifies the distribution.
TowneCore P1 is implemented, development-deployed and visually inspected in
Chrome; physical Chrome/Web Serial flashing and live release retrieval are
pending. The intended public GitHub repository/release is not yet published.
Factory acquisition remains `publication-required` until a validated versioned
release URL is configured. GPL-3.0-or-later and the reviewed six-library Section 7
permission are adopted. The exact nonsecret production configuration and source
verification metadata are now prepared separately from the unchanged nine-file
package. [Release/source-delivery and one-time history gates](PUBLICATION.md)
remain; licensing adoption is not certification or publication authorization.

**Sanitization note:** bench device IDs/UIDs, SSID, DHCP address, DNS/Staff URLs
and workstation names below have been redacted or replaced by illustrative
`example.com`/`keyvalet-example` values. They are not literal historical
measurements or usable installation configuration. Firmware hashes, counts,
dates, results, protocol/partition values and acceptance limitations are retained.
Relative paths identify original local artifacts, not files supplied by a fresh
checkout. Original records remain in private Git history; GH2 does not rewrite it.

## Milestone 11C — production HTTPS/WSS physically accepted

Operator physical acceptance is complete on KeyValet v0.4.2 / ESP32-C6:

- Device `keyvalet-example`, UID `<bench-device-uid>`, TLS hostname
  `keyvalet-example.devices.example.com`.
- Authorized Staff Origin:
  `https://staff.example.com`.
- Non-destructive M10 → M11C partition transition passed byte-for-byte
  preservation verification. Wi-Fi credentials, enrollment, journal and existing
  configuration survived. Existing USB Improv Wi-Fi provisioning passed.
- The device generated its P-256 key and valid CSR; the private key remained on
  the PCB. Manual commissioning used Google Public CA / Google Trust Services
  issuance via ACME DNS-01 and the existing Google Cloud DNS delegation for
  `devices.example.com`. USB certificate installation/activation reached `READY`,
  generation `1`, activation `ACTIVATED`.
- Trusted TLS 1.2 hostname/chain verification, HTTPS response, WSS HTTP 101 and
  application welcome passed from the authorized Staff HTTPS Origin.
- Safari and Chrome passed after the empty-frame receive repair below. Four
  simultaneous browser WSS sessions remained operational for approximately
  eight minutes across repeated heartbeats, without recurrence of the original
  TLS/WebSocket receive failure.

These are operator-reported hardware results, not tests performed by the
closeout agent. The accepted application remains preserved at
`build-m11c-wss-repair-production/keyvalet.bin`, SHA-256
`97de381156fb6dc7294f64a12e28546c095e94e2fa27b5d87f47f9c4d12be1d2`.
This accepts M11C HTTPS/WSS functionality, not a v1.0 release or an unbounded
endurance/resource qualification. Mobile/PWA and browser-native installation
qualification are not inferred from desktop browser acceptance.

**Operational renewal obligation:** the current certificate expires
**February 13, 2027**. Arrange authorized renewal and installation before expiry
using the protected-USB candidate/activation workflow until automation exists.
The cloud certificate-maintenance service remains **`NOT_IMPLEMENTED`**:
automated renewal, fleet issuance, renewal scheduling and remote certificate
lifecycle management are not complete. Manual ACME/DNS-01 issuance and existing
DNS delegation do not implement the proposed future maintenance contract.

### Final offline closeout verification

Final production build uses a separate directory/configuration; the accepted
repair build and all earlier artifacts are untouched:

```text
idf.py -B build-m11c-closeout-production -DSDKCONFIG=/tmp/keyvalet-m11c-closeout-production.sdkconfig -DSDKCONFIG_DEFAULTS=sdkconfig.defaults build
BIN: build-m11c-closeout-production/keyvalet.bin
Bytes: 1134304
SHA-256: 52a230412760157ffcea5b1f6bc91d44fef624e545dc00263070cbdac16b69df
ELF SHA-256: 01ab67178a2f3b441895353f08818f3b252dad222197c6e27d8062a8aee760d1
Build identity: f7eaa18daedb-dirty-production-01ab67178a2f3b441895353f08818f3b252dad222197c6e27d8062a8aee760d1
```

The final build precedes the Git checkpoint, hence its baseline-revision/dirty
prefix. It has the same accepted firmware source. ESP image parsing confirms
all loaded segment addresses and bytes match the accepted repaired application
except the descriptor build timestamp and ELF hash. The accepted timestamp is
November 15, 2026, 18:21:11; the closeout timestamp is 18:53:33. These metadata
and consequent image-digest differences explain the new application hash; it is
not a replacement claim of physical acceptance for a newly flashed binary.
Both applications are 1,134,304 bytes with 1,487,136 bytes OTA-slot headroom.
The partition binary still hashes to
`ad5968bd80275039f52efe875ee6ce7d765ab27b74b79bae8406de9383c5dfad`.

Final existing checks pass: TLS identity/material/NVS/USB fault tests;
empty-frame receive; HTTPS/WSS admission, Origins, four clients, liveness and
cleanup; USB UTC; request/replay allocation; actual config HTTP parsing/auth/CAS;
native WSS verification; reference browser journal/configuration; M5 security
and acceptance plus M7 targeted/purpose-neutral/journal contracts; actual
mbedTLS enrollment/signed commands/replay; canonical settings/replay storage;
USB receiver in production and diagnostic variants; flash/transport gate;
journal/cursor integration and JSON allocation; controller configuration for
both lock/sensor pairs. Signing fixtures remain temporary and outside the repo.
Actual-table transition and production/existing-diagnostic packaging tests pass,
including real offline esptool merge, artifact/descriptor binding, exact hashes,
NOR replacement/data preservation, bounds and overwrite refusal.

Final diff review preserves signed authorization/enrollment/replay/journal/
configuration formats and the accepted M11C partition map. The empty-frame fix
remains present; no compatibility forwarding layer or additional functionality
was introduced during closeout. Both lock/sensor pairs still share parameterized
authorization, arbiter, opening/timeout/alarm/monitoring and journal paths; TLS
gates apply equally. `git diff --check` passes. Only intended M11C source,
configuration, tooling, tests and documentation belong in the checkpoint;
device certificates/CSR, private credentials and generated builds do not.
No hardware or cloud operations were performed during closeout.

### WSS session-stability repair — physically accepted

Before repair, both Safari and Chrome received the welcome and then closed
abnormally (1006), approximately 3–13 seconds after connecting. The original
commissioning successes did not establish stable WSS; subsequent acceptance
explicitly exercised the repaired firmware.

The receive handler unconditionally called `httpd_ws_recv_frame` twice. In the
installed ESP-IDF v5.5.5 `httpd_ws.c`, `frame.len == 0` means parse the remaining
frame header. The length probe already consumes an empty frame's length byte
and mask; the second call attempts another header read, blocking on an idle
connection or consuming the next buffered frame incorrectly. KeyValet sends an
empty PING every 15 seconds on its uptime-based schedule; the browser's empty
PONG triggers this bug. Read failure propagates through the HTTPS transport and
HTTPD handler-error cleanup, consistent with the reported TLS error / failed
second-byte receive. The schedule explains variable time from connection to
failure; that timing correlation is inferred, not captured on the PCB.

Correction: perform the second, payload receive only when `frame.len > 0`.
No timeout, heartbeat, reconnect, API/signature, enrollment, authorization,
journal, Origin, four-client capacity, TLS storage, partition or commissioning
change. No new runtime allocation or task; existing bounded payload ownership
and control-frame handling remain. HTTPD owns socket/session cleanup; queued
heartbeat sends execute in its task, and queued journal sends retain their
payload until the completion callback. These paths were inspected, not changed.

`tests/ws_receive_test.py` executes the actual firmware handler with the installed
IDF frame parser and cJSON over a stubbed byte stream (no sockets/TLS hardware).
The original handler fails its first empty-PONG case; the corrected handler
passes empty PONG/PING/CLOSE/text, back-to-back buffered frames, 400 heartbeat
responses across four client slots, nonempty control payloads/partial reads,
unchanged five-operation dispatch, request bounds, allocation/receive/send
failures and payload cleanup. Existing TLS admission/liveness/cleanup, stack
allocation, config HTTP, native WSS client, browser journal/config, M5 acceptance/
security and M7 targeted-unlock/purpose-neutral/journal contracts pass. Actual
security/mbedTLS enrollment/replay tests also pass. `git diff --check` passes.

Isolated production build (previous production/diagnostic artifacts preserved):

```text
idf.py -B build-m11c-wss-repair-production -DSDKCONFIG=/tmp/keyvalet-m11c-wss-repair-production.sdkconfig -DSDKCONFIG_DEFAULTS=sdkconfig.defaults build
BIN: build-m11c-wss-repair-production/keyvalet.bin
Bytes: 1134304
SHA-256: 97de381156fb6dc7294f64a12e28546c095e94e2fa27b5d87f47f9c4d12be1d2
ELF SHA-256: 5b6d38598db40d83deaec6d962b8ea5e36d1d1c752305324762568c5cb008322
Build identity: f7eaa18daedb-dirty-production-5b6d38598db40d83deaec6d962b8ea5e36d1d1c752305324762568c5cb008322
```

Verified production/canonical PCB identity and descriptor-to-ELF binding.
Firmware version `0.5.0-m5`, ESP-IDF `v5.5.5`; no diagnostic commands included.
OTA headroom: 1,487,136 bytes. Partition binary still hashes to
`ad5968bd80275039f52efe875ee6ce7d765ab27b74b79bae8406de9383c5dfad`.
Static DIRAM remains 115,722 bytes text, 14,728 data and 38,400 BSS; this is not a
runtime heap measurement. No new runtime state or resource increase is required.

Subsequent operator acceptance above confirms sustained Safari/Chrome and four
simultaneous browser sessions across repeated heartbeat periods after repair.
The offline reproducer proves the receive defect and its correction; neither
that test nor the eight-minute observation proves the absence of all future
device/TLS/network issues or long-term heap fragmentation. No hardware operation,
Git staging/commit, cloud change or certificate access was performed during the
repair itself; local final checkpointing is a separate closeout operation.
Repair scope: `main/web_server.c`, `tests/ws_receive_test.py`, this status record;
all pre-existing uncommitted M11C work and commissioning files are preserved.

## Milestone 11C — initial device WSS/TLS implementation and offline evidence

Based on accepted M10 `f7eaa18daedb1c2ae098b900af7f39cdea07eacb`, branch
`hardware/keyvalet-v0.4.2`. Initially offline-only; subsequent operator acceptance
and the session repair are recorded above.
No PCB connection, serial operation, reset/erase/flash, Google resource access,
TowneCore implementation or Git staging/commit/push was performed.

Implemented:

- Replace port-80 HTTP/WS with local ESP-IDF HTTPS/WSS on 443, preserving
  `/api/v1/status`, `/api/v1/ws`, `/api/v1/config` payload/signature semantics.
  Exact active USB-provisioned Origins apply to HTTPS and WSS; absent/null/
  unlisted Origins fail closed. No default localhost exception or cleartext
  fallback. Reference console/native client use normal verified WSS.
- Per-device on-chip P-256 key generation, exact-hostname SAN CSR, public-only
  export and bounded certificate validation. Full station-MAC `device_uid`
  survives key rotation without changing existing short operational `device_id`.
- One narrow production/diagnostic USB TLS RPC `0x82` in the existing receiver:
  candidate preparation, CSR/public/status reads, SHA-256-checked bounded chunk
  installation, explicit activation, abort/discard and physical UTC recovery.
  Standard Improv command semantics and `0x81` enrollment staging remain intact.
  Binary vendor-frame hashes may contain the frame sync sequence without false
  resynchronization; standard UTF-8 request recovery remains unchanged.
- Dedicated 64 KiB TLS NVS at `0x7a0000`, separate candidate/current slots and
  explicit selector. Existing partition extents and operational storage formats
  are unchanged. An ambiguous write inhibits further TLS mutations until reboot;
  known-good active material is not overwritten by candidate writes.
- Safe HTTPS activation/restart with a short shared actuation inhibit: no begin
  during ACTIVE/RESERVED, no output mutex held across server shutdown, no new
  pulse/reservation while inhibited. Existing 350 ms pulse/2000 ms recovery remain.
- Four WSS clients plus one transient HTTPS socket, bounded TLS/I/O, PING/PONG,
  idle/dead-session cleanup, heap/minimum/largest-block and stack/session metrics.
- One scoped local ESP-IDF HTTPS component fix for a leaked TLS session when
  post-handshake transport-context allocation fails; upstream code/config/API
  otherwise unchanged. No SDK installation was edited.
- Settings readiness fields, explicit TLS state/listener/expiry, SSID, full UID,
  security availability and client maximum. No new unsigned administrative API.

UTC decision: retain existing unauthenticated SNTP and add only physically
authorized USB UTC. No LAN time mutation, authenticated network-time subsystem,
certificate-expiration bypass or signed timestamp/replay change. Missing valid
UTC fails TLS readiness/installation/activation and existing signed checks closed.

### M11C offline evidence

Passed actual firmware material/store/USB code with host-built installed mbedTLS:
P-256 generation, persistent key/UID/reboot, OpenSSL-verified SAN CSR, rootless
intermediate-chain validation, key/SAN/usage/signature/time rejection, trailing
data/private-key rejection, bounded transfer/hash/order/expiry, candidate versus
active separation, failed/ambiguous slot/selector commits and recovery/re-key.
These are host entropy/NVS/clock/peripheral models, not ESP32 brownout evidence.

Passed actual extracted HTTPS/WSS admission/client/liveness functions and IDF
secure-open failure function with host stubs: exact Origins, no-valid-time/expiry
denial, fixed four clients, duplicate/close/re-admission/LED counts, PING and dead
session reclamation, handshake failure and allocation-failure cleanup. USB UTC
range/failure/validity checks execute the actual setter with a stubbed OS clock.

Affected accepted regressions passed: actual mbedTLS enrollment/signatures/replay;
CONFIG-V3 HTTP parsing/auth denial/CAS; canonical settings/replay persistence;
journal/cursor/flash gate; parameterized controller for both lock/sensor pairs;
arbiter and replay window; targeted signing/ACK/browser preflight contracts;
reference console journal/configuration behavior; native WSS verification/liveness;
USB RX in both variants and existing MONITOR/IPC integration over a private PTY.
The PTY test needed sandbox escalation for its local simulated endpoint; it did
not open a physical port. Temporary signing fixtures stayed outside the repo.

Both isolated IDF builds pass. Packaging tests verify descriptor/ELF binding,
canonical hardware/variant, deterministic real-esptool merge, hashes/manifests,
OTA size/overwrite refusal and both-slot data preservation. Actual old/new-table
transition simulation verifies exact changed sectors and rejection of unknown
maps/occupied TLS space or any altered operational data. `git diff --check`
passes. No source-controlled secret or generated firmware/build artifact was added.

Reproduce TLS host crypto dependencies with CMake on the installed IDF mbedTLS
source, `ENABLE_TESTING=OFF`, `ENABLE_PROGRAMS=OFF`,
`CMAKE_POSITION_INDEPENDENT_CODE=ON`, build path `/tmp/keyvalet-m11c-mbedtls`;
run `python3 -B tests/tls_identity_test.py` and `tests/tls_transport_test.py`.
Transition test takes `--old-table build/partition_table/partition-table.bin`
and `--new-table build-m11c-production/partition_table/partition-table.bin`.
The original build's accepted table is read, never rebuilt or overwritten.

### M11C verified artifacts

**Historical pre-repair artifacts — superseded, do not select for installation.**
Use the current P0 candidate above; these hashes remain for traceability.

Hardware `keyvalet-v0.4.2`, ESP32-C6/N8; firmware `0.5.0-m5` (unchanged development
version), ESP-IDF `v5.5.5`, DIO/80 MHz/8 MiB. No claim of v1.0 release readiness.

| Variant / artifact | Historical repository-relative path | Bytes / SHA-256 |
| --- | --- | --- |
| Production BIN | `build-m11c-production/keyvalet.bin` | 1,134,288; `4c642523f47daf89dec6c953531364af859195f5c6616be3612dbfcce4a40db0` |
| Production ELF | `build-m11c-production/keyvalet.elf` | `7dc928a16f5162dd60c11f40e044f0c3fd15ed10541adb6f8e171ac3606cd878` |
| Diagnostic BIN | `build-m11c-diagnostics/keyvalet.bin` | 1,139,408; `92e5b7c358fcf48ec4e5f28240b00f76ec3e8549708ee478417b79ac42011b6b` |
| Diagnostic ELF | `build-m11c-diagnostics/keyvalet.elf` | `5801754312f49d5a3c8035eac740e3e9482c1f9f73ffdce7f86fb4c62e5e3311` |

Full embedded/runtime identities (descriptor ELF hash appended to embedded prefix):

```text
f7eaa18daedb-dirty-production-7dc928a16f5162dd60c11f40e044f0c3fd15ed10541adb6f8e171ac3606cd878
f7eaa18daedb-dirty-diagnostic-5801754312f49d5a3c8035eac740e3e9482c1f9f73ffdce7f86fb4c62e5e3311
```

Each OTA slot remains 2,621,440 bytes: production headroom 1,487,152 bytes;
diagnostic 1,482,032 bytes. Both generated partition binaries (under each
`build-m11c-*/partition_table/partition-table.bin`) hash to
`ad5968bd80275039f52efe875ee6ce7d765ab27b74b79bae8406de9383c5dfad`.
Checked packages are under each build's `package-m11c-final/` with release
metadata/manifests/SHA256SUMS. These are ignored generated review artifacts.

### Initial M11C acceptance and deferred boundaries

The initial offline report claimed no live ESP32 heap/fragmentation, handshake
peak, task margin, four-client TLS, Wi-Fi cycling, USB certificate transfer,
public-CA issuance, DNS, mobile/PWA or partition-transition results. Subsequent
operator evidence is recorded above. Status/log instrumentation is present;
linker residual RAM is not runtime free heap. Production static DIRAM consists
of 115,722 bytes text, 14,728 data and 38,400 BSS; diagnostic BSS is 38,408 bytes.
Linker-window residuals are 283,262/283,254 bytes respectively, before runtime
Wi-Fi/tasks/NVS/TLS allocations. USB status also exposes its task stack margin.
Physical acceptance uses the small
sequence in FACTORY_INSTALLATION; repeat lock/sensor actuation is not required.

Channel equivalence is rechecked: TLS/Origin/restart gates are transport-wide;
both lock/sensor pairs still share signed authorization, reservation/arbiter,
actuation, opening classification, timeout/reminder/alarm/silence/monitoring and
journal paths. The maintenance inhibit affects both equally; no channel-specific
exception or purpose-dependent branch was introduced.

Automatic HTTPS maintenance is contract-only and explicitly not implemented.
TOWNECORE_INTEGRATION_V1 revision 2 defines fresh current-key proof, limited
renew/rotate/address/poll operations, idempotency/replay, ownership and expired-
certificate/USB recovery. Google service execution, endpoint/trust provisioning,
and worker/backend rotation acknowledgment remain later integration work.
M12 OTA, authority rotation, cross-reset history epochs, hardware attestation,
irreversible lockdown and remaining electrical safety measurements remain deferred.

### Complete M11C checkpoint source-change inventory

The 55 intended modified/new/deleted paths are listed below, including the WSS
repair and acceptance closeout; removed
`http_policy` files/tests are replaced by the active, provisioned TLS Origin policy.
Generated build/package outputs are ignored and are not in this inventory.

```text
README.md
components/esp_https_server/CMakeLists.txt
components/esp_https_server/Kconfig
components/esp_https_server/LICENSE
components/esp_https_server/README.md
components/esp_https_server/include/esp_https_server.h
components/esp_https_server/src/https_server.c
docs/ARCHITECTURE.md
docs/DECISIONS.md
docs/FACTORY_INSTALLATION.md
docs/FIRMWARE.md
docs/PROTOCOL.md
docs/STATUS.md
docs/STORAGE.md
docs/TOWNECORE_INTEGRATION_V1.md
docs/WIFI_PROVISIONING.md
main/CMakeLists.txt
main/app_main.c
main/hardware_io.c
main/hardware_io.h
main/http_policy.c
main/http_policy.h
main/improv_serial.c
main/security.c
main/security.h
main/time_sync.c
main/time_sync.h
main/tls_identity.c
main/tls_identity.h
main/tls_material.c
main/tls_material.h
main/tls_usb.c
main/tls_usb.h
main/usb_provision.c
main/web_server.c
main/web_server.h
partitions.csv
tests/host_stubs/freertos/task.h
tests/host_stubs/nvs.h
tests/http_policy_test.c
tests/m5_acceptance_contract_test.py
tests/partition_transition_test.py
tests/stack_allocation_contract_test.py
tests/tls_identity_test.c
tests/tls_identity_test.py
tests/tls_transport_test.py
tests/usb_provision_test.c
tests/wifi_flash_gate_test.c
tests/ws_client_test.py
tests/ws_receive_test.py
tools/m5_console.html
tools/package_firmware.py
tools/tls_usb.py
tools/verify_partition_transition.py
tools/ws_client.py
```

## Milestone 10 — factory installation and production enrollment, accepted

Based on accepted M9 `1a7c6e810cc47f4e6a073de4053218db4cf628c4`, on
`hardware/keyvalet-v0.4.2`. The complete 31-path M10 implementation and acceptance
record form one local checkpoint. The operator performed the physical tests
recorded below; the implementation and closeout agent did not contact the PCB,
serial port or device network, or perform reset, erase or flash operations.

Completed:

- `tools/package_firmware.py` produces checked factory/app/component images,
  ESP Web Tools factory/replacement manifests, deterministic metadata and hashes.
  Factory merges are virgin-only. Compatible replacement writes the same app to
  both existing slots, preserving OTA selection and all stored data.
- Universal production first enrollment now works without a compiled root:
  protected USB `0x81` stages the intended P-256 public key for at most five
  minutes; the existing button window and ENROLL-V1 WebSocket proof of that exact
  key are still required. Only proof success commits the unchanged trust blob.
- Competing begins cannot overwrite a live challenge, substituted keys fail
  closed, same-key USB retries do not extend lifetime, and temporary state clears
  on failure/expiry/cancellation/reboot. Ambiguous trust writes latch further
  authorization closed until reboot. No enrolled-key overwrite or rotation API.
- The development-root header/config/API/helper path is removed, not forwarded.
  Both builds use the same initial enrollment. The reference signer uses only its
  operational key; CONTROL stages its public point and the browser relays proof.
- [FACTORY_INSTALLATION](FACTORY_INSTALLATION.md) defines exact packaging and
  installation semantics. [TOWNECORE_INTEGRATION_V1](TOWNECORE_INTEGRATION_V1.md)
  is the versioned implementation handoff for browser/backend/firmware ownership.

The USB key-binding design is explicitly authorized by the M10 architectural
clarification. A button-only arbitrary LAN-key TOFU design was **not** implemented.
Physical USB access remains the trust boundary, not authentication of the
browser/site by the device. Invalid LAN completions can cause denial of service,
but cannot pin a substituted key. Secure commissioning still requires correct
backend key custody, operator/device association and trusted host software.

Focused offline results passed:

- Actual `security.c` with the installed IDF mbedTLS P-256 sources built for host:
  initial enrollment, curve/hex/signature/time validation, USB/button requirements,
  competing requests/completions, cancellation/expiry/deadline clamping,
  fresh-process reboot/persistent trust, corrupt trust, signed commands/replay,
  and writes failing before/after a durable commit.
- USB dispatcher/parser in production and diagnostic variants, standard M9
  behaviors, staging field bounds and variant separation; real MONITOR/CONTROL
  over private PTY/IPC including fingerprint verification. No physical discovery.
- Real esptool merge, byte-identical repeat packaging, manifests/checksums,
  corrupted-image/size/overwrite rejection, and 4 KiB NOR write simulation with
  populated persistent regions and both OTA sequence parities/erased selection.
- Existing signing, targeted unlock, configuration, purpose-neutral/hardware,
  journal synchronization, browser/pairing, stack and WS client Python contracts;
  actual settings/replay canonical storage integration.
- Address/undefined-behavior sanitizers pass enrollment and both USB variants
  (fork/longjmp harnesses disable leak detection; crypto library itself is not
  sanitizer-instrumented). Whitespace and scope checks pass.

Both ESP-IDF v5.5.5 / esptool 4.12.0 builds pass, version `0.5.0-m5`, fixed
`keyvalet-v0.4.2` profile. Production has no privileged diagnostic commands.
No lock/sensor, arbiter, timing, audio, LED/button action, signed operational
canonicalization, Wi-Fi lifecycle, persistent format or partition changes.
Channel equivalence is unchanged: both pairs still share the same parameterized
authorization/controller/alarm/journal paths; M10 adds no channel-specific branch.

### Reviewed M10 artifacts

Use only these final `package/` directories, not intermediate preview packages.
No package contains private keys, compiled Wi-Fi credentials or a building root.

| Variant | App bytes | OTA-slot headroom |
| --- | ---: | ---: |
| Production | 1,030,080 | 1,591,360 (60.706%) |
| Diagnostic | 1,035,200 | 1,586,240 (60.510%) |

Absolute source binary/ELF paths and SHA-256:

```text
build-m10-production/keyvalet.bin
c3628fa2c21889a1d0b826eb91970c10e1ea314fab74aba7522ae8590b7f15ee
build-m10-production/keyvalet.elf
c59965da903f74037a174e0488bf31911860e8d6d3f82409a6d4d33062b0a2bc
build-m10-diagnostics/keyvalet.bin
92526c1690b7a04e6607511f8ec6a6bb0b84eef540de4839cd47c31082e348ea
build-m10-diagnostics/keyvalet.elf
cb1a2970c121baa691e392e3a0d083cd41854beac34eba6b551caffcb0447d94
```

Full embedded runtime identities:

```text
1a7c6e810cc4-dirty-production-c59965da903f74037a174e0488bf31911860e8d6d3f82409a6d4d33062b0a2bc
1a7c6e810cc4-dirty-diagnostic-cb1a2970c121baa691e392e3a0d083cd41854beac34eba6b551caffcb0447d94
```

Factories at offset `0x0`:

```text
build-m10-production/package/keyvalet-factory.bin
1095616 bytes; SHA-256 ed76e731c49210149a6cbc39786e01fc65c6e0f3b4f9015210f0b365cc89a0d4
build-m10-diagnostics/package/keyvalet-factory.bin
1100736 bytes; SHA-256 5ae887843da513804a09c526bfd4e30a0432460e25aeaea445e764b61dbd9b49
```

Each package's `keyvalet-app.bin` is byte-identical to its source `.bin` above.
Its `release.json` and `SHA256SUMS` contain exact component/manifest byte counts
and hashes; `manifest.factory.json` references the merge and
`manifest.replace.json` references the app at `0x10000`/`0x290000`.
Both partition binaries still hash to
`877f5e0f991bb1ecddfbc04d308f52628b5b2b9621104353ee4bd9c5eb12e177`.
All accepted M9 binary/ELF hashes below are preserved unchanged.
These are the accepted **pre-checkpoint** builds: committing the reviewed source
does not change their embedded M9-revision/dirty identities or their hashes.

### Operator physical acceptance — M10 production

The operator accepted the following on bench PCB `keyvalet-example`:

- **Destructive factory reset/install:** a successful complete esptool flash
  erase, followed by the verified 1,095,616-byte production factory image above
  at `0x0`. Write/hash verification passed and production firmware booted.
- **Wi-Fi commissioning:** existing MONITOR/CONTROL Improv scan/provisioning
  connected to `<bench-ssid>`, with DHCP address `<bench-dhcp-address>`.
- **First production enrollment:** the existing development helper's operational
  P-256 public key was staged through CONTROL option 10 / USB RPC `0x81`, with a
  `STAGED` receipt and 300-second lifetime. After WebSocket connection and an
  approximately five-second physical button hold, the reference console received
  `ENROLLED` and journal `ENROLLMENT_ACCEPTED` sequence **2**. WebSocket reconnect
  confirmed `enrolled:true`. The helper was a bench signer, not production
  backend key custody or a browser-held private key.
- **Separate data-preserving replacement:** browser disconnected and MONITOR
  stopped; the 3,072-byte table read at `0x8000` matched the production package
  byte-for-byte. The same verified production application was written to
  `ota_0=0x10000` and `ota_1=0x290000`; both writes passed esptool hash checks.
  No persistent-data regions or OTA selection metadata were erased/overwritten.

After replacement, status reported device `keyvalet-example`, variant `production`,
Wi-Fi connected at `<bench-dhcp-address>`, `enrolled:true`, journal `HEALTHY`, latest
sequence **3**, acknowledged sequence **0**, arbiter `READY` and `utc_valid:true`.
The reference console automatically retrieved all **three** available records.

Factory installation, initial production enrollment and non-destructive USB
replacement **passed**. Installation used **esptool**, not browser-native
flashing. The earlier whole-flash erase destroyed prior history; it was not the
later replacement. Physical continuity evidence is the observed fields/records
above, not a byte-for-byte qualification of every persisted domain. Full
write-range/data-preservation coverage remains the separate offline simulation.

### Remaining acceptance and product boundaries

Browser-native flashing/OS qualification and instrumented electrical/timing/
endurance safety remain unverified. The accepted operator procedure and remaining
browser qualification boundary are in FACTORY_INSTALLATION; no redundant
lock/sensor or M9 failure-matrix testing is requested.

There is no journal epoch identifier across destructive resets; the handoff
specifies explicit history quarantine now and the minimal future device/ACK
epoch decision. Authority rotation/recovery is not implemented. Existing
persisted trust is preserved, not re-enrolled on replacement. M11 must resolve
HTTPS/WSS, localhost-origin and browser local-network constraints. M12 must
implement/qualify OTA authorization, boot selection/validation and downgrade
continuity; an application payload is not an OTA service. Neither milestone nor
TowneCore UI/business logic was begun in M10.

### Exact M10 source scope (31 paths)

- Enrollment firmware: `main/security.c`, `main/security.h`,
  `main/usb_provision.c`, `main/web_server.c`; retired root removed:
  `main/enrollment_root.h`, `main/Kconfig.projbuild`, `sdkconfig.diagnostics`.
- Reference workflow: `tools/improv_serial.py`, `tools/kv_test.py`,
  `tools/m5_signing_helper.py`, `tools/m5_console.html`.
- Offline release packaging: `tools/package_firmware.py`.
- New focused tests: `tests/enrollment_test.c`, `tests/factory_package_test.py`;
  updated contracts/integration: `tests/m5_security_test.py`,
  `tests/m5_acceptance_contract_test.py`, `tests/usb_provision_test.c`,
  `tests/usb_wifi_tool_test.py`; peripheral stubs:
  `tests/host_stubs/esp_err.h`, `tests/host_stubs/esp_random.h`,
  `tests/host_stubs/esp_ota_ops.h`.
- Documentation: `README.md`, `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`,
  `docs/FIRMWARE.md`, `docs/PROTOCOL.md`, `docs/STATUS.md`, `docs/STORAGE.md`,
  `docs/WIFI_PROVISIONING.md`, `docs/FACTORY_INSTALLATION.md`,
  `docs/TOWNECORE_INTEGRATION_V1.md`.

Obsolete ignored workstation root/header files and private signing files are not
read, rewritten or deleted by the new firmware/packager. Their ignore rules are
retained to protect user data, not to support an old trust path.

## Milestone 9 — USB Wi-Fi provisioning, accepted and checkpointed

M9 replaces compile-time credentials with standard Improv Serial in production
and diagnostic firmware. One USB receiver dispatches provisioning and, only in
diagnostic builds, the retained development commands. CONTROL adds Wi-Fi Setup
through MONITOR's actual framed RPC/results, with hidden entry and non-echoed
passwords. Binary TX bypasses VFS CRLF conversion; logs cannot split packets.

One network worker owns scans, candidate association/DHCP, timeout/rollback and
backoff. The last successful credentials stay in `nvs/keyvalet_wifi/station`;
NVS commit follows DHCP and uses the existing output flash gate. No pairing,
button, new authentication mechanism, AP or BLE setup is required. No reboot is
needed for a successful change. Ambiguous persistence errors are explicit and
block further credential successes until reboot, rather than claiming durability.

Canonical device identity, GPIOs, lock-channel equivalence, policy, signing,
replay, journal/ACK formats, partition layout and LED meanings are unchanged.
Focused worker/store, USB variant/parser, real flash-gate and private-PTY tooling
tests cover the new boundaries.

The operator physically accepted diagnostic M9 on the v0.4.2 PCB: USB Improv
through MONITOR/CONTROL, correct device identification, nearby 2.4 GHz scan and
selection, provisioning `<bench-ssid>` with DHCP `<bench-dhcp-address>`, persisted-credential
reconnection after reboot, wrong-password candidate failure, and restoration of
the working network confirmed by its connection chirp. These results remain
accepted and were not repeated during closeout. They do not qualify electrical,
thermal, power-loss durability or exact pulse timing.

Closeout corrects CONTROL's former use of a URL as the current network name.
Read-only KeyValet RPC `0x80` returns connection state and the live SSID from the
existing network snapshot, empty unless connected. Standard Improv URL results
remain URLs. The query adds no credentials reader, state cache, forwarding
wrapper or state-machine change. Firmware and CONTROL must both include this
correction; it is offline-verified and does not require additional PCB testing.

Build artifacts use isolated `build-m9-production`/`build-m9-diagnostics`
directories; accepted M7 artifacts below are preserved. Implementation and
closeout form one checkpoint on `hardware/keyvalet-v0.4.2`, based on
`3d8b9b40d7c3753bdc2caf383e06912db6db8a7c`. No PCB was contacted or flashed by
this closeout. See [WIFI_PROVISIONING.md](WIFI_PROVISIONING.md) for exact scope,
failure/credential lifecycle, tests and user-operated acceptance workflow.

Verified M9 artifacts (ESP-IDF v5.5.5, version `0.5.0-m5`, fixed profile
`keyvalet-v0.4.2`):

| Variant / build prefix | Application bytes | OTA-slot headroom |
| --- | ---: | ---: |
| `3d8b9b40d7c3-dirty-production` | 1,027,376 | 1,594,064 (60.809%) |
| `3d8b9b40d7c3-dirty-diagnostic` | 1,034,016 | 1,587,424 (60.555%) |

Paths and SHA-256:

```text
build-m9-production/keyvalet.bin
1b6872db7afa026e9ba8d32f78697e2a2824ae7139986e5a06030ea3dec4fc8c
build-m9-production/keyvalet.elf
2119787b765d58ce2ee9d42d20f7562af7c434bef926002e5acc2f49477e84b5
build-m9-diagnostics/keyvalet.bin
bf0a8f8cb09c4228ac963b2434ecd895683f6d20dcbef6533f27ab3bd1a17c9f
build-m9-diagnostics/keyvalet.elf
d78029cb0f8dfef5f35da03d25d9018a95c1552afdb33b1cc5b0defb122bd886
```

Full runtime identities append the corresponding ELF SHA-256 to the build
prefix. These pre-commit prefixes identify the actual rebuilt artifacts, not
the eventual checkpoint SHA. They replace the pre-correction M9 images; their
different hashes do not imply a second physical test. Both generated images
pass checksum/hash validation and their embedded
ELF hashes match the files. Production contains Improv but no diagnostic
handler symbols or `KVTEST` command strings. Both partition binaries retain
SHA-256 `877f5e0f991bb1ecddfbc04d308f52628b5b2b9621104353ee4bd9c5eb12e177`.
Accepted M7 application and ELF hashes below remain unchanged.

Initial M9 offline verification passed: four new C executables (worker/store,
production/diagnostic USB and credential flash gate), three affected C
integration executables (controller, canonical storage, journal), eight
existing Python security/configuration/hardware contracts and the private-PTY
MONITOR/CONTROL integration. Address/undefined-behavior sanitizer runs passed
for the new worker and both USB variants; leak detection was disabled in these
fork/longjmp harnesses. Closeout reran the affected worker/store and both USB
variants with address/undefined-behavior sanitizers, plus the private-PTY tool
tests: live SSID, nonconnected states, simulated reinitialization/saved boot,
32-byte/UTF-8 SSIDs, malformed replies, separate URLs and no password output.
Both production/diagnostic builds pass, with unchanged partition binary hashes.
Full diff review confirms canonical operational protocols, policy, controller,
GPIO/arbiter, replay, settings and journal sources are unchanged; both lock/sensor
pairs retain the same parameterized paths without new channel-specific branches.
No secrets or generated artifacts are included. Whitespace checks pass.
Remaining M9 qualification includes production-image physical provisioning,
hidden-network/USB-loss scenarios not reported above and NVS latency/power-loss
durability. No browser UI, factory packaging, TLS or OTA was implemented, and
Milestone 10 has not begun.

## Milestone 7 final checkpoint — implementation and runtime smoke acceptance

KeyValet v0.4.2 is the sole first-product hardware target. This checkpoint on
`hardware/keyvalet-v0.4.2` combines Milestone 7 Pass 3/3B, canonical storage
cleanup and final configuration refinement, based on
`b003ba979d0e76940bf09ba06a064bc71fd90ecf`. It includes retrieval, signed ACK,
circular retention, loss reporting, CONFIG-V3 dependencies and UNLOCK-V2 zero
timeouts. This is not a firmware v1.0 release.

Both locks now persist `sensor_required` (default true). False deliberately
allows signed solenoid-only operation with an OPEN/unknown input, without a door
lifecycle or reminder. Monitoring remains independent. Explicit unlock timeout
zero disables only that operation's authorized-opening reminder; omitted timeout
snapshots the positive persistent default. Values 1–4 remain invalid. Required
sensors still must be known CLOSED, and unexpected openings remain urgent when
monitored. Both channels use the same implementation for both dependency modes.

Administrative configuration now uses only CONFIG-V3, cryptographically binding
both dependency flags plus all existing mutable settings and CAS revision. The
single 28-byte settings schema is version 4. There is no CONFIG-V2 forwarding,
old signature reader, migration, dynamic mapping or lock-enabled subsystem.
UNLOCK-V2 retains distinct `0` versus `DEFAULT` canonical bytes. The reference
console reads/displays both dependencies, changes them through signed admin,
and distinguishes zero, blank/default and positive timeout overrides.

The per-lock guard snapshots dependency during authorization. Dependency changes
invalidate old controller reservations/door bookkeeping, never an active finite
hardware pulse or recovery. Closing a door ends its authorized-opening window,
preventing a zero override from suppressing a subsequent unexpected opening.
Settings persistence errors disable subsequent writes until reboot reload,
preserving durable CAS after ambiguous commits.

Removed settings schema-1 migration and the schema-2 retired delay word,
whole-window replay import and its duplicate RAM header/CRC, historical journal
event names/IDs and the unused LOG_SYNC event, append-only journal adoption,
obsolete FULL health state, and the stale production "no actuation path" boot
message. The helper/console now require all four canonical entries in a complete
CONFIG-V3 message, with both lock entries and false monitoring allowed for any/all sensors. There are no
compatibility forwarding aliases or older storage readers.
The unused diagnostic-active forwarding wrapper and its uncalled hardware
query are removed; structured lock status remains the operational interface.

Hardware capacity is **up to two solenoids/up to four sensors**, not six required
connections. All four sensor entries in CONFIG-V3 are required, but false monitoring is
valid for any unused sensor. Code inspection confirms disabled paired inputs
get neither urgent alarm nor reminder deadline; disabled sensor-only inputs
get neither alarm nor audible preemption. Applying monitoring=false clears that
input's alarm/reminder without changing another channel's state. The helper and
console check completeness/booleans, never physical population. There is no
all-CLOSED/all-enabled/global sensor interlock. With sensor_required=true, each
unlock checks only its own known-CLOSED paired sensor/operation guard and the
shared arbiter; an unused other lock is not a veto. With sensor_required=false,
no door input is needed to complete the operation. Monitoring=false by itself
never bypasses the CLOSED prerequisite of a sensor-required lock.

Settings has one 28-byte schema-4 format; replay has one 12-byte version-2
initialization marker and 160 current 64-byte records. Trust, journal record and
ACK formats retain their current single readers, CRCs and version identifiers.
UNLOCK-V2, CONFIG-V3, ENROLL-V1, JOURNAL-ACK-V1 and `/api/v1/` remain current
contracts, not backward-compatibility paths. See [STORAGE.md](STORAGE.md).

The operator has completed the one-time development reset by fully erasing the
PCB and freshly installing bootloader, partition table, OTA initialization and
the verified diagnostic application. Canonical storage initialized successfully;
no further reset is required for this checkpoint. Settings, enrollment, history
and ACK restarted together. Do not reset replay independently of trust or
journal independently of its checkpoint/ACK. The checkpoint review itself made
no hardware contact. See [STORAGE.md](STORAGE.md) for the reset boundary.

## Physical runtime smoke acceptance — current diagnostic image

The operator reports successful clean installation and runtime smoke acceptance
on the v0.4.2 PCB with full identity:

`b003ba979d0e-dirty-diagnostic-bc6d7dc7251d0fc04c69e32c899c152bf717b556e2d8003438096a0d2e5058e7`

Application: `build-diagnostics/keyvalet.bin`, SHA-256
`da4e1058ab5516ec617607040130bab369a788b23290a8067a683d138451be09`.

- Boot, Wi-Fi and local WebSocket connection succeeded; the correct v0.4.2
  profile and both locks/all four sensors appeared in the purpose-neutral welcome.
- Both locks reported the default `sensor_required: true`; arbiter was READY.
- The fresh circular journal was HEALTHY and synchronization available, with
  sequence 1 and acknowledged sequence 0.
- The browser automatically retrieved the one outstanding record on connection.
- `enrolled: false` correctly reflected the complete erase.

This accepts clean installation, canonical storage initialization, WebSocket
operation and opportunistic retrieval on actual hardware. It does **not** accept
physical signed CONFIG-V3 updates, signed journal ACKs, circular wrapping or
zero-timeout unlocks after this flash; those remain offline-verified only.
Earlier lock/sensor/alarm and signed-targeting acceptance is recorded separately
below and is not presented as a rerun against this image.

## Preserved synchronization and retention

Every WebSocket welcome announces journal bounds, produced watermark, durable
ACK and exact first loss interval. The console retrieves opportunistically and
on explicit request, retries/deduplicates, and exports records plus loss ranges.
Only explicit authority-signed acknowledgment advances the cursor; browser
receipt is not durable TowneCore persistence.

The unchanged 2.5 MiB journal partition has 640 sectors and 40,960 64-byte slots.
Oldest-sector reclamation is independent of synchronization. Healthy post-fill
retention is 40,897–40,960 records. Durable PREPARED precedes erase; verified
erase precedes durable READY; verified body/CRC precedes the commit marker.
Blank initialization uses that same transaction. Any occupied storage without
metadata fails closed without mutation; there is no development-data adoption.

Sequence generations survive wraps and canonical reboot. Occupied partial
writes consume sequences and are reported as gaps. A never-committed attempt
that left all bits erased may reuse its number. No unfilled sector tail is
reported as produced. Missing prefixes and internal holes are explicit, including
the exact cursor-100/oldest-151 interval 101–150. Read batches are at most eight
records and examine at most 16 retained positions. See
[JOURNAL_RETENTION.md](JOURNAL_RETENTION.md).

## Channel equivalence

Code inspection confirms both Lock/Sensor 1 and Lock/Sensor 2 share parameterized
signed authorization, reservations, durable replay and target-identified journal
acceptance, the arbiter/timer/pulse owner, expected-opening classification,
per-operation timeout, monitoring enforcement, unexpected-opening alarms,
reminder activation/clearing, silence and journal generation. The journal flash
gate is also shared. There are no channel-specific policy branches.

The intentional data differences are GPIO10/6 versus GPIO3/7, one-based journal
detail, and default monitoring on for Sensor 1/off for Sensor 2 (both settings
are configurable). The USB parser has two command strings but calls the same
parameterized diagnostic function. Diagnostic expected-opening bookkeeping
does not change production signed-unlock semantics. Sensors 3–4 are sensor-only
and appropriately have no lock timeout/authorization path.

Channel-equivalence review remains mandatory in every subsequent firmware and
acceptance report. Prior Lock/Sensor 1 functional evidence is representative for
the shared logic with independently accepted Lock/Sensor 2 hardware; it is not
a direct physical measurement of Sensor 2's five-second reminder.

## Offline verification

- The real controller/GPIO owner/timer/arbiter passes 12 mirrored cases: both
  locks with required OPEN/unknown rejection, monitoring=false prerequisites,
  sensor-independent unknown/OPEN unlocking, zero/default/positive timeouts,
  unexpected-opening protection, reminder clearing, optional sensor monitoring,
  dependency changes during reservation/active pulse and shared pulse/recovery.
- Real HTTP config handler parsing/serialization with IDF cJSON passes both
  lock flags, all four combinations, incomplete/non-boolean rejection,
  CONFIG-V2 rejection, authentication denial and CAS/persistence/application
  ordering. Crypto/NVS are separate test boundaries, not replaced by these stubs.
- P-256 verification rejects tampering of either dependency, zero-to-DEFAULT,
  lock target, timeout, config default or ACK cursor. The Node console test
  exercises both checkboxes, complete signed-admin requests, effective display,
  zero/blank/positive signing requests and invalid timeout rejection.
- Focused C regressions pass: audio, control policy, HTTP origin, sensor debounce,
  settings schema, UNLOCK/CONFIG canonical bytes, replay window/records, arbiter,
  journal codec and ACK contract.
- Real settings/replay adapters pass blank initialization, populated reboot,
  both dependency flags/all four combinations across reboot and CAS, settings
  CRC/invalid-mask rejection, failed/ambiguous settings commits, replay initialization failures, 1,001 durable entries
  with repeated wraps, missing-marker/orphan rejection, and failures before or
  after durable replay commit.
- Ring simulation passes 123,657 unsynchronized appends, repeated full-size
  wraps, 105 initialization/append power cuts, interrupted reboot erases,
  corrupted/missing checkpoint rejection, CRC/commit loss and exact gaps.
  Address, undefined-behavior and leak sanitizers pass.
- Real journal/cursor/GPIO/arbiter host integration passes ACK reboot,
  failed/lost commits, monotonicity, loss accounting, and both lock-channel
  flash-gate tests. Real P-256 ACK signatures/tampering and authorization order
  pass separate focused tests; this is not end-to-end hardware ACK acceptance.
- Existing Python security, protocol, configuration, browser/pairing, allocation,
  WebSocket tests and Node retrieval/configuration tests pass. Allocation-failure
  serialization emits no incomplete event and leaks no partial object.
- ESP-IDF Linux NVS emulator passes 10,000 writes, 9,840 replacements and 72
  NVS reinitializations with the compact marker's storage footprint. Stable
  used/available entries are 644/1,246; 308 simulated sector erases.
- Canonical ESP32-C6 production/diagnostic builds and `git diff --check` pass.
  No partition-table change, tracked secrets, or tracked generated outputs.

Firmware version remains `0.5.0-m5`; both artifacts embed hardware profile
`keyvalet-v0.4.2` and ESP-IDF v5.5.5. Isolated verified build identities:

| Variant / build ID | Application bytes | ELF SHA-256 |
| --- | ---: | --- |
| `b003ba979d0e-dirty-production` | 1,007,408 | `6b3554c2cb6facc59130b610df6540834994711158ef28b2629c1e51f515295c` |
| `b003ba979d0e-dirty-diagnostic` | 1,022,816 | `bc6d7dc7251d0fc04c69e32c899c152bf717b556e2d8003438096a0d2e5058e7` |

Application binary SHA-256: production
`b1db96a82ae957a091284bb3a4cb3765b4320705566facc9f8bdaf417a4aedac`;
diagnostic `da4e1058ab5516ec617607040130bab369a788b23290a8067a683d138451be09`.

Production artifacts are `build/keyvalet.bin` and
`build/keyvalet.elf`; diagnostic artifacts are
`build-diagnostics/keyvalet.bin` and
`build-diagnostics/keyvalet.elf`. Read-only checkpoint
inspection verified all four hashes, image checksums/validation hashes, and each
application descriptor's matching ELF hash. These accepted artifacts were not
rebuilt for the Git checkpoint: their embedded identities intentionally retain
the pre-checkpoint revision and `dirty` marker. The full runtime identity appends
the ELF SHA-256 to the build ID shown above.

Compared with the preceding canonical-cleanup builds, this refinement adds
1,264 production bytes and 1,312 diagnostic bytes. DIRAM increases 16 bytes:
production 160,884 bytes (35.58%); diagnostic 164,922 bytes (36.48%). App-slot
free space remains 62%/61%. Ring static state remains 5,200 bytes on C6 with a temporary 4 KiB
boot scan buffer. The partition binary SHA-256 remains
`877f5e0f991bb1ecddfbc04d308f52628b5b2b9621104353ee4bd9c5eb12e177`
for both variants.

## Physically accepted baseline — before this work

Milestone 7 Passes 1/2 diagnostic ELF SHA-256
`ef84785dd66185fa5643bf728909aee25416e5dc49a7403f339b81df82917ed5`
completed physical acceptance before cleanup/synchronization changes. Acceptance
covered purpose-neutral status for both locks and all four sensors, then-current
settings persistence, physical pairing/enrollment, signed V2 actuation of each
lock, CONFIG-V2 revision update, Sensor 2 audible urgent alarm, and an approximately
five-second Lock/Sensor 1 reminder clearing on close. Journal authorization
events identified their lock targets. Sensor 2's five-second reminder was not
directly measured. WebSocket-connected LED and audible IP discovery were confirmed.

Earlier bench acceptance covered USB/12 V operation, two connected solenoids,
four sensors, button, LED, piezo, independently recorded simultaneous openings,
ACTIVE/RECOVERY/READY behavior, cross-lock and rapid same-lock PULSE_BUSY
rejection, and the provisional 2000 ms recovery interval. These functional
observations do not establish exact pulse width, electrical isolation, coil
decay, brownout safety, loaded rail stability or thermal endurance.

## Remaining qualifications and deferred v1 work

- Physical acceptance of dependencies/zero timeouts, signed CONFIG-V3 updates,
  signed ACK persistence, circular wrapping, loss and retention recovery remains
  outstanding beyond the accepted clean-install/retrieval smoke test.
  Deliberately verify monitoring policy for each
  unused input and both dependency modes before operation with loads.
- Instrumented flash/NVS latency, endurance, brownout, pulse width and electrical/
  thermal qualification. The shared journal gate excludes active pulses, but
  non-journal replay/settings/ACK NVS writes retain existing timing limitations.
- Final TowneCore durable-persistence/API integration and reference-client contract.
- Production authority provisioning, recovery, secure browser transport/HTTPS,
  OTA/downgrade and physical recovery policy.

No HTTPS or new TowneCore integration is implemented in this correction.

## Checkpoint scope

This includes preserved Pass 3/3B work, canonical cleanup and configuration refinement:
52 previously tracked files and 31 new files. The final scoped review and
regression rerun passed without functional source changes (three new files had
trailing blank lines removed during the index whitespace audit). No prototype paths, compatibility
forwarders, secrets, generated artifacts or partition changes are included.
Local checkpoint message: `Milestone 7: durable journal sync and canonical device policy`.
The commit SHA and final Git status are reported after checkpoint creation.

<details>
<summary>Complete intended file list</summary>

```text
M  README.md
M  docs/ARCHITECTURE.md
M  docs/DECISIONS.md
M  docs/FIRMWARE.md
M  docs/HARDWARE.md
M  docs/PROTOCOL.md
M  docs/STATUS.md
M  docs/hardware/v0.4.2/FIRMWARE_HANDOFF.md
M  main/CMakeLists.txt
M  main/app_main.c
M  main/config_contract.c
M  main/config_contract.h
M  main/control_logic.c
M  main/control_logic.h
M  main/device_controller.c
M  main/device_controller.h
M  main/diagnostic_console.c
M  main/diagnostic_solenoid.c
M  main/diagnostic_solenoid.h
M  main/hardware_io.c
M  main/hardware_io.h
M  main/journal.c
M  main/journal.h
M  main/journal_format.c
M  main/journal_format.h
M  main/replay_record.c
M  main/replay_record.h
M  main/replay_store.c
M  main/replay_store.h
M  main/security.c
M  main/security.h
M  main/settings.c
M  main/settings.h
M  main/settings_schema.c
M  main/settings_schema.h
M  main/unlock_contract.c
M  main/web_server.c
M  tests/config_contract_test.c
M  tests/control_logic_test.c
M  tests/journal_format_test.c
M  tests/m5_acceptance_contract_test.py
M  tests/m5_security_test.py
M  tests/m7_purpose_neutral_test.py
M  tests/m7_targeted_unlock_test.py
M  tests/nvs_replay_stress/README.md
M  tests/nvs_replay_stress/main/stress.c
M  tests/replay_record_test.c
M  tests/settings_schema_test.c
M  tests/stack_allocation_contract_test.py
M  tests/unlock_contract_test.c
M  tools/m5_console.html
M  tools/m5_signing_helper.py
+  docs/JOURNAL_RETENTION.md
+  docs/STORAGE.md
+  main/journal_ring.c
+  main/journal_ring.h
+  main/journal_sync.c
+  main/journal_sync.h
+  main/journal_sync_contract.c
+  main/journal_sync_contract.h
+  tests/config_http_test.py
+  tests/controller_configuration_test.c
+  tests/host_stubs/driver/gpio.h
+  tests/host_stubs/esp_check.h
+  tests/host_stubs/esp_crc.h
+  tests/host_stubs/esp_err.h
+  tests/host_stubs/esp_log.h
+  tests/host_stubs/esp_partition.h
+  tests/host_stubs/esp_system.h
+  tests/host_stubs/esp_timer.h
+  tests/host_stubs/freertos/FreeRTOS.h
+  tests/host_stubs/freertos/queue.h
+  tests/host_stubs/freertos/semphr.h
+  tests/host_stubs/freertos/task.h
+  tests/host_stubs/nvs.h
+  tests/host_stubs/nvs_flash.h
+  tests/journal_console_test.js
+  tests/journal_integration_test.c
+  tests/journal_json_test.py
+  tests/journal_ring_test.c
+  tests/journal_sync_contract_test.c
+  tests/m7_journal_sync_test.py
+  tests/storage_integration_test.c
```

</details>
