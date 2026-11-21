# Espressif radio-library additional permission

Adopted by the copyright holder for KeyValet's repository licensing materials on
November 21, 2026. The primary license remains GPL-3.0-or-later; the complete GPLv3
text in `LICENSE` is unchanged. Adoption is not certification of a compiled
release's legal compliance or approval to publish it.

## Additional permission text

Copyright © 2026 Evan W. Cash (WonkWorks)

Additional permission under GNU GPL version 3 section 7

This additional permission applies only to KeyValet-owned material licensed under
GNU GPL version 3 or, at your option, any later version, for which Evan W. Cash
has authority to grant this permission (the "KeyValet Material"). It does not
apply to material owned by other copyright holders.

The "Specified Libraries" are only the ESP32-C6 object-code versions of:

- `libnet80211.a`, `libpp.a`, `libcore.a`, `libespnow.a` and `libmesh.a`, as
  distributed in `espressif/esp32-wifi-lib` at commit
  `22bb3517b08760ed43b889013f8a122db0d46da0`;
- `libphy.a`, as distributed in `espressif/esp-phy-lib` at commit
  `59c1234e929212aec0fdda75769b759951235536`.

These identities do not depend on the libraries' local filesystem locations.
This permission covers those unmodified vendor archives as linking inputs,
including ordinary linker relocation and section selection, but no other vendor
archives, revisions or hardware targets.

Evan W. Cash grants you additional permission to copy, link or otherwise combine
the KeyValet Material, including modified versions of that material, with the
Specified Libraries and to convey the resulting combined firmware in object-code
form, including commercially, subject to the conditions below.

When relying on this permission, you need not provide the unavailable
implementation source of the Specified Libraries as Corresponding Source solely
because of the GPL conditions applicable to the KeyValet Material. This is the
only source-delivery exception granted here. You must comply with all other
applicable GPL requirements for the KeyValet Material, including providing its
complete Corresponding Source, modifications, required build/install materials
and any applicable Installation Information. Applicable GPL requirements for all
other incorporated components remain unchanged.

You must comply with Espressif's applicable Apache License 2.0 terms for the
Specified Libraries, including applicable license, copyright and notice
requirements. The KeyValet Material remains GPL-3.0-or-later; the Specified
Libraries retain their own licenses. This permission neither relicenses any
third-party material nor grants rights on behalf of Espressif or any other
third-party copyright holder. Retain applicable GPL and additional-permission
notices when conveying a combination in reliance on this permission.

This permission does not limit your GPL rights to use, modify, compile, install
or redistribute the KeyValet Material, or your option to use a later GPL version.
You may remove this additional permission from a copy as GPLv3 section 7 permits;
that copy must then comply without relying on this permission. Authors of new
contributions are not required to extend this permission to their contributions.
