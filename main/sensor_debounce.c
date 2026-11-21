#include "sensor_debounce.h"

#include <stddef.h>

void keyvalet_sensor_debounce_init(keyvalet_sensor_debounce_t *state,
                                   int level, int64_t time_ms)
{
    if (state == NULL) return;
    state->stable_level = level;
    state->candidate_level = level;
    state->candidate_since_ms = time_ms;
}

bool keyvalet_sensor_debounce_sample(keyvalet_sensor_debounce_t *state,
                                     int level, int64_t time_ms,
                                     uint32_t debounce_ms)
{
    if (state == NULL) return false;
    if (level != state->candidate_level) {
        state->candidate_level = level;
        state->candidate_since_ms = time_ms;
        return false;
    }
    if (level != state->stable_level &&
        time_ms - state->candidate_since_ms >= debounce_ms) {
        state->stable_level = level;
        return true;
    }
    return false;
}
