#include "journal_sync_contract.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(keyvalet_journal_cursor_blob_t) == 24,
               "unexpected cursor layout");
_Static_assert(offsetof(keyvalet_journal_cursor_blob_t, crc32) == 16,
               "unexpected cursor CRC");

int keyvalet_journal_ack_canonical(
    char *output, size_t capacity,
    const keyvalet_journal_ack_payload_t *payload)
{
    if (output == NULL || capacity == 0 || payload == NULL ||
        payload->device_id == NULL || payload->operation_id == NULL ||
        payload->nonce_hex == NULL) return -1;
    return snprintf(output, capacity,
        "KEYVALET-JOURNAL-ACK-V1\n%s\nJOURNAL_ACK\n%" PRIu64
        "\n%s\n%s\n%" PRId64 "\n%" PRId64 "\n",
        payload->device_id, payload->highest_contiguous_sequence,
        payload->operation_id, payload->nonce_hex,
        payload->issued_at, payload->expires_at);
}

keyvalet_journal_ack_decision_t keyvalet_journal_ack_decide(
    uint64_t current_acknowledged, uint64_t latest_produced,
    uint64_t requested_acknowledged)
{
    if (requested_acknowledged > latest_produced)
        return KEYVALET_JOURNAL_ACK_BEYOND_LATEST;
    if (requested_acknowledged <= current_acknowledged)
        return KEYVALET_JOURNAL_ACK_ALREADY_ACKNOWLEDGED;
    return KEYVALET_JOURNAL_ACK_ADVANCE;
}

static uint32_t cursor_crc(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

void keyvalet_journal_cursor_encode(
    keyvalet_journal_cursor_blob_t *blob, uint64_t acknowledged_sequence)
{
    memset(blob, 0, sizeof(*blob));
    blob->magic = KEYVALET_JOURNAL_CURSOR_MAGIC;
    blob->version = KEYVALET_JOURNAL_CURSOR_VERSION;
    blob->acknowledged_sequence = acknowledged_sequence;
    blob->crc32 = cursor_crc((const uint8_t *)blob,
        offsetof(keyvalet_journal_cursor_blob_t, crc32));
}

bool keyvalet_journal_cursor_decode(
    const keyvalet_journal_cursor_blob_t *blob,
    uint64_t *acknowledged_sequence)
{
    if (blob == NULL || acknowledged_sequence == NULL ||
        blob->magic != KEYVALET_JOURNAL_CURSOR_MAGIC ||
        blob->version != KEYVALET_JOURNAL_CURSOR_VERSION ||
        blob->crc32 != cursor_crc((const uint8_t *)blob,
            offsetof(keyvalet_journal_cursor_blob_t, crc32))) return false;
    *acknowledged_sequence = blob->acknowledged_sequence;
    return true;
}
