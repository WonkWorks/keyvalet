#pragma once
#include <assert.h>
#include "esp_err.h"
#define ESP_ERROR_CHECK(expression) assert((expression) == ESP_OK)
void esp_restart(void);
