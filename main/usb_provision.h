#pragma once

#include "esp_err.h"
#include "hardware_profile.h"

esp_err_t keyvalet_usb_provision_start(const keyvalet_hardware_profile_t *profile);
