# KeyValet v0.4.2 Firmware Handoff

v0.4.2 identifies the PCB hardware, not the embedded firmware version. Bench
workstation identifiers below are sanitized; physical observations are unchanged.
External KiCad/manufacturing sources are not included in this firmware repository;
no hardware-design license is assigned by the project's firmware license.

The v0.4.2 PCB is the sole canonical first-product platform. Both locks and four
sensors are active. Runtime and public contracts are purpose-neutral; there is
no prototype hardware or development-storage compatibility target. See
[Firmware](../../FIRMWARE.md), [Protocol](../../PROTOCOL.md), and the
[canonical storage/reset boundary](../../STORAGE.md).

For the full construction and electrical record, see [PCB Manufacturing Reference](PCB_MANUFACTURING_REFERENCE.md). For mandatory electrical and output-safety constraints, see [Firmware Requirements](FIRMWARE_REQUIREMENTS.md).

## Hardware Summary

- ESP32-C6-WROOM-1-N8 with 8 MB flash.
- Native USB Serial/JTAG.
- Two independently MOSFET-controlled solenoid outputs.
- Two dedicated conditioned Reed inputs.
- Three expansion GPIO/GND pairs.
- External button and status LED connectors.
- Onboard passive piezo.
- 12 V input, 5 V buck conversion, and 3.3 V regulation.
- No hardware mutual-exclusion interlock between lock outputs.

USB can power the logic but cannot power the lock coils. The onboard piezo uses the 12 V-derived power system.

## GPIO Assignments

| Function | GPIO | Behavior / restriction |
|---|---:|---|
| External button | 2 | Active LOW |
| Status LED | 4 | Active HIGH |
| Reserved | 5 | Unconnected; unconfigured |
| Reed 1 | 6 | Active LOW; open contact reads HIGH, closed reads LOW |
| Reed 2 | 7 | Active LOW; open contact reads HIGH, closed reads LOW |
| Lock 1 | 10 | Active HIGH |
| Lock 2 | 3 | Active HIGH; signed target-specific unlock and guarded diagnostics |
| Piezo | 11 | PWM/timed toggling; default LOW |
| Sensor 3 / Sensor 4 | 18, 19 | Active LOW; internal pull-ups |
| Reserved | 20 | Do not initialize |
| Native USB D− / D+ | 12, 13 | Do not repurpose |
| BOOT | 9 | Strapping input |
| RESET | EN | Active LOW |

GPIO8 and GPIO15 have boot-strapping circuitry and must not be repurposed casually.

## Canonical Firmware Mapping

| v0.4.2 component | Firmware function |
|---|---|
| Lock 1 / GPIO10 | `lock_1`, paired with `sensor_1` |
| Reed 1 / GPIO6 | `sensor_1` |
| Reed 2 / GPIO7 | `sensor_2`, paired with `lock_2` |
| Button / GPIO2 | Existing physical control button |
| LED / GPIO4 | Existing external status indicator |
| Piezo / GPIO11 | Existing audible feedback and alarms |
| Lock 2 / GPIO3 | `lock_2`, paired with `sensor_2` |
| GPIO18 | `sensor_3`; independent sensor-only input with internal pull-up |
| GPIO19 | `sensor_4`; independent sensor-only input with internal pull-up |
| GPIO20 | Reserved and uninitialized |

Both locks share parameterized signed authorization, the sole arbiter/timer,
operation guards, and paired-sensor policy. Signed CONFIG-V3 makes the paired
sensor dependency configurable per lock (default required), allowing deliberate
solenoid-only use. Signed per-operation zero suppresses reminders only.
Independent monitoring applies equally; no cabinet purpose is hard-coded.

## Functional Scope

- Wi-Fi startup and reconnect.
- Local HTTP status and configuration endpoints.
- Local WebSocket server and clients.
- Browser acceptance console.
- USB diagnostic monitor and control menu.
- Physical-button behavior and IP-octet announcement.
- Pairing, enrollment, persistent trusted authority, and signed authorization.
- Signed target-specific unlocking and durable replay protection.
- Persistent settings and revision handling.
- Four independent purpose-neutral sensor monitoring and alarm paths.
- Durable event journaling and journal-before-actuation ordering.
- Existing fail-closed behavior.

- Circular journal retrieval, explicit signed acknowledgment, and exact loss reporting.

There is one canonical storage reader per domain and no development-data import.

## Mandatory Lock Safety Rules

For both production signed unlock and diagnostic pulses:

1. Preload the GPIO10 and GPIO3 output latches LOW before enabling output mode on either pin.
2. Hold both outputs LOW throughout initialization and failure handling.
3. Mark hardware initialization safe before accepting any unlock command.
4. Retain the existing maximum 350 ms pulse for either lock.
5. Permit only one selected lock to energize through the shared arbiter.
6. Apply the same safety policy to both lock channels.
7. Preserve durable replay and journal acceptance before signed actuation.
8. Use one timer and reject requests during active and recovery phases.

The removable 3 A fuse provides overcurrent protection; it is not a mutual-exclusion interlock.

The shared arbiter uses a provisional compile-time 2000 ms recovery interval.
It must eventually be validated against measured flyback decay: LOW MOSFET gates
do not prove that coil current has reached zero.

Milestone 6 functional testing subsequently operated both connected solenoids
individually through USB diagnostics, observed ACTIVE/RECOVERY/READY, and
confirmed `PULSE_BUSY` rejection for cross-lock and rapid same-lock requests.
All four sensors reported CLOSED; Sensors 3 and 4 independently reported opening
and closure with correct journal identities, including simultaneous openings.
USB and 12 V power, the button, LED, and onboard piezo were functional. No scope
measurement established exact pulse duration, gate isolation, coil-current
decay, brownout safety, or thermal endurance.

Both MOSFET gates must be verified with a scope during power ramps, unplugging, brownouts, BOOT/RESET, and watchdog recovery before default-OFF behavior is considered accepted.

## Piezo

The onboard BZ1 is a Same Sky/CUI CPT-9019S-SMT-TR externally driven passive piezo using an MMBT2222A transistor stage from GPIO11. Its rated frequency is approximately 4 kHz.

- Drive it with PWM or timed toggling; a steady HIGH does not produce the intended tone.
- Keep GPIO11 LOW by default and when silent.
- Preserve the accepted sound meanings and sequencing.
- Evaluate the existing frequencies on hardware; do not treat waveform generation as proof of adequate audibility.
- Piezo testing requires the 12 V-derived power system and is not valid under USB-only power.

## Power and Test Status

### Phase 1 USB-powered acceptance — 2026-11-12

The v0.4.2 diagnostic firmware was flashed and exercised on the physical PCB
using USB power only. Its verified application-ELF SHA-256 was
`833f7278681a43187a3446d1ca002233a78012b0d3010719830942c850b3f3b3`.

Physically accepted during this session:

- Native USB Serial/JTAG enumeration and its stable bench-workstation device path.
- Firmware boot, Wi-Fi connection, USB diagnostic monitor, and LED menu.
- GPIO2 button press/release, five-second pairing entry, and short-press cancel.
- GPIO6 Sensor 1 closure, journal entry, ordinary 90-second reminder while
  open, and alarm clearing on close.
- Repeated GPIO4 external LED toggling.
- Normal journal persistence during testing.

The USB-only result is not acceptance of the 12 V power or actuator domains.
At the end of that USB-only phase, the following were unverified (later
functional acceptance is recorded in [STATUS](../../STATUS.md)):

- GPIO3 and GPIO10 electrical default-OFF behavior during reset and power changes.
- Sensor 2 / GPIO7 monitoring.
- Onboard piezo operation under 12 V power.
- Loaded 12 V, 5 V, and 3.3 V power stability.
- Lock 1 actuation, pulse waveform, and flyback behavior.
- Lock 2 electrical safety.
- Full browser enrollment, signing, and authorization regression on this PCB.

### Earlier manufacturing bench observations

Confirmed physical bench results:

- Two assembled boards enumerate successfully over USB-A-to-USB-C.
- Linux USB ID `303a:1001` was observed.
- `/dev/ttyACM0` was observed on the bench workstation.
- The 12 V input, 5 V buck output, and 3.3 V regulator output operate.

Not yet verified by the combined acceptance record:

- USB-C-to-USB-C operation remains unverified/problematic.
- Oscilloscope-confirmed exact pulse timing and output isolation.
- Lock default-OFF behavior across brownout and all fault states.
- Coil-current/flyback decay and thermal endurance.

Do not infer actuator power availability from successful USB, Wi-Fi, authorization, or command handling. A successful authorization is not evidence that a physical lock moved.

## Bring-Up Boundary

Milestone 6 completed functional testing with all four sensors and two connected
loads. Remaining acceptance must use appropriate instrumentation to qualify
exact pulse timing, output isolation, brownout behavior, current decay, and
thermal endurance without expanding the firmware protocol scope.
