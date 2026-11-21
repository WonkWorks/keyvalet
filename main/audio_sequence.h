#pragma once

#include <stddef.h>
#include <stdint.h>

#define KEYVALET_IP_SHORT_FREQUENCY_HZ 1800U
#define KEYVALET_IP_ZERO_FREQUENCY_HZ 2600U
#define KEYVALET_IP_SHORT_TONE_MS 180U
#define KEYVALET_IP_BEEP_GAP_MS 180U
#define KEYVALET_IP_DIGIT_GAP_MS 800U
#define KEYVALET_IP_ZERO_TONE_MS 600U

typedef struct {
    uint32_t frequency_hz;
    uint32_t duration_ms;
} keyvalet_audio_step_t;

size_t keyvalet_ip_octet_sequence(uint8_t octet, keyvalet_audio_step_t *steps,
                                  size_t capacity);
