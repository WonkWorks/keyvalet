# KeyValet v0.4.2 JLCPCB Complete-Assembly Quotation Package

Markdown conversion of the preserved source document [`docs/README.txt`](../../README.txt).

**External-package reference:** this describes a separate hardware handoff.
The KiCad project, libraries/models, manufacturing outputs and validation files
named below are not included in this firmware repository. Their description is
not a claim of public availability or a grant of a hardware-design license.
PCB v0.4.2 is the physical board revision, not the firmware software version.

**Date:** 2026-11-01

**EDA:** KiCad 10.0.8

Open `KeyValet.kicad_pro`. The PCB and all six schematic sheets are editable. Project-local footprint/symbol libraries and 3D models are included. The v0.3 source was preserved separately. No order was placed.

## Board

- 76.2 × 76.2 mm rectangular FR4, nominal 1.6 mm, four copper layers.
- Four 3.2 mm NPTH M3 holes at `(4.5, 4.5)`, `(71.7, 4.5)`, `(4.5, 71.7)`, and `(71.7, 71.7)` mm.
- Coordinates above use the top-left corner, positive Y down.
- The ESP32 antenna extends about 6.25 mm beyond the top edge; the enclosure must leave this region clear of metal, wiring, and mounting hardware.

## Quote Settings

- Four layers; nominal 1.6 mm.
- 1 oz outer copper, 0.5 oz inner copper.
- Green mask, white silk, ENIG.
- No blind/buried vias.
- Requested build: JLC04161H-7628 (published 0.2104 mm prepreg / 1.065 mm core / 0.2104 mm prepreg).

Layer use:

| Layer | Use |
|---|---|
| `F.Cu` | Components, power, and signals |
| `In1.Cu` | Primary GND reference |
| `In2.Cu` | GND fill with a few signal/power routes |
| `B.Cu` | Signals/USB and GND |

High-current 12 V and solenoid paths use 1.5 mm outer traces; individual local IC/pad escapes may be narrower. Ground fills and dedicated source returns are stitched through 120 vias total, including signal vias.

The main USB pair is 0.23 mm wide with approximately 0.18 mm edge spacing on parallel straight runs. Copper path totals, including series-resistor legs, are:

| Signal | Copper path total |
|---|---:|
| D− | 80.179 mm |
| D+ | 82.055 mm |

This is 12 Mbps USB Serial/JTAG; no measured or field-solved 90 ohm claim is made. Bench-check the full panel extension.

## Validation

`validation/DRC.rpt` and `DRC.json` report:

- 0 violations.
- 0 unconnected items.
- 0 schematic parity differences.
- Every available rule category enabled.
- No ignored checks or exclusions.

`ERC.json` and `ERC.rpt` report 0 violations.

An independent audit compares the netlist, actual symbol wires/global labels, and all physical pad assignments. See `CONNECTIVITY_AND_LAYOUT_AUDIT.txt`.

These checks establish connectivity and configured geometry constraints, not measured thermal performance, regulator stability, or EMC compliance.

## Changes from v0.3 (v0.4 Baseline Preserved)

- Completed the 72-component layout and routing; four mounting holes were added.
- **J1/J7/J8:** exact Phoenix 1715721, 5.08 mm THT, 17.5 A nominal.
- **J3–J6:** exact JST B2B-XH-A(LF)(SN), 2.5 mm vertical; mating XHP-2 housings and matching crimp terminals are cable accessories, not PCB parts.
- **D1:** B540C-13-F, C72264, 5 A 40 V SMC replaces the common-feed SMA selection.
- **BZ1:** Same Sky/CUI CPT-9019S-SMT-TR, C95163, 9 × 9 × 1.9 mm externally driven SMT piezo. The manufacturer land pattern is 2.0 × 4.2 mm pads on 8.0 mm centers. Marked pad 1 at left is `+5V_BUCK`; pad 2 at right is the switched low node. BZ1 is included in the factory SMT BOM and placement file.
- **R22:** 2.2 kΩ, C4190. With 15.6 nF maximum piezo capacitance, RC is 34.3 µs and recharge is 97.4% in a 125 µs half-cycle at 4 kHz. The existing 5 V single-ended drive is below the 25 Vp-p absolute maximum. GPIO11, R20 = 220 Ω, and the MMBT2222A driver architecture are retained.
- **D6/D7/D8:** LRC S-LESD8L3.3CT5G, C5274340 replaces out-of-stock C552530. It remains in the same SOD-882 pads and is bidirectional, so the existing pad orientation is electrically non-polar. JLCPCB stock was verified 2026-11-01.
- **L1:** SHOU HAN CYA1265-33UH, C19268671 replaces out-of-stock C7369670. 33 µH ±20%, 6 A thermal current, 12 A saturation current, 54 mΩ typical / 65 mΩ maximum DCR, 13.8 × 12.8 × 6.5 mm maximum. Existing copper pads match its terminal envelope; fab/courtyard and 3D envelope were updated.
- **F1:** Littelfuse 01550900M, C108518 SMT OMNI-BLOK holder replaces the short-stock Keystone 3568 holder. It is rated 10 A at 125 V, UL94V-0, and 260 °C reflow capable. JLCPCB lists it for normal SMT assembly under Economic and Standard PCBA. The 9.8 × 5.0 mm body and 3.8 × 3.0 mm lands on 6.8 mm centers were implemented. It accepts replaceable Nano2 454-series fuses. The removable 3 A fuse is intentionally not in the BOM and will be inserted by the customer.
- **GCT USB4105 land pattern:** pin centers, slots, and locator positions were checked against the manufacturer drawing. Four duplicate GND lands were shortened from 1.15 to 1.0 mm to increase peg-to-copper clearance without moving pins. ESP32 antenna-only silkscreen graphics were moved to the drawing layer off-board.
- A local inductor courtyard was added using its actual body/pad envelope; surrounding parts were moved to resolve assembly spacing.

No confirmed GPIO assignment or circuit net membership changed.

## Connectors and GPIO

| Connector/function | Assignment |
|---|---|
| J1 | Pin 1: +12 V input; pin 2: GND. F1 protects downstream 12 V, not the input cable. |
| J7 Lock 1 | Pin 1: `+12V_LOCK`; pin 2: switched negative. Do not ground pin 2 externally. |
| J8 Lock 2 | Pin 1: `+12V_LOCK`; pin 2: switched negative. Do not ground pin 2 externally. |
| J3 Button | Pin 1: signal; pin 2: GND. |
| J4 Reed 1 | Pin 1: signal; pin 2: GND. |
| J5 Reed 2 | Pin 1: signal; pin 2: GND. |
| J6 LED | Pin 1: resistor-limited GPIO4 output; pin 2: GND. This is not a 12 V LED feed. |
| J2 Expansion | 1: GPIO18; 2: GND; 3: GPIO19; 4: GND; 5: GPIO20; 6: GND. There is no 3.3 V power pin. |
| J9 | Panel USB extension with power/data. |

| Function | GPIO |
|---|---:|
| Button | 2 |
| LED | 4 |
| Reed 1 | 6 |
| Reed 2 | 7 |
| Lock 1 | 10 |
| Lock 2 | 3 |
| Piezo | 11 |
| Reserved/unconnected | 5 |
| USB D− | 12 |
| USB D+ | 13 |

SW1 is reset; SW2 is boot. USB/12 V logic-supply OR-ing remains as v0.3; USB cannot power the lock coils.

## Assembly and Files

`manufacturing/KeyValet_Gerbers_v0.4.2_FINAL.zip` contains copper, masks, both silkscreens, the closed outline, and PTH and NPTH Excellon files; the four 3.2 mm mounting holes are included.

Upload `manufacturing/JLCPCB_FULL_BOM.csv` and `JLCPCB_FULL_CPL.csv`. The full files contain all 72 placed board components: 60 ordinary SMT placements and 12 THT/mixed placements. The BOM has 37 board-component groups. The 30-group SMT-only files are retained for traceability but are **not** the files for a complete build.

`BOM_v0.4.2_JLC_READY.csv` is the detailed engineering BOM. The four JSTs, three screw terminals, 2 × 3 header, three radial electrolytics, USB-C connector, Littelfuse fuse holder, and every SMT part including BZ1/C95163 are explicit.

JLCPCB supports mixed SMT/THT assembly under Economic and Standard PCBA. F1 uses normal SMT reflow and requires no special manufacturing process. The removable 3 A Nano2 fuse is excluded from BOM/CPL and F1 ships empty. See `manufacturing/JLCPCB_PCBA_REMARK.txt` for paste-ready wording.

`KiCad_positions.csv` includes all 72 on-board component origins, including BZ1. CPL is in mm, top side, negative Y. It is corrected from KiCad footprint anchors to the placement origins and zero-degree orientations used by the matched JLCPCB catalog packages. All 72 rows were solved from numbered-pad correspondence and checked against 211 `F.Cu` Gerber pad flashes. See `validation/JLCPCB_PLACEMENT_VERIFICATION_v0.4.2.json`.

The original invalid anchor-only export is retained under `manufacturing` for audit and must not be uploaded. Confirm the corrected file visually in JLCPCB's assembly preview.

All BOM rows have JLCPCB/LCSC identifiers; live stock/cost is quotation-dependent. No component substitutions are authorized by these files.

3D views use vendor/KiCad models where available. F1, L1, and BZ1 are dimensional envelopes based on manufacturer body dimensions, not vendor CAD. The removable fuse is omitted. Models show placement; they do not qualify the mechanical tolerances.

## Prototype Operating Limits

The locks are 12 V, 2 A. Only one may be commanded at a time, with a 350 ms pulse baseline. A firmware interlock is **required**; no hardware mutual exclusion was added. See [Firmware Requirements](FIRMWARE_REQUIREMENTS.md).

Do not infer 3 A continuous buck capability or 600 mA continuous LDO capability from chip headline ratings.

Initial bench checks:

- Current-limited power-up.
- Regulator stability/temperature with Wi-Fi.
- USB enumeration/isolation in both plug orientations.
- Lock pulse/default-OFF behavior.
- Piezo tone.
- Panel LED current.

These are normal prototype bring-up checks, not prerequisites for obtaining a bare-board quotation.

## Genuine Open Items

See `UNRESOLVED_BEFORE_ORDER.txt` (one short list).
