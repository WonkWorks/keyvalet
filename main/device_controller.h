#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "hardware_profile.h"
#include "input_monitor.h"

typedef struct {
    bool sensor_known[KEYVALET_SENSOR_COUNT];
    bool sensor_open[KEYVALET_SENSOR_COUNT];
    bool sensor_monitoring_enabled[KEYVALET_SENSOR_COUNT];
    bool sensor_alarm_active[KEYVALET_SENSOR_COUNT];
    bool sensor_alarm_silenced[KEYVALET_SENSOR_COUNT];
    bool sensor_unexpected_open[KEYVALET_SENSOR_COUNT];
    bool lock_operation_active[KEYVALET_LOCK_COUNT];
    bool lock_sensor_required[KEYVALET_LOCK_COUNT];
    bool button_known, button_pressed;
} keyvalet_device_status_t;

esp_err_t keyvalet_device_controller_start(const keyvalet_hardware_profile_t *profile);
const keyvalet_input_callbacks_t *keyvalet_device_input_callbacks(void);
esp_err_t keyvalet_device_status_get(keyvalet_device_status_t *status);
esp_err_t keyvalet_device_controller_apply_settings(void);
esp_err_t keyvalet_device_unlock_reserve(keyvalet_lock_id_t lock,
                                         bool timeout_present,
                                         uint32_t door_open_timeout_s);
void keyvalet_device_unlock_cancel(keyvalet_lock_id_t lock);
esp_err_t keyvalet_device_unlock_commit(keyvalet_lock_id_t lock);
void keyvalet_device_note_solenoid_activation(keyvalet_lock_id_t lock);
