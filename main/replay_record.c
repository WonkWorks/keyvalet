#include "replay_record.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(keyvalet_replay_record_t) == KEYVALET_REPLAY_RECORD_SIZE,
               "replay record must be exactly 64 bytes");
_Static_assert(offsetof(keyvalet_replay_record_t, magic) == 0,
               "unexpected replay record layout");
_Static_assert(offsetof(keyvalet_replay_record_t, sequence) == 8,
               "unexpected replay record layout");
_Static_assert(offsetof(keyvalet_replay_record_t, accepted_utc) == 16,
               "unexpected replay record layout");
_Static_assert(offsetof(keyvalet_replay_record_t, operation_digest) == 24,
               "unexpected replay record layout");
_Static_assert(offsetof(keyvalet_replay_record_t, nonce_digest) == 40,
               "unexpected replay record layout");
_Static_assert(offsetof(keyvalet_replay_record_t, crc32) == 60,
               "unexpected replay record layout");

uint32_t keyvalet_replay_crc32(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

void keyvalet_replay_record_create(
    keyvalet_replay_record_t *record, uint64_t sequence, int64_t accepted_utc,
    const uint8_t operation_digest[KEYVALET_REPLAY_DIGEST_SIZE],
    const uint8_t nonce_digest[KEYVALET_REPLAY_DIGEST_SIZE])
{
    memset(record, 0, sizeof(*record));
    record->magic = KEYVALET_REPLAY_RECORD_MAGIC;
    record->version = KEYVALET_REPLAY_RECORD_VERSION;
    record->size = sizeof(*record);
    record->sequence = sequence;
    record->accepted_utc = accepted_utc;
    memcpy(record->operation_digest, operation_digest,
           sizeof(record->operation_digest));
    memcpy(record->nonce_digest, nonce_digest, sizeof(record->nonce_digest));
    record->crc32 = keyvalet_replay_crc32(
        record, offsetof(keyvalet_replay_record_t, crc32));
}

bool keyvalet_replay_record_valid(const keyvalet_replay_record_t *record)
{
    return record != NULL && record->magic == KEYVALET_REPLAY_RECORD_MAGIC &&
           record->version == KEYVALET_REPLAY_RECORD_VERSION &&
           record->size == sizeof(*record) && record->sequence != 0 &&
           record->accepted_utc > 0 &&
           record->crc32 == keyvalet_replay_crc32(
               record, offsetof(keyvalet_replay_record_t, crc32));
}

size_t keyvalet_replay_record_slot(uint64_t sequence)
{
    return sequence == 0 ? KEYVALET_REPLAY_CAPACITY :
           (size_t)((sequence - 1U) % KEYVALET_REPLAY_CAPACITY);
}

bool keyvalet_replay_records_recover(
    const keyvalet_replay_record_t records[KEYVALET_REPLAY_CAPACITY],
    const bool present[KEYVALET_REPLAY_CAPACITY],
    keyvalet_replay_window_t *window, uint64_t *highest_sequence)
{
    if (records == NULL || present == NULL || window == NULL ||
        highest_sequence == NULL) return false;

    uint64_t highest = 0;
    size_t present_count = 0;
    for (size_t slot = 0; slot < KEYVALET_REPLAY_CAPACITY; ++slot) {
        if (!present[slot]) continue;
        ++present_count;
        if (!keyvalet_replay_record_valid(&records[slot]) ||
            keyvalet_replay_record_slot(records[slot].sequence) != slot) {
            return false;
        }
        if (records[slot].sequence > highest) highest = records[slot].sequence;
    }

    memset(window, 0, sizeof(*window));
    if (highest == 0) {
        *highest_sequence = 0;
        return present_count == 0;
    }

    const uint64_t expected_count = highest < KEYVALET_REPLAY_CAPACITY ?
                                    highest : KEYVALET_REPLAY_CAPACITY;
    if (present_count != expected_count) return false;
    const uint64_t first = highest - expected_count + 1U;
    for (uint64_t offset = 0; offset < expected_count; ++offset) {
        const uint64_t sequence = first + offset;
        const size_t slot = keyvalet_replay_record_slot(sequence);
        if (!present[slot] || records[slot].sequence != sequence) return false;
        const size_t output = (size_t)offset;
        memcpy(window->operation_digests[output], records[slot].operation_digest,
               KEYVALET_REPLAY_DIGEST_SIZE);
        memcpy(window->nonce_digests[output], records[slot].nonce_digest,
               KEYVALET_REPLAY_DIGEST_SIZE);
    }
    window->count = (uint32_t)expected_count;
    window->next_index = expected_count < KEYVALET_REPLAY_CAPACITY ?
                         (uint32_t)expected_count : 0U;
    window->last_accept_utc = records[keyvalet_replay_record_slot(highest)].accepted_utc;
    if (window->last_accept_utc <= 0) return false;
    *highest_sequence = highest;
    return keyvalet_replay_window_valid(window);
}
