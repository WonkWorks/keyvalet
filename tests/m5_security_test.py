#!/usr/bin/env python3
"""Offline Milestone 5 canonical-signature and safety-order checks."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import m5_signing_helper as helper


def raw_to_der(raw: bytes) -> bytes:
    def integer(value: bytes) -> bytes:
        value = value.lstrip(b"\0") or b"\0"
        if value[0] & 0x80:
            value = b"\0" + value
        return b"\x02" + bytes((len(value),)) + value
    body = integer(raw[:32]) + integer(raw[32:])
    return b"\x30" + bytes((len(body),)) + body


def verify(key: Path, message: str, signature_hex: str) -> bool:
    with tempfile.TemporaryDirectory() as directory:
        directory = Path(directory)
        public = directory / "public.pem"
        message_file = directory / "message"
        signature_file = directory / "signature.der"
        public.write_bytes(helper.run_openssl("pkey", "-in", str(key), "-pubout"))
        message_file.write_text(message)
        signature_file.write_bytes(raw_to_der(bytes.fromhex(signature_hex)))
        result = subprocess.run(
            ["openssl", "dgst", "-sha256", "-verify", str(public),
             "-signature", str(signature_file), str(message_file)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        return result.returncode == 0


def main() -> None:
    helper.initialize_authority()
    device = "keyvalet-a1b2c3"
    challenge = "11" * 32
    enrollment = helper.signed_response(
        "/api/sign/enrollment", {"device_id": device, "challenge": challenge, "expires_in_s": 300}
    )
    enroll_message = (
        f"KEYVALET-ENROLL-V1\n{device}\n{challenge}\n"
        f"{enrollment['operational_public_key']}\nENROLL_FIRST\n"
        f"{enrollment['expires_at']}\n"
    )
    assert verify(helper.OPERATIONAL_KEY, enroll_message, enrollment["signature"])
    assert not verify(helper.OPERATIONAL_KEY, enroll_message.replace(device, "keyvalet-wrong"),
                      enrollment["signature"])

    config = {"expected_revision": 7, "default_door_open_timeout_s": 90,
              "locks": {"lock_1": {"sensor_required": True},
                        "lock_2": {"sensor_required": False}},
              "sensors": {
                  "sensor_1": {"monitoring_enabled": True},
                  "sensor_2": {"monitoring_enabled": False},
                  "sensor_3": {"monitoring_enabled": True},
                  "sensor_4": {"monitoring_enabled": True},
              }}
    signed_config = helper.signed_response(
        "/api/sign/config", {"device_id": device, "config": config}
    )
    config_message = (
        f"KEYVALET-CONFIG-V3\n{device}\nUPDATE_CONFIG\n"
        f"{signed_config['operation_id']}\n{signed_config['nonce']}\n"
        f"{signed_config['issued_at']}\n{signed_config['expires_at']}\n"
        "7\n90\n2\nlock_1.sensor_required=1\nlock_2.sensor_required=0\n4\nsensor_1=1\nsensor_2=0\nsensor_3=1\nsensor_4=1\n"
    )
    assert verify(helper.OPERATIONAL_KEY, config_message, signed_config["signature"])
    assert signed_config["protocol_version"] == 3
    for lock_id, old, new in (("lock_1", 1, 0), ("lock_2", 0, 1)):
        assert not verify(helper.OPERATIONAL_KEY,
            config_message.replace(f"{lock_id}.sensor_required={old}",
                                   f"{lock_id}.sensor_required={new}"),
            signed_config["signature"])
    assert not verify(helper.OPERATIONAL_KEY, config_message.replace("\n90\n", "\n91\n"),
                      signed_config["signature"])
    incomplete = {**config, "sensors": {"sensor_1": {"monitoring_enabled": True}}}
    try:
        helper.signed_response("/api/sign/config", {"device_id": device, "config": incomplete})
    except ValueError:
        pass
    else:
        raise AssertionError("signer accepted an incomplete sensor configuration")

    security_source = (ROOT / "main" / "security.c").read_text()
    replay_header = (ROOT / "main" / "replay_window.h").read_text()
    assert "#define KEYVALET_REPLAY_CAPACITY 160U" in replay_header
    assert "#define AUTH_MAX_LIFETIME_SECONDS 120U" in security_source
    assert "keyvalet_replay_window_add" in security_source
    assert "authorization->expires_at < now" in security_source
    assert "authorization->issued_at > now + AUTH_CLOCK_SKEW_SECONDS" in security_source
    assert "verify_signature(s_trust.operational_public_key" in security_source
    candidate_start = security_source.index("keyvalet_replay_window_t *updated")
    assert security_source.index("*updated = s_replay", candidate_start) < security_source.index("keyvalet_replay_window_add", candidate_start)
    assert security_source.index("keyvalet_replay_store_append", candidate_start) < security_source.index("s_replay = *updated", candidate_start)
    assert security_source.index("s_replay = *updated", candidate_start) < security_source.index("free(updated)", candidate_start)

    replay_record = (ROOT / "main" / "replay_record.c").read_text()
    replay_store = (ROOT / "main" / "replay_store.c").read_text()
    partitions = (ROOT / "partitions.csv").read_text()
    assert "sizeof(keyvalet_replay_record_t) == KEYVALET_REPLAY_RECORD_SIZE" in replay_record
    assert 'nvs_open_from_partition(KEYVALET_REPLAY_PARTITION' in replay_store
    assert "legacy" not in replay_store
    assert replay_store.index("if (s_highest_sequence != 0)") < replay_store.index("result = store_meta()")
    assert "replay_nvs, data, nvs,     0x790000, 0x10000" in partitions

    web_source = (ROOT / "main" / "web_server.c").read_text()
    unlock_start = web_source.index("static esp_err_t handle_targeted_unlock")
    unlock_end = web_source.index("static esp_err_t websocket_pre_handshake")
    unlock_source = web_source[unlock_start:unlock_end]
    assert unlock_source.index("keyvalet_security_authorize_and_consume") < unlock_source.index("keyvalet_journal_append") < unlock_source.index("keyvalet_device_unlock_commit")

    controller = (ROOT / "main" / "device_controller.c").read_text()
    control_header = (ROOT / "main" / "control_logic.h").read_text()
    assert "KEYVALET_EXPECTED_OPEN_WINDOW_MS 5000LL" in control_header
    assert "KEYVALET_OPERATION_NO_OPEN_TIMEOUT_MS 15000LL" in control_header
    assert "s_status.sensor_open[sensor]" in controller
    inputs = (ROOT / "main" / "input_monitor.c").read_text()
    assert "keyvalet_sensor_debounce_init" in inputs
    assert "debounce_sample(&sensors[index]" in inputs
    print("Milestone 5 security contract tests passed")


if __name__ == "__main__":
    main()
