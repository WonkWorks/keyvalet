#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define KEYVALET_TLS_HOST_MAX 253U
#define KEYVALET_TLS_ORIGIN_MAX 192U
#define KEYVALET_TLS_ORIGIN_COUNT 3U
#define KEYVALET_TLS_KEY_MAX 256U
#define KEYVALET_TLS_CHAIN_MAX 6144U
#define KEYVALET_TLS_CSR_MAX 1024U

bool keyvalet_tls_hostname_valid(const char *hostname);
bool keyvalet_tls_origin_valid(const char *origin);
esp_err_t keyvalet_tls_key_generate(uint8_t key[KEYVALET_TLS_KEY_MAX], size_t *length);
esp_err_t keyvalet_tls_public_material(const uint8_t *key, size_t length,
    const char *hostname, char *csr, size_t csr_capacity, char public_key_hex[131]);
esp_err_t keyvalet_tls_material_validate(const uint8_t *key, size_t key_length,
    const char *chain, size_t chain_length, const char *hostname, int64_t now,
    int64_t *not_before, int64_t *not_after);
