#include <assert.h>
#include <stdio.h>

#include "control_logic.h"

int main(void)
{
    assert(keyvalet_open_is_expected(6000, 1000));
    assert(!keyvalet_open_is_expected(6001, 1000));
    assert(!keyvalet_open_is_expected(1000, 0));

    keyvalet_operation_guard_t guard = {0};
    assert(!keyvalet_operation_guard_reserve(&guard, true, false, false));
    assert(!keyvalet_operation_guard_reserve(&guard, true, true, true));
    assert(keyvalet_operation_guard_reserve(&guard, true, true, false));
    assert(!keyvalet_operation_guard_reserve(&guard, true, true, false));
    assert(keyvalet_operation_guard_start(&guard, 1000));
    assert(!keyvalet_operation_guard_tick(&guard, 15999));
    keyvalet_operation_guard_observe(&guard, true);
    assert(!keyvalet_operation_guard_tick(&guard, 999999));
    assert(keyvalet_operation_guard_active(&guard));
    keyvalet_operation_guard_observe(&guard, false);
    assert(!keyvalet_operation_guard_active(&guard));

    assert(keyvalet_operation_guard_reserve(&guard, true, true, false));
    assert(keyvalet_operation_guard_start(&guard, 2000));
    assert(keyvalet_operation_guard_tick(&guard, 17000));
    assert(!keyvalet_operation_guard_active(&guard));

    assert(keyvalet_operation_guard_reserve(&guard, true, true, false));
    keyvalet_operation_guard_cancel(&guard);
    assert(!keyvalet_operation_guard_active(&guard));

    assert(keyvalet_operation_guard_reserve(&guard, true, true, false));
    keyvalet_operation_guard_observe(&guard, true);
    keyvalet_operation_guard_observe(&guard, false);
    assert(!keyvalet_operation_guard_start(&guard, 3000));
    keyvalet_operation_guard_cancel(&guard);

    keyvalet_operation_guard_t locks[2] = {0};
    assert(keyvalet_operation_guard_reserve(&locks[0], true, true, false));
    assert(keyvalet_operation_guard_reserve(&locks[1], true, true, false));
    assert(keyvalet_operation_guard_start(&locks[0], 100));
    assert(keyvalet_operation_guard_start(&locks[1], 200));
    keyvalet_operation_guard_observe(&locks[0], true);
    assert(keyvalet_operation_guard_active(&locks[0]));
    assert(keyvalet_operation_guard_active(&locks[1]));

    keyvalet_operation_guard_t independent = {0};
    assert(keyvalet_operation_guard_reserve(&independent, false, false, true));
    keyvalet_operation_guard_observe(&independent, true);
    assert(keyvalet_operation_guard_start(&independent, 1000));
    assert(!keyvalet_operation_guard_active(&independent));
    assert(!independent.deadline_ms && !independent.door_open);
    keyvalet_operation_guard_observe(&locks[0], false);
    assert(!keyvalet_operation_guard_active(&locks[0]));
    assert(keyvalet_operation_guard_active(&locks[1]));

    /* Expected electronic opening reaches the ordinary reminder threshold. */
    assert(!keyvalet_sensor_reminder_due(true, false, false, 1000, 999));
    assert(keyvalet_sensor_reminder_due(true, false, false, 1000, 1000));

    /* Unexpected opening is urgent and never becomes an ordinary reminder. */
    assert(!keyvalet_sensor_reminder_due(true, true, true, 1000, 999999));

    /* Any unsilenced urgent sensor has priority over an ordinary reminder. */
    bool urgent_active[4] = {false, false, true, true};
    bool urgent_silenced[4] = {false, false, false, true};
    assert(keyvalet_alarm_output_select_many(true, false, urgent_active,
                                             urgent_silenced, 4) ==
           KEYVALET_ALARM_OUTPUT_URGENT);
    urgent_silenced[2] = true;
    assert(keyvalet_alarm_output_select_many(true, false, urgent_active,
                                             urgent_silenced, 4) ==
           KEYVALET_ALARM_OUTPUT_REMINDER);
    urgent_active[3] = false;
    assert(keyvalet_alarm_output_select_many(false, false, urgent_active,
                                             urgent_silenced, 4) ==
           KEYVALET_ALARM_OUTPUT_NONE);

    assert(keyvalet_button_release_action(false, true, false, true) ==
           KEYVALET_BUTTON_ACTION_SILENCE_ALARM);
    assert(keyvalet_button_release_action(false, false, false, true) ==
           KEYVALET_BUTTON_ACTION_CANCEL_PAIRING);
    assert(keyvalet_button_release_action(false, false, true, true) ==
           KEYVALET_BUTTON_ACTION_NONE);
    assert(keyvalet_button_release_action(false, false, false, false) ==
           KEYVALET_BUTTON_ACTION_ANNOUNCE_IP);
    assert(keyvalet_button_release_action(true, false, false, true) ==
           KEYVALET_BUTTON_ACTION_RESTART);
    puts("control logic tests passed");
    return 0;
}
