#pragma once

#include "esp_err.h"
#include "hardware_profile.h"

esp_err_t keyvalet_diagnostic_solenoid_init(const keyvalet_hardware_profile_t *profile);
esp_err_t keyvalet_diagnostic_lock_pulse(keyvalet_lock_id_t lock);
