#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint64_t oldest_available_sequence;
    uint64_t latest_available_sequence;
    uint64_t highest_produced_sequence;
    uint64_t acknowledged_sequence;
    bool unsynchronized_records;
    bool unrecoverable_gap;
    uint64_t gap_start_sequence;
    uint64_t gap_end_sequence;
} keyvalet_journal_sync_status_t;

esp_err_t keyvalet_journal_sync_init(void);
esp_err_t keyvalet_journal_sync_get_status(
    keyvalet_journal_sync_status_t *status);
esp_err_t keyvalet_journal_sync_advance(uint64_t sequence, bool *already_acknowledged);
