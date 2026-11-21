#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "hardware_channels.h"

typedef struct {
    const char *name;
    gpio_num_t status_led_gpio;
    gpio_num_t button_gpio;
    gpio_num_t lock_gpio[KEYVALET_LOCK_COUNT];
    gpio_num_t sensor_gpio[KEYVALET_SENSOR_COUNT];
    bool sensor_internal_pullup[KEYVALET_SENSOR_COUNT];
    gpio_num_t piezo_gpio;
    gpio_num_t reserved_gpio;
    bool status_led_active_high;
    bool button_active_low;
    bool button_internal_pullup;
    bool sensor_open_high;
    bool solenoid_active_high;
    uint32_t solenoid_pulse_ms;
    uint32_t lock_recovery_ms;
    uint32_t input_debounce_ms;
    uint32_t startup_tone_1_hz;
    uint32_t startup_tone_1_ms;
    uint32_t startup_tone_gap_ms;
    uint32_t startup_tone_2_hz;
    uint32_t startup_tone_2_ms;
    uint32_t alarm_silence_ms;
    uint32_t button_restart_hold_ms;
    uint32_t button_pairing_hold_ms;
    uint32_t alarm_tone_hz;
    uint32_t alarm_tone_ms;
    uint32_t alarm_tone_gap_ms;
    uint32_t alarm_repeat_ms;
    uint32_t hold_tone_hz;
    uint32_t hold_tone_ms;
} keyvalet_hardware_profile_t;

extern const keyvalet_hardware_profile_t KEYVALET_V042_PROFILE;

const keyvalet_hardware_profile_t *keyvalet_hardware_profile(void);
