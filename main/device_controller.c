#include "device_controller.h"

#include <stdint.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "control_logic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hardware_io.h"
#include "journal.h"
#include "network_manager.h"
#include "piezo.h"
#include "security.h"
#include "settings.h"
#include "web_server.h"

#define PAIRING_CUE_REPEAT_MS 5000LL
static const char *TAG = "controller";

typedef enum { INPUT_SENSOR_1, INPUT_SENSOR_2, INPUT_SENSOR_3, INPUT_SENSOR_4,
               INPUT_BUTTON } input_type_t;
typedef struct { input_type_t type; bool active; bool initial; } input_event_t;

static const keyvalet_hardware_profile_t *s_profile;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_lock;
static keyvalet_device_status_t s_status;
static keyvalet_settings_t s_settings;
static int64_t s_reminder_deadline[KEYVALET_LOCK_COUNT];
static int64_t s_sensor_silence_deadline[KEYVALET_SENSOR_COUNT];
static int64_t s_next_alarm, s_next_pairing, s_button_pressed;
static int64_t s_last_solenoid[KEYVALET_LOCK_COUNT];
static uint32_t s_operation_timeout_s[KEYVALET_LOCK_COUNT];
static bool s_pairing_hold, s_restart_hold;
static keyvalet_operation_guard_t s_operation_guard[KEYVALET_LOCK_COUNT];

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void record_sensor_event(keyvalet_event_type_t type, uint16_t flags,
                                keyvalet_sensor_id_t sensor)
{
    esp_err_t result = keyvalet_journal_append(type, flags, (uint32_t)sensor + 1U);
    if (result != ESP_OK) ESP_LOGE(TAG, "Unable to persist %s for sensor_%u: %s",
        keyvalet_event_type_name(type), (unsigned)sensor + 1U,
        esp_err_to_name(result));
}

static void post(input_type_t type, bool active, bool initial)
{
    input_event_t event = {type, active, initial};
    if (s_queue == NULL || xQueueSend(s_queue, &event, 0) != pdTRUE)
        ESP_LOGE(TAG, "Input event queue full; type=%d", type);
}
static void sensor_cb(keyvalet_sensor_id_t sensor, bool active, bool initial)
{ post((input_type_t)(INPUT_SENSOR_1 + sensor), active, initial); }
static void button_cb(bool active, bool initial) { post(INPUT_BUTTON, active, initial); }
static const keyvalet_input_callbacks_t CALLBACKS = {
    .sensor_state = sensor_cb,
    .button_state = button_cb,
};

static keyvalet_alarm_output_t alarm_output_locked(void)
{
    bool ordinary = false;
    bool urgent_active[KEYVALET_SENSOR_COUNT] = {0};
    bool urgent_silenced[KEYVALET_SENSOR_COUNT] = {0};
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index) {
        const bool paired = index < KEYVALET_LOCK_COUNT;
        ordinary = ordinary ||
            (paired && s_status.sensor_alarm_active[index] &&
             !s_status.sensor_unexpected_open[index] &&
             !s_status.sensor_alarm_silenced[index]);
        urgent_active[index] = s_status.sensor_alarm_active[index] &&
            (!paired || s_status.sensor_unexpected_open[index]);
        urgent_silenced[index] = s_status.sensor_alarm_silenced[index];
    }
    return keyvalet_alarm_output_select_many(
        ordinary, false, urgent_active,
        urgent_silenced, KEYVALET_SENSOR_COUNT);
}

static void stop_alarm_tone(void)
{
    keyvalet_piezo_activity_t activity = keyvalet_piezo_activity();
    if (activity == KEYVALET_PIEZO_REMINDER_ALARM ||
        activity == KEYVALET_PIEZO_URGENT_ALARM) (void)keyvalet_piezo_stop(activity);
}

static void reconcile_alarm_tone(keyvalet_alarm_output_t output)
{
    const keyvalet_piezo_activity_t activity = keyvalet_piezo_activity();
    if (activity != KEYVALET_PIEZO_REMINDER_ALARM &&
        activity != KEYVALET_PIEZO_URGENT_ALARM) return;
    const keyvalet_piezo_activity_t desired =
        output == KEYVALET_ALARM_OUTPUT_URGENT ? KEYVALET_PIEZO_URGENT_ALARM :
        output == KEYVALET_ALARM_OUTPUT_REMINDER ? KEYVALET_PIEZO_REMINDER_ALARM :
        KEYVALET_PIEZO_NONE;
    if (activity != desired) (void)keyvalet_piezo_stop(activity);
}

static void paired_sensor_state(keyvalet_lock_id_t lock, bool open, bool initial,
                                int64_t time_ms)
{
    const keyvalet_sensor_id_t sensor = (keyvalet_sensor_id_t)lock;
    bool unexpected = false;
    bool alarm_requested = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.sensor_known[sensor] = true;
    s_status.sensor_open[sensor] = open;
    if (open) {
        const bool expected = s_settings.lock_sensor_required[lock] &&
            !initial && keyvalet_open_is_expected(
            time_ms, s_last_solenoid[lock]);
        unexpected = !initial && !expected;
        s_status.sensor_unexpected_open[sensor] = unexpected;
        s_status.sensor_alarm_active[sensor] = unexpected &&
            s_settings.sensor_monitoring_enabled[sensor];
        alarm_requested = s_status.sensor_alarm_active[sensor];
        s_status.sensor_alarm_silenced[sensor] = false;
        const uint32_t reminder_s =
            expected
                ? s_operation_timeout_s[lock]
                : s_settings.default_door_open_timeout_s;
        s_reminder_deadline[lock] = !s_settings.lock_sensor_required[lock] ||
            reminder_s == 0 || unexpected ||
            !s_settings.sensor_monitoring_enabled[sensor] ? 0 : time_ms +
            (int64_t)reminder_s * 1000LL;
        s_sensor_silence_deadline[sensor] = 0;
        s_next_alarm = s_status.sensor_alarm_active[sensor] ? time_ms : 0;
    } else {
        const bool alarm = s_status.sensor_alarm_active[sensor];
        s_status.sensor_alarm_active[sensor] = false;
        s_status.sensor_alarm_silenced[sensor] = false;
        s_status.sensor_unexpected_open[sensor] = false;
        s_operation_timeout_s[lock] = 0;
        s_last_solenoid[lock] = 0; /* Closing completes this authorized opening. */
        s_reminder_deadline[lock] = 0;
        s_sensor_silence_deadline[sensor] = 0;
        keyvalet_operation_guard_observe(&s_operation_guard[lock], false);
        s_status.lock_operation_active[lock] =
            keyvalet_operation_guard_active(&s_operation_guard[lock]);
        const keyvalet_alarm_output_t remaining = alarm_output_locked();
        s_next_alarm = remaining != KEYVALET_ALARM_OUTPUT_NONE ? time_ms : 0;
        xSemaphoreGive(s_lock);
        reconcile_alarm_tone(remaining);
        if (alarm) ESP_LOGW(TAG, "Sensor %u alarm cleared", (unsigned)sensor + 1U);
        if (!initial) record_sensor_event(
            KEYVALET_EVENT_SENSOR_CLOSED, 0, sensor);
        return;
    }
    keyvalet_operation_guard_observe(&s_operation_guard[lock], true);
    s_status.lock_operation_active[lock] =
        keyvalet_operation_guard_active(&s_operation_guard[lock]);
    xSemaphoreGive(s_lock);
    if (!initial) {
        record_sensor_event(KEYVALET_EVENT_SENSOR_OPEN, 0, sensor);
        if (unexpected) {
            record_sensor_event(KEYVALET_EVENT_SENSOR_UNEXPECTED_OPEN, 0,
                                sensor);
            if (alarm_requested) (void)keyvalet_piezo_preempt_for_alarm();
            ESP_LOGW(TAG, "Sensor %u unexpected opening detected",
                     (unsigned)sensor + 1U);
        }
    }
}

static keyvalet_event_type_t sensor_event(bool open, bool alarm, bool silenced)
{
    if (silenced) return KEYVALET_EVENT_SENSOR_ALARM_SILENCED;
    if (alarm) return KEYVALET_EVENT_SENSOR_ALARM;
    return open ? KEYVALET_EVENT_SENSOR_OPEN : KEYVALET_EVENT_SENSOR_CLOSED;
}

static void sensor_only_state(keyvalet_sensor_id_t sensor, bool open, bool initial,
                              int64_t time_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.sensor_known[sensor] = true;
    s_status.sensor_open[sensor] = open;
    s_status.sensor_alarm_active[sensor] = open && !initial &&
        s_settings.sensor_monitoring_enabled[sensor];
    s_status.sensor_alarm_silenced[sensor] = false;
    s_sensor_silence_deadline[sensor] = 0;
    const keyvalet_alarm_output_t remaining = alarm_output_locked();
    s_next_alarm = remaining != KEYVALET_ALARM_OUTPUT_NONE ? time_ms : 0;
    xSemaphoreGive(s_lock);
    reconcile_alarm_tone(remaining);
    if (!initial) {
        record_sensor_event(sensor_event(open, false, false), 0, sensor);
        if (open && s_settings.sensor_monitoring_enabled[sensor]) {
            record_sensor_event(sensor_event(true, true, false), 0, sensor);
            (void)keyvalet_piezo_preempt_for_alarm();
        }
    }
}

static bool alarm_active_locked(void)
{
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index)
        if (s_status.sensor_alarm_active[index]) return true;
    return false;
}

static void silence_alarms(int64_t time_ms)
{
    bool silenced[KEYVALET_SENSOR_COUNT] = {0};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index) {
        if (s_status.sensor_alarm_active[index] && s_status.sensor_open[index]) {
            silenced[index] = s_status.sensor_alarm_silenced[index] = true;
            s_sensor_silence_deadline[index] =
                time_ms + s_profile->alarm_silence_ms;
        }
    }
    bool any = false;
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index)
        any = any || silenced[index];
    if (any) s_next_alarm = 0;
    xSemaphoreGive(s_lock);
    if (any) stop_alarm_tone();
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index)
        if (silenced[index]) record_sensor_event(
            sensor_event(true, false, true), 0, index);
}

static void announce_ip(void)
{
    uint8_t octet;
    if (keyvalet_web_server_client_count() == 0 &&
        keyvalet_network_last_ipv4_octet(&octet) == ESP_OK)
        (void)keyvalet_piezo_play_ip_octet(octet);
}

static void cancel_pairing(void)
{
    keyvalet_security_close_pairing_window();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_next_pairing = 0;
    xSemaphoreGive(s_lock);
    if (keyvalet_piezo_activity() == KEYVALET_PIEZO_PAIRING)
        (void)keyvalet_piezo_stop(KEYVALET_PIEZO_PAIRING);
    ESP_LOGI(TAG, "Physical pairing window cancelled by short button press");
}

static void button_state(bool pressed, bool initial, int64_t time_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.button_known = true;
    s_status.button_pressed = pressed;
    if (pressed) {
        s_button_pressed = time_ms; s_pairing_hold = s_restart_hold = false;
        xSemaphoreGive(s_lock); return;
    }
    bool paired_hold = s_pairing_hold, restart = s_restart_hold;
    bool alarm = alarm_active_locked();
    s_button_pressed = 0; s_pairing_hold = s_restart_hold = false;
    xSemaphoreGive(s_lock);
    if (initial) return;
    const keyvalet_button_action_t action = keyvalet_button_release_action(
        restart, alarm, paired_hold, keyvalet_security_pairing_active());
    if (action == KEYVALET_BUTTON_ACTION_RESTART) {
        ESP_LOGW(TAG, "Restart hold released; restarting safely");
        ESP_ERROR_CHECK(keyvalet_solenoid_force_inactive(s_profile));
        vTaskDelay(pdMS_TO_TICKS(100)); esp_restart();
    }
    if (action == KEYVALET_BUTTON_ACTION_SILENCE_ALARM) silence_alarms(time_ms);
    else if (action == KEYVALET_BUTTON_ACTION_CANCEL_PAIRING) cancel_pairing();
    else if (action == KEYVALET_BUTTON_ACTION_ANNOUNCE_IP) announce_ip();
}

static void update_timers(int64_t time_ms)
{
    bool alarm_started[KEYVALET_LOCK_COUNT] = {0};
    bool sensor_resumed[KEYVALET_SENSOR_COUNT] = {0};
    bool play = false, urgent = false, pair = false, restart = false, cue = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index) {
        if (keyvalet_sensor_reminder_due(
                s_status.sensor_open[index],
                s_status.sensor_alarm_active[index],
                s_status.sensor_unexpected_open[index],
                s_reminder_deadline[index], time_ms)) {
            s_status.sensor_alarm_active[index] = true;
            alarm_started[index] = true;
            s_next_alarm = time_ms;
        }
    }
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index) {
        if (s_sensor_silence_deadline[index] > 0 &&
            time_ms >= s_sensor_silence_deadline[index]) {
            s_sensor_silence_deadline[index] = 0;
            if (s_status.sensor_alarm_active[index] &&
                s_status.sensor_alarm_silenced[index]) {
                s_status.sensor_alarm_silenced[index] = false;
                sensor_resumed[index] = true;
                s_next_alarm = time_ms;
            }
        }
    }
    const keyvalet_alarm_output_t selected_output = alarm_output_locked();
    if (selected_output != KEYVALET_ALARM_OUTPUT_NONE && time_ms >= s_next_alarm) {
        play = true; urgent = selected_output == KEYVALET_ALARM_OUTPUT_URGENT;
    }
    if (s_status.button_pressed && !s_pairing_hold &&
        time_ms - s_button_pressed >= s_profile->button_pairing_hold_ms)
        pair = s_pairing_hold = true;
    if (s_status.button_pressed && !s_restart_hold &&
        time_ms - s_button_pressed >= s_profile->button_restart_hold_ms)
        restart = s_restart_hold = true;
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index) {
        if (keyvalet_operation_guard_tick(&s_operation_guard[index], time_ms)) {
            s_status.lock_operation_active[index] = false;
            s_operation_timeout_s[index] = 0;
            ESP_LOGW(TAG, "Lock %u operation released after 15-second no-open timeout",
                     (unsigned)index + 1U);
        }
    }
    if (!play && keyvalet_security_pairing_active() && time_ms >= s_next_pairing) {
        cue = true; s_next_pairing = time_ms + PAIRING_CUE_REPEAT_MS;
    }
    xSemaphoreGive(s_lock);
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index)
        if (alarm_started[index])
            record_sensor_event(KEYVALET_EVENT_SENSOR_ALARM, 0, index);
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index)
        if (sensor_resumed[index])
            record_sensor_event(sensor_event(true, true, false),
                KEYVALET_EVENT_FLAG_ALARM_RESUMED, index);
    if (play) {
        (void)keyvalet_piezo_preempt_for_alarm();
        esp_err_t result = urgent ? keyvalet_piezo_play_urgent_alarm() : keyvalet_piezo_play_alarm_chirp();
        if (result == ESP_OK) { xSemaphoreTake(s_lock, portMAX_DELAY); s_next_alarm = time_ms + s_profile->alarm_repeat_ms; xSemaphoreGive(s_lock); }
    } else if (cue && !keyvalet_piezo_is_active()) (void)keyvalet_piezo_play_pairing_cue();
    if (pair) {
        esp_err_t result = keyvalet_security_open_pairing_window();
        if (result == ESP_OK) s_next_pairing = 0;
        else ESP_LOGW(TAG, "Pairing window not opened: %s", esp_err_to_name(result));
    }
    if (restart && !keyvalet_piezo_is_active()) (void)keyvalet_piezo_play_hold_indication();
}

static void controller_task(void *context)
{
    (void)context; input_event_t event;
    while (true) {
        if (xQueueReceive(s_queue, &event, pdMS_TO_TICKS(25)) == pdTRUE) {
            int64_t time_ms = now_ms();
            if (event.type == INPUT_SENSOR_1 || event.type == INPUT_SENSOR_2)
                paired_sensor_state(
                    (keyvalet_lock_id_t)(event.type - INPUT_SENSOR_1),
                    event.active, event.initial, time_ms);
            else if (event.type == INPUT_SENSOR_3 || event.type == INPUT_SENSOR_4)
                sensor_only_state((keyvalet_sensor_id_t)(event.type - INPUT_SENSOR_1),
                                  event.active, event.initial, time_ms);
            else button_state(event.active, event.initial, time_ms);
        }
        update_timers(now_ms());
    }
}

esp_err_t keyvalet_device_controller_start(const keyvalet_hardware_profile_t *profile)
{
    if (!profile || s_profile || keyvalet_settings_get(&s_settings) != ESP_OK) return ESP_ERR_INVALID_STATE;
    s_queue = xQueueCreate(16, sizeof(input_event_t)); s_lock = xSemaphoreCreateMutex();
    if (!s_queue || !s_lock) return ESP_ERR_NO_MEM;
    s_profile = profile;
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index)
        s_status.lock_sensor_required[index] = s_settings.lock_sensor_required[index];
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index)
        s_status.sensor_monitoring_enabled[index] =
            s_settings.sensor_monitoring_enabled[index];
    if (xTaskCreate(controller_task, "device_controller", 6144, NULL, 6, NULL) != pdPASS) { s_profile = NULL; return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
const keyvalet_input_callbacks_t *keyvalet_device_input_callbacks(void) { return &CALLBACKS; }
esp_err_t keyvalet_device_status_get(keyvalet_device_status_t *status)
{
    if (!status || !s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY); *status = s_status; xSemaphoreGive(s_lock); return ESP_OK;
}
esp_err_t keyvalet_device_controller_apply_settings(void)
{
    keyvalet_settings_t settings;
    if (keyvalet_settings_get(&settings) != ESP_OK || !s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index) {
        if (settings.lock_sensor_required[index] != s_settings.lock_sensor_required[index]) {
            /* Administrative dependency changes invalidate old door bookkeeping,
             * including any not-yet-actuated reservation. Never alter a pulse. */
            s_operation_guard[index] = (keyvalet_operation_guard_t){0};
            s_status.lock_operation_active[index] = false;
            s_last_solenoid[index] = 0;
            s_operation_timeout_s[index] = 0;
            s_reminder_deadline[index] = 0;
            if (!s_status.sensor_unexpected_open[index]) {
                s_status.sensor_alarm_active[index] = false;
                s_status.sensor_alarm_silenced[index] = false;
            }
        }
        s_status.lock_sensor_required[index] = settings.lock_sensor_required[index];
    }
    s_settings = settings;
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index) {
        s_status.sensor_monitoring_enabled[index] =
            settings.sensor_monitoring_enabled[index];
        if (!settings.sensor_monitoring_enabled[index]) {
            s_status.sensor_alarm_active[index] = false;
            s_status.sensor_alarm_silenced[index] = false;
        }
    }
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index)
        if (!settings.sensor_monitoring_enabled[index])
            s_reminder_deadline[index] = 0;
    const keyvalet_alarm_output_t remaining = alarm_output_locked();
    xSemaphoreGive(s_lock);
    reconcile_alarm_tone(remaining);
    return ESP_OK;
}
esp_err_t keyvalet_device_unlock_reserve(keyvalet_lock_id_t lock,
                                         bool timeout_present,
                                         uint32_t door_open_timeout_s)
{
    if (!s_lock || lock >= KEYVALET_LOCK_COUNT ||
        !keyvalet_unlock_timeout_valid(timeout_present, door_open_timeout_s)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (keyvalet_lock_reserve(s_profile, lock) != ESP_OK)
        return ESP_ERR_INVALID_STATE;
    const keyvalet_sensor_id_t sensor = (keyvalet_sensor_id_t)lock;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!keyvalet_operation_guard_reserve(
            &s_operation_guard[lock], s_settings.lock_sensor_required[lock],
            s_status.sensor_known[sensor],
            s_status.sensor_open[sensor])) {
        xSemaphoreGive(s_lock);
        (void)keyvalet_lock_cancel_reservation(s_profile, lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_operation_timeout_s[lock] =
        timeout_present ? door_open_timeout_s
                        : s_settings.default_door_open_timeout_s;
    s_status.lock_operation_active[lock] = true;
    xSemaphoreGive(s_lock); return ESP_OK;
}
void keyvalet_device_unlock_cancel(keyvalet_lock_id_t lock)
{
    if (!s_lock || lock >= KEYVALET_LOCK_COUNT) return;
    if (keyvalet_lock_cancel_reservation(s_profile, lock) != ESP_OK) {
        const esp_err_t inactive = keyvalet_solenoid_force_inactive(s_profile);
        if (inactive != ESP_OK)
            ESP_LOGE(TAG, "Unable to force lock outputs inactive: %s",
                     esp_err_to_name(inactive));
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    keyvalet_operation_guard_cancel(&s_operation_guard[lock]);
    s_operation_timeout_s[lock] = 0;
    s_status.lock_operation_active[lock] =
        keyvalet_operation_guard_active(&s_operation_guard[lock]);
    xSemaphoreGive(s_lock);
}
esp_err_t keyvalet_device_unlock_commit(keyvalet_lock_id_t lock)
{
    if (!s_lock || lock >= KEYVALET_LOCK_COUNT) return ESP_ERR_INVALID_STATE;
    const keyvalet_sensor_id_t sensor = (keyvalet_sensor_id_t)lock;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!keyvalet_operation_guard_active(&s_operation_guard[lock]) ||
        s_operation_guard[lock].started ||
        s_operation_guard[lock].door_open ||
        (s_operation_guard[lock].sensor_required && s_status.sensor_open[sensor])) {
        xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = keyvalet_lock_pulse_reserved(s_profile, lock);
    if (result == ESP_OK) {
        const int64_t activation_ms = now_ms();
        const bool sensor_required = s_operation_guard[lock].sensor_required;
        s_last_solenoid[lock] = sensor_required ? activation_ms : 0;
        if (!sensor_required) s_operation_timeout_s[lock] = 0;
        if (!keyvalet_operation_guard_start(
                &s_operation_guard[lock], activation_ms))
            result = ESP_FAIL;
        s_status.lock_operation_active[lock] =
            keyvalet_operation_guard_active(&s_operation_guard[lock]);
    }
    xSemaphoreGive(s_lock); return result;
}
void keyvalet_device_note_solenoid_activation(keyvalet_lock_id_t lock)
{
    if (!s_lock || lock >= KEYVALET_LOCK_COUNT) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_last_solenoid[lock] = s_settings.lock_sensor_required[lock] ? now_ms() : 0;
    s_operation_timeout_s[lock] = s_settings.default_door_open_timeout_s;
    xSemaphoreGive(s_lock);
}
