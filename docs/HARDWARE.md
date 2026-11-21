# Hardware

## Canonical KeyValet v0.4.2 PCB

The v0.4.2 PCB is the only supported firmware target.
It supports **up to two solenoids and up to four sensors**. All six canonical
identifiers remain available, but an installation need not populate or use all
channels. Disable monitoring for unused inputs; configuration completeness is
not a physical-population requirement. A signed-unlock channel defaults to
requiring its paired sensor known CLOSED. Signed administrative configuration
may set `sensor_required: false` for deliberate solenoid-only use; input alarm
monitoring remains independent. Unused other channels do not block it.

| Channel | GPIO | Electrical policy |
|---|---:|---|
| Physical button | 2 | Active LOW; external pull-up |
| Lock 2 | 3 | Active HIGH; guarded 350 ms pulse |
| Status LED | 4 | Active HIGH |
| Reserved | 5 | Unconfigured |
| Sensor 1 | 6 | CLOSED=LOW; external pull-up |
| Sensor 2 | 7 | CLOSED=LOW; external pull-up |
| Lock 1 | 10 | Active HIGH; guarded 350 ms pulse |
| Piezo | 11 | LEDC PWM; LOW while silent |
| Sensor 3 | 18 | CLOSED=LOW; internal pull-up |
| Sensor 4 | 19 | CLOSED=LOW; internal pull-up |
| Reserved | 20 | Unconfigured |

Native USB GPIO12/13 and strapping GPIO8/9/15 are not configured. Input debounce
is 25 ms. Lock 1 is paired with Sensor 1 and Lock 2 with Sensor 2; Sensors 3–4
are sensor-only.

Both solenoid latches are preloaded LOW before output mode and asserted LOW
again afterward. One arbiter and timer own both outputs. Requests are rejected
during the pulse and provisional 2000 ms recovery interval. Force-inactive,
cancellation, timer/start-error, and safe-reboot paths drive both outputs LOW.

## Acceptance boundary

Functional bench testing has covered USB and 12 V power, both solenoids, all
four sensors, button, LED, piezo, independent lock actuation, simultaneous sensor
events, arbiter state transitions, and busy rejection. Later Milestone 7 testing
accepted signed V2 actuation of both locks and purpose-neutral status/configuration.

The current diagnostic image also passed an operator-performed full erase/fresh
installation smoke test: boot, Wi-Fi/WebSocket, correct channel/profile welcome,
default sensor dependencies, READY arbiter, HEALTHY fresh journal, and automatic
retrieval of its one outstanding record. This is not a repeat of loaded actuator
or electrical qualification; see [STATUS.md](STATUS.md) for exact image identity
and the distinction between current smoke acceptance and earlier bench evidence.

The following remain unqualified: oscilloscope-verified output isolation, exact
physical pulse duration, coil-current decay, brownout behavior, and thermal
endurance. The 2000 ms recovery interval is provisional.

The unchanged partition layout is:

| Partition | Offset | Size |
|---|---:|---:|
| Default NVS | `0x9000` | 16 KiB |
| OTA data | `0xd000` | 8 KiB |
| PHY init | `0xf000` | 4 KiB |
| OTA 0 | `0x10000` | 2.5 MiB |
| OTA 1 | `0x290000` | 2.5 MiB |
| Journal | `0x510000` | 2.5 MiB |
| Replay NVS | `0x790000` | 64 KiB |

See the [manufacturing reference](hardware/v0.4.2/PCB_MANUFACTURING_REFERENCE.md)
and [firmware requirements](hardware/v0.4.2/FIRMWARE_REQUIREMENTS.md) for the
electrical design and mandatory safety constraints. The
[firmware handoff](hardware/v0.4.2/FIRMWARE_HANDOFF.md) summarizes the canonical
mapping and separates earlier bench evidence from remaining qualifications.
The [storage reset boundary](STORAGE.md) records the operator's completed reset;
it does not authorize further hardware operation.
