#include "hardware_profile.h"

#include "sdkconfig.h"

const keyvalet_hardware_profile_t KEYVALET_V042_PROFILE = {
    .name = "keyvalet-v0.4.2",
    .status_led_gpio = GPIO_NUM_4,
    .button_gpio = GPIO_NUM_2,
    .lock_gpio = {GPIO_NUM_10, GPIO_NUM_3},
    .sensor_gpio = {GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_18, GPIO_NUM_19},
    .sensor_internal_pullup = {false, false, true, true},
    .piezo_gpio = GPIO_NUM_11,
    .reserved_gpio = GPIO_NUM_5,
    .status_led_active_high = true,
    .button_active_low = true,
    .button_internal_pullup = false,
    .sensor_open_high = true,
    .solenoid_active_high = true,
    .solenoid_pulse_ms = 350,
    .lock_recovery_ms = CONFIG_KEYVALET_LOCK_RECOVERY_MS,
    .input_debounce_ms = 25,
    .startup_tone_1_hz = 2600,
    .startup_tone_1_ms = 70,
    .startup_tone_gap_ms = 25,
    .startup_tone_2_hz = 3400,
    .startup_tone_2_ms = 90,
    .alarm_silence_ms = 180000,
    .button_restart_hold_ms = 10000,
    .button_pairing_hold_ms = 5000,
    .alarm_tone_hz = 2200,
    .alarm_tone_ms = 200,
    .alarm_tone_gap_ms = 150,
    .alarm_repeat_ms = 2000,
    .hold_tone_hz = 1000,
    .hold_tone_ms = 120,
};

const keyvalet_hardware_profile_t *keyvalet_hardware_profile(void)
{
    return &KEYVALET_V042_PROFILE;
}
