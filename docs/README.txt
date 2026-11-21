KEYVALET v0.4.2 — JLCPCB COMPLETE-ASSEMBLY QUOTATION PACKAGE
2026-11-01 • KiCad 10.0.8

OPEN: KeyValet.kicad_pro. PCB and all six schematic sheets are editable.
Project-local footprint/symbol libraries and 3D models are included.
The v0.3 source was preserved separately. No order was placed.

BOARD
76.2 x76.2mm rectangular FR4, nominal1.6mm, four copper layers.
Four3.2mm NPTH M3 holes at (4.5,4.5),(71.7,4.5),(4.5,71.7),(71.7,71.7)mm.
Coordinates above use the top-left corner, positive Y down.
The ESP32 antenna extends about6.25mm beyond the top edge; enclosure must
leave this region clear of metal, wiring and mounting hardware.

QUOTE SETTINGS
4layers; nominal1.6mm; 1oz outer copper,0.5oz inner; green mask, white silk;
ENIG; no blind/buried vias. Requested build JLC04161H-7628 (published
0.2104mm prepreg /1.065mm core /0.2104mm prepreg).
F.Cu: components/power/signals; In1.Cu: primary GND reference;
In2.Cu: GND fill with a few signal/power routes; B.Cu: signals/USB and GND.
High-current12V and solenoid paths use1.5mm outer traces; individual local
IC/pad escapes may be narrower. Ground fills and dedicated source returns
are stitched through120 vias total (including signal vias).
Main USB pair is0.23mm wide with approximately0.18mm edge spacing on
parallel straight runs. Copper path totals, including series-resistor legs:
D-80.179mm /D+82.055mm. This is12Mbps USB serial/JTAG; no measured or
field-solved90ohm claim is made. Bench-check the full panel extension.

VALIDATION
validation/DRC.rpt and DRC.json:0 violations,0 unconnected items,
0 schematic parity differences. Every available rule category enabled;
no ignored checks or exclusions. ERC.json and ERC.rpt:0 violations.
Independent audit compares netlist, actual symbol wires/global labels and
all physical pad assignments. See CONNECTIVITY_AND_LAYOUT_AUDIT.txt.
These checks establish connectivity and configured geometry constraints,
not measured thermal performance, regulator stability, or EMC compliance.

CHANGES FROM v0.3 (v0.4 BASELINE PRESERVED)
- Completed72-component layout and routing; four mounting holes added.
- J1/J7/J8: exact Phoenix1715721,5.08mm THT,17.5A nominal.
- J3–J6: exact JST B2B-XH-A(LF)(SN),2.5mm vertical; mating XHP-2 housings
  and matching crimp terminals are cable accessories, not PCB parts.
- D1: B540C-13-F,C72264,5A40V SMC replaces common-feed SMA selection.
- BZ1: Same Sky/CUI CPT-9019S-SMT-TR,C95163,9x9x1.9mm externally driven
  SMT piezo. Manufacturer land pattern is2.0x4.2mm pads on8.0mm centers.
  Marked pad1 at left is +5V_BUCK; pad2 at right is the switched low node.
  BZ1 is included in the factory SMT BOM and placement file.
- R22:2.2k,C4190. With15.6nF maximum piezo capacitance, RC is34.3us and
  recharge is97.4% in a125us half-cycle at4kHz. The existing5V single-ended
  drive is below the25Vp-p absolute maximum. GPIO11, R20=220R and the
  MMBT2222A driver architecture are retained.
- D6/D7/D8: LRC S-LESD8L3.3CT5G,C5274340 replaces out-of-stock C552530.
  It remains in the same SOD-882 pads and is bidirectional, so the existing
  pad orientation is electrically non-polar. JLCPCB stock verified2026-11-01.
- L1: SHOU HAN CYA1265-33UH,C19268671 replaces out-of-stock C7369670.
  33uH +/-20%,6A thermal current,12A saturation current,54mOhm typical /
  65mOhm maximum DCR,13.8x12.8x6.5mm maximum. Existing copper pads match
  its terminal envelope; fab/courtyard and 3D envelope were updated.
- F1: Littelfuse01550900M,C108518 SMT OMNI-BLOK holder replaces the short-stock
  Keystone3568 holder. It is rated10A at125V,UL94V-0 and260C reflow capable.
  JLCPCB lists it for normal SMT assembly under Economic and Standard PCBA.
  The9.8x5.0mm body and3.8x3.0mm lands on6.8mm centers were implemented.
  It accepts replaceable Nano2 454-series fuses. The removable3A fuse is
  intentionally not in the BOM and will be inserted by the customer.
- GCT USB4105 land pattern: pin centers, slots and locator positions checked
  against manufacturer drawing. Four duplicate GND lands shortened from
  1.15 to1.0mm to increase peg-to-copper clearance without moving pins.
  ESP32 antenna-only silkscreen graphics moved to drawing layer off-board.
- Local inductor courtyard added using its actual body/pad envelope;
  surrounding parts moved to resolve assembly spacing.
No confirmed GPIO assignment or circuit net membership changed.

CONNECTORS / GPIO
J1: pin1+12V input, pin2GND. F1 protects downstream12V, not input cable.
J7 lock1 and J8 lock2: pin1+12V_LOCK, pin2switched negative; do not ground
pin2 externally. J3 button, J4 reed1, J5 reed2: pin1signal, pin2GND.
J6 LED: pin1resistor-limited GPIO4 output, pin2GND; not a12V LED feed.
J2 expansion:1GPIO18,2GND,3GPIO19,4GND,5GPIO20,6GND; no3V3 power pin.
GPIO2 button; GPIO4 LED; GPIO6/7 reeds; GPIO10 lock1; GPIO3 lock2;
GPIO11 piezo. GPIO5 remains reserved/unconnected. USB GPIO12D-/13D+.
SW1 reset; SW2 boot. Panel USB extension connects to J9 with power/data.
USB/12V logic supply OR-ing remains as v0.3; USB cannot power lock coils.

ASSEMBLY / FILES
manufacturing/KeyValet_Gerbers_v0.4.2_FINAL.zip: copper, masks, both silkscreens,
closed outline, PTH and NPTH Excellon; four3.2mm mounting holes included.
UPLOAD manufacturing/JLCPCB_FULL_BOM.csv and JLCPCB_FULL_CPL.csv. The full
files contain all72 placed board components:60 ordinary SMT placements and
12 THT/mixed placements. The BOM has37 board-component groups. The 30-group
SMT-only files
are retained for traceability but are NOT the files for a complete build.
BOM_v0.4.2_JLC_READY.csv is the detailed engineering BOM. The four JSTs,
three screw terminals,2x3 header, three radial electrolytics, USB-C connector,
Littelfuse fuse holder and every SMT part including BZ1/C95163 are explicit.
JLCPCB supports mixed SMT/THT assembly under Economic and Standard PCBA.
F1 uses normal SMT reflow and requires no special manufacturing process.
The removable3A Nano2 fuse is excluded from BOM/CPL and F1 ships empty.
See manufacturing/JLCPCB_PCBA_REMARK.txt for paste-ready wording.
KiCad_positions.csv includes all72 on-board component origins, including BZ1.
CPL is in mm, top side, negative Y. It is corrected from KiCad footprint
anchors to the placement origins and zero-degree orientations used by the
matched JLCPCB catalog packages. All72 rows were solved from numbered-pad
correspondence and checked against211 F.Cu Gerber pad flashes. See
validation/JLCPCB_PLACEMENT_VERIFICATION_v0.4.2.json. The original invalid
anchor-only export is retained under manufacturing for audit and must not be
uploaded. Confirm the corrected file visually in JLCPCB's assembly preview.
All BOM rows have JLCPCB/LCSC identifiers; live stock/cost is quotation-dependent.
No component substitutions are authorized by these files.

3D views use vendor/KiCad models where available. F1,L1 and BZ1 are dimensional
envelopes based on manufacturer body dimensions, not vendor CAD. The removable fuse is omitted.
Models show placement; they do not qualify the mechanical tolerances.

PROTOTYPE OPERATING LIMITS
12V2A locks, only one commanded at a time,350ms pulse baseline. Firmware
interlock is REQUIRED; no hardware mutual exclusion was added. See
FIRMWARE_REQUIREMENTS.txt. Do not infer3A continuous buck capability or
600mA continuous LDO capability from chip headline ratings. Initial bench
checks: current-limited power-up; regulator stability/temperature with Wi-Fi;
USB enumeration/isolation both plug orientations; lock pulse/default-OFF;
piezo tone; panel LED current. These are normal prototype bring-up checks,
not prerequisites for obtaining a bare-board quotation.

GENUINE OPEN ITEMS
See UNRESOLVED_BEFORE_ORDER.txt (one short list).
