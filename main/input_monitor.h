#pragma once

#include "esp_err.h"
#include "hardware_profile.h"

typedef struct {
    void (*sensor_state)(keyvalet_sensor_id_t sensor, bool is_open, bool initial);
    void (*button_state)(bool is_pressed, bool initial);
} keyvalet_input_callbacks_t;

esp_err_t keyvalet_input_monitor_start(const keyvalet_hardware_profile_t *profile,
                                       const keyvalet_input_callbacks_t *callbacks);
