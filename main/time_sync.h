#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t keyvalet_time_sync_init(void);
bool keyvalet_time_get_utc(int64_t *utc_seconds);
bool keyvalet_time_is_valid(void);
/* Physically protected USB commissioning/recovery only. */
esp_err_t keyvalet_time_set_usb_utc(int64_t utc_seconds);
