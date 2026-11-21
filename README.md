# KeyValet

KeyValet — An open-source project by WonkWorks.

KeyValet is native ESP-IDF firmware for the custom v0.4.2 ESP32-C6 key-cabinet
controller. The v0.4.2 PCB is the sole supported hardware platform for v1.0.
It operates with TowneCore; each association owns and independently deploys its
TowneCore installation and establishes its own device trust. Neither firmware
downloads nor device ownership require access to a developer's cloud project
or a publisher-operated provisioning service.

## Versions and current release candidate

- **Hardware:** PCB v0.4.2, silkscreened on the board; not a firmware version.
- **Firmware:** `version.txt` and the accepted binary report `0.5.0-m5`, the
  retained development software version. Do not infer v0.4.2 software from the PCB.
- **Distribution:** `keyvalet-m11c-p0-closeout-be41564249ab`, preserved at P0 commit
  `c5df4bcc5a49599e25da67f4aad990de40666c1e`, is the current factory candidate.
  M11C/P0 are engineering milestones, not new semantic firmware versions.

Official firmware distribution uses **public, versioned GitHub Releases** in the
intended `WonkWorks/keyvalet` repository. Neither that repository nor the release
has been published yet. [Publication blockers and source delivery](docs/PUBLICATION.md)
remain open; no live release URL or floating `latest` is configured.

TowneCore P1's Staff Settings/KeyValet browser USB foundation is implemented,
development-deployed and visually inspected. Initial provisioning targets Chrome
on desktop with Web Serial, HTTPS, a user gesture and exclusive USB ownership.
Physical browser-to-KeyValet flashing acceptance is **pending**, as is live
GitHub retrieval after publication. Factory flashing stays disabled until the
approved release URL is configured and validated. Installation, subsequent
Improv Wi-Fi setup, TLS commissioning and operational enrollment are separate
steps; P1 does not complete certificate operations or future OTA.

The firmware supports up to two signed, target-specific lock channels and up to four
independently monitored sensors, a shared solenoid arbiter, autonomous audible
alarms, durable replay protection, a circular journal, physical enrollment,
USB diagnostics, and a browser reference console. Public channel identifiers
are `lock_1`, `lock_2`, and `sensor_1` through `sensor_4`.
Installations may use fewer channels; set unused sensors' monitoring to false.
Complete four-entry configuration does not require four physical sensors.
Signed administrative configuration selects whether each lock requires its
paired sensor (default true), allowing deliberate solenoid-only operation.
An explicit zero unlock timeout suppresses that operation's reminder only.

M11C implements device-local HTTPS/WSS on port 443, per-device P-256 TLS keys,
and protected-USB certificate commissioning. Port 80 and cleartext WebSocket are
removed in both variants. A fresh unit has no listener until a valid identity,
explicit Origin policy and valid UTC are available. Operational signing remains
mandatory; TLS commissioning does not enroll the building authority.
See [commissioning and maintenance boundaries](docs/TOWNECORE_INTEGRATION_V1.md)
and the separately authorized [partition transition](docs/FACTORY_INSTALLATION.md#m10-to-m11c-data-preserving-partition-transition).
M11C is physically accepted on Safari/Chrome with four sustained WSS clients;
see [acceptance and the certificate-renewal obligation](docs/STATUS.md).
Automated certificate maintenance remains `NOT_IMPLEMENTED`.

## Development environment

- ESP32-C6 with 8 MB flash on the KeyValet v0.4.2 PCB
- ESP-IDF v5.5.5
- Native USB Serial/JTAG
- Firmware project `keyvalet`, development version `0.5.0-m5`

Wi-Fi is configured at runtime over native USB using Improv Serial, in both
production and diagnostic firmware. No SSID/password is compiled into an image.
Run `tools/kv_test.py monitor --port DEVICE` in one terminal and
`tools/kv_test.py control` in another; choose **9 — Wi-Fi Setup**. Successful
association/DHCP is committed to a separate NVS namespace. Failed candidates
restore the previous configuration. See [USB Wi-Fi provisioning](docs/WIFI_PROVISIONING.md).

Install ESP-IDF v5.5.5 and its ESP32-C6 tools first; see the portable
[prerequisites and source-build procedure](docs/FACTORY_INSTALLATION.md#build-and-package).
Production and diagnostic builds use separate configuration artifacts:

```sh
idf.py -B build -DSDKCONFIG=sdkconfig.production.local \
  -DSDKCONFIG_DEFAULTS=sdkconfig.defaults build

idf.py -B build-diagnostics -DSDKCONFIG=sdkconfig.diagnostics.local \
  -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.diagnostics' build
```

The diagnostic build alone includes privileged USB controls. Both variants use
protected USB public-key staging, the physical button window and ENROLL-V1 proof
for first enrollment; no compiled root or private signing key enters firmware.
The development signer keeps its operational key under ignored
`secrets/m5-development/`; production key custody belongs to the building backend.

## Documentation

- [Factory candidate, source builds and safe installation](docs/FACTORY_INSTALLATION.md)
- [Publication, corresponding source and history decisions](docs/PUBLICATION.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Hardware](docs/HARDWARE.md)
- [Protocol](docs/PROTOCOL.md)
- [Firmware](docs/FIRMWARE.md)
- [Decisions](docs/DECISIONS.md)
- [Status](docs/STATUS.md)
- [Canonical storage and development reset boundary](docs/STORAGE.md)
- [USB Wi-Fi provisioning and acceptance](docs/WIFI_PROVISIONING.md)
- [TowneCore integration contract, revision 2](docs/TOWNECORE_INTEGRATION_V1.md)
- [PCB manufacturing reference](docs/hardware/v0.4.2/PCB_MANUFACTURING_REFERENCE.md)
- [PCB firmware requirements](docs/hardware/v0.4.2/FIRMWARE_REQUIREMENTS.md)

Hardware documents describe PCB v0.4.2; referenced external KiCad/manufacturing
sources are not included here. No hardware-design license is assigned here.

## License

Copyright © 2026 Evan W. Cash (WonkWorks).

SPDX-License-Identifier: GPL-3.0-or-later

KeyValet-owned firmware, build scripts, host utilities, tests and documentation
are free software: you may redistribute and/or modify them under the GNU General
Public License as published by the Free Software Foundation, either version 3,
or (at your option) any later version. They are provided WITHOUT ANY WARRANTY,
including the implied warranties of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. See the complete, unmodified [GNU GPLv3](LICENSE).

Commercial use and redistribution are permitted under the applicable licenses.
Third-party components retain their own copyright and licensing terms; this grant
does not relicense them or license absent hardware design sources. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

[LICENSE-EXCEPTION.md](LICENSE-EXCEPTION.md) is the adopted GPLv3 Section 7
additional permission for six precisely identified, pinned Espressif ESP32-C6
radio libraries. It permits their combination with KeyValet-owned code and waives
only their unavailable implementation-source delivery under KeyValet's GPL
conditions. GPL source/build/install obligations for KeyValet and applicable
obligations for every other component remain. The permission does not make the
vendor libraries open source or change their Apache-2.0 terms; the combined image
is not wholly GPL-licensed or wholly source-available. Users remain free to modify,
compile, install and redistribute KeyValet under the applicable terms.

Adoption does not certify release compliance or authorize publication. The
[source-delivery/publication gates](docs/PUBLICATION.md) still apply, including
before pushing tracked binaries. Matching P0 source inputs, the preserved
configuration and verification metadata are indexed in the separate
[source-delivery record](releases/source/keyvalet-m11c-p0-closeout-be41564249ab/README.md).
GitHub and this working checkout will form one ongoing development history after
an authorized one-time transition; no parallel codebase or synchronization is planned.
