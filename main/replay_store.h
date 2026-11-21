#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "replay_window.h"

#define KEYVALET_REPLAY_PARTITION "replay_nvs"

esp_err_t keyvalet_replay_store_init(
    bool enrolled, keyvalet_replay_window_t *recovered_window);
esp_err_t keyvalet_replay_store_append(
    const uint8_t operation_digest[KEYVALET_REPLAY_DIGEST_SIZE],
    const uint8_t nonce_digest[KEYVALET_REPLAY_DIGEST_SIZE], int64_t accepted_utc);
