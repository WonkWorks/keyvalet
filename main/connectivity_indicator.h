#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "hardware_profile.h"

esp_err_t keyvalet_connectivity_indicator_start(
    const keyvalet_hardware_profile_t *profile);
void keyvalet_connectivity_indicator_set_wifi(bool connected);
void keyvalet_connectivity_indicator_set_clients(size_t client_count);
