#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define ESP_TIMER_TASK 0
typedef void *esp_timer_handle_t;
typedef struct {
    void (*callback)(void *);
    void *arg;
    int dispatch_method;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;
int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *timer);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
