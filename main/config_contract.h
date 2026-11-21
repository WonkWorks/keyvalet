#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware_channels.h"

#define KEYVALET_CONFIG_PROTOCOL_VERSION 3U

typedef struct {
    const char *device_id;
    const char *operation_id;
    const char *nonce_hex;
    int64_t issued_at;
    int64_t expires_at;
    uint32_t expected_revision;
    uint32_t default_door_open_timeout_s;
    bool lock_sensor_required[KEYVALET_LOCK_COUNT];
    bool sensor_monitoring_enabled[KEYVALET_SENSOR_COUNT];
} keyvalet_config_v3_payload_t;

int keyvalet_config_v3_canonical(char *output, size_t capacity,
                                 const keyvalet_config_v3_payload_t *payload);
