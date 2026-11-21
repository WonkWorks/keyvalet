# Communication Protocol

Acceptance boundary: M10 production passed the operator's esptool factory
installation, USB Improv commissioning, USB-staged/button-gated ENROLL-V1 proof,
and separate both-slot app replacement. Enrollment and automatic retrieval of
three journal records remained available after replacement. Browser-native
flashing is not accepted. CONFIG-V3 updates, JOURNAL-ACK-V1, circular wrap/recovery
and explicit zero-timeout unlocks remain offline-verified, not newly physically
accepted by M10. Earlier signed-channel acceptance remains separate. See
[STATUS.md](STATUS.md) for identities, channel equivalence and qualifications.

All active hardware identifiers are purpose-neutral: `lock_1`, `lock_2`, and
`sensor_1` through `sensor_4`. The canonical hardware always exposes all six.

## Status and live events

`GET /api/v1/status` returns current device, build, Wi-Fi, journal, lock,
sensor, and arbiter state with `Cache-Control: no-store`. `GET /api/v1/ws`
upgrades to WebSocket and supports at most four current clients. A successful
handshake receives the same state as a `welcome` object.

Each `locks` member reports inactive/active state, paired `sensor_id`, `sensor_required`, and
operation state. Each `sensors` member reports physical state, monitoring,
alarm, silence, and unexpected-opening state. Physical state remains visible
when monitoring is disabled.

New live journal records have this shape:

```json
{"type":"event","device_id":"keyvalet-a1b2c3","sequence":43,"event":"SENSOR_OPEN","sensor_id":"sensor_1","uptime_ms":123456,"utc_valid":true,"utc_s":1793476800,"flags":1,"detail":1}
```

Active firmware emits `SENSOR_OPEN` (54), `SENSOR_CLOSED` (55),
`SENSOR_UNEXPECTED_OPEN` (56), `SENSOR_ALARM` (57),
`SENSOR_ALARM_SILENCED` (58), and target-identified
`LOCK_UNLOCK_AUTHORIZED` (48). `detail` is the one-based channel number.
Other current event types are `DEVICE_BOOT` (1), `HEARTBEAT` (2),
`ENROLLMENT_ACCEPTED` (31), and `CONFIG_UPDATED` (32). Historical channel-specific
event names and translations have been removed. Numeric IDs need not be
contiguous; no obsolete identifier is an alias for a current one.

The journal, not WebSocket delivery, is authoritative. Live delivery never
advances the durable synchronization cursor.

## Journal synchronization

Every `welcome` and status object includes `journal_sync` with
`protocol_version`, `oldest_available_sequence`, `latest_available_sequence`,
`highest_produced_sequence`, `acknowledged_sequence`, `unsynchronized_records`,
`unrecoverable_gap`, exact first-missing-interval bounds, `max_batch_size`, and
`available`. This announces
work but does not transmit the backlog.

Read-only retrieval uses:

```json
{"type":"journal_get","protocol_version":1,"request_id":"local-uuid","from_sequence":41,"limit":8}
```

`limit` is 1–8. A `journal_batch` echoes the request and device IDs and returns
`requested_from_sequence`, `next_sequence`, `returned_count`, `more`, a `gap`
object, ascending `records`, and the oldest/latest available and highest
produced sequences from the same storage snapshot. Each record includes device ID, durable
sequence, numeric and named event identity, uptime, nullable UTC, flags, detail,
and the unchanged 12-byte metadata as lowercase hex. Repeating a request is an
idempotent read. At most 16 retained physical positions are examined per request.
Overwritten prefixes use the RAM index and return their full contiguous missing
range without reading discarded flash. A gap-only batch with zero records is
valid; continue from `next_sequence` while `more` is true. Repeated reads do not
mutate storage, but records overwritten between requests cannot be retransmitted.

A state-changing acknowledgment has `type: "journal_ack"`, protocol version 1,
device ID, action `JOURNAL_ACK`, `highest_contiguous_sequence`, and the standard
operation ID, nonce, issue/expiry, and signature fields. Exact signed UTF-8 is:

```text
KEYVALET-JOURNAL-ACK-V1
<device_id>
JOURNAL_ACK
<highest_contiguous_sequence decimal>
<operation_id>
<nonce_hex>
<issued_at_utc_s>
<expires_at_utc_s>
```

Every line ends with LF. The enrolled operational authority, UTC rules, and
durable replay store are shared with other signed commands. After successful
authentication, firmware commits a CRC-protected cursor in the `keyvalet_sync`
NVS namespace before returning `ACKNOWLEDGED`. A signed cursor at or below the
stored cursor returns `ALREADY_ACKNOWLEDGED`; a cursor beyond the produced
journal is rejected. Cursor advancement does not append a journal record.
An ambiguous cursor persistence error disables further acknowledgment writes
until reboot recovery, so a stale RAM value cannot move durable state backward.

If an unacknowledged physical slot is unavailable, metadata sets
`unrecoverable_gap` and retrieval reports explicit missing bounds. Surviving
records remain retrievable. The authority may acknowledge beyond a gap only
after TowneCore durably records that loss. KeyValet cannot prove remote
durability: production integration must durably store, deduplicate, determine
the contiguous cursor, sign the acknowledgment, and forward it.

Storage reclaims the oldest 4 KiB sector when full. Missing sequences include
overwritten records and non-erased interrupted/corrupt records; no events are
fabricated. For cursor 100 and oldest survivor 151, metadata and retrieval
report missing 101–150. Multiple disjoint losses are returned one interval per
batch. A fully acknowledged journal has no unacknowledged gap, even if older
acknowledged records have been reclaimed. Bounds are zero when no record
survives; `highest_produced_sequence` preserves consumed sequence accounting.
An unfilled sector tail is never treated as produced history. The reference
console exports loss ranges alongside records for explicit operator persistence.

## Enrollment

All signatures are ECDSA P-256 over SHA-256. Public keys are 65-byte
uncompressed SEC1 points encoded as 130 lowercase hex characters. Signatures
are fixed-width 64-byte `r || s` encoded as 128 lowercase hex characters.
Nonces and challenges are 32-byte lowercase hex.

First use requires the exact intended operational public key to be temporarily
staged over protected USB. There is no compiled enrollment root or root-signature
fallback in either build. USB establishes key intent, the button establishes
physical participation, and `ENROLL-V1` proves possession of that key's private
counterpart. Previously persisted valid trust remains readable unchanged.

### Protected USB key staging (KeyValet RPC `0x81`)

Uses the existing Improv framing/RPC envelope, explicitly **not a standard
Improv operation**. Type `0x03` payload is command `0x81`, data length `131`,
string length `130`, then 130 lowercase ASCII hex characters (65-byte
uncompressed SEC1 P-256 point starting `04`). Validate exact lengths, no NUL,
hex encoding and actual curve membership. No private key is accepted or needed.

On success, type `0x02` error 0 precedes a type `0x04` RPC `0x81` result with
exactly three strings: `STAGED`, lowercase SHA-256 of the **65 decoded public-key
bytes**, and remaining monotonic lifetime in decimal seconds (1–300, rounded
up). Compare the digest to the backend's intended key. Bad framing, invalid key,
busy/different candidate, enrolled or unavailable security returns error 1;
never interpret a transport write receipt as success.

Staging is RAM-only and permitted only before first enrollment. There is one
candidate. Repeating the same USB key is idempotent without extending its
deadline; a different one is rejected. A staged key lasts at most five minutes,
and clears on reboot, expiry, successful or failed proof, physical pairing
cancellation/expiry. Staging may precede or follow the five-second button hold;
it does not open the existing ten-minute pairing window itself. Expired state
is cleared by the existing controller/security polling or on the next request.
An unsuccessful preflight (`enroll_begin` without a button/window) does not
consume the staged key. There is no network operation that can stage a key,
persist trust directly or replace an enrolled authority.

### WebSocket challenge and proof

Within both the live staged-key lifetime and physical window, send exactly:

```json
{"type":"enroll_begin"}
```

Response fields are `type:"enrollment_challenge"`, `device_id`, 64-character
lowercase `challenge`, and integer `expires_in_s`. It is a new random 32-byte
challenge whose monotonic lifetime is clamped by the remaining staged-key and
physical-window deadlines (never more than 300 seconds). The returned seconds
are rounded up, not a new extension of the deadline. A competing begin is
rejected without replacing a live challenge. It is not bound to a particular
WebSocket descriptor; proof remains bound to device, challenge and USB key.

The backend signs with the staged operational **private** key this exact UTF-8
form, including the final LF. No root delegation is involved:

```text
KEYVALET-ENROLL-V1
<device_id>
<challenge_hex>
<operational_public_key_hex>
ENROLL_FIRST
<expires_at_utc_s>
```

Relay the resulting fields:

```json
{"type":"enroll_complete","challenge":"<64 lowercase hex>","operational_public_key":"<130 lowercase hex>","expires_at":1800000060,"signature":"<128 lowercase hex r||s>"}
```

No explicit device-ID field is consumed in this completion message: firmware
derives its own ID for canonicalization. UTC must be valid; integer `expires_at`
must be between device UTC now and now+300 inclusive. Use a short expiry within
the reported remaining lifetime; the monotonic challenge/key/window deadlines
apply independently. The reference signer uses at most 60 seconds. Signatures
are not interchangeable with operational command canonicalizations.

Only the exact USB key and live challenge may pass. Signature verification and
first-use trust commit are serialized; one completion can succeed. The unchanged
104-byte `trust_v1` NVS blob is committed before
`{"type":"command_result","accepted":true,"code":"ENROLLED"}`. Success clears
candidate, challenge and physical window. The handler attempts an
`ENROLLMENT_ACCEPTED` journal append afterwards; a journal failure is logged but
does not undo an already durable enrollment. Refresh status to see `enrolled`.

Enrollment failures return `command_result`, `accepted:false`, no operation ID:

| Code | Meaning / retry |
| --- | --- |
| `MALFORMED` | Missing/invalid JSON completion fields; clears candidate/challenge and closes physical window |
| `ESP_ERR_INVALID_STATE` | Not ready/already enrolled, missing/expired stage/window/challenge, competing begin or substituted key |
| `ESP_ERR_INVALID_ARG` | Bad hex/length, unavailable UTC or signed expiry outside allowed bounds |
| `ESP_ERR_INVALID_RESPONSE` | Invalid ECDSA proof |
| `ESP_ERR_TIMEOUT` | Challenge expired during proof verification |
| Other ESP error | Storage/internal failure; do not assume enrollment succeeded |

A failed completion clears the temporary candidate/challenge, not persisted
trust. Re-stage through USB before another proof; the physical window may remain
open except after malformed completion/cancellation/expiry. A competing begin
retains the live transaction. An attacker can cause denial of service by
consuming a challenge or submitting invalid completion, but cannot substitute
its key or acquire the intended private key. After ambiguous NVS set/commit
failure, further enrollment/authorization fails closed until reboot reloads
actual durable trust. Do not automatically erase or overwrite uncertain state.

An enrolled device rejects staging/begin/complete; no in-place authority rotation
or reset API exists. Operational commands still verify only the persisted key.
See [TowneCore handoff](TOWNECORE_INTEGRATION_V1.md) for ownership, identity/history
limits, browser constraints and future recovery responsibilities.

## Signed targeted unlock

The only unlock request has `type: "unlock"`, `protocol_version: 2`,
`device_id`, `action: "UNLOCK"`, `lock_id`, operation ID, nonce, issue/expiry
times, and signature. `lock_id` is exactly `lock_1` or `lock_2`.
`door_open_timeout_s` is optional. Omission uses the persistent default; explicit
integer `0` disables this operation's authorized-opening reminder; integers
5–3600 select seconds. Values 1–4, negative, fractional, string and boolean values
are invalid. Persistent defaults remain 5–3600 seconds.

The signed canonical form is:

```text
KEYVALET-UNLOCK-V2
<device_id>
UNLOCK
<lock_id>
<door_open_timeout_s decimal including 0, or DEFAULT when omitted>
<operation_id>
<nonce_hex>
<issued_at_utc_s>
<expires_at_utc_s>
```

Every line ends with LF. Omission snapshots the persisted default for this
operation; a supplied positive timeout is temporary and starts when the paired
sensor opens. Explicit zero signs the line `0`, not `DEFAULT`; zero disables
only that reminder, never a required sensor prerequisite or urgent alarm policy.
The controller reserves the target guard and shared arbiter before
consuming replay state. Durable replay and `LOCK_UNLOCK_AUTHORIZED` journal
acceptance precede the pulse. Failure cancels the reservation without delayed
actuation.

By default each lock requires its fixed paired sensor known CLOSED. Signed
administrative configuration may set `locks.<lock_id>.sensor_required: false`.
That permits sensor-independent actuation with an OPEN/unknown/uninstalled input.
The operation guard is released at successful pulse start, with no paired-door
timer or OPEN/CLOSED lifecycle; the shared arbiter still enforces the finite pulse
and recovery interval. Zero, positive and omitted timeouts are accepted but do
not create a door reminder for a sensor-independent lock. A monitored input
opening in this mode is still unexpected and urgent. An unlock request cannot
change the persistent dependency; only signed configuration can do so.

The pre-v1 purpose-named unlock request and its signature format are not
accepted.

## Signed configuration

`GET /api/v1/config` returns protocol version 3, storage schema version 4,
revision, default door-open timeout, both lock dependency flags and all four
sensor monitoring flags. The canonical default configuration is:

```json
{"protocol_version":3,"schema_version":4,"revision":1,"default_door_open_timeout_s":90,"locks":{"lock_1":{"sensor_required":true},"lock_2":{"sensor_required":true}},"sensors":{"sensor_1":{"monitoring_enabled":true},"sensor_2":{"monitoring_enabled":false},"sensor_3":{"monitoring_enabled":true},"sensor_4":{"monitoring_enabled":true}}}
```

`PATCH /api/v1/config` requires protocol version 3, device ID, action
`UPDATE_CONFIG`, expected revision, complete `locks` and `sensors` objects,
replay/time fields, and signature. All six flags must be JSON booleans.

The canonical form is:

```text
KEYVALET-CONFIG-V3
<device_id>
UPDATE_CONFIG
<operation_id>
<nonce_hex>
<issued_at_utc_s>
<expires_at_utc_s>
<expected_revision>
<default_door_open_timeout_s>
2
lock_1.sensor_required=<0 or 1>
lock_2.sensor_required=<0 or 1>
4
sensor_1=<0 or 1>
sensor_2=<0 or 1>
sensor_3=<0 or 1>
sensor_4=<0 or 1>
```

Every line ends with LF. Timeout bounds are 5–3600 seconds. The complete-state
update uses compare-and-swap revision semantics and is committed to NVS before
live application and response. CONFIG-V3 replaces earlier development signatures:
there is no CONFIG-V1/V2 reader, signature fallback or migration. The version
increment explicitly binds the new security-relevant dependencies. UNLOCK-V2,
ENROLL-V1, JOURNAL-ACK-V1 and the `/api/v1/` route namespace remain current.
All four sensor entries must be present, but any or all may specify
`monitoring_enabled: false`. This describes the complete configuration of the
available capacity, not four required installed/connected/CLOSED sensors. The
signer and console validate entry completeness and boolean values, not physical
population. Unused inputs remain visible in telemetry without generating alarms
when monitoring is disabled. An unused lock does not block the other lock;
target-specific authorization is mandatory, and the target's own known-CLOSED
paired-sensor guard applies whenever `sensor_required` is true. Monitoring=false
does not bypass that guard. There is no lock-level enable/discovery setting.

Changing a dependency invalidates that lock's previous door bookkeeping and any
not-yet-actuated controller reservation. A previously started finite hardware
pulse/recovery is not altered. Ordinary reminders are cleared on that change;
independently monitored urgent alarms remain. Closing a paired door completes
its authorized-opening window; a subsequent opening needs a new authorization
to be expected. A settings persistence error disables further writes until
reboot reloads durable state, avoiding a stale-RAM CAS after an ambiguous commit.

Per-operation overrides and opening guards are volatile, as before. Reboot
starts normal input initialization using persisted configuration; an operation's
zero or positive temporary override is not a new persisted settings value.
Following a settings persistence error, live settings remain the last accepted
RAM values until reboot loads whichever blob actually became durable. Treat the
failed administrative response as uncertain and reload before proceeding.

## Replay and transport security

Unlock, configuration, and journal acknowledgment share a durable 160-entry replay ring. Each accepted
identity stores separate 128-bit SHA-256 truncations of operation ID and nonce;
reuse of either is rejected. Signed lifetime is at most 120 seconds with at
most 15 seconds future issue skew. Invalid time, trust, target, action,
signature, replay state, or persistence fails closed.

The existing replay window also requires **strictly increasing device UTC
acceptance seconds**, shared across unlock/configuration/ACK. Two otherwise
distinct valid signed operations in the same device UTC second cannot both
consume replay state; the later one returns a time/replay failure. A clock
rollback similarly blocks acceptance until it advances past the stored value.
Changing `issued_at` does not bypass this check on actual device acceptance time.
TowneCore must serialize/cadence signed mutations and retry within validity
bounds; M10 does not change this accepted replay policy.

M11C replaces cleartext with HTTPS/WSS on port 443 at the commissioned hostname.
No port-80 listener or certificate-warning bypass exists in either variant.
The application messages/signatures above are unchanged. TLS authenticates the
server and protects transport; it does not replace signed capabilities or
authenticate a staff user to KeyValet. Safari/Chrome four-client acceptance is
recorded in STATUS; broader browser/OS/PWA qualification remains pending.

Both HTTPS requests and WSS admission require an exact non-null Origin from the
active USB-provisioned list. Absent, literal `null`, malformed/oversized and
unlisted Origins fail closed. At most three canonical Origins are provisioned:
HTTPS DNS origins without path, query, credentials or default `:443`, or the
explicit localhost test origins `http://localhost:8765` and
`http://127.0.0.1:8765`. Localhost is never implicitly enabled. Native clients
must also send a provisioned Origin; Origin is not cryptographic authentication.
The reference console uses HTTPS/WSS to the entered commissioned hostname while
its signing helper may remain on localhost. No firmware development exception
or insecure client verification flag is needed.

Four WSS clients remain fixed; a fifth socket permits short HTTPS overlap.
Admission never evicts a healthy WSS client. The server sends empty PING every
15 seconds and closes clients without any received frame for 60 seconds;
browsers automatically PONG. Non-WSS idle sessions are reclaimed on that sweep.
Handshake timeout is 3 seconds, send/receive timeout 2 seconds; TCP keepalive
also applies. Physical commands are never retried automatically after a close.

Status/welcome additionally expose `device_uid` (full station MAC, distinct from
the unchanged short operational `device_id`), `ssid`, `wifi_available`,
`enrollment_available`, `websocket_max_clients:4`, `tls` and `resources`.
`tls` contains `state`, `ready`, `listening`, `hostname`, `generation`,
`certificate_expires_at`, `candidate_available`, `activation`, and
`maintenance_state:"NOT_IMPLEMENTED"`. Expiration is the earliest supplied-chain
expiration. States distinguish UNAVAILABLE, UNCOMMISSIONED, STORAGE_UNCERTAIN,
CLOCK_REQUIRED, NOT_YET_VALID, EXPIRED and READY. `ready` describes date-valid
active material; `listening` describes the actual listener. A storage error can
leave a valid existing identity ready while writes are inhibited.
`resources` contains byte-valued free/minimum/largest heap and HTTPS/worker
stack margins, current/created/closed TLS sessions and TLS error counts.
These counters are instrumentation, not hardware attestation or capacity proof.

## Diagnostic USB protocol

Both variants expose the standard Improv Serial Wi-Fi provisioning protocol on
native USB. It supports device info, state, scan, credentials and connection URL;
the standard optional network-state query is also implemented. A read-only
KeyValet RPC `0x80` returns `[connection_state, current_ssid]` from the live
network snapshot; SSID is empty unless connected. This extension uses existing
framing and does not change standard RPCs or reinterpret their URL fields.
The protected USB `0x81` operation only stages a public enrollment candidate;
WebSocket proof and the button window are still required to persist trust.
This interface never exposes lock actuation, policy changes, journal mutation,
password retrieval or firmware-installation commands. See
[WIFI_PROVISIONING.md](WIFI_PROVISIONING.md) for framing, limits and recovery.

## Protected USB TLS RPC `0x82` — implemented in both variants

This KeyValet-specific operation reuses the existing receiver and vendor RPC
framing, not standard Improv command semantics. RPC data is binary. An incoming
type-3 frame payload is `[0x82, data_length, action, action_data...]`; outer
Improv lengths/checksum remain unchanged. Successful type-4 results use ordinary
length-prefixed UTF-8 strings. Rejection produces Improv error 1. No private
key, operational signature or password is ever returned.

| Action | Binary action data | Result / meaning |
| --- | --- | --- |
| `0` | none | `[tls_state, CANDIDATE/NO_CANDIDATE, activation_state]` |
| `1` | kind u8, total u16-LE, SHA-256[32] | `[OK]`; begin one transfer, replacing any volatile incomplete transfer |
| `2` | offset u16-LE, 1–220 bytes | `[OK]`; strictly sequential, no gaps/overlaps |
| `3` | none | `[OK]` only after full length/hash verification and durable candidate preparation/installation |
| `4` | object u8, offset u16-LE | `[total_decimal, offset_decimal, lowercase_hex_chunk]`; at most 100 decoded bytes |
| `5` | none | `[PENDING]`; queue activation, not proof of completion; query status |
| `6` | none | `[OK]`; discard volatile transfer/export only |
| `7` | none | `[OK]`; discard inactive candidate, never active identity |
| `8` | UTC seconds u64-LE | `[OK]`; physically authorized clock input, 2026-01-01 through 2100-01-01 |

Transfer kind 1 is canonical ASCII JSON, at most 1024 bytes, exactly
`{"hostname":"assigned.public.name","origins":["https://staff.example.com"],"rotate_key":false}`.
The example domains are placeholders, not firmware defaults. One to three
distinct Origins are required; strings may not contain JSON escape sequences.
Names must be lowercase DNS names, at most 253 bytes, without wildcard, terminal
dot, IP literal or `.local`. False preserves an existing candidate/current key
or creates the initial key; true explicitly creates a replacement P-256 key.
Wi-Fi must be connected for the guaranteed RF entropy source. Configuration
prepares a candidate; it does not immediately change the live hostname/Origins.

Transfer kind 2 is leaf-first PEM certificate plus CA chain, at most 6144 bytes
and four certificates; NULs are rejected. One transfer has a fixed 30-second
lifetime from begin, including interruption; reboot/abort clears it. Length,
hash, order or timeout failure cannot activate or partially install a chain.

Read objects: 1 candidate CSR PEM, 2 candidate SEC1 public point (130 lowercase
hex characters), 3 JSON TLS status, 4 active CSR, 5 active SEC1 public point.
Offset zero creates an export snapshot; subsequent offsets read that snapshot.
The status JSON includes device ID/UID, active hostname/Origins/generation,
readiness/candidate/expiration, activation, `usb_stack_free_bytes` and deferred maintenance state.
No export includes private material. Keep one USB transaction owner.

Activation states are IDLE, PENDING, ACTIVATED or FAILED. Query after an uncertain
response/disconnect. PENDING inhibits other TLS mutations until the worker
finishes; it waits for ACTIVE/RESERVED operations before safely restarting HTTPS.
ACTIVATED means the selector committed, not a browser's TLS trust verdict.
An expired/invalid candidate is rejected without replacing the active identity.
After ambiguous storage failure reboot reloads durable truth before another
write. See STORAGE for corrupt-store recovery and FACTORY_INSTALLATION for the
separate partition transition.

UTC action 8 is USB-only and reuses existing UTC validity. Unauthenticated SNTP
remains the normal policy; USB time is physical authorization, not authenticated
network time. Invalid/missing UTC or expired certificates do not enable fallback
transport. No LAN clock mutation, signature timestamp change or replay reset is
introduced. Standard Improv connection results contain a commissioned HTTPS URL
only when TLS is ready and the URL fits one standard result; otherwise no URL.

Diagnostic builds accept exact allowlisted `KVTEST` commands for finite Lock 1
or Lock 2 pulses, chirp, LED, status, safe reboot, and bounded read-only journal
inspection. Production omits the parser. Neither build exposes arbitrary GPIO,
flash erase, unsigned journal acknowledgment, or OTA commands.
