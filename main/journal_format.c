#include "journal_format.h"

#include <string.h>

#define JOURNAL_MAGIC 0x524a564bU
#define CRC_OFFSET 52U
#define RESERVED_OFFSET 56U
#define COMMIT_OFFSET 60U

static void put_u16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *target, uint32_t value)
{
    for (size_t index = 0; index < 4; ++index) {
        target[index] = (uint8_t)(value >> (index * 8));
    }
}

static void put_u64(uint8_t *target, uint64_t value)
{
    for (size_t index = 0; index < 8; ++index) {
        target[index] = (uint8_t)(value >> (index * 8));
    }
}

static uint16_t get_u16(const uint8_t *source)
{
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8);
}

static uint32_t get_u32(const uint8_t *source)
{
    uint32_t value = 0;
    for (size_t index = 0; index < 4; ++index) {
        value |= (uint32_t)source[index] << (index * 8);
    }
    return value;
}

static uint64_t get_u64(const uint8_t *source)
{
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
        value |= (uint64_t)source[index] << (index * 8);
    }
    return value;
}

static uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

void keyvalet_journal_record_encode(uint8_t output[KEYVALET_JOURNAL_RECORD_SIZE],
                                    const keyvalet_journal_record_t *record,
                                    bool committed)
{
    memset(output, 0, KEYVALET_JOURNAL_RECORD_SIZE);
    put_u32(output, JOURNAL_MAGIC);
    put_u16(output + 4, KEYVALET_JOURNAL_FORMAT_VERSION);
    put_u16(output + 6, KEYVALET_JOURNAL_RECORD_SIZE);
    put_u64(output + 8, record->sequence);
    put_u64(output + 16, record->uptime_ms);
    put_u64(output + 24, (uint64_t)record->wall_time_s);
    put_u16(output + 32, (uint16_t)record->type);
    put_u16(output + 34, record->flags);
    put_u32(output + 36, record->detail);
    memcpy(output + 40, record->metadata, sizeof(record->metadata));
    put_u32(output + CRC_OFFSET, crc32(output, CRC_OFFSET));
    put_u32(output + RESERVED_OFFSET, 0);
    put_u32(output + COMMIT_OFFSET,
            committed ? KEYVALET_JOURNAL_COMMIT_MARKER : UINT32_MAX);
}

bool keyvalet_journal_record_decode(const uint8_t input[KEYVALET_JOURNAL_RECORD_SIZE],
                                    keyvalet_journal_record_t *record)
{
    if (get_u32(input) != JOURNAL_MAGIC ||
        get_u16(input + 4) != KEYVALET_JOURNAL_FORMAT_VERSION ||
        get_u16(input + 6) != KEYVALET_JOURNAL_RECORD_SIZE ||
        get_u32(input + CRC_OFFSET) != crc32(input, CRC_OFFSET) ||
        get_u32(input + RESERVED_OFFSET) != 0 ||
        get_u32(input + COMMIT_OFFSET) != KEYVALET_JOURNAL_COMMIT_MARKER) {
        return false;
    }

    if (record != NULL) {
        record->sequence = get_u64(input + 8);
        record->uptime_ms = get_u64(input + 16);
        record->wall_time_s = (int64_t)get_u64(input + 24);
        record->type = (keyvalet_event_type_t)get_u16(input + 32);
        record->flags = get_u16(input + 34);
        record->detail = get_u32(input + 36);
        memcpy(record->metadata, input + 40, sizeof(record->metadata));
    }
    return true;
}

bool keyvalet_journal_slot_is_erased(const uint8_t input[KEYVALET_JOURNAL_RECORD_SIZE])
{
    for (size_t index = 0; index < KEYVALET_JOURNAL_RECORD_SIZE; ++index) {
        if (input[index] != UINT8_MAX) {
            return false;
        }
    }
    return true;
}

const char *keyvalet_event_type_name(keyvalet_event_type_t type)
{
    switch (type) {
    case KEYVALET_EVENT_DEVICE_BOOT: return "DEVICE_BOOT";
    case KEYVALET_EVENT_HEARTBEAT: return "HEARTBEAT";
    case KEYVALET_EVENT_LOCK_UNLOCK_AUTHORIZED: return "LOCK_UNLOCK_AUTHORIZED";
    case KEYVALET_EVENT_SENSOR_OPEN: return "SENSOR_OPEN";
    case KEYVALET_EVENT_SENSOR_CLOSED: return "SENSOR_CLOSED";
    case KEYVALET_EVENT_SENSOR_UNEXPECTED_OPEN: return "SENSOR_UNEXPECTED_OPEN";
    case KEYVALET_EVENT_SENSOR_ALARM: return "SENSOR_ALARM";
    case KEYVALET_EVENT_SENSOR_ALARM_SILENCED: return "SENSOR_ALARM_SILENCED";
    case KEYVALET_EVENT_ENROLLMENT_ACCEPTED: return "ENROLLMENT_ACCEPTED";
    case KEYVALET_EVENT_CONFIG_UPDATED: return "CONFIG_UPDATED";
    default: return "UNKNOWN";
    }
}
