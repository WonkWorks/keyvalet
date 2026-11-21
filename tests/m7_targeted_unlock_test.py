#!/usr/bin/env python3
"""Focused Milestone 7 target-aware signing and safety-boundary checks."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))
import m5_signing_helper as helper
from m5_security_test import verify


def canonical(device: str, signed: dict[str, object]) -> str:
    timeout = signed.get("door_open_timeout_s", "DEFAULT")
    return (
        f"KEYVALET-UNLOCK-V2\n{device}\nUNLOCK\n{signed['lock_id']}\n"
        f"{timeout}\n{signed['operation_id']}\n{signed['nonce']}\n"
        f"{signed['issued_at']}\n{signed['expires_at']}\n"
    )


def rejected(request: dict[str, object]) -> None:
    try:
        helper.signed_response("/api/sign/unlock", request)
        raise AssertionError("invalid targeted unlock was signed")
    except ValueError:
        pass


def main() -> None:
    helper.initialize_authority()
    device = "keyvalet-a1b2c3"
    lock1 = helper.signed_response(
        "/api/sign/unlock", {"device_id": device, "lock_id": "lock_1"}
    )
    lock2 = helper.signed_response(
        "/api/sign/unlock",
        {"device_id": device, "lock_id": "lock_2", "door_open_timeout_s": 240},
    )
    assert lock1["protocol_version"] == 2 and lock1["action"] == "UNLOCK"
    assert "door_open_timeout_s" not in lock1
    assert verify(helper.OPERATIONAL_KEY, canonical(device, lock1), lock1["signature"])
    assert verify(helper.OPERATIONAL_KEY, canonical(device, lock2), lock2["signature"])
    assert not verify(
        helper.OPERATIONAL_KEY,
        canonical(device, lock1).replace("\nlock_1\n", "\nlock_2\n"),
        lock1["signature"],
    )
    assert not verify(
        helper.OPERATIONAL_KEY,
        canonical(device, lock2).replace("\n240\n", "\n241\n"),
        lock2["signature"],
    )
    rejected({"device_id": device})
    rejected({"device_id": device, "lock_id": "lock_3"})
    zero = helper.signed_response("/api/sign/unlock",
        {"device_id": device, "lock_id": "lock_1", "door_open_timeout_s": 0})
    assert zero["door_open_timeout_s"] == 0
    assert "\n0\n" in canonical(device, zero)
    assert verify(helper.OPERATIONAL_KEY, canonical(device, zero), zero["signature"])
    assert not verify(helper.OPERATIONAL_KEY,
        canonical(device, zero).replace("\n0\n", "\nDEFAULT\n"), zero["signature"])
    for timeout in (1, 2, 3, 4, -1, True, "0"):
        rejected({"device_id": device, "lock_id": "lock_1", "door_open_timeout_s": timeout})
    rejected({"device_id": device, "lock_id": "lock_1", "door_open_timeout_s": 3.5})
    rejected({"device_id": device, "lock_id": "lock_1", "door_open_timeout_s": 3601})

    web = (ROOT / "main" / "web_server.c").read_text()
    targeted = web[web.index("static esp_err_t handle_targeted_unlock"):
                   web.index("static esp_err_t websocket_pre_handshake")]
    assert targeted.index("keyvalet_device_unlock_reserve") < targeted.index(
        "keyvalet_security_authorize_and_consume"
    ) < targeted.index("keyvalet_journal_append") < targeted.index(
        "keyvalet_device_unlock_commit"
    )
    assert "KEYVALET_EVENT_LOCK_UNLOCK_AUTHORIZED" in targeted
    assert "(uint32_t)lock + 1U" in targeted
    assert '"lock_id"' in web
    assert '"unlock_courtesy"' not in web
    assert '"unlock"' in web

    controller = (ROOT / "main" / "device_controller.c").read_text()
    profile = (ROOT / "main" / "hardware_profile.c").read_text()
    assert "s_operation_guard[KEYVALET_LOCK_COUNT]" in controller
    assert "s_operation_timeout_s[KEYVALET_LOCK_COUNT]" in controller
    assert "s_last_solenoid[KEYVALET_LOCK_COUNT]" in controller
    assert "s_operation_timeout_s[lock]" in controller
    assert "s_settings.default_door_open_timeout_s" in controller
    assert "lock_available" not in controller
    assert "s_status.sensor_unexpected_open[sensor]" in controller
    assert "s_last_solenoid[lock]" in controller
    assert "paired_sensor_state" in controller
    assert "lock_available" not in profile

    partitions = (ROOT / "partitions.csv").read_text()
    assert "replay_nvs, data, nvs,     0x790000, 0x10000" in partitions
    print("Milestone 7 targeted unlock tests passed")


if __name__ == "__main__":
    main()
