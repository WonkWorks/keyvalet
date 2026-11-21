#!/usr/bin/env python3
"""Static contracts for hardware channels, diagnostics, and output safety."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


def main() -> None:
    profiles = (ROOT / "main" / "hardware_profile.c").read_text()
    kconfig = (ROOT / "main" / "Kconfig.projbuild").read_text()
    hardware = (ROOT / "main" / "hardware_io.c").read_text()
    inputs = (ROOT / "main" / "input_monitor.c").read_text()
    controller = (ROOT / "main" / "device_controller.c").read_text()
    diagnostics = (ROOT / "main" / "diagnostic_console.c").read_text()

    v042 = section(profiles, "KEYVALET_V042_PROFILE", "keyvalet_hardware_profile")
    assert '.name = "keyvalet-v0.4.2"' in v042
    assert ".button_gpio = GPIO_NUM_2" in v042
    assert ".lock_gpio = {GPIO_NUM_10, GPIO_NUM_3}" in v042
    assert ".sensor_gpio = {GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_18, GPIO_NUM_19}" in v042
    assert ".sensor_internal_pullup = {false, false, true, true}" in v042
    assert "sensor_monitor_always" not in profiles
    assert ".lock_recovery_ms = CONFIG_KEYVALET_LOCK_RECOVERY_MS" in v042
    assert "GPIO_NUM_20" not in v042
    for forbidden in ("GPIO_NUM_8", "GPIO_NUM_9", "GPIO_NUM_12",
                      "GPIO_NUM_13", "GPIO_NUM_15"):
        assert forbidden not in v042

    assert "KEYVALET_TTE_PROFILE" not in profiles
    assert "KEYVALET_HARDWARE_" not in kconfig
    assert "config KEYVALET_LOCK_RECOVERY_MS" in kconfig
    assert "default 2000" in kconfig

    initialization = hardware[hardware.index("esp_err_t keyvalet_hardware_io_init"):]
    assert initialization.index("preload_solenoids_inactive(profile)") < initialization.index(
        "gpio_config(&solenoid_config)"
    ) < initialization.index("force_solenoids_inactive_locked(profile)") < initialization.index(
        "s_safe_outputs_ready = true"
    )
    pulse = section(
        hardware, "static esp_err_t start_lock_pulse_locked",
        "esp_err_t keyvalet_lock_cancel_reservation"
    )
    assert "keyvalet_lock_arbiter_request" in pulse
    assert "keyvalet_lock_arbiter_start_reserved" in pulse
    assert "profile->lock_gpio[lock]" in pulse
    assert "esp_timer_start_once" in pulse
    assert "keyvalet_lock_arbiter_fault" in pulse
    assert re.search(r"lock_gpio\[lock\].*true", pulse, re.DOTALL)
    assert re.search(
        r"esp_timer_start_once.*if \(result != ESP_OK\).*"
        r"force_solenoids_inactive_locked.*keyvalet_lock_arbiter_fault",
        pulse,
        re.DOTALL,
    )
    timer_callback = section(
        hardware, "static void solenoid_timer_callback", "esp_err_t keyvalet_lock_pulse"
    )
    assert re.search(
        r"esp_timer_start_once.*!= ESP_OK.*keyvalet_lock_arbiter_fault.*"
        r"s_safe_outputs_ready = false",
        timer_callback,
        re.DOTALL,
    )

    assert "for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index)" in inputs
    assert "profile->input_debounce_ms" in inputs
    assert "report_sensor_change(index" in inputs
    assert "sensor_only_state" in controller
    assert "s_sensor_silence_deadline" in controller
    assert "KEYVALET_EVENT_SENSOR_ALARM" in controller
    assert "record_sensor_event" in controller

    assert 'strcmp(command, "KVTEST PULSE LOCK_1")' in diagnostics
    assert 'strcmp(command, "KVTEST PULSE LOCK_2")' in diagnostics
    assert "keyvalet_diagnostic_lock_pulse(KEYVALET_LOCK_2)" in diagnostics
    assert "sensor_1=%s sensor_2=%s" in diagnostics
    cmake = (ROOT / "main" / "CMakeLists.txt").read_text()
    diagnostic_sources = section(cmake, "if(CONFIG_KEYVALET_DIAGNOSTICS)", "else()")
    assert '"diagnostic_console.c"' in diagnostic_sources
    assert '"diagnostic_solenoid.c"' in diagnostic_sources

    web = (ROOT / "main" / "web_server.c").read_text()
    unlock = section(web, "static esp_err_t handle_targeted_unlock", "static esp_err_t websocket_pre_handshake")
    assert unlock.index("keyvalet_security_authorize_and_consume") < unlock.index(
        "keyvalet_journal_append"
    ) < unlock.index("keyvalet_device_unlock_commit")
    commit = section(controller, "esp_err_t keyvalet_device_unlock_commit", "void keyvalet_device_note")
    assert "keyvalet_lock_pulse_reserved(s_profile, lock)" in commit

    partitions = (ROOT / "partitions.csv").read_text()
    assert "journal,    data, 0x40,    0x510000, 0x280000" in partitions
    assert "replay_nvs, data, nvs,     0x790000, 0x10000" in partitions
    print("hardware channel and lock safety contract tests passed")


if __name__ == "__main__":
    main()
