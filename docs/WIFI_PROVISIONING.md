# USB Wi-Fi provisioning — Milestone 9

## Scope and security

KeyValet v0.4.2 exposes Improv Wi-Fi Serial in **both production and diagnostic
firmware**, over its existing native USB Serial/JTAG port. The Torx enclosure,
cabinet bracket and mechanical override establish the accepted physical-access
boundary. Running-firmware Wi-Fi setup needs no button, enrollment, pairing
window, network authentication, admin password, BLE or temporary access point.

This interface only identifies the device, reports nonsecret connectivity,
scans and configures Wi-Fi. It never returns a saved password or changes lock
policy, trust, enrollment, replay, journal or ACK state. It has no firmware
installation commands. Physical ROM USB recovery is a separate existing
capability; Secure Boot/eFuses were not changed.

Production has no `KVTEST` diagnostic handlers. Diagnostic builds retain their
existing finite actuator, LED, chirp, status, journal-read and safe-reboot commands.
Adding provisioning does not make those commands production capabilities.

M10 adds only a separate, explicitly KeyValet-specific USB public-key staging
RPC `0x81` to this same receiver. It is not Wi-Fi configuration or completed
enrollment. Physical button participation and the existing WebSocket proof still
establish persistent trust. See [PROTOCOL — Enrollment](PROTOCOL.md#enrollment)
for its exact contract; all standard Improv and M9 `0x80` behavior stays intact.

## Credential lifecycle

There are no compiled Wi-Fi credentials and no fallback to a local header.
The former wrapper/example headers were removed. An ignored old
`wifi_credentials.local.h` may remain on a developer workstation to avoid
discarding private user data; it is not included or used by either build.

One network worker initializes station Wi-Fi even with no credentials. In that
state it does not connect automatically, but scans and USB provisioning work;
autonomous sensors, alarms and journal behavior remain independent of Wi-Fi.

Credentials live in one 112-byte, version-1 CRC-protected NVS blob:
default `nvs`, namespace `keyvalet_wifi`, key `station`. There is no second driver
credential store (`WIFI_STORAGE_RAM`) and no CONFIG-V3 field/schema change.
See [STORAGE.md](STORAGE.md). Passwords are plaintext in the current unencrypted
NVS, protected by the accepted physical boundary, not by the Improv checksum.

For a new candidate:

1. Keep the last successfully committed credentials; put the candidate only in RAM.
2. Stop the station and consume its STOP event before applying the candidate.
   This isolates queued prior connection events. A candidate has up to 30 seconds
   for association and DHCP after restart, with a separate five-second stop bound.
3. Require an associated AP matching the candidate and a nonzero current netif
   address matching the acquired-IP event. Internet access/SNTP is not required.
4. Wait for any active lock pulse through the existing output flash gate, hold
   that gate across NVS set/commit, then report success. No reboot is required.
5. On wrong password, unavailable network, disconnect or DHCP timeout, discard
   the candidate and restore the previous configuration. Previously unreachable
   networks remain unreachable; the guarantee is preservation of credentials.

Ordinary reconnect uses the same worker and 1, 2, 4, 8, 16, 30-second backoff,
capped at 30 seconds. Boot/reconnect and an unchanged successful submission do
not rewrite credentials. A lost USB client does not cancel the bounded device
transaction; reconnect and query state to determine its current outcome.

An NVS error is not durable success. The device emits standard Improv unknown
error `0xff`, logs an explicit nonsecret persistence failure, and restores its
previous runtime configuration. Because an error can follow an actually durable
commit, reboot may load the candidate instead. Further credential successes
are blocked until reboot reloads durable state, including submissions matching
the old RAM credentials. There is no automatic erase or stale-data rewrite.
Power loss before candidate commit leaves the last committed credentials;
atomic single-blob NVS recovery remains the IDF durability assumption.

Missing credentials mean unconfigured. Corrupt/unsupported Wi-Fi data is not
silently erased or adopted: the service stays unconfigured and permits explicit
USB replacement of that one Wi-Fi record. Other namespaces are never reset.

## Wire protocol and limits

The implementation follows [Improv Serial](https://www.improv-wifi.com/serial/):
`IMPROV`, version 1, packet type, one-byte payload length, payload, and additive
eight-bit checksum over preceding bytes. Output adds a trailing LF outside the
frame. RPC data has its own command/length and length-prefixed UTF-8 strings.

| RPC | Supported behavior |
| --- | --- |
| `0x01` Wi-Fi settings | Submit SSID/password; state 3 during attempt; state 4 and URL result only after durable success |
| `0x02` Current state | STOPPED=0 if unavailable, AUTHORIZED=2 offline/unconfigured, PROVISIONING=3, PROVISIONED=4 online; online URL result |
| `0x03` Device information | `KeyValet`, firmware version, `ESP32-C6`, canonical device ID |
| `0x04` Networks | SSID, RSSI, standard YES/NO authentication flag; fourth string describes actual authentication type; empty result terminates list |
| `0x07` Network state | Standard optional RPC: flags 2=Wi-Fi/offline, 3=Wi-Fi/online; online URL follows |
| `0x80` KeyValet current network | Read-only vendor query, no data: two strings, connection state and actual current SSID |

The returned URL, when TLS is commissioned and ready, is the implemented
`https://COMMISSIONED-HOSTNAME/api/v1/status` resource, not a
new hosted setup page. Standard Improv does not expose the current SSID, and the
M11C HTTPS status additionally reports SSID. CONTROL still uses the narrowly
scoped USB `0x80` extension before TLS commissioning, not a URL slot or remembered selection. Its
result always contains exactly two strings: `connected`, `disconnected`,
`unconfigured`, `provisioning` or `unavailable`, followed by the SSID only when
connected (otherwise an empty string). It reads the existing locked network
snapshot, whose SSID is established after matching AP/DHCP validation and cleared
on disconnect. Saved-network reconnection after reboot uses that same path.
No password, stored candidate, additional state cache or mutation is exposed.
Standard RPCs `0x01`–`0x04` and `0x07` retain their original wire behavior.
The `0x80` ID is KeyValet-specific, not assigned by the Improv standard.
Standard Improv hostname/device-name mutation RPCs are not implemented and return
unknown-command. Protected TLS hostname/Origin commissioning uses the distinct
KeyValet vendor RPC `0x82` documented in PROTOCOL, not an Improv command extension.
Errors use standard codes: 1 malformed/busy/unavailable request, 2 unsupported
RPC, 3 failed connection/scan/timeout, 255 internal/persistence failure. Standard
Improv has no dedicated persistence-error code; MONITOR's nonsecret device log
gives the specific reason. A successful local IPC write is never a Wi-Fi result.

Frames are at most 265 bytes. Nested lengths, checksums, UTF-8, embedded NULs
and field bounds are validated. Partial RX frames expire after one second;
a new binary header resynchronizes interrupted input. Diagnostic text is never
parsed from a credential payload. Binary packets bypass console CRLF conversion
and enqueue atomically through the same USB driver with a 50 ms write timeout
and 1 KiB TX buffer. The stdout lock prevents ordinary log/diagnostic writers
from splitting packets. Disconnection/backpressure can drop responses: retry
state/read requests rather than treating missing responses as success.

SSID length is 1–32 UTF-8 bytes. Text must exclude NUL, control characters,
overlong UTF-8 and surrogate encodings. Password is empty for an explicitly
selected open network, 8–63 UTF-8 bytes, or 64 hexadecimal PSK digits. Nonempty
passwords require at least WPA2-Personal; WPA3 SAE uses both PWE methods when
enabled in IDF. WEP, WPA-only and enterprise provisioning are not supported.

Scanning is native to the C6's **2.4 GHz radio**. At most 32 AP records are
read; empty/unsafe SSIDs are skipped. Equal SSID/security entries collapse to
the strongest result; ordering is descending RSSI, then SSID and authentication.
Different security types under one SSID remain distinguishable. Manual SSID
entry supports hidden networks. An established connection is scanned without
an intentional disconnect; RF/dwell effects still require bench acceptance.
Reconnecting stations are stopped before scanning to avoid concurrent connect
and scan operations. Scans have a ten-second bound after start.

## Terminal workflow

MONITOR claims exclusive terminal access (`TIOCEXCL`). CONTROL connects through its mode-0600
Unix socket and uses actual standard Improv frames/results. IPC permits one
outstanding Improv RPC, has a 2 KiB request bound and finite response deadlines.
Passwords are entered with `getpass` (no echo fallback), never in command-line
arguments, logs or generated artifacts. MONITOR suppresses binary frames,
including echoed credential RPCs, from ordinary terminal output.

After separately authorized installation, run in the repository:

```sh
python3 -B tools/kv_test.py monitor --port '<operator-verified-port>'
```

In another terminal:

```sh
python3 -B tools/kv_test.py control
```

Choose **9 — Wi-Fi Setup**. The tool identifies KeyValet, queries current
connectivity and actual SSID via `0x80`, scans, displays numbered results, offers
`M` for manual/hidden SSID, and reads a non-echoed password. It reports progress and only reports
saved success after the device's state-4 plus successful credential RPC result.
Success displays requested SSID and resulting IP. A failure/USB timeout is not
success; query/retry after reconnect. Explicit `--port` never falls back to
another serial device if that path disappears.

Existing diagnostic menu choices 1–8 remain. For one command through MONITOR:

```sh
python3 -B tools/kv_test.py command STATUS
```

`OK sent STATUS` confirms only transmission; read firmware `KVRESP` in MONITOR.
No diagnostic hardware commands are executed by offline verification.

## Browser expectations

A future Web Serial client can provision with the standard RPCs directly, without
CONTROL, the signing helper or a new firmware provisioning mechanism. It needs
a supported secure-context browser, user permission/port selection, and exclusive
USB ownership. Stop MONITOR before browser access or flashing. Native USB may
disconnect/re-enumerate across reset; reacquire the port and query state.
Application-reported identity is not cryptographic device authentication.
No browser installer/UI, TLS, OTA or enrollment changes are included in M9.

## Offline evidence and physical acceptance

- `wifi_provision_test.c`: real network worker/store with fake IDF events/NVS;
  fresh/saved/corrupt boot, DHCP success/timeout, failed candidate recovery,
  backoff, busy requests, scan ordering/deduplication, stale events, event loss,
  USB-independent completion, CRC and ambiguous persistence.
- `usb_provision_test.c`: real receiver and handlers compiled as production and
  diagnostic; framing/limits, 100,000 noise bytes, malformed/truncated recovery,
  diagnostic coexistence, no password output and disconnected TX.
  Closeout cases cover live SSID, empty SSID in every nonconnected state,
  reinitialized snapshots, 32-byte/UTF-8 names, and unchanged standard URL results.
- `wifi_flash_gate_test.c`: real credential adapter, GPIO/timer owner and arbiter;
  both programmed 350 ms pulses finish before credentials are written; a
  competing pulse is excluded during commit.
- `usb_wifi_tool_test.py`: real MONITOR/IPC over a private pseudo-terminal,
  fragmented frames/logs, scanning, actual results rather than send receipts,
  hidden entry and secret suppression. No physical port discovery occurs.
  Closeout cases cover actual SSID presentation in independent client sessions,
  all nonconnected states, malformed replies and URL/SSID separation.
- Affected mirrored controller, hardware, security/configuration contracts and
  canonical storage regressions passed in the initial M9 verification; their
  sources and canonical formats/partitions remain unchanged. Sanitizers cover the new parser/worker tests; forked
  harnesses disable leak checking and are not a claim of leak qualification.

Production and diagnostic M9 builds are isolated in `build-m9-production` and
`build-m9-diagnostics`, with generated SDK configs under `/tmp/keyvalet-m9-*.sdkconfig`.
The accepted M7 `build` and `build-diagnostics` artifacts remain untouched.
Current hashes and artifact identities are in the implementation report; do not
infer identity from a directory name.

Bench identifiers below are redacted placeholders, not literal SSID/address
observations. Test results, hashes and acceptance boundaries are unchanged.

The operator physically accepted M9 diagnostic firmware: MONITOR/CONTROL Improv
communication, correct identification, nearby 2.4 GHz scan/selection,
`<bench-ssid>` provisioning with DHCP `<bench-dhcp-address>`, reboot reconnection from
persisted credentials, wrong-password candidate failure, and restoration of the
previous network confirmed by the Wi-Fi connection chirp. The pre-correction
diagnostic ELF's offline SHA-256 was `baaf14baf5697793938572e00fb3f2411e8f77b4ed10a4d32650e856fe423ab5`.
These substantive physical results remain accepted; they are not being repeated.

The subsequent SSID display correction is verified offline in both builds. No
additional PCB testing is required for this closeout. No PCB was contacted by
the closeout work. The rebuilt image hashes differ from the physically tested
image; see [STATUS.md](STATUS.md). The corrected CONTROL and firmware must be
used together for the new read-only query; no obsolete-firmware fallback exists.

For reference only, the M9 diagnostic application-only command is below.
**Not executed or requested again by this closeout.**
Stop MONITOR first. This writes the app only; do not substitute erase/full-install
commands or rewrite OTA initialization during this test.

```sh
python -B -m esptool \
  --chip esp32c6 \
  --port '<operator-verified-port>' \
  --baud 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x10000 build-m9-diagnostics/keyvalet.bin
```

During M10, the operator physically accepted production Improv scan/provisioning
and post-replacement reconnection using the same MONITOR/CONTROL workflow; see
[STATUS.md](STATUS.md) for that record. This does not establish browser-native
flashing or the entire M9 failure matrix on production hardware.

Manual hidden-network and USB-loss scenarios beyond the reported acceptance,
NVS latency/power-loss durability and physical pulse width are not established
by the completed physical tests.
Neither offline evidence nor the accepted functional tests qualify electrical,
thermal or exact pulse-timing safety. No reset of other namespaces is part of M9.

## Exact implementation file scope

Network ownership/lifecycle: `main/network_manager.c`, `main/network_manager.h`.
Credential validation/storage: `main/wifi_store.c`, `main/wifi_store.h`.
Standard framing: `main/improv_serial.c`, `main/improv_serial.h`.
Single USB dispatcher: `main/usb_provision.c`, `main/usb_provision.h`.
Diagnostic refactor/build wiring: `main/diagnostic_console.c`,
`main/diagnostic_console.h`, `main/app_main.c`, `main/CMakeLists.txt`,
`main/Kconfig.projbuild`. Obsolete compiled-credential files removed:
`main/wifi_credentials.h`, `main/wifi_credentials.example.h`.

Terminal codec/workflow: `tools/improv_serial.py`, `tools/kv_test.py`.
Focused tests: `tests/wifi_provision_test.c`, `tests/usb_provision_test.c`,
`tests/wifi_flash_gate_test.c`, `tests/usb_wifi_tool_test.py`.
New peripheral stubs: `tests/host_stubs/driver/usb_serial_jtag.h`,
`tests/host_stubs/driver/usb_serial_jtag_vfs.h`, `tests/host_stubs/esp_app_desc.h`,
`tests/host_stubs/esp_event.h`, `tests/host_stubs/esp_mac.h`,
`tests/host_stubs/esp_netif.h`, `tests/host_stubs/esp_wifi.h`,
`tests/host_stubs/sdkconfig.h`.

Documentation: `README.md`, `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`,
`docs/FIRMWARE.md`, `docs/PROTOCOL.md`, `docs/STATUS.md`, `docs/STORAGE.md`,
`docs/WIFI_PROVISIONING.md`. No canonical policy/security/journal/partition source
was modified. The accepted implementation and closeout correction form one M9
checkpoint on `hardware/keyvalet-v0.4.2`; no Milestone 10 work is included.
