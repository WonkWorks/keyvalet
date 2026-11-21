#!/usr/bin/env python3
"""Focused static contracts for M5 browser/preflight and pairing cancellation."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import m5_signing_helper as helper


def main() -> None:
    helper.initialize_authority()
    valid = {
        "device_id": "keyvalet-a1b2c3",
        "config": {
            "expected_revision": 1,
            "default_door_open_timeout_s": 120,
            "locks": {"lock_1": {"sensor_required": True},
                      "lock_2": {"sensor_required": True}},
            "sensors": {
                "sensor_1": {"monitoring_enabled": True},
                "sensor_2": {"monitoring_enabled": False},
                "sensor_3": {"monitoring_enabled": True},
                "sensor_4": {"monitoring_enabled": True},
            },
        },
    }
    signed = helper.signed_response("/api/sign/config", valid)
    assert signed["expected_revision"] == 1
    invalid = {"device_id": valid["device_id"], "config": dict(valid["config"])}
    del invalid["config"]["expected_revision"]
    try:
        helper.signed_response("/api/sign/config", invalid)
        raise AssertionError("missing expected_revision was accepted")
    except ValueError as error:
        assert "expected_revision" in str(error)

    web = (ROOT / "main" / "web_server.c").read_text()
    assert '"Access-Control-Allow-Methods", "GET, PATCH, OPTIONS"' in web
    assert '"Access-Control-Allow-Headers", "Content-Type"' in web
    assert 'HTTPD_403_FORBIDDEN' in web
    assert 'keyvalet_tls_allowed_origin(origin)' in web

    controller = (ROOT / "main" / "device_controller.c").read_text()
    security = (ROOT / "main" / "security.c").read_text()
    console = (ROOT / "tools" / "m5_console.html").read_text()
    assert "KEYVALET_BUTTON_ACTION_CANCEL_PAIRING" in controller
    assert "keyvalet_security_close_pairing_window();" in controller
    assert "KEYVALET_PIEZO_PAIRING" in controller
    close_start = security.index("void keyvalet_security_close_pairing_window")
    close_end = security.index("esp_err_t keyvalet_security_begin_enrollment")
    close_source = security[close_start:close_end]
    assert "s_pairing_deadline_us = 0" in close_source
    assert "clear_enrollment_locked();" in close_source
    clear_source = security[security.index("static void clear_enrollment_locked"):
                            security.index("static bool enrollment_key_active_locked")]
    assert "s_challenge_active = false" in clear_source
    assert "s_challenge_deadline_us = 0" in clear_source
    assert "memset(s_challenge, 0, sizeof(s_challenge))" in clear_source
    assert "memset(s_enrollment_key, 0, sizeof(s_enrollment_key))" in clear_source
    assert "Read settings before signing an update" in console
    assert "await readConfiguration()" in console
    print("M5 browser and pairing acceptance contract tests passed")


if __name__ == "__main__":
    main()
