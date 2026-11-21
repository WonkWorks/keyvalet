#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEYVALET_EXPECTED_OPEN_WINDOW_MS 5000LL
#define KEYVALET_OPERATION_NO_OPEN_TIMEOUT_MS 15000LL

typedef struct {
    bool reserved;
    bool started;
    bool door_open;
    bool sensor_required;
    int64_t deadline_ms;
} keyvalet_operation_guard_t;

typedef enum {
    KEYVALET_ALARM_OUTPUT_NONE,
    KEYVALET_ALARM_OUTPUT_REMINDER,
    KEYVALET_ALARM_OUTPUT_URGENT,
} keyvalet_alarm_output_t;

typedef enum {
    KEYVALET_BUTTON_ACTION_NONE,
    KEYVALET_BUTTON_ACTION_RESTART,
    KEYVALET_BUTTON_ACTION_SILENCE_ALARM,
    KEYVALET_BUTTON_ACTION_CANCEL_PAIRING,
    KEYVALET_BUTTON_ACTION_ANNOUNCE_IP,
} keyvalet_button_action_t;

bool keyvalet_open_is_expected(int64_t open_time_ms, int64_t activation_time_ms);
bool keyvalet_operation_guard_reserve(keyvalet_operation_guard_t *guard,
                                      bool sensor_required,
                                      bool door_known, bool door_open);
bool keyvalet_operation_guard_start(keyvalet_operation_guard_t *guard,
                                    int64_t activation_time_ms);
void keyvalet_operation_guard_cancel(keyvalet_operation_guard_t *guard);
void keyvalet_operation_guard_observe(keyvalet_operation_guard_t *guard,
                                      bool door_open);
bool keyvalet_operation_guard_tick(keyvalet_operation_guard_t *guard,
                                   int64_t now_ms);
bool keyvalet_operation_guard_active(const keyvalet_operation_guard_t *guard);
bool keyvalet_sensor_reminder_due(bool sensor_open, bool alarm_active,
                                  bool unexpected_open, int64_t deadline_ms,
                                  int64_t now_ms);
keyvalet_alarm_output_t keyvalet_alarm_output_select_many(
    bool ordinary_active, bool ordinary_silenced,
    const bool *urgent_active, const bool *urgent_silenced, size_t count);
keyvalet_button_action_t keyvalet_button_release_action(
    bool restart_hold, bool alarm_active, bool pairing_hold,
    bool pairing_active);
