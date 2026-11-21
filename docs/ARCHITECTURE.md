# Architecture

## Hardware ownership

`hardware_profile` contains the single v0.4.2 mapping. `hardware_io` exclusively
owns GPIO outputs and drives both solenoids through one `lock_arbiter` and one
timer. Signed operations transition `READY -> RESERVED -> ACTIVE -> RECOVERY ->
READY`; diagnostics transition directly from READY to ACTIVE. Electrical or
timer failures enter fail-closed FAULT.

`input_monitor` continuously samples and independently debounces all four
sensors. `device_controller` owns array-indexed sensor state, monitoring,
expected/unexpected classification, paired-channel reminders, sensor-only urgent
alarms, silence deadlines, operation guards, pairing/button behavior, and shared
piezo priority. Lock/Sensor 1 and Lock/Sensor 2 use the same parameterized path.

## Security and persistence

`security` owns physical pairing, protected-USB staged first-use public keys,
ENROLL-V1 proof/trust persistence, signature/time validation,
and the in-memory replay window. `replay_store` owns the dedicated NVS ring and
its initialization marker. `settings` owns one compact CRC-protected schema-4
NVS blob. Each store has one reader; there are no development-format importers.

`journal_format` owns the fixed 64-byte record codec and current generic event
names. `journal_ring` owns sequence recovery, oldest-sector reclamation,
the bounded bitmap, and deterministic reads using the original record codec.
`journal` owns the ESP flash/NVS adapter, mutex, telemetry, and existing hourly
heartbeat. Sequence maps to physical slot modulo capacity; only the oldest
sector is erased, after a durable PREPARED checkpoint. READY is durable before
new records are programmed. See [retention and recovery](JOURNAL_RETENTION.md).
Missing metadata on populated storage fails closed. Only wholly erased storage
can initialize a new circular generation. Exact formats and the coordinated
development reset boundary are in [STORAGE.md](STORAGE.md).

`journal_sync` owns the monotonic CRC-protected NVS acknowledgment cursor and
derives availability/gap status from journal bounds. `journal_sync_contract`
owns the versioned canonical acknowledgment bytes and monotonic decision rules.

## Network boundary

`network_manager` has one worker owning station Wi-Fi, scans, DHCP candidates,
last-successful configuration, reconnect backoff, device identity and RSSI.
IDF events are copied into its bounded queue; a station-STOP barrier separates
credential generations. `wifi_store` owns only the CRC-protected Wi-Fi blob in
`keyvalet_wifi/station`; candidate data stays volatile until DHCP succeeds.
Credential writes use the existing output flash gate, not a second arbiter.
`web_server` owns status, WebSocket clients, enrollment transport,
signed V2 unlock, signed CONFIG-V3 updates, and a bounded post-commit live-event
queue. It also exposes bounded idempotent journal retrieval and signed
acknowledgment. Queue/send failure cannot roll back a durable journal append.

The server now uses ESP-IDF HTTPS/WSS exclusively, port 443. `tls_material`
handles P-256/CSR and installed-certificate consistency; `tls_identity` owns
bounded active/candidate NVS records in `tls_nvs`. `tls_usb` adds one narrow
vendor RPC to the existing USB receiver. No device private key is exported.
The full station-MAC `device_uid` is stable across TLS rotation; the existing
short `device_id` and operational signatures remain unchanged. Neither MAC
string is attestation: commissioning binds the public key through protected USB.

TLS activation runs in the web worker, not an HTTP handler. A short maintenance
inhibit rejects new reservations/pulses and cannot begin during ACTIVE/RESERVED.
It holds no output mutex across HTTPS shutdown, so existing request completion
cannot deadlock against that mutex. The worker stops HTTPS, commits the candidate
selector and restarts under the inhibit. Existing 350 ms timer/recovery semantics
are unchanged. Network/time/expiry shutdown uses the same safe boundary.

Four WSS clients remain the fixed target, with a fifth socket for short HTTPS
overlap. No LRU eviction of healthy WSS clients; 3-second TLS handshake and
2-second I/O bounds, TCP keepalive and 15-second server PING/60-second receive
liveness apply. Status/logs expose heap/minimum/largest-block and task margins.
Four browser sessions passed the operator's approximately eight-minute PCB
observation (see STATUS); instrumentation is not long-term resource qualification.
The local `esp_https_server` component is IDF v5.5.5 with one allocation-failure
cleanup correction; see its README. No SDK installation is modified.

The unlock order is target reservation, signed authorization and durable replay,
durable journal acceptance, then nonblocking pulse. The per-lock operation guard
is held until an observed OPEN/CLOSED cycle or a 15-second no-open timeout while
the paired sensor remains closed when `sensor_required` is true. When false,
the guard ends at pulse start and never creates a door lifecycle; independent
monitoring continues. Zero suppresses an authorized-opening reminder without
changing a required sensor prerequisite or urgent policy. Dependency changes
clear old controller bookkeeping, never hardware pulse/recovery state.

Journal flash work acquires the output mutex only after ACTIVE pulse completion;
it releases that mutex while waiting so the task timer can lower both outputs.
The held gate excludes pulse starts during erase/checkpoint/write. Flash still
adds finite scheduling and pre-actuation latency; host tests do not measure PCB
timing or eliminate existing non-journal NVS contention.

## Diagnostic boundary

`usb_provision` owns the sole USB Serial/JTAG receiver in both builds and
dispatches standard Improv frames using the bounded `improv_serial` codec.
Complete binary responses bypass VFS newline translation and use the stdout
lock to exclude log interleaving. Driver writes have a bounded timeout; USB loss
does not cancel the network worker's candidate transaction.

Production contains no privileged `KVTEST` parser. A diagnostic build adds exact,
allowlisted status, LED, chirp, finite lock-pulse, reboot, and bounded read-only
journal commands. It exposes neither arbitrary GPIO nor flash erase.

MONITOR remains the terminal port owner, with CONTROL using its private local
IPC for actual Improv requests/results. The signing helper is not involved in
Wi-Fi setup. The narrow production Improv parser is always present.

M10 first enrollment uses the same receiver for a narrow public-key staging
RPC; only the button-gated WebSocket proof persists authority. There is no
compiled root or duplicate enrollment system. `tools/package_firmware.py`
packages IDF artifacts without contacting hardware. Factory installation and
both-slot USB replacement are distinct from destructive recovery.

TowneCore durable persistence integration, certificate-maintenance execution, authority
rotation/recovery, cross-reset journal identity and OTA remain planned work.
