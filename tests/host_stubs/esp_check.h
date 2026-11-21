#pragma once
#include "esp_err.h"
#define ESP_RETURN_ON_ERROR(call, ...) do { esp_err_t result_ = (call); \
    if (result_ != ESP_OK) return result_; } while (0)
