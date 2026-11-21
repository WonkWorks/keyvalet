#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wifi_store.h"

#define KEYVALET_IMPROV_MAX_FRAME 265U
typedef struct {
    uint8_t bytes[KEYVALET_IMPROV_MAX_FRAME];
    size_t used;
    int64_t last_byte_ms;
} keyvalet_improv_parser_t;

/* Returns 1 for a complete frame, -1 for malformed/expired input, 0 otherwise.
 * Completed payload remains valid until the next feed; caller clears secrets. */
int keyvalet_improv_feed(keyvalet_improv_parser_t *parser, uint8_t byte, int64_t now_ms);
bool keyvalet_improv_credentials(const uint8_t *payload, size_t length,
                                 keyvalet_wifi_credentials_t *credentials);
size_t keyvalet_improv_frame(uint8_t *output, uint8_t type,
                              const uint8_t *payload, size_t length);
size_t keyvalet_improv_result(uint8_t *output, uint8_t command,
                               const char *const *strings, size_t count);
