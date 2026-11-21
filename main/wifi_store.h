#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    char ssid[33];
    char password[65];
} keyvalet_wifi_credentials_t;

bool keyvalet_wifi_text_valid(const uint8_t *text, size_t length);
bool keyvalet_wifi_credentials_valid(const keyvalet_wifi_credentials_t *credentials);
void keyvalet_wifi_credentials_clear(keyvalet_wifi_credentials_t *credentials);
esp_err_t keyvalet_wifi_store_load(keyvalet_wifi_credentials_t *credentials);
esp_err_t keyvalet_wifi_store_save(const keyvalet_wifi_credentials_t *credentials);
