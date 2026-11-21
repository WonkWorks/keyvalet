#include "replay_window.h"

#include <stddef.h>
#include <string.h>

bool keyvalet_replay_window_valid(const keyvalet_replay_window_t *window)
{
    return window != NULL && window->count <= KEYVALET_REPLAY_CAPACITY &&
           window->next_index < KEYVALET_REPLAY_CAPACITY;
}

bool keyvalet_replay_window_contains(const keyvalet_replay_window_t *window,
                                     const uint8_t operation_digest[KEYVALET_REPLAY_DIGEST_SIZE],
                                     const uint8_t nonce_digest[KEYVALET_REPLAY_DIGEST_SIZE])
{
    if (!keyvalet_replay_window_valid(window) || operation_digest == NULL ||
        nonce_digest == NULL) return false;
    for (uint32_t index = 0; index < window->count; ++index)
        if (memcmp(window->operation_digests[index], operation_digest,
                   KEYVALET_REPLAY_DIGEST_SIZE) == 0 ||
            memcmp(window->nonce_digests[index], nonce_digest,
                   KEYVALET_REPLAY_DIGEST_SIZE) == 0) return true;
    return false;
}

bool keyvalet_replay_window_add(keyvalet_replay_window_t *window,
                                const uint8_t operation_digest[KEYVALET_REPLAY_DIGEST_SIZE],
                                const uint8_t nonce_digest[KEYVALET_REPLAY_DIGEST_SIZE],
                                int64_t now_utc)
{
    if (!keyvalet_replay_window_valid(window) || operation_digest == NULL ||
        nonce_digest == NULL ||
        now_utc <= window->last_accept_utc ||
        keyvalet_replay_window_contains(window, operation_digest, nonce_digest))
        return false;
    memcpy(window->operation_digests[window->next_index], operation_digest,
           KEYVALET_REPLAY_DIGEST_SIZE);
    memcpy(window->nonce_digests[window->next_index], nonce_digest,
           KEYVALET_REPLAY_DIGEST_SIZE);
    window->next_index = (window->next_index + 1U) % KEYVALET_REPLAY_CAPACITY;
    if (window->count < KEYVALET_REPLAY_CAPACITY) ++window->count;
    window->last_accept_utc = now_utc;
    return true;
}
