#!/usr/bin/env python3
"""Focused Pass 2 purpose-neutral public-contract checks."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    web = (ROOT / "main" / "web_server.c").read_text()
    controller = (ROOT / "main" / "device_controller.c").read_text()
    settings = (ROOT / "main" / "settings.c").read_text()
    journal = (ROOT / "main" / "journal_format.h").read_text()
    console = (ROOT / "tools" / "m5_console.html").read_text()
    helper = (ROOT / "tools" / "m5_signing_helper.py").read_text()

    assert '\\\"locks\\\":%s,\\\"sensors\\\":%s' in web
    assert '\\\"monitoring_enabled\\\":%s' in web
    assert '\\\"alarm_active\\\":%s' in web
    assert '\\\"unexpected_open\\\":%s' in web
    status_source = web[web.index("static int build_status_json"):web.index(
        "static int build_event_json")]
    assert '\\\"courtesy\\\"' not in status_source
    assert '\\\"emergency\\\"' not in status_source
    assert "profile->sensor_available[index]" not in web

    assert "KEYVALET-CONFIG-V3" in helper
    assert "default_door_open_timeout_s" in helper
    assert "sensor_lines" in helper
    assert "KEYVALET_CONFIG_PROTOCOL_VERSION" in web
    assert "keyvalet_config_v3_canonical" in (ROOT / "main" / "security.c").read_text()
    assert "lock_sensor_required[index]" in web
    assert '"sensor_required"' in web
    assert '"locks"' in web
    unlock = web[web.index("static esp_err_t handle_targeted_unlock"):
                 web.index("static bool add_journal_record_json")]
    assert "sensor_required" not in unlock  # administrative setting, not unlock override

    assert "settings_blob_t" in settings
    assert "migrate" not in settings and "retired_" not in settings
    assert "sensor_monitoring_enabled" in controller
    assert "open && !initial &&" in controller
    assert "KEYVALET_EVENT_SENSOR_OPEN = 54" in journal
    assert "record_sensor_event" in controller
    assert "sensor_id" in web

    assert "Courtesy delay" not in console
    assert "Emergency Reed" not in console
    assert "Sensor 4 monitoring" in console
    print("Milestone 7 purpose-neutral contract tests passed")


if __name__ == "__main__":
    main()
