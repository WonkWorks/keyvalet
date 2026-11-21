#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "replay_window.h"

int main(void)
{
    keyvalet_replay_window_t window = {0};
    uint8_t operation[KEYVALET_REPLAY_DIGEST_SIZE] = {1};
    uint8_t nonce[KEYVALET_REPLAY_DIGEST_SIZE] = {2};
    assert(keyvalet_replay_window_valid(&window));
    assert(!keyvalet_replay_window_contains(&window, operation, nonce));
    assert(keyvalet_replay_window_add(&window, operation, nonce, 100));
    assert(keyvalet_replay_window_contains(&window, operation, nonce));
    uint8_t second_operation[KEYVALET_REPLAY_DIGEST_SIZE] = {3};
    uint8_t second_nonce[KEYVALET_REPLAY_DIGEST_SIZE] = {4};
    assert(!keyvalet_replay_window_add(&window, operation, second_nonce, 101));
    assert(!keyvalet_replay_window_add(&window, second_operation, nonce, 101));
    assert(!keyvalet_replay_window_add(&window, second_operation, second_nonce, 100));
    assert(keyvalet_replay_window_add(&window, second_operation, second_nonce, 101));

    keyvalet_replay_window_t rebooted;
    memcpy(&rebooted, &window, sizeof(rebooted));
    assert(keyvalet_replay_window_contains(&rebooted, operation, nonce));
    assert(!keyvalet_replay_window_add(&rebooted, operation, second_nonce, 102));
    assert(!keyvalet_replay_window_add(&rebooted, second_operation, nonce, 102));

    for (unsigned index = 0; index < KEYVALET_REPLAY_CAPACITY + 5U; ++index) {
        uint8_t item_operation[KEYVALET_REPLAY_DIGEST_SIZE] = {0};
        uint8_t item_nonce[KEYVALET_REPLAY_DIGEST_SIZE] = {0};
        item_operation[0] = (uint8_t)(index + 10U);
        item_operation[1] = (uint8_t)((index + 10U) >> 8);
        item_nonce[0] = (uint8_t)(index + 20U);
        item_nonce[1] = (uint8_t)((index + 20U) >> 8);
        assert(keyvalet_replay_window_add(&rebooted, item_operation, item_nonce,
                                          200 + index));
    }
    assert(rebooted.count == KEYVALET_REPLAY_CAPACITY);
    assert(keyvalet_replay_window_valid(&rebooted));
    puts("replay window tests passed");
    return 0;
}
