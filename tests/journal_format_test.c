#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "journal_format.h"

int main(void)
{
    keyvalet_journal_record_t source = {
        .sequence = 42,
        .uptime_ms = 123456789,
        .wall_time_s = 1730000000,
        .type = KEYVALET_EVENT_SENSOR_ALARM,
        .flags = KEYVALET_EVENT_FLAG_WALL_TIME_VALID |
                 KEYVALET_EVENT_FLAG_ALARM_RESUMED,
        .detail = 7,
        .metadata = {1, 2, 3, 4},
    };
    uint8_t encoded[KEYVALET_JOURNAL_RECORD_SIZE];
    keyvalet_journal_record_encode(encoded, &source, true);
    keyvalet_journal_record_t decoded;
    assert(keyvalet_journal_record_decode(encoded, &decoded));
    assert(decoded.sequence == source.sequence);
    assert(decoded.uptime_ms == source.uptime_ms);
    assert(decoded.wall_time_s == source.wall_time_s);
    assert(decoded.type == source.type);
    assert(decoded.flags == source.flags);
    assert(decoded.detail == source.detail);
    assert(memcmp(decoded.metadata, source.metadata, sizeof(source.metadata)) == 0);

    encoded[40] ^= 1U;
    assert(!keyvalet_journal_record_decode(encoded, NULL));
    encoded[40] ^= 1U;
    encoded[63] = 0xff;
    assert(!keyvalet_journal_record_decode(encoded, NULL));

    keyvalet_journal_record_encode(encoded, &source, false);
    assert(!keyvalet_journal_record_decode(encoded, NULL));
    memset(encoded, 0xff, sizeof(encoded));
    assert(keyvalet_journal_slot_is_erased(encoded));
    encoded[10] = 0xfe;
    assert(!keyvalet_journal_slot_is_erased(encoded));

    assert(strcmp(keyvalet_event_type_name(KEYVALET_EVENT_LOCK_UNLOCK_AUTHORIZED),
                  "LOCK_UNLOCK_AUTHORIZED") == 0);
    assert(strcmp(keyvalet_event_type_name(KEYVALET_EVENT_SENSOR_UNEXPECTED_OPEN),
                  "SENSOR_UNEXPECTED_OPEN") == 0);
    assert(strcmp(keyvalet_event_type_name(KEYVALET_EVENT_SENSOR_OPEN),
                  "SENSOR_OPEN") == 0);
    assert(strcmp(keyvalet_event_type_name(KEYVALET_EVENT_SENSOR_ALARM_SILENCED),
                  "SENSOR_ALARM_SILENCED") == 0);
    puts("journal format tests passed");
    return 0;
}
