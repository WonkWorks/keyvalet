#include "journal_ring.h"

#include <string.h>

#define META_MAGIC 0x4b564a52U
#define META_VERSION 1U
#define META_PREPARED 1U
#define META_READY 2U

static size_t slot_for(const keyvalet_journal_ring_t *ring, uint64_t sequence)
{
    return (size_t)((sequence - 1U) %
        (ring->sector_count * KEYVALET_JOURNAL_SLOTS_PER_SECTOR));
}

static uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static void put64(uint8_t *data, uint64_t value)
{
    for (unsigned index = 0; index < 8; ++index)
        data[index] = (uint8_t)(value >> (index * 8U));
}

static uint64_t get64(const uint8_t *data)
{
    uint64_t result = 0;
    for (unsigned index = 0; index < 8; ++index)
        result |= (uint64_t)data[index] << (index * 8U);
    return result;
}

static bool save_meta(keyvalet_journal_ring_t *ring, unsigned phase)
{
    uint8_t data[KEYVALET_JOURNAL_RING_META_SIZE] = {0};
    put64(data, META_MAGIC);
    put64(data + 8, META_VERSION | ((uint64_t)phase << 32));
    put64(data + 16, ring->active_base);
    put64(data + 24, ring->sector_count);
    put64(data + 32, crc32(data, 32));
    return ring->storage.save_meta(ring->storage.context, data, sizeof(data));
}

static bool bit_valid(const keyvalet_journal_ring_t *ring, size_t slot)
{
    return (ring->valid[slot / 64U] & (UINT64_C(1) << (slot % 64U))) != 0;
}

bool keyvalet_journal_ring_contains(const keyvalet_journal_ring_t *ring,
    uint64_t sequence)
{
    return sequence > ring->retired_through && sequence < ring->next_sequence &&
           bit_valid(ring, slot_for(ring, sequence));
}

static void bounds(keyvalet_journal_ring_t *ring)
{
    ring->oldest_sequence = 0;
    ring->latest_sequence = 0;
    ring->valid_count = 0;
    for (size_t sector = 0; sector < ring->sector_count; ++sector)
        ring->valid_count += (size_t)__builtin_popcountll(ring->valid[sector]);
    if (ring->valid_count == 0) return;
    for (uint64_t seq = ring->retired_through + 1U; seq < ring->next_sequence; ++seq)
        if (keyvalet_journal_ring_contains(ring, seq)) {
            ring->oldest_sequence = seq;
            break;
        }
    for (uint64_t seq = ring->next_sequence - 1U; seq > ring->retired_through; --seq)
        if (keyvalet_journal_ring_contains(ring, seq)) {
            ring->latest_sequence = seq;
            break;
        }
}

static void retire_sector(keyvalet_journal_ring_t *ring)
{
    const uint64_t capacity = ring->sector_count * 64U;
    ring->retired_through = ring->active_base > capacity
        ? ring->active_base - capacity + 63U : 0;
    ring->valid[slot_for(ring, ring->active_base) / 64U] = 0;
    bounds(ring);
}

/* PREPARED is durable before erase. READY is durable before any new body write.
 * An interrupted PREPARED erase is repeated; stale old commits are ignored. */
static bool prepare_sector(keyvalet_journal_ring_t *ring)
{
    ring->active_base = ring->next_sequence;
    if (!save_meta(ring, META_PREPARED)) return false;
    retire_sector(ring);
    const size_t sector = slot_for(ring, ring->active_base) / 64U;
    if (!ring->storage.erase_sector(ring->storage.context, sector)) return false;
    uint8_t raw[KEYVALET_JOURNAL_RECORD_SIZE];
    for (size_t within = 0; within < 64U; ++within) {
        if (!ring->storage.read(ring->storage.context,
                sector * 4096U + within * 64U, raw, sizeof(raw)) ||
            !keyvalet_journal_slot_is_erased(raw)) return false;
    }
    if (!save_meta(ring, META_READY)) return false;
    ring->ready = true;
    return true;
}

bool keyvalet_journal_ring_recover(keyvalet_journal_ring_t *ring,
    const keyvalet_journal_storage_t *storage, size_t sector_count,
    uint8_t scratch[KEYVALET_JOURNAL_SECTOR_SIZE])
{
    if (sector_count < 2 || sector_count > KEYVALET_JOURNAL_MAX_SECTORS)
        return false;
    memset(ring, 0, sizeof(*ring));
    ring->storage = *storage;
    ring->sector_count = sector_count;
    uint8_t meta[KEYVALET_JOURNAL_RING_META_SIZE];
    bool present;
    if (!storage->load_meta(storage->context, meta, sizeof(meta), &present))
        goto fail;
    unsigned phase = 0;
    if (present) {
        phase = (unsigned)(get64(meta + 8) >> 32);
        ring->active_base = get64(meta + 16);
        if (get64(meta) != META_MAGIC || (uint32_t)get64(meta + 8) != META_VERSION ||
            (phase != META_READY && phase != META_PREPARED) ||
            get64(meta + 24) != sector_count ||
            get64(meta + 32) != crc32(meta, 32) ||
            ring->active_base == 0 || (ring->active_base - 1U) % 64U != 0 ||
            ring->active_base > UINT64_MAX - 64U) goto fail;
        const uint64_t capacity = sector_count * 64U;
        ring->retired_through = ring->active_base > capacity
            ? ring->active_base - capacity + 63U : 0;
        ring->next_sequence = ring->active_base;
    } else {
        ring->next_sequence = 1;
    }
    const size_t active_sector = present
        ? slot_for(ring, ring->active_base) / 64U : SIZE_MAX;
    for (size_t sector = 0; sector < sector_count; ++sector) {
        if (!storage->read(storage->context, sector * 4096U, scratch, 4096U))
            goto fail;
        for (size_t within = 0; within < 64U; ++within) {
            const size_t slot = sector * 64U + within;
            const uint8_t *raw = scratch + within * 64U;
            const bool occupied = !keyvalet_journal_slot_is_erased(raw);
            /* Missing metadata is acceptable only for entirely blank storage.
             * Do not adopt development logs or reinterpret a lost checkpoint. */
            if (!present && occupied) goto fail;
            if (present && sector == active_sector && phase == META_PREPARED)
                continue;
            if (present && sector == active_sector && occupied)
                ring->next_sequence = ring->active_base + within + 1U;
            keyvalet_journal_record_t record;
            if (!keyvalet_journal_record_decode(raw, &record)) continue;
            if (record.sequence == 0 || slot_for(ring, record.sequence) != slot)
                goto fail;
            if (present && (record.sequence <= ring->retired_through ||
                record.sequence >= ring->active_base + 64U)) goto fail;
            ring->valid[sector] |= UINT64_C(1) << within;
        }
    }
    bounds(ring);
    if (present) {
        if (phase == META_PREPARED) {
            /* Startup has already forced both lock outputs LOW. */
            if (!prepare_sector(ring)) goto fail;
        } else ring->ready = true;
    } else {
        /* Establish the same durable transaction used for every later sector.
         * A cut before PREPARED leaves blank storage; after it recovery retries
         * the erase. No record may precede a durable READY checkpoint. */
        if (!prepare_sector(ring)) goto fail;
    }
    return true;
fail:
    ring->failed = true;
    return false;
}

bool keyvalet_journal_ring_append(keyvalet_journal_ring_t *ring,
    keyvalet_journal_record_t *record)
{
    if (ring->failed || ring->next_sequence > UINT64_MAX - 64U) return false;
    if (!ring->ready || ring->next_sequence >= ring->active_base + 64U) {
        ring->ready = false;
        if (!prepare_sector(ring)) {
            ring->failed = true;
            return false;
        }
    }
    record->sequence = ring->next_sequence++;
    const size_t slot = slot_for(ring, record->sequence);
    const size_t offset = slot * KEYVALET_JOURNAL_RECORD_SIZE;
    uint8_t raw[64], verify[64];
    keyvalet_journal_record_encode(raw, record, true);
    /* Preserve body/CRC-before-commit and verify both stages. */
    if (!ring->storage.write(ring->storage.context, offset, raw, 60U) ||
        !ring->storage.read(ring->storage.context, offset, verify, 60U) ||
        memcmp(raw, verify, 60U) != 0 ||
        !ring->storage.write(ring->storage.context, offset + 60U, raw + 60U, 4U) ||
        !ring->storage.read(ring->storage.context, offset, verify, 64U) ||
        memcmp(raw, verify, 64U) != 0) {
        /* A persistence error is ambiguous: require reboot before more writes. */
        ring->failed = true;
        return false;
    }
    ring->valid[slot / 64U] |= UINT64_C(1) << (slot % 64U);
    ++ring->valid_count;
    if (ring->oldest_sequence == 0) ring->oldest_sequence = record->sequence;
    ring->latest_sequence = record->sequence;
    return true;
}

bool keyvalet_journal_ring_read(keyvalet_journal_ring_t *ring,
    uint64_t sequence, keyvalet_journal_record_t *record)
{
    if (!keyvalet_journal_ring_contains(ring, sequence)) return false;
    uint8_t raw[64];
    if (!ring->storage.read(ring->storage.context, slot_for(ring, sequence) * 64U,
                           raw, sizeof(raw))) return false;
    if (!keyvalet_journal_record_decode(raw, record) || record->sequence != sequence) {
        const size_t slot = slot_for(ring, sequence);
        ring->valid[slot / 64U] &= ~(UINT64_C(1) << (slot % 64U));
        bounds(ring);
        return false;
    }
    return true;
}

/* Skip complete bitmap words without flash reads or per-sequence 64-bit modulo
 * across an intact/full journal. Worst case is bounded by 640 bitmap words. */
static uint64_t advance_run(const keyvalet_journal_ring_t *ring,
                           uint64_t sequence, bool valid)
{
    while (sequence < ring->next_sequence) {
        const size_t slot = slot_for(ring, sequence);
        const unsigned bit = (unsigned)(slot % 64U);
        const uint64_t mask = UINT64_MAX << bit;
        const uint64_t bits = ring->valid[slot / 64U];
        if ((bits & mask) == (valid ? mask : 0)) {
            const uint64_t next = sequence + 64U - bit;
            sequence = next < ring->next_sequence ? next : ring->next_sequence;
        } else {
            const uint64_t different = (valid ? ~bits : bits) & mask;
            return sequence + (unsigned)__builtin_ctzll(different) - bit;
        }
    }
    return sequence;
}

bool keyvalet_journal_ring_first_gap(const keyvalet_journal_ring_t *ring,
    uint64_t from, uint64_t *start, uint64_t *end)
{
    *start = *end = 0;
    if (from == 0) from = 1;
    if (from >= ring->next_sequence) return false;
    if (from <= ring->retired_through) {
        *start = from;
        *end = ring->retired_through;
        *end = advance_run(ring, *end + 1U, false) - 1U;
        return true;
    }
    const uint64_t missing = advance_run(ring, from, true);
    if (missing < ring->next_sequence) {
        *start = missing;
        *end = advance_run(ring, missing, false) - 1U;
    }
    return *start != 0;
}

bool keyvalet_journal_ring_batch(keyvalet_journal_ring_t *ring,
    uint64_t from, keyvalet_journal_record_t *records, size_t capacity,
    size_t *count, uint64_t *next, bool *more,
    uint64_t *gap_start, uint64_t *gap_end)
{
    if (from == 0 || capacity == 0) return false;
    if (ring->failed) return false;
    uint64_t seq = from;
    *count = 0;
    *gap_start = *gap_end = 0;
    if (seq <= ring->retired_through) {
        *gap_start = seq;
        *gap_end = ring->retired_through;
        *gap_end = advance_run(ring, *gap_end + 1U, false) - 1U;
        seq = *gap_end + 1U;
    } else {
        for (size_t scanned = 0;
             seq < ring->next_sequence && *count < capacity &&
             scanned < KEYVALET_JOURNAL_BATCH_SCAN_LIMIT; ++scanned) {
            if (keyvalet_journal_ring_contains(ring, seq)) {
                if (*gap_start != 0) break;
                if (!keyvalet_journal_ring_read(ring, seq, &records[*count]))
                    return false;
                ++*count;
            } else {
                if (*gap_start == 0) *gap_start = seq;
                *gap_end = seq;
            }
            ++seq;
        }
    }
    *next = seq;
    *more = seq < ring->next_sequence;
    return true;
}
