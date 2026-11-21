# Third-party notices and dependency inventory

KeyValet-owned code is GPL-3.0-or-later; see [README](README.md#license) and
[LICENSE](LICENSE). This does not replace any upstream license or copyright.
Commercial redistribution is subject to each applicable license. This inventory
is preparation, not approval to distribute the accepted binary; see the
[binary-library and corresponding-source blocker](docs/PUBLICATION.md).

## Baseline and notices

The preserved P0 build uses ESP-IDF **v5.5.5**, commit
`b774170ff46c393eeb5e495ea37936038d3f4f4f`, and its ESP32-C6 dependency tree.
The installed SDK was unmodified; the project-local HTTPS override is separate.
Version labels below are the inspected sources, not a promise that all upstream
files or optional components are linked. The preserved build's component list
and link map determine the actual binary scope; do not treat every tool shipped
with an SDK/toolchain as firmware content.

| Component / location | License and attribution | Distribution treatment |
| --- | --- | --- |
| ESP-IDF core and local `components/esp_https_server` | Apache-2.0; Espressif Systems. Local source/header retain their exact SPDX/copyright headers. | Full [Apache license](components/esp_https_server/LICENSE); retain applicable upstream notices. The [component README](components/esp_https_server/README.md) documents the one TLS allocation-failure cleanup patch. |
| mbedTLS 3.6.6, `components/mbedtls/mbedtls` in SDK | Apache-2.0 OR GPL-2.0-or-later; The Mbed TLS Contributors and per-file holders | Apache-2.0 can be selected for this integration; retain upstream notices. [Pinned license](https://github.com/espressif/mbedtls/blob/9d669eadb1955d348986b9280156710aaaadf79f/LICENSE). Do not label the dependency GPL-2.0-only. |
| FreeRTOS V10.5.1, SDK `components/freertos/FreeRTOS-Kernel` | MIT; Copyright (C) 2021 Amazon.com, Inc. or its affiliates; source identifies Espressif 2023–2025 contributions | [Unmodified MIT text](licenses/third-party/FreeRTOS-MIT.txt); retain applicable per-file copyright notices. |
| lwIP 2.2.0, SDK `components/lwip/lwip` | BSD-3-Clause; Swedish Institute of Computer Science and per-file contributors | [Full notice](licenses/third-party/lwIP-BSD.txt); retain additional linked-file notices. |
| cJSON 1.7.19, SDK `components/json/cJSON` | MIT; Dave Gamble and cJSON contributors | [Full notice](licenses/third-party/cJSON-MIT.txt). |
| wpa_supplicant/hostapd, SDK `components/wpa_supplicant` | BSD-3-Clause; Jouni Malinen and contributors; SDK modifications retain their headers | [Full upstream notice](licenses/third-party/wpa-supplicant-BSD.txt); retain applicable per-file SDK notices. |
| HTTP parser, SDK `components/http_parser` | MIT-style; Igor Sysoev, Joyent and Node contributors as identified upstream | [Full notice](licenses/third-party/http-parser-MIT.txt). |
| Espressif precompiled Wi-Fi/PHY radio libraries: the six identified below | Apache-2.0 Object form; Espressif Systems | Retain the Apache license and upstream notices. The adopted [Section 7 permission](LICENSE-EXCEPTION.md) waives only their unavailable implementation source under the holder's KeyValet GPL conditions; Apache terms remain intact. |
| newlib `libc.a` and associated runtime support | Multiple permissive/file-specific terms, not one blanket MIT license | [Unmodified toolchain newlib notice collection](licenses/third-party/newlib.txt); resolve the actual linked-file notice scope before distribution. |
| GCC runtime, GCC 14.2.0, including incorporated `libstdc++.a` support | GPLv3 with GCC Runtime Library Exception 3.1, subject to that exception's conditions | [Runtime exception](licenses/third-party/GCC-RUNTIME-EXCEPTION.txt) and [GPLv3 text](LICENSE). `libgcc.a` is a link input; the runtime exception is not a blanket waiver of KeyValet's GPL conditions. |

The seven texts under `licenses/third-party/` are byte-for-byte copies of the
inspected SDK/toolchain notices. The existing component Apache license is also
unchanged. Their original holders and terms are preserved, not replaced by the
project attribution. Upstream copyright year differences between a COPYING file
and source headers must be retained rather than normalized.

## Six incorporated binary-only archives and adopted permission

The accepted closeout map's retained, nonzero memory sections identify:

- From `espressif/esp32-wifi-lib` at
  `22bb3517b08760ed43b889013f8a122db0d46da0`, target ESP32-C6:
  `libnet80211.a`, `libpp.a`, `libcore.a`, `libespnow.a`, `libmesh.a`.
- From `espressif/esp-phy-lib` at
  `59c1234e929212aec0fdda75769b759951235536`, target ESP32-C6: `libphy.a`.

Their implementation source is unavailable in the pinned upstream repositories.
The [Wi-Fi](https://github.com/espressif/esp32-wifi-lib/blob/22bb3517b08760ed43b889013f8a122db0d46da0/README.rst)
and [PHY](https://github.com/espressif/esp-phy-lib/blob/59c1234e929212aec0fdda75769b759951235536/README.rst)
terms explicitly distribute Apache-2.0 Object form. Apache source compatibility
does not itself excuse unavailable Corresponding Source in a GPL combination.

[LICENSE-EXCEPTION.md](LICENSE-EXCEPTION.md) is the holder's adopted additional
permission only over KeyValet-owned material, for these exact six vendor inputs.
It waives only the specified libraries' unavailable implementation source under
KeyValet's GPL conditions. KeyValet source, local patches, configuration,
build/install materials and other applicable source obligations remain required.
No vendor copyright/license terms or third-party rights are changed. Adoption is
not legal certification or approval to publish the compiled release.

`libcoexist.a` is installed but not incorporated into this application; the
incorporated coexistence adapter is available SDK source. `libsmartconfig.a`,
`libwapi.a` and `libbtbb.a` appear as linker inputs without retained members.
RF test archives are not incorporated. None is covered by the adopted permission.
Archive names do not imply that their complete named features are enabled.

## Pinned external inputs

All paths here are relative to the ESP-IDF checkout, not files vendored in KeyValet:

- `components/esp_wifi/lib`: `22bb3517b08760ed43b889013f8a122db0d46da0`
- `components/esp_phy/lib`: `59c1234e929212aec0fdda75769b759951235536`
- `components/esp_coex/lib`: `79e618f033e926d134decc6d95bb1b1850bd032c`
  (installed SDK input, not incorporated vendor archive or additional-permission scope)
- `components/mbedtls/mbedtls`: `9d669eadb1955d348986b9280156710aaaadf79f`
- `components/lwip/lwip`: `fd432e4ee2cfb7f7f1c7eb7227e0173412e7b84e`
- `components/json/cJSON`: `b2890c8d76bbb64e710585ebc0a917196b9c67e7`

Use the [ESP-IDF v5.5.5 source tree](https://github.com/espressif/esp-idf/tree/v5.5.5)
and its recursive submodules, including file-specific headers/licenses. Source
delivery must cover required nonexcluded dependencies, not merely this list.

## External development tools, not permanent firmware contents

- ESP-IDF's installed toolchain package is `esp-14.2.0_20260121`, using GCC 14.2.0.
  Compiler/build-tool GPL terms are distinct from linked runtime terms above.
- esptool **4.12.0** is GPL-2.0-or-later according to installed package metadata.
  It packages/inspects/installs images; its Python code is not embedded in the
  approved application. Its transient RAM flasher stubs have independent MIT/
  Apache notices; they are not permanent P0 flash-image components. Redistributing
  the tool or stubs requires their own licenses/notices, not relicensing as KeyValet.
- Python, CMake, Ninja, host C compiler, OpenSSL and Node are external tools for
  builds/tests/reference utilities. They are not bundled by this repository.
- The manifests use the ESP Web Tools format; neither ESP Web Tools nor TowneCore's
  browser esptool-js vendor assets are bundled in KeyValet. They are separate
  projects with independently managed licensing.

Hardware documentation was converted from preserved engineering reference texts.
Referenced KiCad, BOM, Gerber and vendor CAD files are not included, and GH2
assigns no hardware-design license or permissions over absent vendor assets.

Before binary publication, review the complete release notices/source-delivery
arrangement and supply the materials described in [PUBLICATION](docs/PUBLICATION.md)
and the [P0 source record](releases/source/keyvalet-m11c-p0-closeout-be41564249ab/README.md).
The full SDK's per-file notices and toolchain runtime terms remain applicable;
the additional permission does not waive them. No upstream notice
or GPL exception authorizes disclosure of operational credentials.
