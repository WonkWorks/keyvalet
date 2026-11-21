#pragma once

#include "esp_err.h"
#include "hardware_profile.h"

esp_err_t keyvalet_diagnostic_console_init(const keyvalet_hardware_profile_t *profile);
void keyvalet_diagnostic_console_handle(const char *command);
