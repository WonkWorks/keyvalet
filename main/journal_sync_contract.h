#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEYVALET_JOURNAL_SYNC_PROTOCOL_VERSION 1U
#define KEYVALET_JOURNAL_SYNC_MAX_BATCH 8U
#define KEYVALET_JOURNAL_CURSOR_MAGIC 0x4b564a43U
#define KEYVALET_JOURNAL_CURSOR_VERSION 1U

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t acknowledged_sequence;
    uint32_t crc32;
} keyvalet_journal_cursor_blob_t;

typedef struct {
    const char *device_id;
    uint64_t highest_contiguous_sequence;
    const char *operation_id;
    const char *nonce_hex;
    int64_t issued_at;
    int64_t expires_at;
} keyvalet_journal_ack_payload_t;

typedef enum {
    KEYVALET_JOURNAL_ACK_ADVANCE,
    KEYVALET_JOURNAL_ACK_ALREADY_ACKNOWLEDGED,
    KEYVALET_JOURNAL_ACK_BEYOND_LATEST,
} keyvalet_journal_ack_decision_t;

int keyvalet_journal_ack_canonical(
    char *output, size_t capacity,
    const keyvalet_journal_ack_payload_t *payload);
keyvalet_journal_ack_decision_t keyvalet_journal_ack_decide(
    uint64_t current_acknowledged, uint64_t latest_produced,
    uint64_t requested_acknowledged);
void keyvalet_journal_cursor_encode(
    keyvalet_journal_cursor_blob_t *blob, uint64_t acknowledged_sequence);
bool keyvalet_journal_cursor_decode(
    const keyvalet_journal_cursor_blob_t *blob,
    uint64_t *acknowledged_sequence);
