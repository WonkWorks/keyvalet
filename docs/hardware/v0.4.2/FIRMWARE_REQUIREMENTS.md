# KeyValet v0.4.2 Required Firmware Operating Contract

Markdown conversion of the preserved source document [`docs/FIRMWARE_REQUIREMENTS.txt`](../../FIRMWARE_REQUIREMENTS.txt).

> **No firmware changes are included in this reference.**

## Piezo

GPIO11 drives the existing MMBT2222A piezo stage. Generate firmware-controlled chirps near the CPT-9019S-SMT-TR rated 4 kHz frequency; a steady HIGH is not a tone for this externally driven piezo. Default GPIO11 LOW during boot/reset.

## Lock Outputs

GPIO10 controls Lock 1; GPIO3 controls Lock 2. Both are active HIGH. Retain the existing 350 ms pulse behavior. Both outputs default OFF.

The user requires never activating both outputs together. Firmware must implement a single shared lock arbiter, not independent timers that can overlap. Serialize or reject a competing request; avoid races between network, button, and retry paths.

- Set both output latches LOW before enabling output mode.
- Do not allow unlock requests until initialization is complete.
- Clear both outputs on cancellation/errors.
- Verify watchdog and reset behavior on hardware.

Bound each pulse to 350 ms under fault/error paths. Determine maximum repetition from coil/fuse/diode heating.

Account for flyback decay: both MOSFET gates LOW does not mean coil current instantly becomes zero. A measured dead interval may be needed before energizing the other channel if “never together” means no overlap in actual coil current, rather than no overlap in commanded outputs.

## Hardware Safety Boundary

This hardware has independent gate pull-downs but no mutual-exclusion logic or independent supply-fault gate inhibit. The 3 A fuse is not an interlock.

Verify both gates with a scope during:

- Power ramps.
- Unplugging.
- Brownouts.
- BOOT/RESET.
- Watchdog recovery.

Complete those checks before closing the default-OFF requirement.
