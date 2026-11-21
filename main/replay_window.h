#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KEYVALET_REPLAY_CAPACITY 160U
#define KEYVALET_REPLAY_DIGEST_SIZE 16U

typedef struct {
    uint32_t next_index;
    uint32_t count;
    int64_t last_accept_utc;
    uint8_t operation_digests[KEYVALET_REPLAY_CAPACITY][KEYVALET_REPLAY_DIGEST_SIZE];
    uint8_t nonce_digests[KEYVALET_REPLAY_CAPACITY][KEYVALET_REPLAY_DIGEST_SIZE];
} keyvalet_replay_window_t;

bool keyvalet_replay_window_valid(const keyvalet_replay_window_t *window);
bool keyvalet_replay_window_contains(const keyvalet_replay_window_t *window,
                                     const uint8_t operation_digest[KEYVALET_REPLAY_DIGEST_SIZE],
                                     const uint8_t nonce_digest[KEYVALET_REPLAY_DIGEST_SIZE]);
bool keyvalet_replay_window_add(keyvalet_replay_window_t *window,
                                const uint8_t operation_digest[KEYVALET_REPLAY_DIGEST_SIZE],
                                const uint8_t nonce_digest[KEYVALET_REPLAY_DIGEST_SIZE],
                                int64_t now_utc);
