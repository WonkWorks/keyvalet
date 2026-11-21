#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "hardware_channels.h"
#include "settings_schema.h"

typedef struct {
    uint32_t schema_version;
    uint32_t revision;
    uint32_t default_door_open_timeout_s;
    bool lock_sensor_required[KEYVALET_LOCK_COUNT];
    bool sensor_monitoring_enabled[KEYVALET_SENSOR_COUNT];
} keyvalet_settings_t;

esp_err_t keyvalet_settings_init(void);
esp_err_t keyvalet_settings_get(keyvalet_settings_t *settings);
esp_err_t keyvalet_settings_update(const keyvalet_settings_t *settings,
                                   uint32_t expected_revision);
bool keyvalet_settings_validate(const keyvalet_settings_t *settings);
