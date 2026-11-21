#pragma once

#include "journal_format.h"

/* Geometry is fixed by the existing v0.4.2 partition, not by stored records. */
#define KEYVALET_JOURNAL_SECTOR_SIZE 4096U
#define KEYVALET_JOURNAL_SLOTS_PER_SECTOR 64U
#define KEYVALET_JOURNAL_MAX_SECTORS 640U
#define KEYVALET_JOURNAL_RING_META_SIZE 40U
#define KEYVALET_JOURNAL_BATCH_SCAN_LIMIT 16U

/* The same engine runs against ESP flash/NVS and fault-injecting host storage. */
typedef struct {
    void *context;
    bool (*read)(void *context, size_t offset, void *data, size_t length);
    bool (*write)(void *context, size_t offset, const void *data, size_t length);
    bool (*erase_sector)(void *context, size_t sector);
    bool (*load_meta)(void *context, uint8_t *data, size_t length, bool *present);
    bool (*save_meta)(void *context, const uint8_t *data, size_t length);
} keyvalet_journal_storage_t;

typedef struct {
    keyvalet_journal_storage_t storage;
    uint64_t valid[KEYVALET_JOURNAL_MAX_SECTORS];
    size_t sector_count;
    uint64_t active_base;
    uint64_t next_sequence;
    uint64_t retired_through;
    uint64_t oldest_sequence;
    uint64_t latest_sequence;
    size_t valid_count;
    bool ready;
    bool failed;
} keyvalet_journal_ring_t;

bool keyvalet_journal_ring_recover(keyvalet_journal_ring_t *ring,
    const keyvalet_journal_storage_t *storage, size_t sector_count,
    uint8_t scratch[KEYVALET_JOURNAL_SECTOR_SIZE]);
bool keyvalet_journal_ring_append(keyvalet_journal_ring_t *ring,
    keyvalet_journal_record_t *record);
bool keyvalet_journal_ring_read(keyvalet_journal_ring_t *ring,
    uint64_t sequence, keyvalet_journal_record_t *record);
bool keyvalet_journal_ring_contains(const keyvalet_journal_ring_t *ring,
    uint64_t sequence);
bool keyvalet_journal_ring_first_gap(const keyvalet_journal_ring_t *ring,
    uint64_t from, uint64_t *start, uint64_t *end);
bool keyvalet_journal_ring_batch(keyvalet_journal_ring_t *ring,
    uint64_t from, keyvalet_journal_record_t *records, size_t capacity,
    size_t *count, uint64_t *next, bool *more,
    uint64_t *gap_start, uint64_t *gap_end);
