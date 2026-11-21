#include "security.h"

#include <ctype.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#include "network_manager.h"
#include "nvs.h"
#include "replay_window.h"
#include "replay_store.h"
#include "time_sync.h"

#define SECURITY_NAMESPACE "keyvalet"
#define TRUST_KEY "trust_v1"
#define TRUST_MAGIC 0x4b565452U
#define TRUST_FORMAT_VERSION 1U
#define PAIRING_WINDOW_SECONDS 600U
#define CHALLENGE_LIFETIME_SECONDS 300U
#define AUTH_MAX_LIFETIME_SECONDS 120U
#define AUTH_CLOCK_SKEW_SECONDS 15U

typedef struct {
    uint32_t magic;
    uint32_t version;
    char device_id[KEYVALET_DEVICE_ID_MAX];
    uint8_t operational_public_key[65];
    uint8_t reserved[3];
    uint32_t crc32;
} trust_blob_t;

_Static_assert(sizeof(trust_blob_t) == 104, "unexpected trust layout");
_Static_assert(offsetof(trust_blob_t, crc32) == 100, "unexpected trust CRC");

static const char *TAG = "security";
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static trust_blob_t s_trust;
static keyvalet_replay_window_t s_replay;
static bool s_enrolled;
static int64_t s_pairing_deadline_us;
static uint8_t s_challenge[32];
static int64_t s_challenge_deadline_us;
static bool s_challenge_active;
static bool s_ready;
static uint8_t s_enrollment_key[65];
static int64_t s_enrollment_key_deadline_us;

static esp_err_t derive_device_id(char output[KEYVALET_DEVICE_ID_MAX])
{
    uint8_t mac[6];
    const esp_err_t result = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (result != ESP_OK) return result;
    snprintf(output, KEYVALET_DEVICE_ID_MAX, "keyvalet-%02x%02x%02x",
             mac[3], mac[4], mac[5]);
    return ESP_OK;
}

static uint32_t blob_crc(const void *blob, size_t crc_offset)
{
    return esp_crc32_le(0, blob, crc_offset);
}

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
}

static bool valid_operation_id(const char *value)
{
    if (value == NULL || value[0] == '\0' ||
        strlen(value) >= KEYVALET_OPERATION_ID_MAX) return false;
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor != '\0'; ++cursor) {
        if (!(isalnum(*cursor) || *cursor == '-' || *cursor == '_' ||
              *cursor == '.' || *cursor == ':')) return false;
    }
    return true;
}

static bool decode_hex_exact(const char *hex, uint8_t *output, size_t size)
{
    if (hex == NULL || strlen(hex) != size * 2U) {
        return false;
    }
    for (size_t index = 0; index < size; ++index) {
        const int high = hex_nibble(hex[index * 2U]);
        const int low = hex_nibble(hex[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        output[index] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static void encode_hex(const uint8_t *input, size_t size, char *output)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0; index < size; ++index) {
        output[index * 2U] = digits[input[index] >> 4];
        output[index * 2U + 1U] = digits[input[index] & 0x0f];
    }
    output[size * 2U] = '\0';
}

static esp_err_t verify_signature(const uint8_t public_key[65],
                                  const char *message, const char *signature_hex)
{
    uint8_t signature[64];
    uint8_t digest[32];
    if (!decode_hex_exact(signature_hex, signature, sizeof(signature))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mbedtls_sha256((const unsigned char *)message, strlen(message), digest, 0) != 0) {
        return ESP_FAIL;
    }
    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    int result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if (result == 0) {
        result = mbedtls_ecp_point_read_binary(&group, &point, public_key, 65);
    }
    if (result == 0) {
        result = mbedtls_mpi_read_binary(&r, signature, 32);
    }
    if (result == 0) {
        result = mbedtls_mpi_read_binary(&s, signature + 32, 32);
    }
    if (result == 0) {
        result = mbedtls_ecdsa_verify(&group, digest, sizeof(digest), &point, &r, &s);
    }
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return result == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static bool valid_public_key(const uint8_t public_key[65])
{
    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    int result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if (result == 0 && public_key[0] == 0x04)
        result = mbedtls_ecp_point_read_binary(&group, &point, public_key, 65);
    else result = -1;
    if (result == 0) result = mbedtls_ecp_check_pubkey(&group, &point);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return result == 0;
}

static void clear_enrollment_locked(void)
{
    memset(s_enrollment_key, 0, sizeof(s_enrollment_key));
    s_enrollment_key_deadline_us = 0;
    s_challenge_active = false;
    s_challenge_deadline_us = 0;
    memset(s_challenge, 0, sizeof(s_challenge));
}

static bool enrollment_key_active_locked(void)
{
    if (!s_enrollment_key_deadline_us || esp_timer_get_time() >= s_enrollment_key_deadline_us) {
        clear_enrollment_locked();
        return false;
    }
    return true;
}

static bool pairing_active_locked(void)
{
    if (s_enrollment_key_deadline_us) (void)enrollment_key_active_locked();
    if (s_pairing_deadline_us == 0 || esp_timer_get_time() >= s_pairing_deadline_us) {
        if (s_pairing_deadline_us) clear_enrollment_locked();
        s_pairing_deadline_us = 0;
        return false;
    }
    return true;
}

static esp_err_t load_state(void)
{
    char device_id[KEYVALET_DEVICE_ID_MAX];
    ESP_RETURN_ON_ERROR(derive_device_id(device_id), TAG,
                        "Unable to derive trust-bound device identity");
    size_t size = sizeof(s_trust);
    esp_err_t result = nvs_get_blob(s_nvs, TRUST_KEY, &s_trust, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        memset(&s_trust, 0, sizeof(s_trust));
        s_enrolled = false;
    } else if (result != ESP_OK || size != sizeof(s_trust) ||
               s_trust.magic != TRUST_MAGIC ||
               s_trust.version != TRUST_FORMAT_VERSION ||
               s_trust.crc32 != blob_crc(&s_trust, offsetof(trust_blob_t, crc32)) ||
               strncmp(s_trust.device_id, device_id, sizeof(s_trust.device_id)) != 0 ||
               !valid_public_key(s_trust.operational_public_key)) {
        ESP_LOGE(TAG, "Trust storage is corrupt or unsupported; control disabled");
        return ESP_ERR_INVALID_CRC;
    } else {
        s_enrolled = true;
    }

    return keyvalet_replay_store_init(s_enrolled, &s_replay);
}

static esp_err_t confirm_running_application(void)
{
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    esp_err_t result = esp_ota_get_state_partition(running, &state);
    if (result != ESP_OK) return result;
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        result = esp_ota_mark_app_valid_cancel_rollback();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Unable to validate replay-aware application: %s",
                     esp_err_to_name(result));
            return result;
        }
        ESP_LOGW(TAG, "Replay-aware application validated; automatic rollback cancelled");
    } else if (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED) {
        return ESP_ERR_INVALID_STATE;
    }
#else
    ESP_LOGW(TAG, "OTA rollback is disabled; replay-aware downgrade protection depends on production provisioning");
#endif
    return ESP_OK;
}

esp_err_t keyvalet_security_init(void)
{
    if (s_lock != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t result = nvs_open(SECURITY_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (result == ESP_OK) {
        result = load_state();
    }
    if (result == ESP_OK) {
        result = confirm_running_application();
    }
    s_ready = result == ESP_OK;
    return result;
}

bool keyvalet_security_is_available(void)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ready = s_ready;
    xSemaphoreGive(s_lock);
    return ready;
}

bool keyvalet_security_is_enrolled(void)
{
    if (s_lock == NULL || !s_ready) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool enrolled = s_enrolled;
    xSemaphoreGive(s_lock);
    return enrolled;
}

esp_err_t keyvalet_security_stage_enrollment_key(const char *public_key_hex,
    char fingerprint_hex[65], uint32_t *seconds_remaining)
{
    uint8_t key[65], fingerprint[32];
    if (!s_lock || !s_ready) return ESP_ERR_INVALID_STATE;
    if (!fingerprint_hex || !seconds_remaining ||
        !decode_hex_exact(public_key_hex, key, sizeof(key)) || !valid_public_key(key) ||
        mbedtls_sha256(key, sizeof(key), fingerprint, 0) != 0) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t result = ESP_OK;
    if (!s_ready || s_enrolled) result = ESP_ERR_NOT_ALLOWED;
    else {
        (void)pairing_active_locked(); /* Retire an expired prior button window first. */
        if (enrollment_key_active_locked()) {
            /* Retry a lost USB response without replacing/extending the key.
             * The existing short-button cancellation clears it explicitly. */
            if (memcmp(s_enrollment_key, key, sizeof(key))) result = ESP_ERR_INVALID_STATE;
        } else {
            memcpy(s_enrollment_key, key, sizeof(key));
            s_enrollment_key_deadline_us = esp_timer_get_time() +
                (int64_t)CHALLENGE_LIFETIME_SECONDS * 1000000LL;
        }
    }
    if (result == ESP_OK) {
        encode_hex(fingerprint, sizeof(fingerprint), fingerprint_hex);
        *seconds_remaining = (uint32_t)((s_enrollment_key_deadline_us - esp_timer_get_time() + 999999) / 1000000);
    }
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t keyvalet_security_open_pairing_window(void)
{
    if (s_lock == NULL || !s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_enrolled) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_ALLOWED;
    }
    (void)pairing_active_locked();
    s_pairing_deadline_us = esp_timer_get_time() +
                            (int64_t)PAIRING_WINDOW_SECONDS * 1000000LL;
    /* Preserve a USB-staged candidate and any live challenge. Another hold
     * cannot extend their deadlines or substitute an enrollment request. */
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "Physical pairing window opened for %u seconds",
             PAIRING_WINDOW_SECONDS);
    return ESP_OK;
}

bool keyvalet_security_pairing_active(void)
{
    if (s_lock == NULL) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool active = pairing_active_locked();
    xSemaphoreGive(s_lock);
    return active;
}

uint32_t keyvalet_security_pairing_seconds_remaining(void)
{
    if (s_lock == NULL) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t remaining = 0;
    if (pairing_active_locked()) {
        remaining = (uint32_t)((s_pairing_deadline_us - esp_timer_get_time() + 999999) /
                               1000000);
    }
    xSemaphoreGive(s_lock);
    return remaining;
}

void keyvalet_security_close_pairing_window(void)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_pairing_deadline_us = 0;
    clear_enrollment_locked();
    xSemaphoreGive(s_lock);
}

esp_err_t keyvalet_security_begin_enrollment(char challenge_hex[65], uint32_t *seconds_remaining)
{
    if (challenge_hex == NULL || seconds_remaining == NULL || s_lock == NULL || !s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_ready || s_enrolled || !pairing_active_locked() || !enrollment_key_active_locked()) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_challenge_active) {
        /* A competing begin must not invalidate the intended client's proof. */
        if (esp_timer_get_time() >= s_challenge_deadline_us) clear_enrollment_locked();
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    esp_fill_random(s_challenge, sizeof(s_challenge));
    s_challenge_deadline_us = esp_timer_get_time() +
                              (int64_t)CHALLENGE_LIFETIME_SECONDS * 1000000LL;
    if (s_challenge_deadline_us > s_pairing_deadline_us) {
        s_challenge_deadline_us = s_pairing_deadline_us;
    }
    if (s_challenge_deadline_us > s_enrollment_key_deadline_us)
        s_challenge_deadline_us = s_enrollment_key_deadline_us;
    s_challenge_active = true;
    encode_hex(s_challenge, sizeof(s_challenge), challenge_hex);
    *seconds_remaining = (uint32_t)((s_challenge_deadline_us - esp_timer_get_time() + 999999) / 1000000);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t keyvalet_security_complete_enrollment(
    const char *challenge_hex, const char *operational_public_key_hex,
    int64_t expires_at, const char *signature_hex)
{
    uint8_t challenge[32];
    uint8_t public_key[65];
    char device_id[KEYVALET_DEVICE_ID_MAX];
    int64_t now;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (!s_ready || s_enrolled) goto done;
    result = ESP_ERR_INVALID_ARG;
    if (!decode_hex_exact(challenge_hex, challenge, sizeof(challenge)) ||
        !decode_hex_exact(operational_public_key_hex, public_key, sizeof(public_key)) ||
        derive_device_id(device_id) != ESP_OK ||
        !keyvalet_time_get_utc(&now) || expires_at < now ||
        expires_at > now + CHALLENGE_LIFETIME_SECONDS) {
        goto failed;
    }
    result = ESP_ERR_INVALID_STATE;
    if (!pairing_active_locked() || !enrollment_key_active_locked() || !s_challenge_active ||
        esp_timer_get_time() >= s_challenge_deadline_us ||
        memcmp(challenge, s_challenge, sizeof(challenge)) ||
        memcmp(public_key, s_enrollment_key, sizeof(public_key))) goto failed;
    char message[512];
    const int length = snprintf(message, sizeof(message),
        "KEYVALET-ENROLL-V1\n%s\n%s\n%s\nENROLL_FIRST\n%" PRId64 "\n",
        device_id, challenge_hex,
        operational_public_key_hex, expires_at);
    if (length < 0 || (size_t)length >= sizeof(message)) {
        result = ESP_ERR_INVALID_SIZE;
        goto failed;
    }
    /* Existing ENROLL-V1 bytes, now proving possession of the exact USB-pinned
     * candidate rather than authority granted by a compiled master root. */
    result = verify_signature(s_enrollment_key, message, signature_hex);
    if (result != ESP_OK) goto failed;
    if (esp_timer_get_time() >= s_challenge_deadline_us) {
        result = ESP_ERR_TIMEOUT;
        goto failed;
    }
    trust_blob_t pending = {.magic = TRUST_MAGIC, .version = TRUST_FORMAT_VERSION};
    snprintf(pending.device_id, sizeof(pending.device_id), "%s", device_id);
    memcpy(pending.operational_public_key, public_key, sizeof(public_key));
    pending.crc32 = blob_crc(&pending, offsetof(trust_blob_t, crc32));
    result = nvs_set_blob(s_nvs, TRUST_KEY, &pending, sizeof(pending));
    if (result == ESP_OK) {
        result = nvs_commit(s_nvs);
    }
    if (result == ESP_OK) {
        s_trust = pending;
        s_enrolled = true;
        s_pairing_deadline_us = 0;
        clear_enrollment_locked();
        ESP_LOGW(TAG, "First enrollment accepted; pairing window closed");
        goto done;
    }
    s_ready = false;
    ESP_LOGE(TAG, "Enrollment persistence uncertain; reboot required before further authorization");
failed:
    clear_enrollment_locked();
done:
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t keyvalet_security_authorize_and_consume(
    keyvalet_auth_action_t action, const keyvalet_authorization_t *authorization,
    const keyvalet_config_v3_payload_t *config_payload,
    const keyvalet_unlock_authorization_payload_t *unlock_payload,
    const keyvalet_journal_ack_payload_t *journal_ack_payload)
{
    if (!s_ready || authorization == NULL ||
        authorization->nonce_hex == NULL || authorization->signature_hex == NULL ||
        !valid_operation_id(authorization->operation_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t nonce[32];
    int64_t now;
    if (!decode_hex_exact(authorization->nonce_hex, nonce, sizeof(nonce)) ||
        !keyvalet_time_get_utc(&now) ||
        authorization->issued_at > now + AUTH_CLOCK_SKEW_SECONDS ||
        authorization->expires_at < now ||
        authorization->expires_at <= authorization->issued_at ||
        authorization->expires_at - authorization->issued_at > AUTH_MAX_LIFETIME_SECONDS) {
        return ESP_ERR_INVALID_ARG;
    }
    char message[768];
    int length;
    if (action == KEYVALET_AUTH_UPDATE_CONFIG) {
        if (config_payload == NULL) {
            return ESP_ERR_INVALID_ARG;
        }
        if (config_payload->device_id == NULL ||
            strcmp(config_payload->device_id, keyvalet_network_device_id()) != 0 ||
            config_payload->operation_id == NULL ||
            strcmp(config_payload->operation_id, authorization->operation_id) != 0 ||
            config_payload->nonce_hex == NULL ||
            strcmp(config_payload->nonce_hex, authorization->nonce_hex) != 0 ||
            config_payload->issued_at != authorization->issued_at ||
            config_payload->expires_at != authorization->expires_at) {
            return ESP_ERR_INVALID_ARG;
        }
        length = keyvalet_config_v3_canonical(
            message, sizeof(message), config_payload);
    } else if (action == KEYVALET_AUTH_UNLOCK) {
        if (unlock_payload == NULL || unlock_payload->device_id == NULL ||
            strcmp(unlock_payload->device_id, keyvalet_network_device_id()) != 0 ||
            unlock_payload->operation_id == NULL ||
            unlock_payload->nonce_hex == NULL ||
            strcmp(unlock_payload->operation_id, authorization->operation_id) != 0 ||
            strcmp(unlock_payload->nonce_hex, authorization->nonce_hex) != 0 ||
            unlock_payload->issued_at != authorization->issued_at ||
            unlock_payload->expires_at != authorization->expires_at) {
            return ESP_ERR_INVALID_ARG;
        }
        length = keyvalet_unlock_v2_canonical(
            message, sizeof(message), unlock_payload);
    } else if (action == KEYVALET_AUTH_JOURNAL_ACK) {
        if (journal_ack_payload == NULL ||
            journal_ack_payload->device_id == NULL ||
            strcmp(journal_ack_payload->device_id,
                   keyvalet_network_device_id()) != 0 ||
            journal_ack_payload->operation_id == NULL ||
            journal_ack_payload->nonce_hex == NULL ||
            strcmp(journal_ack_payload->operation_id,
                   authorization->operation_id) != 0 ||
            strcmp(journal_ack_payload->nonce_hex,
                   authorization->nonce_hex) != 0 ||
            journal_ack_payload->issued_at != authorization->issued_at ||
            journal_ack_payload->expires_at != authorization->expires_at) {
            return ESP_ERR_INVALID_ARG;
        }
        length = keyvalet_journal_ack_canonical(
            message, sizeof(message), journal_ack_payload);
    } else return ESP_ERR_INVALID_ARG;
    if (length < 0 || (size_t)length >= sizeof(message)) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t operation_digest[32];
    uint8_t nonce_digest[32];
    if (mbedtls_sha256((const unsigned char *)authorization->operation_id,
                       strlen(authorization->operation_id), operation_digest, 0) != 0 ||
        mbedtls_sha256(nonce, sizeof(nonce), nonce_digest, 0) != 0) {
        return ESP_FAIL;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_enrolled) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (keyvalet_replay_window_contains(&s_replay, operation_digest,
                                        nonce_digest)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = verify_signature(s_trust.operational_public_key, message,
                                        authorization->signature_hex);
    if (result == ESP_OK) {
        keyvalet_replay_window_t *updated = malloc(sizeof(*updated));
        if (updated == NULL) {
            result = ESP_ERR_NO_MEM;
        } else {
            *updated = s_replay;
            if (!keyvalet_replay_window_add(updated, operation_digest,
                                            nonce_digest, now)) {
                result = ESP_ERR_TIMEOUT;
            } else {
                result = keyvalet_replay_store_append(
                    operation_digest, nonce_digest, now);
                if (result == ESP_OK) {
                    s_replay = *updated;
                }
            }
            free(updated);
        }
    }
    xSemaphoreGive(s_lock);
    return result;
}
