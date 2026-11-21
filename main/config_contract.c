#include "config_contract.h"

#include <inttypes.h>
#include <stdio.h>

int keyvalet_config_v3_canonical(char *output, size_t capacity,
                                 const keyvalet_config_v3_payload_t *payload)
{
    if (output == NULL || capacity == 0 || payload == NULL ||
        payload->device_id == NULL || payload->operation_id == NULL ||
        payload->nonce_hex == NULL) return -1;
    int used = snprintf(output, capacity,
        "KEYVALET-CONFIG-V3\n%s\nUPDATE_CONFIG\n%s\n%s\n%" PRId64
        "\n%" PRId64 "\n%" PRIu32 "\n%" PRIu32 "\n%u\n",
        payload->device_id, payload->operation_id, payload->nonce_hex,
        payload->issued_at, payload->expires_at, payload->expected_revision,
        payload->default_door_open_timeout_s, KEYVALET_LOCK_COUNT);
    if (used < 0 || (size_t)used >= capacity) return -1;
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index) {
        const int added = snprintf(output + used, capacity - (size_t)used,
            "lock_%u.sensor_required=%u\n", (unsigned)index + 1U,
            payload->lock_sensor_required[index] ? 1U : 0U);
        if (added < 0 || (size_t)added >= capacity - (size_t)used) return -1;
        used += added;
    }
    const int added = snprintf(output + used, capacity - (size_t)used,
                               "%u\n", KEYVALET_SENSOR_COUNT);
    if (added < 0 || (size_t)added >= capacity - (size_t)used) return -1;
    used += added;
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index) {
        const int added = snprintf(output + used, capacity - (size_t)used,
            "sensor_%u=%u\n", (unsigned)index + 1U,
            payload->sensor_monitoring_enabled[index] ? 1U : 0U);
        if (added < 0 || (size_t)added >= capacity - (size_t)used) return -1;
        used += added;
    }
    return used;
}
