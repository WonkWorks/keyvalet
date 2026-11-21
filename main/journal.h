#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "journal_format.h"

typedef enum {
    KEYVALET_JOURNAL_UNAVAILABLE,
    KEYVALET_JOURNAL_HEALTHY,
    KEYVALET_JOURNAL_DEGRADED,
} keyvalet_journal_health_t;

typedef struct {
    uint32_t format_version;
    size_t partition_size;
    size_t record_capacity;
    size_t valid_record_count;
    size_t corrupt_record_count;
    size_t gap_count;
    size_t used_bytes;
    size_t available_bytes;
    uint64_t oldest_sequence;
    uint64_t latest_sequence;
    uint64_t produced_sequence;
    uint64_t retired_through_sequence;
    uint64_t max_append_us;
    uint64_t max_erase_us;
    keyvalet_journal_health_t health;
} keyvalet_journal_stats_t;

typedef void (*keyvalet_journal_commit_listener_t)(
    const keyvalet_journal_record_t *record, void *context);

esp_err_t keyvalet_journal_init(void);
esp_err_t keyvalet_journal_append(keyvalet_event_type_t type, uint16_t flags,
                                  uint32_t detail);
esp_err_t keyvalet_journal_get_stats(keyvalet_journal_stats_t *stats);
esp_err_t keyvalet_journal_get_sync_view(uint64_t from_sequence,
    keyvalet_journal_stats_t *stats, uint64_t *gap_start, uint64_t *gap_end);
esp_err_t keyvalet_journal_read_recent(keyvalet_journal_record_t *records,
                                       size_t capacity, size_t *record_count);
esp_err_t keyvalet_journal_read_batch(
    uint64_t start_sequence, keyvalet_journal_record_t *records,
    size_t capacity, size_t *record_count, uint64_t *next_sequence,
    bool *more, uint64_t *gap_start_sequence, uint64_t *gap_end_sequence,
    keyvalet_journal_stats_t *snapshot);
esp_err_t keyvalet_journal_set_commit_listener(
    keyvalet_journal_commit_listener_t listener, void *context);
esp_err_t keyvalet_journal_start_heartbeat(void);
const char *keyvalet_journal_health_name(keyvalet_journal_health_t health);
