#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "config_contract.h"
#include "journal_sync_contract.h"
#include "unlock_contract.h"

#define KEYVALET_CHALLENGE_HEX_LENGTH 64U
#define KEYVALET_PUBLIC_KEY_HEX_LENGTH 130U
#define KEYVALET_SIGNATURE_HEX_LENGTH 128U
#define KEYVALET_OPERATION_ID_MAX 64U
#define KEYVALET_NONCE_HEX_LENGTH 64U

typedef enum {
    KEYVALET_AUTH_UNLOCK,
    KEYVALET_AUTH_UPDATE_CONFIG,
    KEYVALET_AUTH_JOURNAL_ACK,
} keyvalet_auth_action_t;

typedef struct {
    const char *operation_id;
    const char *nonce_hex;
    int64_t issued_at;
    int64_t expires_at;
    const char *signature_hex;
} keyvalet_authorization_t;

esp_err_t keyvalet_security_init(void);
bool keyvalet_security_is_enrolled(void);
bool keyvalet_security_is_available(void);
/* Only the physically protected USB receiver may call this first-use operation.
 * No NVS write; candidate expires in at most 300 seconds or on enrollment failure. */
esp_err_t keyvalet_security_stage_enrollment_key(const char *public_key_hex,
    char fingerprint_hex[65], uint32_t *seconds_remaining);
esp_err_t keyvalet_security_open_pairing_window(void);
bool keyvalet_security_pairing_active(void);
uint32_t keyvalet_security_pairing_seconds_remaining(void);
void keyvalet_security_close_pairing_window(void);
esp_err_t keyvalet_security_begin_enrollment(char challenge_hex[65], uint32_t *seconds_remaining);
esp_err_t keyvalet_security_complete_enrollment(
    const char *challenge_hex, const char *operational_public_key_hex,
    int64_t expires_at, const char *signature_hex);
esp_err_t keyvalet_security_authorize_and_consume(
    keyvalet_auth_action_t action, const keyvalet_authorization_t *authorization,
    const keyvalet_config_v3_payload_t *config_payload,
    const keyvalet_unlock_authorization_payload_t *unlock_payload,
    const keyvalet_journal_ack_payload_t *journal_ack_payload);
