#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware_channels.h"

#define KEYVALET_UNLOCK_PROTOCOL_VERSION 2U
#define KEYVALET_UNLOCK_TIMEOUT_MIN_S 5U
#define KEYVALET_UNLOCK_TIMEOUT_MAX_S 3600U

typedef struct {
    const char *device_id;
    keyvalet_lock_id_t lock;
    bool timeout_present;
    uint32_t door_open_timeout_s;
    const char *operation_id;
    const char *nonce_hex;
    int64_t issued_at;
    int64_t expires_at;
} keyvalet_unlock_authorization_payload_t;

const char *keyvalet_lock_id_name(keyvalet_lock_id_t lock);
bool keyvalet_lock_id_parse(const char *name, keyvalet_lock_id_t *lock);
bool keyvalet_unlock_timeout_valid(bool present, uint32_t timeout_s);
int keyvalet_unlock_v2_canonical(
    char *output, size_t capacity,
    const keyvalet_unlock_authorization_payload_t *payload);
