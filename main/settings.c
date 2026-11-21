#include "settings.h"

#include <stddef.h>
#include <string.h>

#include "esp_crc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "settings_schema.h"

#define SETTINGS_MAGIC 0x4b565354U
#define SETTINGS_NAMESPACE "keyvalet"
#define SETTINGS_KEY "settings"

typedef struct {
    uint32_t magic;
    uint32_t schema_version;
    uint32_t revision;
    uint32_t default_door_open_timeout_s;
    uint32_t lock_sensor_required_bits;
    uint8_t sensor_monitoring_enabled[KEYVALET_SENSOR_COUNT];
    uint32_t crc32;
} settings_blob_t;

_Static_assert(sizeof(settings_blob_t) == 28, "unexpected settings layout");
_Static_assert(offsetof(settings_blob_t, crc32) == 24, "unexpected settings CRC");

static const char *TAG = "settings";
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static keyvalet_settings_t s_settings;
static bool s_storage_valid;

static uint32_t blob_crc(const settings_blob_t *blob)
{
    return esp_crc32_le(0, (const uint8_t *)blob,
                        offsetof(settings_blob_t, crc32));
}

bool keyvalet_settings_validate(const keyvalet_settings_t *settings)
{
    return settings != NULL && keyvalet_settings_values_valid(
        settings->schema_version, settings->default_door_open_timeout_s);
}

static void use_defaults(void)
{
    s_settings = (keyvalet_settings_t) {
        .schema_version = KEYVALET_SETTINGS_SCHEMA_VERSION,
        .revision = 1,
        .default_door_open_timeout_s = 90,
        .lock_sensor_required = {true, true},
        .sensor_monitoring_enabled = {true, false, true, true},
    };
}

static esp_err_t store_locked(const keyvalet_settings_t *settings)
{
    settings_blob_t blob = {
        .magic = SETTINGS_MAGIC,
        .schema_version = settings->schema_version,
        .revision = settings->revision,
        .default_door_open_timeout_s = settings->default_door_open_timeout_s,
    };
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index)
        if (settings->lock_sensor_required[index])
            blob.lock_sensor_required_bits |= 1U << index;
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index) {
        blob.sensor_monitoring_enabled[index] =
            settings->sensor_monitoring_enabled[index] ? 1U : 0U;
    }
    blob.crc32 = blob_crc(&blob);
    esp_err_t result = nvs_set_blob(s_nvs, SETTINGS_KEY, &blob, sizeof(blob));
    if (result == ESP_OK) {
        result = nvs_commit(s_nvs);
    }
    return result;
}

esp_err_t keyvalet_settings_init(void)
{
    if (s_lock != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    use_defaults();
    esp_err_t result = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (result != ESP_OK) {
        return result;
    }
    size_t size = 0;
    result = nvs_get_blob(s_nvs, SETTINGS_KEY, NULL, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = store_locked(&s_settings);
        if (result == ESP_OK) {
            s_storage_valid = true;
            ESP_LOGI(TAG, "Stored default settings revision 1");
        }
        return result;
    }
    if (result != ESP_OK || size != sizeof(settings_blob_t)) {
        ESP_LOGE(TAG, "Settings storage is corrupt or unsupported; failing closed");
        use_defaults();
        return ESP_ERR_INVALID_CRC;
    }
    settings_blob_t blob;
    result = nvs_get_blob(s_nvs, SETTINGS_KEY, &blob, &size);
    if (result != ESP_OK) return result;
    bool bools_valid = true;
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index)
        bools_valid = bools_valid && blob.sensor_monitoring_enabled[index] <= 1U;
    if (blob.magic != SETTINGS_MAGIC || blob.crc32 != blob_crc(&blob) ||
        !bools_valid ||
        (blob.lock_sensor_required_bits >> KEYVALET_LOCK_COUNT) != 0 ||
        blob.schema_version != KEYVALET_SETTINGS_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Settings storage is corrupt or unsupported; failing closed");
        return ESP_ERR_INVALID_CRC;
    }
    s_settings = (keyvalet_settings_t) {
        .schema_version = blob.schema_version,
        .revision = blob.revision,
        .default_door_open_timeout_s = blob.default_door_open_timeout_s,
    };
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index)
        s_settings.lock_sensor_required[index] =
            (blob.lock_sensor_required_bits & (1U << index)) != 0;
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index)
        s_settings.sensor_monitoring_enabled[index] =
            blob.sensor_monitoring_enabled[index] != 0;
    if (!keyvalet_settings_validate(&s_settings)) {
        ESP_LOGE(TAG, "Settings values are unsupported; failing closed");
        use_defaults();
        return ESP_ERR_INVALID_ARG;
    }
    s_storage_valid = true;
    return ESP_OK;
}

esp_err_t keyvalet_settings_get(keyvalet_settings_t *settings)
{
    if (settings == NULL || s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *settings = s_settings;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t keyvalet_settings_update(const keyvalet_settings_t *settings,
                                   uint32_t expected_revision)
{
    if (s_lock == NULL || !s_storage_valid || !keyvalet_settings_validate(settings)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!keyvalet_settings_revision_matches(s_settings.revision, expected_revision)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    keyvalet_settings_t updated = *settings;
    updated.revision = expected_revision + 1U;
    const esp_err_t result = store_locked(&updated);
    if (result == ESP_OK) {
        s_settings = updated;
    } else s_storage_valid = false; /* Ambiguous commit: reload before another CAS. */
    xSemaphoreGive(s_lock);
    return result;
}
