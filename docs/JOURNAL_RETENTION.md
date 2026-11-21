# Circular journal retention — Milestone 7 Pass 3B

## Canonical storage

The unchanged `journal` partition occupies `0x510000`–`0x78ffff`: 2,621,440
bytes, 640 aligned 4,096-byte erase sectors, 64 records per sector, and 40,960
physical 64-byte record slots. No sectors are reserved for new headers. Record
magic, version, field offsets, CRC coverage, commit marker, metadata, and channel
detail remain. Only current event names are retained. There is one circular
reader, no historical event translator, and no append-only adoption path.
See [STORAGE.md](STORAGE.md) for the coordinated development reset boundary.

The inspected ESP-IDF v5.5.5 generic driver uses 4 KiB sectors and 256-byte
program pages. Each aligned 64-byte record's 60-byte body and four-byte commit
stay within one such page. Programming changes bits from 1 to 0; reuse requires
sector erase. The adapter deliberately requests only 4 KiB, so the driver's
64 KiB block-erase optimization cannot destroy neighboring newer sectors.
The installed PCB's flash model was not queried in this offline pass.

The ring maps sequence `s` to slot `(s - 1) % 40960`. Sequences never reset on
wrap. A 5,120-byte validity bitmap is recovered from all sectors at boot; the
ring state is approximately 5.2 KiB, independent of event count. Boot uses a
temporary 4 KiB heap buffer. There is no second event store.
The final ESP32-C6 object reports `s_ring` as 5,200 bytes; the host test struct
is 5,232 bytes because its callback pointers are wider.

## Reclamation transaction

At a sector boundary, all newer sectors remain untouched. The next/oldest
sector is reclaimed in this order:

1. Commit a CRC-protected 40-byte `ring_v1` blob in default NVS namespace
   `keyvalet_jrnl`. It identifies geometry, the new sector's base sequence, and
   phase `PREPARED`.
2. Logically retire the victim sector's old sequences. Erase exactly one 4 KiB
   sector and verify every byte is erased.
3. Commit the same checkpoint with phase `READY`.
4. Program and verify each record's 60-byte body/CRC, then program its four-byte
   commit marker and verify the complete record.

Two metadata commits cover 64 normal appends; there is no metadata write per
record. The checkpoint is separate from Pass 3's acknowledgment cursor. It
contains no event payload or acknowledgment authority.

Once full, capacity reclamation happens regardless of acknowledgment or network
state. No unneeded young sector is erased to favor an acknowledged record.
Sector granularity means healthy retained count varies from 40,897 to 40,960
after filling: up to 63 usable positions belong to the freshly reclaimed sector.
Acknowledged and unacknowledged records follow the same oldest-first policy.

## Recovery and loss accounting

`PREPARED` means the victim is unavailable even if an interrupted erase left
old CRC-valid records behind. Reboot repeats that sector's erase and verifies it
before committing `READY`; it never treats stale victim data as new records.
`READY` means recovery scans the active sector for occupied slots, including
partial records, and resumes after the last occupied slot. Other sectors are
validated by CRC, commit marker, logical window, and sequence-to-slot mapping.
The checkpoint's sector base preserves the preceding sequence watermark even
when power failed before the first new record was written.

Non-erased partial records consume their assigned sequence and are reported as
unavailable. An attempt that left no programmed bits and no committed record
may reuse that never-committed sequence after reboot. Successful durable
sequences are never reused. A flash/NVS error disables further writes until
reboot recovery; no erase/reset fallback masks corrupt metadata. Missing
metadata with any occupied slot fails closed without writes or erases. On
wholly erased storage, initialization establishes PREPARED, erases/verifies the
first sector, and commits READY before accepting records. Cuts at every initial
checkpoint/erase boundary recover by the same transaction rules as later wraps.

`oldest_available_sequence` and `latest_available_sequence` describe surviving
CRC-valid records (zero when none). Additive `highest_produced_sequence` covers
the assigned/consumed sequence watermark, including interrupted occupied slots.
The unfilled tail of an active sector is never reported as produced. Retrieval
and acknowledgment bounds use this watermark so a final lost record can be
explicitly accounted for even when no later valid event exists.

`journal_sync` reports the first complete missing interval after the durable
acknowledgment. Each `journal_batch` reports one explicit missing interval and a
next cursor; additional separated gaps are discovered by continuing retrieval.
An overwritten prefix is skipped in one response with its exact bounds, without
reading discarded slots. For example, cursor 100 and oldest survivor 151 report
101–150. Surviving records are returned in ascending sequence order. Capacity
can change between requests, so retry remains a read with explicit loss reporting,
not a promise of indefinite availability or byte-identical responses after wrap.

The `KEYVALET-JOURNAL-ACK-V1` canonical bytes, enrolled authority, time rules,
and durable replay protection are unchanged. Only a valid signed acknowledgment
can persist cursor advancement, including advancement across a loss TowneCore
has durably recorded. The reference console exports records and loss ranges
and requires explicit development operator attestation before signing.
An ambiguous NVS cursor-write/commit error disables further acknowledgments
until reboot reloads the durable value, preventing an older RAM cursor from
overwriting a higher value that may already have reached flash. Lock operations
are independent of synchronization readiness.

## Scheduling, flash wear, and limits of offline evidence

ESP-IDF v5.5.5's configured SPI1 flash driver disables caches and normal task
execution during flash commands; automatic suspend is disabled. The 20 ms
erase-yield setting does not establish a 20 ms bound for one sector erase.
The solenoid timer uses `ESP_TIMER_TASK`, so merely assigning reclamation to
another task would not protect pulse timing.

Journal append therefore acquires the existing output mutex only when the
arbiter is not ACTIVE. While ACTIVE it releases that mutex and yields one tick,
letting the timer callback lower both outputs. It then holds the mutex across
the ring's NVS checkpoints, erase, and record writes, excluding pulse starts.
The lock order is journal mutex -> output mutex; timer callbacks never acquire
the journal mutex. No new thread, background cloud connection, or periodic
heartbeat policy is introduced. The existing hourly heartbeat remains unchanged.

The signed-unlock journal commit still precedes actuation. It may now wait for
one oldest-sector erase/checkpoint transaction at a boundary; this is finite
maintenance latency, not a synchronization dependency. Other tasks, sensors,
network handling, and the LED updater can also be delayed by the shared flash
and mutex. Existing replay/settings/acknowledgment NVS writes are outside this
journal-specific gate and retain their pre-existing flash-timing limitations.
No hard latency or physical 350 ms pulse guarantee is inferred from host tests.

Diagnostics expose boot-relative `max_append_us` (excluding an active-pulse
wait) and `max_erase_us`. These are measurements to collect later on the PCB.
The gate's real firmware code is tested with simulated time and competing
pulse requests; a simulated 25 ms erase is a test stimulus, not a PCB measurement.

Steady state erases each journal sector once per 40,960 appends. The smaller
default NVS area also sees two checkpoint commits per 64 appends and its own
garbage collection; it may be the endurance/latency bottleneck. Interrupted
PREPARED recovery can repeat one erase and checkpoint. Flash lifetime, actual
erase/program latency, default-NVS free space and wear, brownout behavior, and
pulse width under concurrent non-journal persistence require physical
qualification. Guarantees assume NVS's atomic single-blob recovery, successful
flash operations affecting only their addressed sector, and intact hardware;
unrecoverable metadata corruption requires operator recovery, never automatic
data erasure.

Partition backups/restores must retain matching journal ring metadata
and acknowledgment state in NVS. A production downgrade/recovery policy remains
separate work.

## Offline verification

`tests/journal_ring_test.c` runs the production ring engine against simulated NOR
flash that enforces 1-to-0 programming. It covers blank initialization and
populated canonical reboot, nonblank/missing-metadata rejection without mutation,
CRC/commit failures, repeated wraps, no-sync operation, prefix and
internal gaps, corrupted/missing metadata, and 105 cuts before/during/after
initial initialization and subsequent PREPARED save, erase, READY save, body,
and commit. It also interrupts reboot
reclamation repeatedly. A full-geometry run appends 123,657 events with 1,933
sector erases and 3,866 checkpoint commits.

`tests/journal_integration_test.c` compiles the real journal adapter, cursor
module, GPIO owner, timer callback, and arbiter against host peripheral/NVS
stubs. It verifies cursor recovery across successful/failed/lost commits,
acknowledgment after loss, exact loss bounds, journal flash exclusion while
either lock is active, release of the output mutex while waiting, and competing
pulse requests blocked by the held gate.

`tests/journal_console_test.js` verifies automatic/explicit retrieval, duplicate
response handling, reconnect from the durable cursor, exported gaps, and absence
of automatic acknowledgment. The focused Python signing test verifies real
P-256 acknowledgment signatures and rejects tampered device, operation, and
sequence. Existing security, configuration, sensor, arbiter, journal-codec, and
browser regressions remain required.

`tests/journal_json_test.py` compiles the actual firmware record serializer with
the installed ESP-IDF cJSON and fails each allocation in turn. Failed
serialization neither sends an incomplete event nor leaks its partial object.
