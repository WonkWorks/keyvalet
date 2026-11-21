# Architectural Decisions

## Canonical hardware

The custom KeyValet v0.4.2 PCB is the sole supported v1.0 hardware target.
Firmware contains one fixed hardware profile and no compile-time board selector
or automatic board detection. Production and diagnostic variants differ only in
the inclusion of privileged development USB tooling. First enrollment and Wi-Fi
provisioning use the same production-capable mechanisms in both variants.

## Physical trust bootstrap and ordinary replacement

A physical button alone cannot identify an intended LAN-supplied public key.
M10 therefore stages one temporary operational public key over the existing
protected USB receiver, then requires the physical button window and existing
ENROLL-V1 proof of possession of that exact key. The compiled development-root
path is removed directly, not forwarded. No root, private key or building-specific
image is required. Existing trust is never overwritten; failed/uncertain trust
persistence disables enrollment until reboot. See PROTOCOL for exact behavior.

Ordinary compatible USB replacement writes the verified app to both existing OTA
slots and preserves selection metadata and all persistent data. The merged
factory image is only for virgin/fully erased boards, never an ordinary update.
M10 added no partition or stored-format changes. M11C adds only `tls_nvs` in
previously unallocated flash; existing data formats/offsets remain unchanged.
No epoch migration or OTA engine is added.
See FACTORY_INSTALLATION and TOWNECORE_INTEGRATION_V1 for explicit responsibilities
and remaining cross-reset history/authority-rotation decisions.

## Physical USB Wi-Fi configuration

USB Wi-Fi provisioning is always available to an operator with physical access
to the protected enclosure. It uses Improv Serial, not a second admin password,
button/window, signed CONFIG-V3 field, AP or BLE provisioning mechanism. Both
build variants have one shared USB receiver; privileged diagnostic commands
remain diagnostic-only. Network candidates never replace saved credentials
before DHCP and durable persistence. See [WIFI_PROVISIONING.md](WIFI_PROVISIONING.md).

## Purpose-neutral channels

Active firmware uses `lock_1`, `lock_2`, and `sensor_1`–`sensor_4`. Lock 1 is
paired with Sensor 1 and Lock 2 with Sensor 2; Sensors 3–4 are sensor-only.
Cabinet purpose is not encoded in firmware identifiers, configuration, status,
diagnostics, journal events, or browser tooling.

Both lock/sensor pairs must share parameterized authorization, arbitration,
actuation, expected-opening, timeout, alarm, silence, monitoring, and journal
paths. Every subsequent implementation and acceptance report must explicitly
recheck equivalence and list any channel-specific exception before treating one
channel's physical result as representative.

Hardware capacity is not an operational requirement. Firmware exposes up to
two solenoids and four sensors, not six mandatory physical connections.
CONFIG-V3 carries both lock flags and all four sensor entries; unused inputs should have
`monitoring_enabled: false`. Completeness of this message does not require
installation, closure or enabled monitoring of every input. There is no dynamic
hardware profile, discovery protocol or lock-level enable setting.

Signed unlocking checks only the requested lock's paired sensor/operation guard,
plus the shared arbiter and authorization. Unused channels do not veto another
lock. The target's own sensor must be known CLOSED whenever its signed-admin
`sensor_required` setting is true, even when monitoring is off. Setting it false
deliberately permits solenoid-only operation, without synthetic CLOSED inputs,
dynamic mappings or a separate lock-enable subsystem. Input monitoring remains
independent. A sensor-independent operation has no authorized-opening reminder.

## Versioned signed control

Unlock uses only `KEYVALET-UNLOCK-V2`, with an explicit `lock_id` and signed
decimal timeout (including explicit zero) or `DEFAULT`. Zero suppresses only an
authorized-opening reminder. Configuration uses only `KEYVALET-CONFIG-V3`.
Its incremented version binds both per-lock dependency flags; no older signed
configuration canonicalization is retained. Persistent default timeout remains
5–3600 seconds; zero is only a per-operation override.
The earlier purpose-named unlock and configuration protocols are not public
compatibility boundaries.

The controller reserves the target operation guard and shared arbiter before
replay consumption. Signature/time/replay validation, durable replay commit,
and durable target-identified journal append all precede the 350 ms pulse.
Failures remain fail-closed. Busy or recovery requests are rejected rather than
queued.

## First-product canonical storage

There is no deployed installed base. The Waveshare experiment and earlier PCB
firmware are development history, not compatibility requirements. Git preserves
those experiments; runtime has one settings, replay, trust, circular-journal,
and acknowledgment reader, with no migration, adoption, forwarding alias, or
historical event translator. See [STORAGE.md](STORAGE.md) for exact formats and
the coordinated development-reset boundary, now completed by the operator's
full erase and fresh diagnostic installation.

Settings schema 4 directly includes both dependency bits without migration or
retired fields. Replay has one 12-byte
initialization marker and fixed 64-byte records; no earlier-blob importer or
duplicate RAM blob header/CRC remains. Current version numbers identify formats,
not multiple supported readers. UNLOCK-V2, CONFIG-V3, ENROLL-V1, ACK-V1, and the
`/api/v1/` route namespace remain the sole current contracts in their domains.

Corrupt, unsupported, missing, or discontinuous security state fails closed.
Firmware never resets default NVS or replay NVS to recover from an error.
The journal alone reclaims its oldest 4 KiB sector as normal circular retention.

## Hardware safety

GPIO10 and GPIO3 are preloaded LOW before output configuration, asserted LOW
after configuration, and forced LOW on cancellation, timer/start failure, and
safe reboot. Both outputs share one timer and arbiter. The provisional recovery
interval is 2000 ms; functional bench observation does not substitute for
instrumented flyback, brownout, pulse-width, isolation, or thermal qualification.

## Journal and transport boundaries

The 64-byte journal format and all existing partition extents remain fixed. The existing
40,960-slot partition is circular: capacity exhaustion reclaims the oldest
64-record sector regardless of synchronization, with exact missing sequences
reported after the acknowledged cursor. Sector PREPARED/READY checkpoints in
NVS protect erase ordering and preserve sequence generations across reboot.
Blank journal initialization durably establishes PREPARED/READY before writing
any record. Populated storage without metadata fails closed; no append-only
journal adoption or second event store exists.
WebSocket live delivery is not an acknowledgment and cannot delete records.
Every connection announces journal bounds and the durable cursor. Clients pull
ascending batches of at most eight records; reads are idempotent and examine at
most 16 slots per request.

The acknowledgment cursor is a separate CRC-protected record in default NVS.
Only a replay-protected `KEYVALET-JOURNAL-ACK-V1` signature from the enrolled
authority may advance it, and the NVS commit precedes success response. No
journal record is generated for acknowledgment, avoiding an endless catch-up
tail. Gaps are explicit and do not stop ordinary lock operations. TowneCore,
not KeyValet or the browser, owns remote durable storage and loss accounting.

Journal maintenance waits for active pulse completion and excludes pulse starts
using the existing output mutex. Single-sector erase and NVS checkpoint latency
still delay flash-backed work and scheduling. Physical timing and NVS endurance
are acceptance requirements; details and offline evidence are in
[JOURNAL_RETENTION.md](JOURNAL_RETENTION.md).

M11C uses HTTPS/WSS only, with exact USB-provisioned Origins for both CORS and
WebSocket admission. Missing, `null`, and unlisted Origins are rejected. Origin
is not client authentication: telemetry/retrieval still have no per-user device
login. Signed capabilities remain mandatory for mutations. TLS supplies server
identity and confidentiality; mobile/PWA/local-network permission acceptance is
still physical work, not inferred from a build.

## M11C TLS identity and recovery

The approved 64 KiB `tls_nvs` starts at `0x7a0000`; no existing allocation moves.
USB physical access authorizes hostname/Origin provisioning and UTC recovery,
but never exports the device private key or replaces operational trust. Keys
are generated only with connected Wi-Fi/RF entropy, independently of the building
authority. Full-MAC identity/assigned hostname do not change on TLS re-keying.
Validated candidates are committed separately; one NVS selector is activation.
Ambiguous persistence errors block further TLS writes until reboot. No automatic
NVS erase, fallback certificate, port-80 listener, or expiration bypass exists.

Retain the accepted unauthenticated SNTP UTC policy. USB UTC is a physically
authorized input, not cryptographically authenticated time. Missing valid UTC
blocks certificate installation/activation/listening and existing signed-command
checks. No authenticated network-time subsystem is added.

Google Public CA/Cloud DNS are backend choices, not firmware APIs. The future
maintenance contract is defined in TOWNECORE_INTEGRATION_V1; no outbound worker,
Cloud Run service, ACME account, DNS administration or cloud operational path is
implemented here. TLS keys in unencrypted flash remain physically extractable;
mandatory Secure Boot/encryption/eFuse lockdown is expressly outside scope.

The final Milestone 7 physical smoke test accepts clean installation, canonical
storage initialization, local WebSocket operation and automatic journal retrieval.
It does not establish physical acceptance of CONFIG-V3 updates, signed ACKs,
circular wrap/recovery or zero-timeout unlocks. Earlier channel acceptance and
current parameterized-path equivalence provide representative evidence, not
new measurements. Exact artifact identities and outstanding production
qualifications are recorded in [STATUS.md](STATUS.md).

## Build identity and secret boundary

Every build reports Git revision/state, production or diagnostic variant, and
the ESP-IDF application-ELF SHA-256. Wi-Fi credentials are runtime NVS data;
private signing keys are ignored local inputs. Neither is tracked or printed
in identity output.
