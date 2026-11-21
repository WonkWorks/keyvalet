#include "journal.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hardware_io.h"
#include "journal_ring.h"
#include "journal_sync_contract.h"
#include "nvs.h"
#include "time_sync.h"

#define JOURNAL_PARTITION_SUBTYPE ((esp_partition_subtype_t)0x40)
#define HEARTBEAT_INTERVAL_MS (60ULL * 60ULL * 1000ULL)

static const char *TAG = "journal";
static const esp_partition_t *s_partition;
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_metadata_nvs;
static keyvalet_journal_ring_t s_ring;
static bool s_heartbeat_started;
static uint64_t s_max_append_us, s_max_erase_us;
static keyvalet_journal_commit_listener_t s_commit_listener;
static void *s_commit_listener_context;

static uint64_t uptime_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static bool storage_read(void *context, size_t offset, void *data, size_t length)
{
    return esp_partition_read(context, offset, data, length) == ESP_OK;
}

static bool storage_write(void *context, size_t offset, const void *data, size_t length)
{
    return esp_partition_write(context, offset, data, length) == ESP_OK;
}

static bool storage_erase_sector(void *context, size_t sector)
{
    const int64_t start = esp_timer_get_time();
    /* Exactly one 4 KiB sector: never trigger the driver's 64 KiB block erase. */
    const esp_err_t result = esp_partition_erase_range(
        context, sector * KEYVALET_JOURNAL_SECTOR_SIZE, KEYVALET_JOURNAL_SECTOR_SIZE);
    const uint64_t elapsed = (uint64_t)(esp_timer_get_time() - start);
    if (elapsed > s_max_erase_us) s_max_erase_us = elapsed;
    return result == ESP_OK;
}

static bool storage_load_meta(void *context, uint8_t *data, size_t length, bool *present)
{
    (void)context;
    size_t size = length;
    const esp_err_t result = nvs_get_blob(s_metadata_nvs, "ring_v1", data, &size);
    *present = result != ESP_ERR_NVS_NOT_FOUND;
    return result == ESP_ERR_NVS_NOT_FOUND || (result == ESP_OK && size == length);
}

static bool storage_save_meta(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    esp_err_t result = nvs_set_blob(s_metadata_nvs, "ring_v1", data, length);
    if (result == ESP_OK) result = nvs_commit(s_metadata_nvs);
    return result == ESP_OK;
}

esp_err_t keyvalet_journal_init(void)
{
    if (s_partition != NULL) return ESP_ERR_INVALID_STATE;
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, JOURNAL_PARTITION_SUBTYPE, "journal");
    if (partition == NULL || partition->size % KEYVALET_JOURNAL_SECTOR_SIZE != 0)
        return ESP_ERR_NOT_FOUND;
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    esp_err_t result = nvs_open("keyvalet_jrnl", NVS_READWRITE, &s_metadata_nvs);
    if (result != ESP_OK) return result;
    uint8_t *scratch = malloc(KEYVALET_JOURNAL_SECTOR_SIZE);
    if (scratch == NULL) return ESP_ERR_NO_MEM;
    const keyvalet_journal_storage_t storage = {
        .context = (void *)partition,
        .read = storage_read,
        .write = storage_write,
        .erase_sector = storage_erase_sector,
        .load_meta = storage_load_meta,
        .save_meta = storage_save_meta,
    };
    /* Startup has already initialized both outputs LOW. No network/task owner
     * is allowed to append until recovery (including pending erase) completes. */
    const bool recovered = keyvalet_journal_ring_recover(
        &s_ring, &storage, partition->size / KEYVALET_JOURNAL_SECTOR_SIZE, scratch);
    free(scratch);
    if (!recovered) {
        ESP_LOGE(TAG, "Journal recovery/metadata invalid; storage left fail-closed");
        return ESP_ERR_INVALID_STATE;
    }
    s_partition = partition;
    ESP_LOGI(TAG, "Circular journal recovered: capacity=%u retained=%u "
             "oldest=%" PRIu64 " latest=%" PRIu64 " produced=%" PRIu64,
             (unsigned)(s_ring.sector_count * 64U), (unsigned)s_ring.valid_count,
             s_ring.oldest_sequence, s_ring.latest_sequence, s_ring.next_sequence - 1U);
    return ESP_OK;
}

esp_err_t keyvalet_journal_append(keyvalet_event_type_t type, uint16_t flags,
                                  uint32_t detail)
{
    if (s_partition == NULL || s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* Lock order: journal -> output gate. The gate waits with the output mutex
     * released while a pulse is ACTIVE. Pulse callbacks never acquire journal.
     * Hold it through NVS checkpoints, erase and commit, excluding pulse start. */
    esp_err_t result = keyvalet_lock_flash_begin();
    if (result != ESP_OK) {
        xSemaphoreGive(s_lock);
        return result;
    }
    const int64_t start = esp_timer_get_time();
    keyvalet_journal_record_t record = {
        .uptime_ms = uptime_ms(),
        .type = type,
        .flags = flags & ~KEYVALET_EVENT_FLAG_WALL_TIME_VALID,
        .detail = detail,
    };
    int64_t utc;
    if (keyvalet_time_get_utc(&utc)) {
        record.wall_time_s = utc;
        record.flags |= KEYVALET_EVENT_FLAG_WALL_TIME_VALID;
    }
    const bool committed = keyvalet_journal_ring_append(&s_ring, &record);
    const uint64_t elapsed = (uint64_t)(esp_timer_get_time() - start);
    if (elapsed > s_max_append_us) s_max_append_us = elapsed;
    keyvalet_lock_flash_end();
    keyvalet_journal_commit_listener_t listener = s_commit_listener;
    void *listener_context = s_commit_listener_context;
    xSemaphoreGive(s_lock);
    if (!committed) {
        ESP_LOGE(TAG, "Journal append failed; reboot recovery required");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Recorded seq=%" PRIu64 " type=%s uptime_ms=%" PRIu64,
             record.sequence, keyvalet_event_type_name(type), record.uptime_ms);
    if (listener != NULL) listener(&record, listener_context);
    return ESP_OK;
}

static void stats_locked(keyvalet_journal_stats_t *stats)
{
    const size_t capacity = s_ring.sector_count * 64U;
    const size_t retained_slots = (size_t)(s_ring.next_sequence - 1U -
                                         s_ring.retired_through);
    uint64_t gap_start, gap_end;
    size_t gaps = 0;
    uint64_t cursor = s_ring.retired_through + 1U;
    while (keyvalet_journal_ring_first_gap(&s_ring, cursor, &gap_start, &gap_end)) {
        ++gaps;
        cursor = gap_end + 1U;
    }
    *stats = (keyvalet_journal_stats_t) {
        .format_version = KEYVALET_JOURNAL_FORMAT_VERSION,
        .partition_size = s_partition->size,
        .record_capacity = capacity,
        .valid_record_count = s_ring.valid_count,
        .corrupt_record_count = retained_slots - s_ring.valid_count,
        .gap_count = gaps,
        .used_bytes = retained_slots * 64U,
        .available_bytes = (capacity - retained_slots) * 64U,
        .oldest_sequence = s_ring.oldest_sequence,
        .latest_sequence = s_ring.latest_sequence,
        .produced_sequence = s_ring.next_sequence - 1U,
        .retired_through_sequence = s_ring.retired_through,
        .max_append_us = s_max_append_us,
        .max_erase_us = s_max_erase_us,
        .health = s_ring.failed ? KEYVALET_JOURNAL_UNAVAILABLE :
                  gaps ? KEYVALET_JOURNAL_DEGRADED : KEYVALET_JOURNAL_HEALTHY,
    };
}

esp_err_t keyvalet_journal_get_stats(keyvalet_journal_stats_t *stats)
{
    if (stats == NULL) return ESP_ERR_INVALID_ARG;
    if (s_partition == NULL || s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    stats_locked(stats);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t keyvalet_journal_get_sync_view(uint64_t from_sequence,
    keyvalet_journal_stats_t *stats, uint64_t *gap_start, uint64_t *gap_end)
{
    if (from_sequence == 0 || stats == NULL || gap_start == NULL || gap_end == NULL)
        return ESP_ERR_INVALID_ARG;
    if (s_partition == NULL || s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    stats_locked(stats);
    (void)keyvalet_journal_ring_first_gap(&s_ring, from_sequence, gap_start, gap_end);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t keyvalet_journal_read_batch(
    uint64_t start_sequence, keyvalet_journal_record_t *records,
    size_t capacity, size_t *record_count, uint64_t *next_sequence,
    bool *more, uint64_t *gap_start_sequence, uint64_t *gap_end_sequence,
    keyvalet_journal_stats_t *snapshot)
{
    if (records == NULL || capacity == 0 || capacity > KEYVALET_JOURNAL_SYNC_MAX_BATCH ||
        record_count == NULL || next_sequence == NULL || more == NULL ||
        gap_start_sequence == NULL || gap_end_sequence == NULL || start_sequence == 0 ||
        snapshot == NULL)
        return ESP_ERR_INVALID_ARG;
    if (s_partition == NULL || s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool read = keyvalet_journal_ring_batch(
        &s_ring, start_sequence, records, capacity, record_count,
        next_sequence, more, gap_start_sequence, gap_end_sequence);
    if (read) stats_locked(snapshot);
    xSemaphoreGive(s_lock);
    return read ? ESP_OK : ESP_FAIL;
}

esp_err_t keyvalet_journal_read_recent(keyvalet_journal_record_t *records,
                                       size_t capacity, size_t *record_count)
{
    if (records == NULL || record_count == NULL || capacity == 0)
        return ESP_ERR_INVALID_ARG;
    if (s_partition == NULL || s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t count = 0;
    /* Diagnostics request 20; scan at most 64 slots, even in a corrupt log. */
    uint64_t seq = s_ring.next_sequence;
    for (size_t scanned = 0; seq > s_ring.retired_through + 1U &&
         count < capacity && scanned < 64U; ++scanned) {
        --seq;
        if (keyvalet_journal_ring_contains(&s_ring, seq)) {
            if (!keyvalet_journal_ring_read(&s_ring, seq, &records[count])) {
                xSemaphoreGive(s_lock);
                return ESP_FAIL;
            }
            ++count;
        }
    }
    for (size_t left = 0; left < count / 2; ++left) {
        keyvalet_journal_record_t temporary = records[left];
        records[left] = records[count - left - 1U];
        records[count - left - 1U] = temporary;
    }
    *record_count = count;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t keyvalet_journal_set_commit_listener(
    keyvalet_journal_commit_listener_t listener, void *context)
{
    if (s_partition == NULL || s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_commit_listener = listener;
    s_commit_listener_context = context;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

/* Existing hourly heartbeat policy is preserved; retention adds no new task. */
static void heartbeat_task(void *context)
{
    (void)context;
    uint64_t next_heartbeat_ms = uptime_ms() + HEARTBEAT_INTERVAL_MS;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        const uint64_t now = uptime_ms();
        if (now >= next_heartbeat_ms) {
            esp_err_t result = keyvalet_journal_append(KEYVALET_EVENT_HEARTBEAT, 0, 0);
            if (result != ESP_OK)
                ESP_LOGE(TAG, "Unable to record heartbeat: %s", esp_err_to_name(result));
            next_heartbeat_ms = now + HEARTBEAT_INTERVAL_MS;
        }
    }
}

esp_err_t keyvalet_journal_start_heartbeat(void)
{
    if (s_partition == NULL || s_heartbeat_started) return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(heartbeat_task, "journal_heartbeat", 3072, NULL, 3, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    s_heartbeat_started = true;
    return ESP_OK;
}

const char *keyvalet_journal_health_name(keyvalet_journal_health_t health)
{
    switch (health) {
    case KEYVALET_JOURNAL_HEALTHY: return "HEALTHY";
    case KEYVALET_JOURNAL_DEGRADED: return "DEGRADED";
    default: return "UNAVAILABLE";
    }
}
