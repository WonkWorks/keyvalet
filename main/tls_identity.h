#pragma once

#include "tls_material.h"

typedef struct {
    bool available, active, candidate, writable, ready;
    uint32_t generation;
    int64_t not_before, not_after;
    char hostname[KEYVALET_TLS_HOST_MAX + 1];
    char origins[KEYVALET_TLS_ORIGIN_COUNT][KEYVALET_TLS_ORIGIN_MAX];
    const char *state;
} keyvalet_tls_status_t;

typedef struct {
    uint32_t schema, generation, key_length, chain_length;
    char hostname[KEYVALET_TLS_HOST_MAX + 1];
    char origins[KEYVALET_TLS_ORIGIN_COUNT][KEYVALET_TLS_ORIGIN_MAX];
    uint8_t key[KEYVALET_TLS_KEY_MAX];
    char chain[KEYVALET_TLS_CHAIN_MAX + 1];
    uint8_t digest[32];
} keyvalet_tls_credentials_t;

esp_err_t keyvalet_tls_identity_init(void);
/* Full station MAC, stable across TLS rotations; not hardware attestation. */
const char *keyvalet_tls_device_uid(void);
void keyvalet_tls_status_get(keyvalet_tls_status_t *status);
/* USB-only mutation; no HTTP/WebSocket handler may call these functions. */
esp_err_t keyvalet_tls_prepare(const char *hostname,
    const char origins[KEYVALET_TLS_ORIGIN_COUNT][KEYVALET_TLS_ORIGIN_MAX], bool rotate_key);
esp_err_t keyvalet_tls_install(const char *chain, size_t length);
esp_err_t keyvalet_tls_activate(void);
esp_err_t keyvalet_tls_discard_candidate(void);
esp_err_t keyvalet_tls_export(bool candidate, char *csr, size_t capacity, char public_key_hex[131]);
esp_err_t keyvalet_tls_credentials_load(keyvalet_tls_credentials_t **credentials);
void keyvalet_tls_credentials_free(keyvalet_tls_credentials_t *credentials);
/* Returned stored string stays valid until activation (old HTTPS already stopped). */
const char *keyvalet_tls_allowed_origin(const char *origin);
