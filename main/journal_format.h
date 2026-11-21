#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEYVALET_JOURNAL_FORMAT_VERSION 1U
#define KEYVALET_JOURNAL_RECORD_SIZE 64U
#define KEYVALET_JOURNAL_COMMIT_MARKER 0x544d4f43U

typedef enum {
    KEYVALET_EVENT_DEVICE_BOOT = 1,
    KEYVALET_EVENT_HEARTBEAT = 2,
    KEYVALET_EVENT_LOCK_UNLOCK_AUTHORIZED = 48,
    KEYVALET_EVENT_SENSOR_OPEN = 54,
    KEYVALET_EVENT_SENSOR_CLOSED = 55,
    KEYVALET_EVENT_SENSOR_UNEXPECTED_OPEN = 56,
    KEYVALET_EVENT_SENSOR_ALARM = 57,
    KEYVALET_EVENT_SENSOR_ALARM_SILENCED = 58,
    KEYVALET_EVENT_ENROLLMENT_ACCEPTED = 31,
    KEYVALET_EVENT_CONFIG_UPDATED = 32,
} keyvalet_event_type_t;

enum {
    KEYVALET_EVENT_FLAG_WALL_TIME_VALID = 1U << 0,
    KEYVALET_EVENT_FLAG_ALARM_RESUMED = 1U << 1,
};

typedef struct {
    uint64_t sequence;
    uint64_t uptime_ms;
    int64_t wall_time_s;
    keyvalet_event_type_t type;
    uint16_t flags;
    uint32_t detail;
    uint8_t metadata[12];
} keyvalet_journal_record_t;

void keyvalet_journal_record_encode(uint8_t output[KEYVALET_JOURNAL_RECORD_SIZE],
                                    const keyvalet_journal_record_t *record,
                                    bool committed);
bool keyvalet_journal_record_decode(const uint8_t input[KEYVALET_JOURNAL_RECORD_SIZE],
                                    keyvalet_journal_record_t *record);
bool keyvalet_journal_slot_is_erased(const uint8_t input[KEYVALET_JOURNAL_RECORD_SIZE]);
const char *keyvalet_event_type_name(keyvalet_event_type_t type);
