#!/usr/bin/env python3
"""Actual mbedTLS/key/CSR/chain and firmware NVS/USB fault tests. Offline only."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.check_output(args, stderr=subprocess.STDOUT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mbedtls-build", type=Path, default=Path("/tmp/keyvalet-m11c-mbedtls"))
    args = parser.parse_args()
    if not os.environ.get("IDF_PATH"):
        parser.error("Set IDF_PATH to the ESP-IDF checkout (source its export.sh).")
    IDF = Path(os.environ["IDF_PATH"])
    MBED = IDF / "components/mbedtls/mbedtls"
    for required in (IDF / "components/json/cJSON/cJSON.c",
                     IDF / "components/json/cJSON/cJSON.h", MBED / "include/mbedtls/x509.h"):
        if not required.is_file():
            parser.error(f"Missing SDK dependency: {required}; check IDF_PATH and SDK submodules.")
    for library in ("libmbedx509.a", "libmbedcrypto.a"):
        if not (args.mbedtls_build / "library" / library).is_file():
            parser.error(f"Missing host mbedTLS library: {library}; pass --mbedtls-build pointing to a host PIC build of the SDK's mbedTLS.")
    for executable in ("cc", "openssl"):
        if not shutil.which(executable):
            parser.error(f"Missing host dependency: {executable} must be on PATH.")
    with tempfile.TemporaryDirectory(prefix="keyvalet-tls-test-") as temporary:
        tmp = Path(temporary)
        shared = tmp / "firmware.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-D_GNU_SOURCE", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-Wno-sign-compare", "-I" + str(ROOT / "tests/host_stubs"), "-I" + str(ROOT / "main"),
            "-I" + str(MBED / "include"), "-I" + str(IDF / "components/json/cJSON"),
            str(ROOT / "tests/tls_identity_test.c"), str(ROOT / "main/tls_material.c"),
            str(IDF / "components/json/cJSON/cJSON.c"),
            str(args.mbedtls_build / "library/libmbedx509.a"), str(args.mbedtls_build / "library/libmbedcrypto.a"),
            "-lpthread", "-lm", "-o", str(shared)], check=True)
        fw = C.CDLL(str(shared))
        fw.test_usb.restype = C.c_char_p
        fw.test_usb.argtypes = [C.c_char_p, C.c_size_t]
        fw.keyvalet_tls_device_uid.restype = C.c_char_p
        fw.test_clock.argtypes = [C.c_int64, C.c_int]
        fw.test_micros.argtypes = [C.c_int64]
        fw.keyvalet_tls_install.argtypes = [C.c_char_p, C.c_size_t]
        fw.keyvalet_tls_allowed_origin.argtypes = [C.c_char_p]
        fw.keyvalet_tls_allowed_origin.restype = C.c_char_p
        fw.test_clock(int(time.time()), 1)
        fw.test_reset()
        assert fw.keyvalet_tls_device_uid() == b"keyvalet-000102030405"
        host = b"kv-test.devices.example.com"

        def usb(body):
            value = fw.test_usb(body, len(body))
            return json.loads(value) if value else None

        def public(candidate=True):
            csr, key = C.create_string_buffer(2048), C.create_string_buffer(131)
            assert fw.test_export(candidate, csr, key) == 0
            return csr.value, key.value

        ca = tmp / "ca.pem"
        run("openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes",
            "-keyout", str(tmp / "ca.key"), "-out", str(ca), "-days", "90", "-subj", "/CN=Host test CA",
            "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign,cRLSign")
        run("openssl", "req", "-new", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes",
            "-keyout", str(tmp / "issuer.key"), "-out", str(tmp / "issuer.csr"), "-subj", "/CN=Host test intermediate")
        (tmp / "issuer.extensions").write_text("basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\n")
        run("openssl", "x509", "-req", "-in", str(tmp / "issuer.csr"), "-CA", str(ca),
            "-CAkey", str(tmp / "ca.key"), "-set_serial", "77", "-days", "80",
            "-extfile", str(tmp / "issuer.extensions"), "-out", str(tmp / "issuer.pem"))

        def certificate(csr, san="DNS:kv-test.devices.example.com", eku="serverAuth", days=30):
            (tmp / "device.csr").write_bytes(csr)
            (tmp / "extensions").write_text("basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n"
                f"extendedKeyUsage={eku}\nsubjectAltName={san}\n")
            run("openssl", "x509", "-req", "-in", str(tmp / "device.csr"), "-CA", str(tmp / "issuer.pem"),
                "-CAkey", str(tmp / "issuer.key"), "-set_serial", "123", "-days", str(days),
                "-extfile", str(tmp / "extensions"), "-out", str(tmp / "device.pem"))
            fw.test_clock(int(time.time()) + 5, 1)
            # Like public-CA delivery: include the intermediate, not the root.
            return (tmp / "device.pem").read_bytes() + (tmp / "issuer.pem").read_bytes()

        def install(chain):
            return fw.keyvalet_tls_install(chain, len(chain))

        assert not fw.test_ready()
        fw.test_network(0)
        assert fw.test_prepare(host, 0) != 0
        fw.test_network(1)
        assert fw.test_prepare(host, 0) == 0
        csr, original_key = public()
        assert len(original_key) == 130 and original_key.startswith(b"04")
        (tmp / "device.csr").write_bytes(csr)
        assert b"Certificate request self-signature verify OK" in run("openssl", "req", "-verify", "-noout", "-in", str(tmp / "device.csr"))
        text = run("openssl", "req", "-text", "-noout", "-in", str(tmp / "device.csr"))
        assert b"DNS:" + host in text
        assert fw.test_reboot() == 0 and public()[1] == original_key
        good = certificate(csr)
        assert install(good + b"unparsed trailing data") != 0
        assert install(good + b"-----BEGIN PRIVATE KEY-----\nAAAA\n-----END PRIVATE KEY-----\n") != 0
        tampered = bytearray(good)
        location = tampered.index(b"-----END CERTIFICATE-----") - 5
        tampered[location] = ord("A") if tampered[location] != ord("A") else ord("B")
        assert install(bytes(tampered)) != 0
        for san in ("DNS:wrong.example.com", "DNS:*.devices.example.com", "DNS:kv-test.devices.example.com,DNS:other.example.com"):
            assert install(certificate(csr, san)) != 0
        assert install(certificate(csr, eku="clientAuth")) != 0
        assert install(b"not a certificate") != 0
        assert install(good + b"\0") != 0
        assert install(b"a" * 6145) != 0
        fw.test_clock(int(time.time()), 0)
        assert install(good) != 0
        fw.test_clock(int(time.time()) - 86400, 1)
        assert install(good) != 0
        fw.test_clock(int(time.time()) + 31 * 86400, 1)
        assert install(good) != 0
        fw.test_clock(int(time.time()), 1)
        assert install(good) == 0 and not fw.test_ready()
        fw.test_clock(int(time.time()), 0)
        assert fw.keyvalet_tls_activate() != 0
        fw.test_clock(int(time.time()) + 5, 1)
        assert fw.keyvalet_tls_activate() == 0 and fw.test_ready()
        first_generation = fw.test_generation()
        assert fw.test_reboot() == 0 and fw.test_ready() and public(False)[1] == original_key
        for origin in (None, b"null", b"https://evil.example.com", b"https://staff.example.com.evil", b"http://staff.example.com"):
            assert not fw.keyvalet_tls_allowed_origin(origin)
        assert fw.keyvalet_tls_allowed_origin(b"https://staff.example.com") == b"https://staff.example.com"
        assert fw.keyvalet_tls_allowed_origin(b"http://localhost:8765") == b"http://localhost:8765"

        # Candidate failure never replaces active credentials. Each error latches
        # writes off until reboot, including errors after an actually durable write.
        for failure in (1, 2, 3):
            fw.test_failure(failure)
            assert fw.test_prepare(host, 1) != 0 and not fw.test_writable()
            assert fw.test_ready() and public(False)[1] == original_key
            assert fw.test_prepare(host, 1) != 0
            assert fw.test_reboot() == 0 and fw.test_ready()
            assert public(False)[1] == original_key
            assert fw.keyvalet_tls_discard_candidate() == 0

        assert fw.test_prepare(host, 1) == 0
        next_csr, next_key = public()
        assert next_key != original_key and install(good) != 0
        assert install(certificate(next_csr)) == 0
        fw.test_failure(3)
        assert fw.keyvalet_tls_activate() != 0 and not fw.test_writable()
        assert public(False)[1] == original_key
        assert fw.test_reboot() == 0 and public(False)[1] == next_key
        assert fw.test_generation() > first_generation
        assert fw.keyvalet_tls_device_uid() == b"keyvalet-000102030405"
        assert usb(b"\x00")[1] == "NO_CANDIDATE"  # old slot is not a rollback candidate
        assert fw.keyvalet_tls_activate() != 0

        # Actual vendor transfer engine: integrity, bounds, ordering, expiration.
        data = json.dumps({"hostname": host.decode(), "origins": ["https://staff.example.com"], "rotate_key": False}).encode()
        begin = bytes((1, 1)) + struct.pack("<H", len(data)) + hashlib.sha256(data).digest()
        assert usb(begin) == ["OK"]
        assert usb(b"\x02\x01\x00a") is None
        assert usb(b"\x03") is None
        assert usb(begin) == ["OK"]
        assert usb(b"\x02\0\0" + data) == ["OK"]
        assert usb(b"\x03") == ["OK"]
        assert public()[1] == next_key
        assert usb(begin[:-1] + bytes((begin[-1] ^ 1,))) == ["OK"]
        assert usb(b"\x02\0\0" + data) == ["OK"]
        assert usb(b"\x03") is None
        assert usb(begin) == ["OK"]
        fw.test_micros(30000001)
        assert usb(b"\x03") is None
        assert usb(bytes((1, 2)) + struct.pack("<H", 6145) + bytes(32)) is None
        assert usb(b"\x04\x01\0\0") is not None
        assert usb(b"\x04\x01\xff\xff") is None
        assert usb(b"\x06") == ["OK"]
        assert usb(b"\x07") == ["OK"] and fw.test_ready()
        fw.test_clock(int(time.time()) + 31 * 86400, 1)
        assert not fw.test_ready()  # expired active tuple retained for USB recovery
        fw.test_clock(int(time.time()), 1)
        assert fw.test_ready()
        print("PASS: actual P-256/CSR/SAN/key/chain/time validation; durable candidate/selector faults; reboot/re-key; Origins; bounded USB integrity/order/expiry/recovery")


if __name__ == "__main__":
    main()
