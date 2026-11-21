#include "unlock_contract.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

const char *keyvalet_lock_id_name(keyvalet_lock_id_t lock)
{
    if (lock == KEYVALET_LOCK_1) return "lock_1";
    if (lock == KEYVALET_LOCK_2) return "lock_2";
    return NULL;
}

bool keyvalet_lock_id_parse(const char *name, keyvalet_lock_id_t *lock)
{
    if (name == NULL || lock == NULL) return false;
    if (strcmp(name, "lock_1") == 0) *lock = KEYVALET_LOCK_1;
    else if (strcmp(name, "lock_2") == 0) *lock = KEYVALET_LOCK_2;
    else return false;
    return true;
}

bool keyvalet_unlock_timeout_valid(bool present, uint32_t timeout_s)
{
    return !present || timeout_s == 0 || (timeout_s >= KEYVALET_UNLOCK_TIMEOUT_MIN_S &&
                        timeout_s <= KEYVALET_UNLOCK_TIMEOUT_MAX_S);
}

int keyvalet_unlock_v2_canonical(
    char *output, size_t capacity,
    const keyvalet_unlock_authorization_payload_t *payload)
{
    if (output == NULL || capacity == 0 || payload == NULL ||
        payload->device_id == NULL || payload->operation_id == NULL ||
        payload->nonce_hex == NULL ||
        !keyvalet_unlock_timeout_valid(payload->timeout_present,
                                       payload->door_open_timeout_s)) {
        return -1;
    }
    const char *lock_name = keyvalet_lock_id_name(payload->lock);
    if (lock_name == NULL) return -1;
    char timeout[16];
    if (payload->timeout_present) {
        const int timeout_length = snprintf(
            timeout, sizeof(timeout), "%" PRIu32, payload->door_open_timeout_s);
        if (timeout_length < 0 || (size_t)timeout_length >= sizeof(timeout)) return -1;
    } else {
        memcpy(timeout, "DEFAULT", sizeof("DEFAULT"));
    }
    return snprintf(output, capacity,
                    "KEYVALET-UNLOCK-V2\n%s\nUNLOCK\n%s\n%s\n%s\n%s\n%" PRId64
                    "\n%" PRId64 "\n",
                    payload->device_id, lock_name, timeout,
                    payload->operation_id, payload->nonce_hex,
                    payload->issued_at, payload->expires_at);
}
