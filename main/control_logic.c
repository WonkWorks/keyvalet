#include "control_logic.h"

#include <stddef.h>

bool keyvalet_open_is_expected(int64_t open_time_ms, int64_t activation_time_ms)
{
    return activation_time_ms > 0 && open_time_ms >= activation_time_ms &&
           open_time_ms - activation_time_ms <= KEYVALET_EXPECTED_OPEN_WINDOW_MS;
}

bool keyvalet_operation_guard_reserve(keyvalet_operation_guard_t *guard,
                                      bool sensor_required,
                                      bool door_known, bool door_open)
{
    if (guard == NULL || guard->reserved ||
        (sensor_required && (!door_known || door_open))) return false;
    guard->reserved = true;
    guard->started = false;
    guard->door_open = false;
    guard->sensor_required = sensor_required;
    guard->deadline_ms = 0;
    return true;
}

bool keyvalet_operation_guard_start(keyvalet_operation_guard_t *guard,
                                    int64_t activation_time_ms)
{
    if (guard == NULL || !guard->reserved || guard->started ||
        (guard->sensor_required && guard->door_open))
        return false;
    if (!guard->sensor_required) {
        /* The shared output arbiter owns pulse/recovery. No door lifecycle. */
        *guard = (keyvalet_operation_guard_t){0};
        return true;
    }
    guard->started = true;
    guard->deadline_ms = activation_time_ms + KEYVALET_OPERATION_NO_OPEN_TIMEOUT_MS;
    return true;
}

void keyvalet_operation_guard_cancel(keyvalet_operation_guard_t *guard)
{
    if (guard != NULL && guard->reserved && !guard->started) guard->reserved = false;
}

void keyvalet_operation_guard_observe(keyvalet_operation_guard_t *guard,
                                      bool door_open)
{
    if (guard == NULL || !guard->sensor_required) return;
    if (door_open) {
        guard->door_open = true;
        return;
    }
    if (guard->reserved && guard->started) {
        guard->reserved = false;
        guard->started = false;
        guard->door_open = false;
        guard->deadline_ms = 0;
    } else if (!guard->reserved) {
        guard->door_open = false;
    }
}

bool keyvalet_operation_guard_tick(keyvalet_operation_guard_t *guard,
                                   int64_t now_ms)
{
    if (guard != NULL && guard->reserved && guard->started && !guard->door_open &&
        now_ms >= guard->deadline_ms) {
        guard->reserved = false;
        guard->started = false;
        guard->deadline_ms = 0;
        return true;
    }
    return false;
}

bool keyvalet_operation_guard_active(const keyvalet_operation_guard_t *guard)
{
    return guard != NULL && guard->reserved;
}

bool keyvalet_sensor_reminder_due(bool sensor_open, bool alarm_active,
                                  bool unexpected_open, int64_t deadline_ms,
                                  int64_t now_ms)
{
    return sensor_open && !alarm_active && !unexpected_open && deadline_ms > 0 &&
           now_ms >= deadline_ms;
}

keyvalet_alarm_output_t keyvalet_alarm_output_select_many(
    bool ordinary_active, bool ordinary_silenced,
    const bool *urgent_active, const bool *urgent_silenced, size_t count)
{
    if (urgent_active != NULL && urgent_silenced != NULL) {
        for (size_t index = 0; index < count; ++index)
            if (urgent_active[index] && !urgent_silenced[index])
                return KEYVALET_ALARM_OUTPUT_URGENT;
    }
    return ordinary_active && !ordinary_silenced
               ? KEYVALET_ALARM_OUTPUT_REMINDER : KEYVALET_ALARM_OUTPUT_NONE;
}

keyvalet_button_action_t keyvalet_button_release_action(
    bool restart_hold, bool alarm_active, bool pairing_hold,
    bool pairing_active)
{
    if (restart_hold) return KEYVALET_BUTTON_ACTION_RESTART;
    if (alarm_active) return KEYVALET_BUTTON_ACTION_SILENCE_ALARM;
    if (pairing_hold) return KEYVALET_BUTTON_ACTION_NONE;
    if (pairing_active) return KEYVALET_BUTTON_ACTION_CANCEL_PAIRING;
    return KEYVALET_BUTTON_ACTION_ANNOUNCE_IP;
}
