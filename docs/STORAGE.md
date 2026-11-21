# Canonical first-release storage

KeyValet v0.4.2 is the first product, not a successor maintaining development
data compatibility. Each store has exactly one reader. Superseded blobs and
append-only journals are rejected, never migrated, adopted, or silently erased.
Current version identifiers detect unsupported/corrupt data; they do not imply
an older reader. All integers below use the ESP32-C6 little-endian layout.

## Formats and ownership

| Store | Partition / namespace / key | Canonical format |
| --- | --- | --- |
| TLS identity | `tls_nvs` / `tls_identity` / `identity0`, `identity1`, `selected` | Bounded schema-1 credential slots; SHA-256 integrity; one-byte active selector, independent of all operational stores |
| Wi-Fi | `nvs` / `keyvalet_wifi` / `station` | 112 bytes: `KVWF`, version u16=1, size u16=112, SSID[33], password[65], two zero padding bytes, CRC32 over bytes 0–107 at offset 108 |
| Settings | `nvs` / `keyvalet` / `settings` | 28 bytes: magic u32, schema **4** u32, revision u32, default timeout u32, lock-required bitmask u32 (bits 0/1 for Locks 1/2, upper bits zero), four u8 monitoring flags, CRC32 over bytes 0–23 |
| Trust | `nvs` / `keyvalet` / `trust_v1` | 104 bytes, version **1**: magic/version u32, 24-byte device ID, uncompressed P-256 operational public key (65 bytes), three zero alignment bytes, CRC32 over all preceding bytes |
| Replay marker | `replay_nvs` / `replay` / `meta` | 12 bytes: magic u32, version **2** u16, size u16, CRC32 over bytes 0–7 |
| Replay records | `replay_nvs` / `replay` / `r000`–`r159` | 64 bytes, version **1**: magic/version/size header, sequence u64, accepted UTC i64, separate 16-byte operation and nonce digests, four zero alignment bytes, CRC32 at offset 60 |
| Journal records | `journal` raw partition | 64 bytes, version **1**: magic/version/size, sequence, uptime, UTC, event/flags, detail, 12-byte metadata, CRC32 at 52, zero word at 56, commit marker at 60 |
| Journal checkpoint | `nvs` / `keyvalet_jrnl` / `ring_v1` | 40 bytes, version **1**: magic u64, version/phase u32 each, sector base u64, sector count u64, CRC32 held in final u64 |
| Journal ACK | `nvs` / `keyvalet_sync` / `cursor_v1` | 24-byte C6 blob, version **1**: magic/version u32, acknowledged sequence u64, CRC32 over bytes 0–15, zero tail alignment bytes |

Alignment bytes keep current record sizes and deterministic CRC coverage; they
are not retired policy fields. Trust's device identity and operational key are
still required for physical enrollment and signed commands. The replay marker
distinguishes initialized storage from missing security state. The journal
checkpoint identifies sequence generations and makes sector erase safe. The ACK
is separate because authority acknowledgment must not be inferred from retention.
None is a compatibility reader. The existing hourly heartbeat remains a current
uptime/audit event. Generic unknown-event fallback is not historical translation.

M9 adds only the dedicated Wi-Fi namespace. Its candidate is committed once
after association and DHCP, under the existing solenoid flash gate. Reconnect
and unchanged successful credentials do not rewrite NVS. Failure before commit
keeps the old credentials; an ambiguous write/commit error blocks further Wi-Fi
successes until reboot reloads durable state, even for a request identical to
the old RAM credentials. Existing policy/trust/replay/journal/ACK formats and
partitions are untouched. No erase/reset or development-format adoption occurs.
Wi-Fi passwords are not encrypted at rest in the current unencrypted NVS;
they are never returned by Improv or exposed in normal tooling output.

Settings defaults are revision 1, timeout 90 seconds, monitoring `[on, off, on,
on]`, with both locks sensor-required. Configuration exposes schema 4 and uses
only CONFIG-V3 signatures, binding both dependency flags. CRC, size, schema,
boolean values, unused mask bits and timeout bounds are checked. A settings
write error disables further updates until reboot reloads durable revision and
flags, including an ambiguous commit that may already have reached flash.
All four flags are stored even in partially populated installations. False is
valid for any/all sensors; it disables that input's alarm/reminder monitoring,
not its status visibility. No stored field requires all physical channels in use.
Invalid settings retain safe runtime defaults and reject persistent updates;
they are not automatically repaired on flash.

Replay initializes empty only before enrollment, with no marker and no record
keys. An enrolled device missing its marker, orphan records, invalid records,
unsupported versions, or a discontinuous suffix fail closed. A durable record
append precedes in-memory acceptance. Persistence errors disable new replay
appends until reboot, including errors whose commit may actually be durable.
There is no duplicate persisted whole-window blob or RAM blob header/CRC.

Trust and ACK use their existing single current readers. Missing trust means
unenrolled, not corrupt trust recovery. Missing ACK means zero acknowledged;
corrupt/unsupported ACK fails synchronization closed, not lock actuation.
ACK advancement is signed, replay protected, monotonic, bounded by produced
history, and committed before success. An ambiguous commit disables subsequent
ACK writes until reboot reloads durable state.

M10 retains the exact trust blob and existing valid enrollments. First enrollment
now pins the USB-staged operational key after ENROLL-V1 proof and physical
participation, not root delegation. Candidate/challenge remain RAM-only. Trust
set/commit errors disable further enrollment/authorization until reboot, including
an error after a durable write. A full erase still restarts device-ID/sequence
pairs: there is no globally unique journal epoch in records, retrieval or signed
ACKs. The ring checkpoint is not a cross-reset generation identifier. See
[TowneCore handoff](TOWNECORE_INTEGRATION_V1.md#history-continuity-reset-generations-and-authority-replacement)
for the explicit recovery/history boundary and deferred minimum epoch design.

## Circular journal guarantees

The 640-sector journal retains up to 40,960 events, reclaims only the oldest
4 KiB sector, and does not depend on connectivity or acknowledgment. Durable
PREPARED precedes erase; erase verification precedes durable READY; record body
and CRC verification precede the commit marker. A PREPARED reboot repeats only
the victim erase, ignoring stale records there. Other sectors survive.

On blank initialization every slot must be erased before the first checkpoint
is created. Populated storage without metadata is an error, not a new journal.
Interrupted occupied slots consume sequence numbers and become explicit gaps.
A never-committed attempt that programmed no bits may reuse its number. Wraps
and canonical reboot never reuse successfully committed sequences. Gap reporting
includes retired prefixes and internal holes, including a final consumed slot
without a surviving later event. See [JOURNAL_RETENTION.md](JOURNAL_RETENTION.md)
for the full fault model, bounded reads, and flash scheduling limitations.

## One-time bench development reset — completed by operator

Earlier bench firmware predates this canonical storage model. Its settings and
replay marker are deliberately incompatible, and its append-only journal has no
circular checkpoint. A coordinated selective reset of the following three data
partitions was the minimum development-reset recommendation:

| Partition | Offset | Length | Reason |
| --- | ---: | ---: | --- |
| `nvs` | `0x9000` | `0x4000` | Remove development settings, trust, old replay blob, journal checkpoint, and ACK as one fresh enrollment/history epoch |
| `journal` | `0x510000` | `0x280000` | Start wholly erased circular storage; no append-only adoption or old event translation |
| `replay_nvs` | `0x790000` | `0x10000` | Remove the old marker and development identities; create canonical replay state before new enrollment |

These are coordinated initialization actions, not a transaction spanning
partitions. Keep firmware from running until all three are reset and the intended
image is installed, under separate authorization and a controlled hardware-safe
procedure. Do not reset replay alone while preserving enrolled trust. Do not reset
journal alone while preserving a checkpoint/ACK from a different history epoch.

A full flash erase was not necessary for that recommendation. A selective reset
would leave bootloader, partition table,
`otadata`, `phy_init`, `ota_0`, and `ota_1` untouched by storage reset. Default
NVS reset also discards any SDK-owned NVS state. Expect physical re-pairing,
re-enrollment, configuration revision 1/default monitoring, journal sequence 1,
and ACK zero. Signing keys on the workstation are not affected. Reapply desired
sensor monitoring and timeout after enrollment.
The dependency refinement adds no new partition reset requirement: settings
schema 4 directly replaces development layouts in the coordinated `nvs`
initialization. Replay, trust, journal and ACK formats are unchanged by this
refinement. Both dependency flags restart true; changing them is signed admin work.

Key-level clearing could retain SDK-owned NVS state, but is not implemented or
required for disposable bench data. It must clear all KeyValet namespaces and
both raw/dedicated stores consistently; do not add a permanent migration/reset
API solely for this one-time development operation.

The operator subsequently chose a **complete flash erase** and installed the
verified diagnostic bootloader, partition table, OTA initialization and app.
That Milestone 7 diagnostic application's SHA-256 was
`da4e1058ab5516ec617607040130bab369a788b23290a8067a683d138451be09`;
its full identity and artifact provenance are recorded in [STATUS.md](STATUS.md).
Physical smoke acceptance confirmed boot, Wi-Fi/WebSocket, both default-required
locks/all four sensors, READY arbiter, HEALTHY journal at sequence 1/ACK 0,
available synchronization, automatic retrieval of one record, and unenrolled
state. This satisfies the one-time fresh-storage requirement; no additional
erase/reset is requested. The checkpoint review did not contact hardware.

## Offline evidence and physical acceptance boundary

The canonical settings/replay integration harness boots real adapters in fresh
processes over shared mock NVS: blank initialization, populated reboot, both
dependency flags/all four combinations, CAS, settings CRC/invalid-bit rejection,
failed/ambiguous settings writes, replay wraps, missing marker with trust, orphan-state rejection,
and errors before/after a durable replay commit. Journal integration tests run
the real adapter/cursor/output owner with peripheral stubs; they verify ACK reboot,
failed/lost commits, monotonicity, exact gaps, and flash exclusion for both locks.
P-256 signing/tamper checks and authorization-order contracts are separate tests,
not a claim of an end-to-end hardware-signed ACK acceptance test.

M10 later passed a separate operator factory erase/install, initial production
enrollment and non-destructive both-slot USB replacement. Post-replacement
observations confirmed retained enrollment, Wi-Fi, a healthy journal at sequence
3/ACK 0 and retrieval of all three records. These are functional continuity
observations, not a physical byte-level audit of all settings/replay/storage.
The initial erase destroyed earlier data; only the subsequent app replacement
preserved the new history. See [STATUS.md](STATUS.md) for exact evidence and
[FACTORY_INSTALLATION.md](FACTORY_INSTALLATION.md) for write-range guarantees
and offline data-preservation simulation.

Signed CONFIG-V3 updates, signed ACK persistence, circular wrap/recovery and
zero-timeout unlocks remain offline-verified, not newly physically accepted by M10.
NVS atomic single-blob recovery and
address-contained flash operations remain durability assumptions. Flash/NVS
latency, endurance, brownout behavior and physical pulse width under concurrent
persistence still need instrumented qualification. External replay of an entire
old flash snapshot is outside current anti-replay guarantees; authority
rotation/recovery, automatic certificate maintenance and downgrade policy remain deferred.

## M11C dedicated TLS storage

`tls_nvs` is 64 KiB at `0x7a0000..0x7affff`; the remaining tail begins at
`0x7b0000`. Existing NVS, OTA metadata, PHY, both 2.5 MiB app slots, 2.5 MiB
journal and replay NVS retain their exact offsets/sizes/formats. Deploying this
map requires the explicit FACTORY_INSTALLATION transition, not app-only flashing.

Each credential blob is 7280 bytes on ESP32-C6: schema/generation/key length/
chain length as four u32s, hostname[254], Origins[3][192], key DER[256], PEM
chain[6145], SHA-256[32], and one zero trailing alignment byte. The digest covers
the 7247 bytes before it. Empty Origins after the first are unused. Bounds are
253 hostname bytes, three 191-byte Origins, 256 key bytes, 6144 chain bytes and
four certificates. Buffers/padding are zero-initialized. Keys are never returned
by any export/status operation; owned credential allocations are zeroized on free.

Only the inactive slot is written while preparing/installing a replacement.
The current slot remains intact. A validated candidate is durably committed
before success; activation separately commits `selected`. Reboot uses that
selector, never the highest generation automatically. A previous lower-generation
slot is retained but is not treated as a new candidate. Errors whose durability
is ambiguous latch TLS writes off until reboot; no stale-RAM overwrite follows.
NVS atomic blob/selector recovery is an SDK assumption exercised by host fault
models, not measured brownout safety. A corrupt active store fails closed;
explicit operator recovery can restore/recommission only TLS storage without
erasing operational authority, settings, journal or replay.

This is unencrypted NVS. SHA-256 detects corruption, not malicious physical
rewriting; cabinet/USB physical protections remain the trust boundary. Two slots
fit comfortably within 64 KiB without adding a filesystem or custom flash log.
