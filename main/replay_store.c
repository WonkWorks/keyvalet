#include "replay_store.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "replay_record.h"

#define REPLAY_NAMESPACE "replay"
#define REPLAY_META_KEY "meta"
#define REPLAY_META_MAGIC 0x4b56524dU
#define REPLAY_META_VERSION 2U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t crc32;
} replay_meta_t;

_Static_assert(sizeof(replay_meta_t) == 12, "unexpected replay marker size");
_Static_assert(offsetof(replay_meta_t, crc32) == 8,
               "unexpected replay metadata layout");

static const char *TAG = "replay_store";
static nvs_handle_t s_nvs;
static uint64_t s_highest_sequence;
static bool s_ready;

static void slot_key(size_t slot, char key[8])
{
    snprintf(key, 8, "r%03u", (unsigned)slot);
}

static bool meta_valid(const replay_meta_t *meta)
{
    return meta->magic == REPLAY_META_MAGIC &&
           meta->version == REPLAY_META_VERSION && meta->size == sizeof(*meta) &&
           meta->crc32 == keyvalet_replay_crc32(meta, offsetof(replay_meta_t, crc32));
}

static esp_err_t store_meta(void)
{
    replay_meta_t meta = {
        .magic = REPLAY_META_MAGIC,
        .version = REPLAY_META_VERSION,
        .size = sizeof(replay_meta_t),
    };
    meta.crc32 = keyvalet_replay_crc32(&meta, offsetof(replay_meta_t, crc32));
    esp_err_t result = nvs_set_blob(s_nvs, REPLAY_META_KEY, &meta, sizeof(meta));
    return result == ESP_OK ? nvs_commit(s_nvs) : result;
}

static esp_err_t load_records(keyvalet_replay_window_t *window, uint64_t *highest)
{
    keyvalet_replay_record_t *records = calloc(KEYVALET_REPLAY_CAPACITY,
                                                sizeof(*records));
    bool *present = calloc(KEYVALET_REPLAY_CAPACITY, sizeof(*present));
    if (records == NULL || present == NULL) {
        free(present);
        free(records);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t final_result = ESP_OK;
    for (size_t slot = 0; slot < KEYVALET_REPLAY_CAPACITY; ++slot) {
        char key[8];
        slot_key(slot, key);
        size_t size = sizeof(records[slot]);
        const esp_err_t result = nvs_get_blob(s_nvs, key, &records[slot], &size);
        if (result == ESP_ERR_NVS_NOT_FOUND) continue;
        if (result != ESP_OK || size != sizeof(records[slot])) {
            final_result = result == ESP_OK ? ESP_ERR_INVALID_SIZE : result;
            goto cleanup;
        }
        if (records[slot].magic == KEYVALET_REPLAY_RECORD_MAGIC &&
            records[slot].version != KEYVALET_REPLAY_RECORD_VERSION) {
            final_result = ESP_ERR_NOT_SUPPORTED;
            goto cleanup;
        }
        present[slot] = true;
    }
    final_result = keyvalet_replay_records_recover(
        records, present, window, highest) ? ESP_OK : ESP_ERR_INVALID_CRC;
cleanup:
    free(present);
    free(records);
    return final_result;
}

esp_err_t keyvalet_replay_store_init(
    bool enrolled, keyvalet_replay_window_t *recovered_window)
{
    if (s_ready || recovered_window == NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t result = nvs_flash_init_partition(KEYVALET_REPLAY_PARTITION);
    if (result != ESP_OK) return result;
    result = nvs_open_from_partition(KEYVALET_REPLAY_PARTITION, REPLAY_NAMESPACE,
                                     NVS_READWRITE, &s_nvs);
    if (result != ESP_OK) return result;

    replay_meta_t meta;
    size_t size = sizeof(meta);
    result = nvs_get_blob(s_nvs, REPLAY_META_KEY, &meta, &size);
    if (result == ESP_OK) {
        if (size != sizeof(meta)) return ESP_ERR_INVALID_SIZE;
        if (meta.magic != REPLAY_META_MAGIC) return ESP_ERR_INVALID_CRC;
        if (meta.version != REPLAY_META_VERSION) return ESP_ERR_NOT_SUPPORTED;
        if (!meta_valid(&meta)) return ESP_ERR_INVALID_CRC;
        result = load_records(recovered_window, &s_highest_sequence);
        if (result != ESP_OK) return result;
    } else if (result == ESP_ERR_NVS_NOT_FOUND && !enrolled) {
        /* A missing marker is only a virgin store when there are no records.
         * Never bless orphaned state or reset replay protection implicitly. */
        result = load_records(recovered_window, &s_highest_sequence);
        if (result != ESP_OK) return result;
        if (s_highest_sequence != 0) return ESP_ERR_INVALID_STATE;
        result = store_meta();
        if (result != ESP_OK) return result;
        ESP_LOGI(TAG, "Initialized empty durable replay ring before enrollment");
    } else if (result == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Enrolled device has no replay marker; failing closed");
        return ESP_ERR_INVALID_STATE;
    } else {
        return result;
    }

    s_ready = true;
    ESP_LOGI(TAG, "Replay ring ready: %u records, highest sequence=%" PRIu64,
             (unsigned)recovered_window->count, s_highest_sequence);
    return ESP_OK;
}

esp_err_t keyvalet_replay_store_append(
    const uint8_t operation_digest[KEYVALET_REPLAY_DIGEST_SIZE],
    const uint8_t nonce_digest[KEYVALET_REPLAY_DIGEST_SIZE], int64_t accepted_utc)
{
    if (!s_ready || operation_digest == NULL || nonce_digest == NULL ||
        accepted_utc <= 0) return ESP_ERR_INVALID_STATE;
    if (s_highest_sequence == UINT64_MAX) {
        s_ready = false;
        return ESP_ERR_INVALID_STATE;
    }
    const uint64_t sequence = s_highest_sequence + 1U;
    keyvalet_replay_record_t record;
    keyvalet_replay_record_create(&record, sequence, accepted_utc,
                                  operation_digest, nonce_digest);
    char key[8];
    slot_key(keyvalet_replay_record_slot(sequence), key);
    esp_err_t result = nvs_set_blob(s_nvs, key, &record, sizeof(record));
    if (result == ESP_OK) result = nvs_commit(s_nvs);
    if (result != ESP_OK) {
        s_ready = false;
        ESP_LOGE(TAG, "Replay record persistence failed; control disabled until reboot: %s",
                 esp_err_to_name(result));
        return result;
    }
    s_highest_sequence = sequence;
    return ESP_OK;
}
