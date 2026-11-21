#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int stable_level;
    int candidate_level;
    int64_t candidate_since_ms;
} keyvalet_sensor_debounce_t;

void keyvalet_sensor_debounce_init(keyvalet_sensor_debounce_t *state,
                                   int level, int64_t time_ms);
bool keyvalet_sensor_debounce_sample(keyvalet_sensor_debounce_t *state,
                                     int level, int64_t time_ms,
                                     uint32_t debounce_ms);
