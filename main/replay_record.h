#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "replay_window.h"

#define KEYVALET_REPLAY_RECORD_MAGIC 0x4b565252U
#define KEYVALET_REPLAY_RECORD_VERSION 1U
#define KEYVALET_REPLAY_RECORD_SIZE 64U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint64_t sequence;
    int64_t accepted_utc;
    uint8_t operation_digest[KEYVALET_REPLAY_DIGEST_SIZE];
    uint8_t nonce_digest[KEYVALET_REPLAY_DIGEST_SIZE];
    uint8_t reserved[4];
    uint32_t crc32;
} keyvalet_replay_record_t;

uint32_t keyvalet_replay_crc32(const void *data, size_t size);
void keyvalet_replay_record_create(
    keyvalet_replay_record_t *record, uint64_t sequence, int64_t accepted_utc,
    const uint8_t operation_digest[KEYVALET_REPLAY_DIGEST_SIZE],
    const uint8_t nonce_digest[KEYVALET_REPLAY_DIGEST_SIZE]);
bool keyvalet_replay_record_valid(const keyvalet_replay_record_t *record);
size_t keyvalet_replay_record_slot(uint64_t sequence);
bool keyvalet_replay_records_recover(
    const keyvalet_replay_record_t records[KEYVALET_REPLAY_CAPACITY],
    const bool present[KEYVALET_REPLAY_CAPACITY],
    keyvalet_replay_window_t *window, uint64_t *highest_sequence);
