# Replay NVS stress harness

This isolated ESP-IDF application performs 10,000 replacements of the same 160 fixed replay-record keys in a real 64 KiB NVS partition. Every 137 writes it reconstructs the ring, deinitializes and reinitializes NVS to simulate a restart, then verifies the active 160-sequence suffix. At completion it reports NVS used, free, available, and total entries.

Activate the portable ESP-IDF 5.5.5 environment described in
[FACTORY_INSTALLATION](../../docs/FACTORY_INSTALLATION.md#build-and-package) first.
The ESP32-C6 target build verifies the test against that NVS implementation:

```sh
source "$IDF_PATH/export.sh"
idf.py -B build-c6 -D SDKCONFIG=/tmp/keyvalet-replay-stress-sdkconfig -D IDF_TARGET=esp32c6 build
```

For a no-hardware execution, build and run it with ESP-IDF's Linux flash-partition emulator:

```sh
idf.py --preview -B build-linux set-target linux build
./build-linux/keyvalet_replay_nvs_stress.elf
```

The Linux build requires host development prerequisites, including `libbsd-dev`
on Debian/Ubuntu. Install those for a new environment. The historical bench
instead used temporary header/link shims against its existing runtime library;
those local scaffolding files are not supplied or required by this repository.

This correction's run passed 10,000 writes, 9,840 replacements and 72 NVS
reinitializations. Rollover usage stayed at 644 used/1,246 available entries;
the emulator reported 308 sector erases. It stores the compact marker's 12-byte
footprint; marker semantics and actual settings/replay adapters are tested by
`tests/storage_integration_test.c`. The shim is host build scaffolding, not
firmware storage compatibility code. Do not substitute physical hardware for
this offline stress test.
