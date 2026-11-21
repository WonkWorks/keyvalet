# Firmware

## Startup and hardware safety

Firmware is native ESP-IDF for the sole KeyValet v0.4.2 target. Startup:

1. Preloads GPIO10 and GPIO3 LOW, configures both outputs, and asserts both LOW.
2. Initializes LED, button, all four sensors, journal, settings, trust, and
   replay storage before enabling signed control.
3. Starts the controller, piezo, connectivity indicator, network services,
   event publication, startup chirp, and diagnostic task when compiled.

Both lock outputs share one arbiter and one timer. A pulse is programmed for
350 ms and followed by a provisional 2000 ms recovery interval. Cancellation,
timer/start failure, force-inactive, and safe reboot drive both outputs LOW.

## Parameterized channels

All sensors are sampled continuously with 25 ms debounce. Monitoring settings
gate alarm/reminder policy without hiding state. Lock 1/Sensor 1 and Lock
2/Sensor 2 enter the same parameterized state function and use array-indexed
last-actuation times, operation guards, temporary timeout values, reminder
deadlines, silence deadlines, alarm state, and journal generation.

For a sensor-required lock, an opening within five seconds after actuation is
expected. An expected opening starts its positive signed timeout or persistent default;
expiry produces the ordinary reminder. Other openings are unexpected and, when
monitoring is enabled, start the urgent alarm immediately. Closing clears that
sensor's timers and alarm without suppressing another channel. Sensors 3–4 are
sensor-only urgent sources.
Explicit timeout zero creates no reminder deadline, but leaves prerequisites,
expected/unexpected classification, monitoring and urgent alarms intact. Close
completes the authorized-opening window, preventing a stale override (including
zero) from applying to a later unrelated opening.

These are capacity limits: an installation may use fewer than two solenoids or
four sensors. Disable monitoring for unused inputs; a pulled-up, unconnected
input may report OPEN and journal physical transitions but cannot start an
urgent alarm or reminder while monitoring is disabled. Applying a disabled
setting also clears that sensor's active alarm and paired reminder deadline,
without clearing another sensor's alarm. No global all-CLOSED/all-enabled check
exists. With the default `sensor_required: true`, signed unlock requires only the
target's paired sensor known CLOSED, its guard, shared arbiter and authorization; an
unused other lock/input does not prevent it. Disabling alarm monitoring is not
a bypass for the target sensor's fail-closed unlock guard.

Signed CONFIG-V3 may independently set either lock's `sensor_required` false.
Then OPEN or unknown paired state does not block actuation, the controller guard
ends at successful pulse start, and no paired reminder lifecycle is created.
Shared pulse/recovery and replay/journal-before-actuation remain mandatory. An
independent input opening is unexpected and can still raise an urgent alarm if
its monitoring is enabled. Disable monitoring separately for unused inputs.

Urgent alarms have priority over ordinary reminders. A short button release
silences active alarms first, otherwise cancels pairing, otherwise announces the
IP octet when eligible. Five seconds opens the pairing window; ten seconds
requests release-gated safe reboot.

## Signed control order

For either lock the order is:

1. Reserve the paired operation guard and shared hardware arbiter.
2. Validate device, target, timeout, UTC window, signature, and replay identity.
3. Durably append replay state.
4. Durably append target-identified authorization to the journal.
5. Start the selected finite pulse.

Sensor-required no-open operations release after 15 seconds while the sensor
remains closed. An OPEN/CLOSED cycle releases its own guard; sensor-independent
operations release their guard at pulse start, not after a door timeout.
Failures are fail-closed. A dependency change clears stale door bookkeeping and
invalidates a pending controller reservation, without altering an active pulse.

## Persistent state

Settings schema 4 stores revision, default 5–3600 second door-open timeout, two
sensor-required bits and four monitoring flags in 28 bytes with CRC. Both locks
default to sensor-required. There is no retired delay word or
earlier schema reader. Defaults are revision 1, 90 seconds, and monitoring
enabled for Sensors 1, 3, and 4 (Sensor 2 defaults off).
CONFIG-V3 binds the complete settings update to the authority signature and CAS
revision. Settings write errors disable subsequent updates until reboot reload,
so an ambiguous durable commit cannot be overwritten using an older RAM revision.

Replay protection uses the unchanged 64 KiB `replay_nvs` partition and 160
fixed 64-byte records. One 12-byte version-2 marker identifies initialized
storage. Trust, signature/time checking, replay persistence-before-acceptance,
and fail-closed corruption handling remain. There is no earlier-blob importer.

The journal remains 40,960 fixed 64-byte slots at `0x510000`, now managed as a
circular log. A durable PREPARED checkpoint precedes reclamation of each oldest
4 KiB sector; READY is committed after erase verification and before new body
writes. Body and CRC are verified before the commit marker. Sequence generations
continue across wraps. Interrupted or corrupt occupied slots remain explicit
gaps. Healthy retained count varies from 40,897 to 40,960 once filled.
Only current event names and one-based channel detail are exposed. There is no
historical event translation or append-only adoption. Missing ring metadata
requires completely blank journal storage for initialization. See
[JOURNAL_RETENTION.md](JOURNAL_RETENTION.md) for recovery and loss semantics.

The journal synchronization cursor is stored independently in default NVS as a
versioned CRC-protected blob. It is loaded across reboot and advances only after
signed authority validation and durable replay consumption. Cursor persistence
is committed before success is returned; a lost response can be retried with a
new signed operation. Journal record size, trust format, cursor format, replay
record size, and the partition layout remain unchanged. Settings and replay
marker formats deliberately reject development data. See [STORAGE.md](STORAGE.md)
for the operator's completed clean installation and remaining physical acceptance;
no reset is performed by firmware.

## Network and diagnostics

Wi-Fi is station-only with DHCP and SNTP. A fresh device initializes Wi-Fi and
USB with no saved credentials and does not attempt a connection. Runtime Improv
Serial provisioning is available in both variants without pairing, a button
press, or network authentication, relying on the protected physical USB port.
No compile-time credential header is used. One worker coordinates scanning,
30-second candidate attempts, rollback and 1–30 second reconnect backoff.
The previous credentials are retained until successful DHCP and NVS commit;
candidate failure restores them without reboot. Internet/SNTP is not needed to
declare provisioning success (signed operations retain their UTC requirements).
See [WIFI_PROVISIONING.md](WIFI_PROVISIONING.md).

First enrollment in either variant requires a valid operational public key
temporarily staged over protected USB, the existing five-second button hold,
and ENROLL-V1 proof of possession through WebSocket. No compiled development
root or private key is used. The staged candidate is bounded to five minutes,
never replaces stored trust, and clears on expiry/failure/cancellation/reboot.
The ten-minute physical window alone cannot accept an arbitrary LAN key.
Trust persistence errors latch authorization closed until reboot reloads NVS.
See [PROTOCOL](PROTOCOL.md#enrollment) for exact bytes and concurrency rules.

Network failure does not stop local
monitoring or journaling. Status and up to four WebSocket clients expose current
state and post-commit live events. Queue or client failure cannot roll back the
journal.

Welcome/status announces journal synchronization bounds. Clients explicitly
pull deterministic batches of at most eight records; each request scans at most
16 slots and allocates only the fixed eight-record stack batch before bounded
JSON serialization. Prefix loss uses the RAM index without scanning discarded
flash. Reads never mark records acknowledged. Journal flash maintenance waits
for an active pulse to finish and gates subsequent pulse starts; sector erase
and checkpoint latency can delay requests and normal task scheduling. The
reference console retrieves opportunistically
but requires an explicit, strongly labeled operator action to sign an
acknowledgment after external durable persistence.

Production has no diagnostic parser. Diagnostic builds add allowlisted USB
status, LED, chirp, finite lock pulses, safe reboot, and bounded read-only
journal inspection. They never expose arbitrary GPIO or erase.

Canonical isolated builds are:

```sh
idf.py -B build -DSDKCONFIG=sdkconfig.production.local \
  -DSDKCONFIG_DEFAULTS=sdkconfig.defaults build

idf.py -B build-diagnostics -DSDKCONFIG=sdkconfig.diagnostics.local \
  -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.diagnostics' build
```

Each image reports revision, clean/dirty state, production/diagnostic variant,
and full application-ELF SHA-256. Private signing keys remain ignored local
inputs. Wi-Fi credentials are runtime NVS data, never tracked source or build
inputs. An obsolete ignored local credential header is no longer included.

## Not implemented in this pass

TowneCore persistence integration, automatic certificate/DNS maintenance, authority
rotation/recovery, cross-reset journal identity, OTA policy, and additional electrical safety
qualification remain deferred.

Factory/replacement packaging and the versioned browser/backend responsibilities
are in [FACTORY_INSTALLATION](FACTORY_INSTALLATION.md) and
[TOWNECORE_INTEGRATION_V1](TOWNECORE_INTEGRATION_V1.md). TowneCore P1's Chrome/Web
Serial foundation is implemented/development-deployed and visually inspected;
physical browser flashing and live release retrieval remain pending. OTA and
automated certificate operations remain unimplemented. See
[publication/source delivery](PUBLICATION.md). PCB v0.4.2 is hardware, distinct
from firmware `0.5.0-m5` and the P0 distribution ID.

## M11C secure transport and commissioning

Both variants expose only HTTPS/WSS at the commissioned DNS name on port 443.
No valid active TLS tuple or valid UTC means no network listener, not plaintext
fallback. Certificate expiry closes sessions through the safe transport inhibit;
handlers also reject requests once readiness is lost. Existing payloads,
ENROLL-V1/UNLOCK-V2/CONFIG-V3/ACK-V1 canonical bytes and persisted formats remain.

`tls_material` creates a P-256 DER private key (at most 256 bytes), a PEM CSR
with exactly the assigned DNS SAN, and a SEC1 public point. `tls_identity` stores
these independently of operational authority. Protected USB RPC `0x82` prepares,
exports public data, accepts up to 6144 bytes of leaf-first PEM chain, validates,
and separately requests activation. `tools/tls_usb.py` is the operator/reference
client; stop MONITOR first. Exact framing is in PROTOCOL; lifecycle and future
cloud contract are in TOWNECORE_INTEGRATION_V1.

Candidate validation checks P-256 key match, one exact DNS SAN (no CN fallback,
wildcard or additional names), server usage, supported signature algorithms,
chain signatures/CA constraints and validity. The supplied terminal CA is only
a consistency anchor; the browser independently establishes public trust.
Validity uses the intersection of supplied chain dates. Unauthenticated SNTP
remains the existing UTC policy; USB UTC recovery is narrowly physical. Neither
time source is presented as cryptographically authenticated network time.

TLS records have active/inactive slots and an explicit activation selector.
Writes use the existing flash gate; an uncertain write disables further TLS
mutations this boot while an existing known-good listener can continue. Reboot
reloads durable truth. A short actuation inhibit protects shutdown/activation/
restart, refuses ACTIVE/RESERVED, and does not hold the output mutex across
server shutdown. Clients must query outcomes, never automatically replay a
physical operation after reconnecting.

The HTTPS task has 16 KiB stack; USB 12 KiB; web worker 8 KiB. Runtime status/logs
report heap, minimum heap, largest block, HTTPS/worker stack margins, session
counts and TLS errors. USB TLS status also reports its task's stack margin.
Four WSS plus one transient HTTPS socket are configured; handshakes are serialized
by IDF's server, bounded to 3 seconds. No outbound TLS
worker exists. Safari/Chrome and four sustained sessions are physically accepted
(see STATUS). Peak resource, long-duration fragmentation and broader reconnect/
mobile/PWA qualification are not implied by that observation.
