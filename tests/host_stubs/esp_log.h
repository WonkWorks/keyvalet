#pragma once
#include <stdbool.h>
#include <stdio.h>
/* Type-check log arguments without printing each simulated append. */
#define ESP_LOGI(tag, ...) do { (void)(tag); if (false) printf(__VA_ARGS__); } while (0)
#define ESP_LOGE ESP_LOGI
#define ESP_LOGW ESP_LOGI
