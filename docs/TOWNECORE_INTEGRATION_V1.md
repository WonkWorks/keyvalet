# TowneCore firmware integration contract — revision 2 (M11C)

Scope: the actual KeyValet v0.4.2 firmware and release/tooling interfaces. This
handoff is versioned independently of UNLOCK-V2, CONFIG-V3, ENROLL-V1, ACK-V1 and
the `/api/v1/` namespace. M11C implements device WSS/TLS/USB mechanisms, not a
cloud certificate worker or M12 OTA service. TowneCore P1's native Staff Settings
browser USB foundation is now implemented, development-deployed and visually
inspected in Chrome, separately from this firmware checkpoint. Physical browser
flashing and live GitHub retrieval remain pending. Exact build artifacts and
operator acceptance are in [STATUS.md](STATUS.md): M10 factory installation, enrollment and
both-slot replacement passed using esptool and the reference tools. M11C adds
accepted non-destructive transition, manual public-CA/USB commissioning and
sustained four-client Safari/Chrome WSS operation. Browser-native
flashing and production TowneCore key custody/integration are not yet accepted.

## Ownership and authority

| Owner | Responsibility |
| --- | --- |
| KeyValet | GPIO safety, shared lock arbiter, local policy/alarms, USB setup, signature/time/replay validation, durable journal, bounded retrieval and signed ACK persistence |
| TowneCore backend | User authentication/permissions, persistent building authority/private-key custody, signing, durable event/loss storage, deduplication and ACK decision; recovery audit/history association |
| TowneCore browser | Authorized source selection, guarded standard USB flashing, Improv, staging the **public** building key via protected USB, local WS relay, operator progress/errors |

A building's authority is not a staff account or the installer's private key.
Personnel changes do not require re-enrollment. Store private keys in the
building-controlled backend/key service, never ordinary browser JS or firmware.
The development helper demonstrates signing only; it is not a production
user-management or key-custody service. No publisher-wide root is required.
Each association independently owns/deploys TowneCore and its trust relationship;
no access to the developer's cloud account or publisher provisioning service is
required. See the [project license](../README.md#license) and
[third-party notices](../THIRD_PARTY_NOTICES.md).

## Factory/replacement source of truth

[FACTORY_INSTALLATION.md](FACTORY_INSTALLATION.md) defines package schema
`keyvalet-release-v1`, relative artifacts, actual offsets, validation, universal
factory image, application-only payload, both-slot data-preserving replacement
and separate destructive recovery. `tools/package_firmware.py` produces both
ESP Web Tools manifests and checksums from the IDF build. `partitions.csv` and
STORAGE are authoritative for geometry and stored formats.

The canonical distribution is public, versioned GitHub Release assets from the
intended `WonkWorks/keyvalet` repository, not permanent GCS/Staff firmware storage
or a second firmware repository. The repository/release is not yet published.
TowneCore's `KEYVALET_RELEASE_BASE_URL` remains unset, producing
`publication-required`; factory flashing is disabled until the exact approved
release is configured and validated. Never flash from a floating `latest`.
Direct browser retrieval is preferred; an existing bounded stateless TowneCore
download route may proxy public assets when CORS prevents it. Public downloads
need no GitHub authentication, OAuth or service-account credentials. Acquisition
is separate from the Web Serial engine for future OTA catalog reuse.

P1 initially targets desktop Chrome/Web Serial, including macOS, over HTTPS with
explicit user gesture, port permission and exclusive ownership. Keep release
identity, SHA-256 checks, compatibility/factory guards and written-flash
verification. Custom/local acquisition must meet the same contract; this does
not claim an implemented custom-source UI. Verify chip family **and** physical v0.4.2/N8 board,
8 MiB flash, version/layout/storage compatibility, hashes and release provenance.
ESP Web Tools alone does not verify KeyValet release SHA-256 or disallow erase.
Never treat a factory merge as ordinary replacement or infer boot selection from
an app address. No device-side initial download or proprietary flasher exists.
PCB v0.4.2 is hardware, `0.5.0-m5` is the embedded software version, and
`keyvalet-m11c-p0-closeout-be41564249ab` is the factory distribution candidate.
See [publication/source blockers](PUBLICATION.md) before distributing its binaries.

## USB provisioning and bootstrap sequence

Use the single existing USB receiver. Release/reacquire its descriptor between
ROM flashing and running-firmware RPC; MONITOR, browser and another flasher must
not share the port. Reconnect explicitly after reset/re-enumeration. Do not assume
a successful USB write means a completed device operation.

1. Standard Improv RPC `0x03` returns `KeyValet`, actual firmware version,
   `ESP32-C6`, and device ID. Compare the physical unit and expected metadata.
2. RPC `0x04` returns bounded 2.4 GHz scan entries and empty terminator. Submit
   SSID/password with RPC `0x01`; honor actual state/progress/error results.
3. Accept durable Wi-Fi success only on state 4 plus its successful RPC result.
   Standard RPC `0x07` yields flags/URL; KeyValet `0x80` yields current state/SSID.
   A URL is omitted until TLS is ready (or when too long for one Improv result).
   An endpoint URL is not an SSID. After USB loss, query current state rather
   than claiming success/failure from the old host transaction alone.
4. Use protected USB TLS RPC `0x82` to prepare a device-generated P-256 key,
   assigned hostname and exact allowed Staff Origin(s). Bind full-MAC
   `device_uid`, CSR public key and hostname to the authenticated building
   installation. The MAC is not attestation; the physical USB ceremony establishes
   the intended public key. Neither browser nor backend receives its private key.
5. Certificate automation is future work. The accepted bench used Google Public
   CA ACME and Cloud DNS DNS-01; this is not a requirement to use a developer's
   Google project. The association supplies its own authorized issuer/DNS process.
   Until automated, an authorized operator supplies the issued
   chain through the same USB interface; no test/self-signed certificate is
   compiled in or installed automatically. Install, activate and query status.
   Verify browser-trusted WSS to the assigned name. The initial unit otherwise
   has no HTTP/WS listener. If needed, supply UTC with USB action 8; see PROTOCOL.
6. Obtain the intended building operational public point from the authenticated
   backend. Send KeyValet RPC `0x81` over this protected physical USB connection.
   Verify `STAGED`, fingerprint and remaining lifetime against that **same** key.
   This is volatile eligibility, not enrollment or persistent trust.
7. The operator holds the existing button five seconds and releases before ten.
   This opens the ten-minute physical enrollment window. Staging may occur before
   or during the window; its independent five-minute deadline never extends.
8. Connect WSS and complete the existing challenge/proof flow below before the
   shortest device deadline. Only then record enrollment as established.

[WIFI_PROVISIONING.md](WIFI_PROVISIONING.md) is the single detailed source of
truth for framing, limits, scan fields, Wi-Fi candidates, persistence and errors.
PROTOCOL below defines the narrow `0x81` extension and enrollment. Production
and diagnostics share both provisioning and staging. Only diagnostics expose
the privileged `KVTEST` development menu. Neither build exports private keys,
enrollment reset, arbitrary GPIO or an unsigned operational-command bypass.

## WebSocket enrollment and readiness

Endpoints: `wss://<commissioned-hostname>/api/v1/ws`, read-only
`https://<commissioned-hostname>/api/v1/status`, and HTTPS CONFIG-V3 `/api/v1/config`.
`tools/m5_console.html` connects directly to the entered commissioned hostname. It
uses the localhost helper solely to sign the challenge; the browser never holds
a private key. See [PROTOCOL — Enrollment](PROTOCOL.md#enrollment) for exact
request/response fields, signatures, deadlines, errors and competing requests.

M10 replaces root delegation with proof of possession of the exact USB-staged
key, retaining `KEYVALET-ENROLL-V1` canonical bytes. No old root-signature fallback
or second USB enrollment flow exists. Only the WS proof writes the unchanged
trust record. USB is trusted to identify the intended key; the button proves
local participation; the signature proves key possession. LAN knowledge of a
public key alone grants neither staging nor signing authority.

Current readiness evidence:

- Improv info and `welcome`/status identify device, fixed hardware profile,
  firmware, full build ID and production/diagnostic variant.
- `0x80` and `0x07` establish actual SSID/connectivity and reachable address.
- `command_result {accepted:true, code:"ENROLLED"}` follows trust commit; refresh
  status/WS because welcome is a connection-time snapshot, not a periodic push.
- Status reports `enrolled`, `pairing_active`, `pairing_seconds_remaining`,
  `utc_valid`, locks/sensors, arbiter state/recovery, journal health and sync
  availability. The intended operational key comes from the USB staging receipt
  plus successful proof, not an unimplemented status key-export field.
- Read CONFIG-V3 and compare desired policy. Only the selected lock's own
  sensor/dependency and shared arbiter govern readiness; unused channels do not
  all need to be CLOSED, enabled or installed. Do not use an unlock as a generic
  enrollment/readiness probe. `utc_valid` is required for enrollment and signed
  operations; Wi-Fi/DHCP success alone does not imply SNTP/time readiness.

These are application reports, **not authenticated hardware attestation**.
M11C adds a device TLS private key and browser-validated server identity, not
individually signed telemetry or hardware attestation. TowneCore must retain the
physically commissioned UID/public-key/hostname association; `enrolled:true`
alone does not prove which authority is pinned. `enrollment_available` separates
unavailable security state from a genuinely unenrolled unit. `tls.ready` and
`tls.listening` separate valid material from a running listener; USB status is
available when no secure listener can start. PROTOCOL defines added status fields.

The single-press IPv4 suffix announcement when WS-disconnected remains part of
onboarding. Existing alarm-silence, pairing cancellation, five-second enrollment
hold and ten-second safe-reboot priorities are unchanged. The external LED still
indicates WS connectivity, not key verification or installer approval.

## History continuity, reset generations and authority replacement

Keep these separate in TowneCore:

1. Physical PCB evidence (operator label/USB hardware identity). Runtime
   `keyvalet-xxxxxx` uses only the final **24 bits** of the station MAC; collisions
   are possible and it is not attestation or a globally unique database key.
2. TowneCore's durable device-record identifier, independent of staff/signing key.
3. The current building public signing authority and its custody/audit history.
4. The journal history epoch and record sequence.

Normal replacement preserves current journal/ACK/trust/replay. Existing record
and batch identity is device ID + sequence; there is **no persistent random
journal-generation identifier** on the wire or in the record/ACK signature.
The ring checkpoint protects within-history sequence continuity, not uniqueness
after full destructive reset. A reset can reuse the same device ID/sequence.
An old signed ACK can also be ambiguous across a reset with the same authority,
fresh replay store and overlapping validity period. Do not silently merge/dedup
that new stream into an old history or relay old queued capabilities.

For current bench recovery, TowneCore must explicitly quarantine/reset the
association into a new backend history epoch after a documented destructive
reset, retaining old accepted records immutably. It cannot automatically infer
an epoch from a reboot, lower sequence, key change, firmware version or UTC.
An out-of-band backend epoch does not add cryptographic epoch binding on-device.

Before unattended production cross-reset synchronization, the smallest coherent
device change is one random 128-bit journal epoch created only with truly fresh
coordinated storage, retained through normal updates/reboots. Expose it in
status/retrieval/records and bind it into the next signed ACK contract; bind new
operational capabilities to the lifecycle as required by the reset threat model.
This requires an explicit protocol/storage decision, not an M10 speculative
migration. No epoch, journal format change or destructive reset is implemented.

Already-enrolled devices reject staging and first enrollment. There is no silent
key replacement, trust reset or production re-enrollment API. A future authorized
rotation/recovery design must retire the old key for new commands while keeping
past TowneCore events immutable; historical records are not re-signed under a
new authority. The current protocol does not yet implement that transition.

Existing retrieval/ACK contract remains [PROTOCOL](PROTOCOL.md#journal-synchronization):
durably store/deduplicate records **and explicit gaps**, then sign ACK; browser
memory/export alone is not backend durability. No TowneCore event database or
business logic is implemented in this repository.

The existing shared replay policy permits only strictly increasing **device
UTC acceptance seconds** across unlock, configuration and ACK. Schedule these
mutations serially with suitable cadence/retry; distinct operation IDs/nonces
or future `issued_at` values do not permit two acceptances in one device second.
Do not turn an ACK retry into a repeated actuator operation. See PROTOCOL's
replay/time bounds; this constraint is unchanged, not a new M10 rate limiter.

## Device management versus future Keys transactions

The immediate integration is Ops Manager KeyValet Settings: commissioned
identity, connectivity, read-only status/configuration and already-signed
administrative CONFIG-V3. Exact Origins apply to HTTPS and WSS, including
explicit treatment of absent/null Origin. USB controls are physical setup and
recovery, not LAN admin APIs. Read-only status is not a second user login system.

Future checkout/return Keys workflows remain TowneCore business logic. They may
obtain ordinary signed UNLOCK-V2 capabilities and relay them over local WSS;
the cloud must never forward commands directly to KeyValet. A TLS disconnect
does not prove an operation failed. Query status/journal and reconcile; never
blindly repeat a solenoid command. Signed configuration, ACK and operational
canonicalization/replay/storage are unchanged by M11C.

## Deferred device-authenticated certificate maintenance contract

This is the minimum proposed **future** HTTPS interface, not an implemented
firmware worker, deployed route or cloud service. No endpoint URL is currently
stored/provisioned by `0x82`, and no maintenance signing operation is exposed
over USB or WebSocket. Implement/qualify the worker and backend together later.

Infrastructure: Google Public CA, Cloud DNS, one minimal request-driven Cloud
Run maintenance service, Cloud Storage conditional-generation writes for durable
coordination, and Secret Manager for the central ACME account key. No Firestore,
broker, permanent connection or operational cloud polling. Staff IAP remains
intact; device routes occupy a separately authorized ingress boundary. Google
project/permissions/resources must be verified separately; none were accessed.

The registry binds stable full-MAC `device_uid` (21 ASCII bytes), commissioned
hostname, building association and current P-256 verification key established
through protected USB. The old 24-bit operational `device_id` remains unchanged
and is not a globally unique database key. Backend ownership is association-
controlled, independent of a manager's Staff account. MAC alone cannot authorize
registration, replacement or certificate issuance. Public material is available
as the SEC1 point and CSR/SPKI; use SHA-256 of DER SubjectPublicKeyInfo as the
canonical key fingerprint.

Logical routes:

1. `POST /keyvalet/v1/challenge`: bounded request with registered `device_uid`,
   current `key_fingerprint` and `operation_id`. Return random 128-bit hex
   `challenge_id`, random 256-bit hex `nonce`, and UTC `expires_at` no more than
   60 seconds ahead. This route grants no authority; rate-limit unknown identities
   and challenge creation. Store a bounded, single-use challenge durably.
2. `POST /keyvalet/v1/maintenance`: JSON version 1, the above fields, `kind`,
   commissioned `hostname`, optional `ipv4`, optional CSR DER as base64,
   optional `job_id`, and raw P-256/SHA-256 `signature` as 128 lowercase hex
   characters (fixed-width r||s). Verify against the registered current key,
   not the building operational key and not merely a presented certificate.

Exact proposed signed UTF-8, LF-terminated, no whitespace normalization:

```text
KEYVALET-TLS-MAINT-V1
<physically provisioned maintenance HTTPS audience>
POST
/keyvalet/v1/maintenance
<device_uid>
<current SPKI SHA-256, 64 lowercase hex>
<operation_id, 32 lowercase hex>
<challenge_id, 32 lowercase hex>
<nonce, 64 lowercase hex>
<expires_at, decimal seconds>
<kind>
<commissioned hostname>
<canonical RFC1918 IPv4 or NONE>
<SHA-256 of CSR DER, 64 lowercase hex, or NONE>
<job_id, 32 lowercase hex, or NONE>
```

Only four kinds are permitted: `renew` (same registered key/name CSR),
`rotate_key` (new on-device key's signed CSR, authorized by current key),
`update_ipv4` (that device's A record only), and `poll` (resume that device's
existing issuance job). Missing fields use the literal NONE in canonical text,
never an empty ambiguous value. Reject extra action types, arbitrary DNS names,
external/public addresses, substituted keys/CSRs, user/authority changes,
operational commands and journal data. Cap request JSON at 4096 bytes and response
JSON at 8192 bytes, with chain PEM at the implemented 6144-byte/four-certificate
limit; do not return arbitrary downloaded content.

Atomically consume the challenge with a conditional state write before effects.
Bind `operation_id` to a digest of semantic fields (excluding fresh challenge/
authentication metadata). A retry uses a fresh challenge and the same semantic
operation ID; return its prior result/job, never duplicate an issuance. Reusing
the ID with changed semantic content is `OPERATION_CONFLICT`. Registry/current-
key updates and pending-key transitions also require conditional writes. In-memory
nonces or container files cannot provide cross-instance/restart coordination.

Responses echo operation ID and have `state:COMPLETE` or `PENDING`. COMPLETE
returns the issued PEM chain for issuance, or confirmed address/revision for DNS.
PENDING returns `job_id` and bounded `retry_after_s`; a later authenticated poll
advances/retrieves that same job. Errors distinguish INVALID_REQUEST, UNKNOWN_DEVICE,
NOT_AUTHORIZED, CHALLENGE_EXPIRED, REPLAY, OPERATION_CONFLICT, KEY_MISMATCH,
HOSTNAME_MISMATCH, BUSY, CA_PENDING, RATE_LIMITED and TEMPORARILY_UNAVAILABLE.
Permanent validation/auth failures stop automatic retries; transient failures
honor Retry-After with exponential backoff/jitter and a bounded attempt budget.
Each request must complete or persist a resumable step before returning; no
untracked background work after a Cloud Run response.

Schedule from actual certificate validity/compatible renewal information well
before expiry, not fixed frequent polling. Update DNS after a changed DHCP
address, suppress unchanged writes, and never substitute the public NAT address.
Certificate installation reuses the same key/SAN/chain/time/size checks and safe
candidate activation as USB. Maintenance cannot change the assigned hostname,
Origins, service audience or building authority; such policy changes require
separately authorized physical commissioning or an explicitly designed signed
administrative contract, not a permissive maintenance response.

An expired local certificate does not retire its registered proof key: recovery
can authenticate an outbound request with that key while validating the cloud
server normally. No Staff IAP session is needed. Rotation binds the new CSR/key
to old-key authorization and must preserve old/pending credentials through an
acknowledged transition; hostname/UID remain stable. Backend revocation or lost
keys require explicit association recovery over protected USB, never first-key
LAN takeover. Service URL/trust provisioning, concrete retry budgets and the
power-failure-safe rotation acknowledgment are worker/backend implementation
details still to be finalized together—not claimed implemented by this contract.

Unauthenticated SNTP remains the approved UTC policy. USB UTC is an explicitly
physical recovery input, not authenticated network time. Maintenance must not
bypass backend TLS verification after clock loss. Without valid UTC/certificate,
secure operations fail closed; protected USB remains available. Public DNS may
need scoped building-local resolution for rebinding protection or WAN outages.
Cert expiry and uncached public DNS mean indefinite offline WSS is not promised.

## Concrete remaining milestones

- Certificate maintenance service/worker remains `NOT_IMPLEMENTED`, including
  automated renewal, fleet issuance, scheduling and remote lifecycle management.
  Manual Google Public CA ACME DNS-01 issuance and Cloud DNS delegation were used
  for accepted commissioning, not automation. The current certificate expires
  February 13, 2027; protected-USB renewal is required before expiry until the
  future service exists. Broader resource/endurance and mobile/PWA qualification
  remain; desktop four-client WSS is accepted. Never weaken
  verification or signed commands to work around browser/network restrictions.
- M12: authenticated OTA delivery/installation/selection, validation/rollback
  and downgrade/replay continuity. M10 supplies an app payload, not an OTA API;
  do not upload the factory merge to a future app-only writer.
- Production release: qualify the pinned browser library/OS/native USB behavior,
  building key custody/recovery, cross-reset epoch policy and remaining
  electrical/timing/endurance safety.

This revision is implementation-grade for the paths it describes; it explicitly
does not promise automatic cross-reset reconciliation, unattended TLS rotation,
operational authority rotation, hardware attestation or a completed installer.
