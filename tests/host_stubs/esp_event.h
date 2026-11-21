#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef const char *esp_event_base_t;
extern const char WIFI_EVENT[], IP_EVENT[];
#define ESP_EVENT_ANY_ID -1
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id,
    void (*handler)(void *, esp_event_base_t, int32_t, void *), void *argument);
