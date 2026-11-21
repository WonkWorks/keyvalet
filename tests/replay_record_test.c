#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "replay_record.h"

static void digests(uint64_t value, uint8_t operation[16], uint8_t nonce[16])
{
    memset(operation, 0, 16);
    memset(nonce, 0, 16);
    memcpy(operation, &value, sizeof(value));
    value ^= UINT64_C(0xa5a5a5a55a5a5a5a);
    memcpy(nonce, &value, sizeof(value));
}

static void put(keyvalet_replay_record_t records[160], bool present[160],
                uint64_t sequence)
{
    uint8_t operation[16], nonce[16];
    digests(sequence, operation, nonce);
    const size_t slot = keyvalet_replay_record_slot(sequence);
    keyvalet_replay_record_create(&records[slot], sequence,
                                  (int64_t)sequence + 1000,
                                  operation, nonce);
    present[slot] = true;
}

int main(void)
{
    assert(sizeof(keyvalet_replay_record_t) == 64);
    uint8_t operation[16] = {0}, nonce[16] = {0};
    keyvalet_replay_record_t invalid;
    keyvalet_replay_record_create(&invalid, 1, 0, operation, nonce);
    assert(!keyvalet_replay_record_valid(&invalid));
    keyvalet_replay_record_t records[160] = {0};
    bool present[160] = {0};
    keyvalet_replay_window_t window;
    uint64_t highest = 99;
    assert(keyvalet_replay_records_recover(records, present, &window, &highest));
    assert(highest == 0 && window.count == 0 && window.next_index == 0);

    for (uint64_t sequence = 1; sequence <= 4000; ++sequence) {
        put(records, present, sequence);
        assert(keyvalet_replay_records_recover(records, present, &window, &highest));
        assert(highest == sequence);
        assert(window.count == (sequence < 160 ? sequence : 160));
        assert(window.last_accept_utc == (int64_t)sequence + 1000);
        uint8_t operation[16], nonce[16];
        digests(sequence, operation, nonce);
        assert(keyvalet_replay_window_contains(&window, operation, nonce));
        if (sequence == 161) {
            digests(1, operation, nonce);
            assert(!keyvalet_replay_window_contains(&window, operation, nonce));
            digests(2, operation, nonce);
            assert(keyvalet_replay_window_contains(&window, operation, nonce));
        }
    }

    const size_t active_slot = keyvalet_replay_record_slot(highest - 10);
    present[active_slot] = false;
    assert(!keyvalet_replay_records_recover(records, present, &window, &highest));
    present[active_slot] = true;
    records[active_slot].crc32 ^= 1U;
    assert(!keyvalet_replay_records_recover(records, present, &window, &highest));
    records[active_slot].crc32 ^= 1U;
    records[active_slot].version++;
    records[active_slot].crc32 = keyvalet_replay_crc32(
        &records[active_slot], offsetof(keyvalet_replay_record_t, crc32));
    assert(!keyvalet_replay_records_recover(records, present, &window, &highest));

    puts("replay record tests passed (4000 persisted-record reconstructions)");
    return 0;
}
