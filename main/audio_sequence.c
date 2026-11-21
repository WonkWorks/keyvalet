#include "audio_sequence.h"

#include <stdbool.h>
#include <stdio.h>

static bool append_step(keyvalet_audio_step_t *steps, size_t capacity,
                        size_t *count, uint32_t frequency_hz,
                        uint32_t duration_ms)
{
    if (*count >= capacity) return false;
    steps[(*count)++] = (keyvalet_audio_step_t) {frequency_hz, duration_ms};
    return true;
}

size_t keyvalet_ip_octet_sequence(uint8_t octet, keyvalet_audio_step_t *steps,
                                  size_t capacity)
{
    if (steps == NULL) return 0;
    char digits[4];
    const int digit_count = snprintf(digits, sizeof(digits), "%u", (unsigned)octet);
    if (digit_count < 1 || digit_count > 3) return 0;
    size_t count = 0;
    for (int digit_index = 0; digit_index < digit_count; ++digit_index) {
        const unsigned digit = (unsigned)(digits[digit_index] - '0');
        if (digit == 0) {
            if (!append_step(steps, capacity, &count, KEYVALET_IP_ZERO_FREQUENCY_HZ,
                             KEYVALET_IP_ZERO_TONE_MS)) return 0;
        } else {
            for (unsigned beep = 0; beep < digit; ++beep) {
                if (!append_step(steps, capacity, &count,
                                 KEYVALET_IP_SHORT_FREQUENCY_HZ,
                                 KEYVALET_IP_SHORT_TONE_MS)) return 0;
                if (beep + 1U < digit &&
                    !append_step(steps, capacity, &count, 0,
                                 KEYVALET_IP_BEEP_GAP_MS)) return 0;
            }
        }
        if (digit_index + 1 < digit_count &&
            !append_step(steps, capacity, &count, 0,
                         KEYVALET_IP_DIGIT_GAP_MS)) return 0;
    }
    return count;
}
