#include "tls_identity.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hardware_io.h"
#include "network_manager.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "time_sync.h"
#include "esp_mac.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"

static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static keyvalet_tls_status_t s_status;
static int s_active = -1, s_candidate = -1;
static uint32_t s_generation;
static char s_device_uid[22];
static const char *const slots[] = {"identity0", "identity1"};
_Static_assert(sizeof(keyvalet_tls_credentials_t) < 8192, "Bounded TLS slot exceeds storage budget");

void keyvalet_tls_credentials_free(keyvalet_tls_credentials_t *credentials)
{
    if (credentials) { mbedtls_platform_zeroize(credentials, sizeof(*credentials)); free(credentials); }
}

static esp_err_t read_slot(int slot, keyvalet_tls_credentials_t *record)
{
    size_t length = sizeof(*record);
    esp_err_t result = nvs_get_blob(s_nvs, slots[slot], record, &length);
    if (result != ESP_OK) return result;
    uint8_t digest[32];
    mbedtls_sha256((const uint8_t *)record, offsetof(keyvalet_tls_credentials_t, digest), digest, 0);
    if (length != sizeof(*record) || record->schema != 1 || !record->generation ||
        memcmp(record->digest, digest, sizeof(digest)) || record->hostname[KEYVALET_TLS_HOST_MAX] ||
        !keyvalet_tls_hostname_valid(record->hostname) || !record->key_length ||
        record->key_length > KEYVALET_TLS_KEY_MAX || record->chain_length > KEYVALET_TLS_CHAIN_MAX ||
        record->chain[record->chain_length]) return ESP_ERR_INVALID_CRC;
    for (unsigned i = 0; i < KEYVALET_TLS_ORIGIN_COUNT; ++i)
        if (record->origins[i][KEYVALET_TLS_ORIGIN_MAX - 1] ||
            (record->origins[i][0] && !keyvalet_tls_origin_valid(record->origins[i]))) return ESP_ERR_INVALID_CRC;
    if (!record->origins[0][0]) return ESP_ERR_INVALID_CRC;
    if (record->chain_length)
        return keyvalet_tls_material_validate(record->key, record->key_length,
            record->chain, record->chain_length, record->hostname, 0, NULL, NULL);
    return keyvalet_tls_public_material(record->key, record->key_length, record->hostname, NULL, 0, NULL);
}

static esp_err_t persist(const char *key, const void *bytes, size_t length)
{
    if (!s_status.writable) return ESP_ERR_INVALID_STATE;
    esp_err_t result = keyvalet_lock_flash_begin();
    if (result == ESP_OK) {
        result = nvs_set_blob(s_nvs, key, bytes, length);
        if (result == ESP_OK) result = nvs_commit(s_nvs);
        keyvalet_lock_flash_end();
        /* Any error may follow a durable write. Do not mutate again this boot
         * or infer which selector/slot won. Existing in-RAM server stays valid. */
        if (result != ESP_OK) s_status.writable = false;
    }
    return result;
}

static esp_err_t write_slot(int slot, keyvalet_tls_credentials_t *record)
{
    mbedtls_sha256((const uint8_t *)record, offsetof(keyvalet_tls_credentials_t, digest), record->digest, 0);
    return persist(slots[slot], record, sizeof(*record));
}

static void adopt_metadata(const keyvalet_tls_credentials_t *record)
{
    s_status.active = true; s_status.generation = record->generation;
    memcpy(s_status.hostname, record->hostname, sizeof(s_status.hostname));
    memcpy(s_status.origins, record->origins, sizeof(s_status.origins));
    (void)keyvalet_tls_material_validate(record->key, record->key_length, record->chain,
        record->chain_length, record->hostname, 0, &s_status.not_before, &s_status.not_after);
}

esp_err_t keyvalet_tls_identity_init(void)
{
    if (s_lock) return ESP_ERR_INVALID_STATE;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return ESP_FAIL;
    snprintf(s_device_uid, sizeof(s_device_uid), "keyvalet-%02x%02x%02x%02x%02x%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    esp_err_t result = nvs_flash_init_partition("tls_nvs");
    if (result != ESP_OK) return result; /* Never erase/reformat on error. */
    result = nvs_open_from_partition("tls_nvs", "tls_identity", NVS_READWRITE, &s_nvs);
    if (result != ESP_OK) return result;
    keyvalet_tls_credentials_t *record = calloc(1, sizeof(*record));
    if (!record) return ESP_ERR_NO_MEM;
    uint8_t selected;
    size_t length = sizeof(selected);
    result = nvs_get_blob(s_nvs, "selected", &selected, &length);
    if (result == ESP_OK) {
        if (length != 1 || selected > 1 || read_slot(selected, record) != ESP_OK || !record->chain_length) result = ESP_ERR_INVALID_CRC;
        else { s_active = selected; adopt_metadata(record); s_generation = record->generation; }
    } else if (result == ESP_ERR_NVS_NOT_FOUND) result = ESP_OK;
    if (result == ESP_OK) {
        const int candidate = s_active == 0 ? 1 : 0;
        esp_err_t pending = read_slot(candidate, record);
        if (pending == ESP_OK) { if (record->generation > s_generation) { s_candidate = candidate; s_generation = record->generation; } }
        else if (pending != ESP_ERR_NVS_NOT_FOUND) result = pending;
    }
    keyvalet_tls_credentials_free(record);
    s_status.available = result == ESP_OK;
    s_status.writable = result == ESP_OK;
    return result;
}

const char *keyvalet_tls_device_uid(void) { return s_device_uid; }

void keyvalet_tls_status_get(keyvalet_tls_status_t *status)
{
    if (!s_lock) { memset(status, 0, sizeof(*status)); status->state = "UNAVAILABLE"; return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status; status->candidate = s_candidate >= 0;
    int64_t now = 0;
    const bool clock_valid = keyvalet_time_get_utc(&now);
    status->ready = status->active && clock_valid &&
        now >= status->not_before && now < status->not_after;
    status->state = !status->available ? "UNAVAILABLE" : !status->writable ? "STORAGE_UNCERTAIN" :
        !status->active ? "UNCOMMISSIONED" : !clock_valid ? "CLOCK_REQUIRED" :
        now < status->not_before ? "NOT_YET_VALID" : now >= status->not_after ? "EXPIRED" : "READY";
    xSemaphoreGive(s_lock);
}

esp_err_t keyvalet_tls_prepare(const char *hostname,
    const char origins[KEYVALET_TLS_ORIGIN_COUNT][KEYVALET_TLS_ORIGIN_MAX], bool rotate_key)
{
    if (!s_lock || !keyvalet_tls_hostname_valid(hostname) || !origins || !origins[0][0]) return ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; i < KEYVALET_TLS_ORIGIN_COUNT; ++i)
        if (strnlen(origins[i], KEYVALET_TLS_ORIGIN_MAX) == KEYVALET_TLS_ORIGIN_MAX ||
            (origins[i][0] && !keyvalet_tls_origin_valid(origins[i]))) return ESP_ERR_INVALID_ARG;
    /* RF must be active for the ESP32-C6's guaranteed entropy source. */
    keyvalet_network_status_t network;
    if (keyvalet_network_status_get(&network) != ESP_OK || !network.connected) return ESP_ERR_INVALID_STATE;
    keyvalet_tls_credentials_t *record = calloc(1, sizeof(*record));
    if (!record) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t result = !s_status.writable || s_generation == UINT32_MAX ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (result == ESP_OK && !rotate_key) {
        if (s_candidate >= 0) result = read_slot(s_candidate, record);
        else if (s_active >= 0) result = read_slot(s_active, record);
    }
    if (result == ESP_OK && (rotate_key || !record->key_length)) {
        size_t length = 0;
        result = keyvalet_tls_key_generate(record->key, &length);
        record->key_length = (uint32_t)length;
    }
    if (result == ESP_OK) {
        record->schema = 1; record->generation = s_generation + 1;
        record->chain_length = 0; memset(record->chain, 0, sizeof(record->chain));
        memset(record->hostname, 0, sizeof(record->hostname)); strcpy(record->hostname, hostname);
        memcpy(record->origins, origins, sizeof(record->origins));
        const int slot = s_active == 0 ? 1 : 0;
        result = write_slot(slot, record);
        if (result == ESP_OK) { s_candidate = slot; s_generation = record->generation; }
    }
    xSemaphoreGive(s_lock); keyvalet_tls_credentials_free(record); return result;
}

esp_err_t keyvalet_tls_install(const char *chain, size_t length)
{
    if (!s_lock || !chain || !length || length > KEYVALET_TLS_CHAIN_MAX || memchr(chain, 0, length)) return ESP_ERR_INVALID_ARG;
    int64_t now;
    if (!keyvalet_time_get_utc(&now)) return ESP_ERR_INVALID_STATE;
    keyvalet_tls_credentials_t *record = calloc(1, sizeof(*record));
    if (!record) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t result = s_candidate < 0 ? ESP_ERR_INVALID_STATE : read_slot(s_candidate, record);
    if (result == ESP_OK) {
        memset(record->chain, 0, sizeof(record->chain)); memcpy(record->chain, chain, length);
        record->chain_length = (uint32_t)length;
        result = keyvalet_tls_material_validate(record->key, record->key_length, record->chain,
            length, record->hostname, now, NULL, NULL);
        if (result == ESP_OK) result = write_slot(s_candidate, record);
    }
    xSemaphoreGive(s_lock); keyvalet_tls_credentials_free(record); return result;
}

esp_err_t keyvalet_tls_activate(void)
{
    /* Caller is the web worker with lock actuation inhibited and old server
     * stopped. Selector commit is the only activation point. */
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    keyvalet_tls_credentials_t *record = calloc(1, sizeof(*record));
    if (!record) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now;
    esp_err_t result = s_candidate < 0 || !keyvalet_time_get_utc(&now) ? ESP_ERR_INVALID_STATE : read_slot(s_candidate, record);
    if (result == ESP_OK) result = keyvalet_tls_material_validate(record->key, record->key_length,
        record->chain, record->chain_length, record->hostname, now, NULL, NULL);
    if (result == ESP_OK) {
        uint8_t selected = (uint8_t)s_candidate;
        result = persist("selected", &selected, sizeof(selected));
        if (result == ESP_OK) { s_active = selected; s_candidate = -1; adopt_metadata(record); }
    }
    xSemaphoreGive(s_lock); keyvalet_tls_credentials_free(record); return result;
}

esp_err_t keyvalet_tls_discard_candidate(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t result = !s_status.writable ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (result == ESP_OK && s_candidate >= 0) {
        result = keyvalet_lock_flash_begin();
        if (result == ESP_OK) {
            result = nvs_erase_key(s_nvs, slots[s_candidate]);
            if (result == ESP_OK) result = nvs_commit(s_nvs);
            keyvalet_lock_flash_end();
            if (result == ESP_OK) s_candidate = -1;
            else s_status.writable = false;
        }
    }
    xSemaphoreGive(s_lock); return result;
}

esp_err_t keyvalet_tls_export(bool candidate, char *csr, size_t capacity, char public_key_hex[131])
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    keyvalet_tls_credentials_t *record = calloc(1, sizeof(*record));
    if (!record) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int slot = candidate ? s_candidate : s_active;
    esp_err_t result = slot < 0 ? ESP_ERR_INVALID_STATE : read_slot(slot, record);
    if (result == ESP_OK) result = keyvalet_tls_public_material(record->key, record->key_length, record->hostname, csr, capacity, public_key_hex);
    xSemaphoreGive(s_lock); keyvalet_tls_credentials_free(record); return result;
}

esp_err_t keyvalet_tls_credentials_load(keyvalet_tls_credentials_t **credentials)
{
    *credentials = NULL;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    keyvalet_tls_credentials_t *record = calloc(1, sizeof(*record));
    if (!record) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t result = s_active < 0 ? ESP_ERR_INVALID_STATE : read_slot(s_active, record);
    xSemaphoreGive(s_lock);
    if (result != ESP_OK) keyvalet_tls_credentials_free(record);
    else *credentials = record;
    return result;
}

const char *keyvalet_tls_allowed_origin(const char *origin)
{
    if (!s_lock || !origin || !strcmp(origin, "null")) return NULL;
    const char *allowed = NULL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (unsigned i = 0; s_status.active && i < KEYVALET_TLS_ORIGIN_COUNT; ++i)
        if (s_status.origins[i][0] && !strcmp(origin, s_status.origins[i])) allowed = s_status.origins[i];
    xSemaphoreGive(s_lock); return allowed;
}
