#include "journal_sync.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "journal.h"
#include "journal_sync_contract.h"
#include "nvs.h"

#define SYNC_NAMESPACE "keyvalet_sync"
#define SYNC_CURSOR_KEY "cursor_v1"
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static uint64_t s_acknowledged_sequence;
static bool s_ready;

esp_err_t keyvalet_journal_sync_init(void)
{
    if (s_lock != NULL) return ESP_ERR_INVALID_STATE;
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    esp_err_t result = nvs_open(SYNC_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (result != ESP_OK) return result;
    keyvalet_journal_cursor_blob_t blob = {0};
    size_t size = sizeof(blob);
    result = nvs_get_blob(s_nvs, SYNC_CURSOR_KEY, &blob, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        s_acknowledged_sequence = 0;
        result = ESP_OK;
    } else if (result != ESP_OK || size != sizeof(blob) ||
               !keyvalet_journal_cursor_decode(
                   &blob, &s_acknowledged_sequence)) {
        return ESP_ERR_INVALID_CRC;
    }
    s_ready = result == ESP_OK;
    return result;
}

esp_err_t keyvalet_journal_sync_get_status(
    keyvalet_journal_sync_status_t *status)
{
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_ready || s_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint64_t acknowledged = s_acknowledged_sequence;
    const uint64_t first_unacknowledged = acknowledged == UINT64_MAX
        ? UINT64_MAX : acknowledged + 1U;
    keyvalet_journal_stats_t stats;
    uint64_t gap_start, gap_end;
    const esp_err_t result = keyvalet_journal_get_sync_view(
        first_unacknowledged, &stats, &gap_start, &gap_end);
    xSemaphoreGive(s_lock);
    if (result != ESP_OK) return result;
    if (stats.health == KEYVALET_JOURNAL_UNAVAILABLE) return ESP_ERR_INVALID_STATE;
    if (acknowledged > stats.produced_sequence) return ESP_ERR_INVALID_STATE;
    *status = (keyvalet_journal_sync_status_t) {
        .oldest_available_sequence = stats.oldest_sequence,
        .latest_available_sequence = stats.latest_sequence,
        .highest_produced_sequence = stats.produced_sequence,
        .acknowledged_sequence = acknowledged,
        .unsynchronized_records = acknowledged < stats.produced_sequence,
        .unrecoverable_gap = gap_start != 0,
        .gap_start_sequence = gap_start,
        .gap_end_sequence = gap_end,
    };
    return ESP_OK;
}

esp_err_t keyvalet_journal_sync_advance(uint64_t sequence, bool *already_acknowledged)
{
    if (already_acknowledged == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_ready || s_lock == NULL) return ESP_ERR_INVALID_STATE;
    keyvalet_journal_stats_t stats;
    esp_err_t result = keyvalet_journal_get_stats(&stats);
    if (result != ESP_OK) return result;
    if (stats.health == KEYVALET_JOURNAL_UNAVAILABLE) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_acknowledged_sequence > stats.produced_sequence) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const keyvalet_journal_ack_decision_t decision =
        keyvalet_journal_ack_decide(
            s_acknowledged_sequence, stats.produced_sequence, sequence);
    if (decision == KEYVALET_JOURNAL_ACK_BEYOND_LATEST) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    if (decision == KEYVALET_JOURNAL_ACK_ALREADY_ACKNOWLEDGED) {
        *already_acknowledged = true;
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    keyvalet_journal_cursor_blob_t blob;
    keyvalet_journal_cursor_encode(&blob, sequence);
    result = nvs_set_blob(s_nvs, SYNC_CURSOR_KEY, &blob, sizeof(blob));
    if (result == ESP_OK) result = nvs_commit(s_nvs);
    if (result == ESP_OK) s_acknowledged_sequence = sequence;
    else s_ready = false; /* Commit may be durable despite an error response.
                          * Do not overwrite it from an older RAM cursor. */
    *already_acknowledged = false;
    xSemaphoreGive(s_lock);
    return result;
}
