/* Execute the production controller, GPIO owner, pulse timer and arbiter.
 * Peripheral/security/network services are host stubs. Each case forks before
 * initialization so no test-only reset API or alternate policy is needed. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../main/device_controller.c"

static int64_t simulated_us = 1000000, timer_due;
static esp_timer_create_args_t timer_args;
static unsigned gpio[32], ordinary_count, urgent_count;
static keyvalet_piezo_activity_t audio;
static keyvalet_journal_record_t events[128];
static size_t event_count;
static keyvalet_settings_t configuration = {
    .schema_version = KEYVALET_SETTINGS_SCHEMA_VERSION, .revision = 1,
    .default_door_open_timeout_s = 90, .lock_sensor_required = {true, true},
    .sensor_monitoring_enabled = {true, true, true, true},
};
static const keyvalet_hardware_profile_t profile = {
    .lock_gpio = {10, 3}, .sensor_gpio = {6, 7, 18, 19},
    .status_led_gpio = 4, .button_gpio = 2, .solenoid_active_high = true,
    .solenoid_pulse_ms = 350, .lock_recovery_ms = 2000,
    .alarm_repeat_ms = 2000, .alarm_silence_ms = 180000,
    .button_pairing_hold_ms = 5000, .button_restart_hold_ms = 10000,
};

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t mutex = malloc(sizeof(*mutex));
    assert(mutex && pthread_mutex_init(mutex, NULL) == 0);
    return mutex;
}
int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{ (void)timeout; assert(pthread_mutex_lock(mutex) == 0); return 1; }
int xSemaphoreGive(SemaphoreHandle_t mutex)
{ assert(pthread_mutex_unlock(mutex) == 0); return 1; }
void vSemaphoreDelete(SemaphoreHandle_t mutex)
{ assert(pthread_mutex_destroy(mutex) == 0); free(mutex); }
QueueHandle_t xQueueCreate(unsigned length, size_t item_size)
{ (void)length; (void)item_size; return &configuration; }
int xQueueSend(QueueHandle_t queue, const void *item, unsigned timeout)
{ (void)queue; (void)item; (void)timeout; return pdTRUE; }
int xQueueReceive(QueueHandle_t queue, void *item, unsigned timeout)
{ (void)queue; (void)item; (void)timeout; return 0; }
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
    void *argument, unsigned priority, void *handle)
{ (void)task; (void)name; (void)stack; (void)argument; (void)priority; (void)handle; return pdPASS; }
int64_t esp_timer_get_time(void) { return simulated_us; }
static void advance(unsigned ms)
{
    const int64_t target = simulated_us + (int64_t)ms * 1000;
    while (timer_due && timer_due <= target) {
        simulated_us = timer_due; timer_due = 0;
        timer_args.callback(timer_args.arg);
    }
    simulated_us = target;
}
void vTaskDelay(unsigned ticks) { advance(ticks); }
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *timer)
{ timer_args = *args; *timer = &timer_args; return ESP_OK; }
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout)
{ (void)timer; timer_due = simulated_us + (int64_t)timeout; return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level)
{ gpio[pin] = level; return ESP_OK; }
esp_err_t gpio_config(const gpio_config_t *config)
{ (void)config; return ESP_OK; }
void esp_restart(void) { abort(); }
esp_err_t keyvalet_settings_get(keyvalet_settings_t *settings)
{ *settings = configuration; return ESP_OK; }
esp_err_t keyvalet_journal_append(keyvalet_event_type_t type, uint16_t flags, uint32_t detail)
{
    assert(event_count < 128);
    events[event_count++] = (keyvalet_journal_record_t){.type=type, .flags=flags, .detail=detail};
    return ESP_OK;
}
size_t keyvalet_web_server_client_count(void) { return 0; }
esp_err_t keyvalet_network_last_ipv4_octet(uint8_t *octet)
{ *octet = 5; return ESP_OK; }
bool keyvalet_security_pairing_active(void) { return false; }
esp_err_t keyvalet_security_open_pairing_window(void) { return ESP_OK; }
void keyvalet_security_close_pairing_window(void) { }
keyvalet_piezo_activity_t keyvalet_piezo_activity(void) { return audio; }
bool keyvalet_piezo_is_active(void) { return audio != KEYVALET_PIEZO_NONE; }
esp_err_t keyvalet_piezo_stop(keyvalet_piezo_activity_t activity)
{ assert(activity == audio); audio = KEYVALET_PIEZO_NONE; return ESP_OK; }
esp_err_t keyvalet_piezo_preempt_for_alarm(void) { return ESP_OK; }
esp_err_t keyvalet_piezo_play_urgent_alarm(void)
{ audio = KEYVALET_PIEZO_URGENT_ALARM; ++urgent_count; return ESP_OK; }
esp_err_t keyvalet_piezo_play_alarm_chirp(void)
{ audio = KEYVALET_PIEZO_REMINDER_ALARM; ++ordinary_count; return ESP_OK; }
esp_err_t keyvalet_piezo_play_pairing_cue(void) { return ESP_OK; }
esp_err_t keyvalet_piezo_play_hold_indication(void) { return ESP_OK; }
esp_err_t keyvalet_piezo_play_ip_octet(uint8_t octet) { (void)octet; return ESP_OK; }

static void state(unsigned lock, bool open, bool initial)
{ paired_sensor_state(lock, open, initial, now_ms()); }
static void apply(void)
{ assert(keyvalet_device_controller_apply_settings() == ESP_OK); }
static void start(unsigned lock, bool present, unsigned timeout)
{
    assert(keyvalet_device_unlock_reserve(lock, present, timeout) == ESP_OK);
    assert(keyvalet_device_unlock_commit(lock) == ESP_OK);
    assert(gpio[profile.lock_gpio[lock]] && !gpio[profile.lock_gpio[1U-lock]]);
}
static void required_zero(unsigned lock)
{
    assert(keyvalet_device_unlock_reserve(lock, true, 0) != ESP_OK); /* unknown */
    state(lock, true, true);
    assert(keyvalet_device_unlock_reserve(lock, true, 0) != ESP_OK); /* open */
    configuration.sensor_monitoring_enabled[lock] = false; apply();
    assert(keyvalet_device_unlock_reserve(lock, true, 0) != ESP_OK);
    configuration.sensor_monitoring_enabled[lock] = true; apply();
    state(lock, false, false);
    start(lock, true, 0);
    advance(10); state(lock, true, false);
    assert(!s_status.sensor_unexpected_open[lock] && !s_reminder_deadline[lock]);
    advance(100000); update_timers(now_ms());
    assert(!ordinary_count && !urgent_count && s_status.lock_operation_active[lock]);
    state(lock, false, false);
    assert(!s_status.lock_operation_active[lock]);
    /* A closed cycle ends authorization, not an indefinite no-alarm capability. */
    state(lock, true, false); update_timers(now_ms());
    assert(s_status.sensor_unexpected_open[lock] && urgent_count == 1);
    assert(events[event_count-1].type == KEYVALET_EVENT_SENSOR_UNEXPECTED_OPEN);
    assert(events[event_count-1].detail == lock+1);
}
static void unexpected_zero(unsigned lock)
{
    state(lock, false, true);
    start(lock, true, 0);
    advance(5001); state(lock, true, false); update_timers(now_ms());
    assert(s_status.sensor_unexpected_open[lock] && urgent_count == 1);
    assert(!s_reminder_deadline[lock]);
}
static void positive_and_default(unsigned lock)
{
    state(lock, false, true);
    /* Other unused/open inputs do not veto the target. */
    configuration.sensor_monitoring_enabled[1U-lock] = false; apply();
    state(1U-lock, true, true);
    start(lock, true, 5);
    advance(100); state(lock, true, false);
    assert(s_reminder_deadline[lock] == now_ms()+5000);
    advance(4999); update_timers(now_ms()); assert(!ordinary_count);
    advance(1); update_timers(now_ms()); assert(ordinary_count == 1 && !urgent_count);
    state(lock, false, false); assert(audio == KEYVALET_PIEZO_NONE);
    configuration.default_door_open_timeout_s = 17; apply();
    start(lock, false, 0);
    configuration.default_door_open_timeout_s = 90; apply();
    advance(100); state(lock, true, false);
    assert(s_reminder_deadline[lock] == now_ms()+17000); /* snapshot at reserve */
    advance(17000); update_timers(now_ms()); assert(ordinary_count == 2);
}
static void independent(unsigned lock)
{
    configuration.lock_sensor_required[lock] = false;
    for (size_t i = 0; i < 4; ++i) configuration.sensor_monitoring_enabled[i] = false;
    apply();
    assert(!s_status.lock_sensor_required[lock]);
    /* Unknown and OPEN both work; an input edge during authorization is not a veto. */
    assert(keyvalet_device_unlock_reserve(lock, true, 0) == ESP_OK);
    state(lock, true, false);
    assert(keyvalet_device_unlock_commit(lock) == ESP_OK);
    assert(!s_status.lock_operation_active[lock] && !s_operation_guard[lock].reserved);
    assert(!s_last_solenoid[lock] && !s_reminder_deadline[lock]);
    assert(keyvalet_device_unlock_reserve(lock, true, 0) != ESP_OK); /* ACTIVE */
    assert(keyvalet_device_unlock_reserve(1U-lock, true, 0) != ESP_OK);
    advance(349); assert(gpio[profile.lock_gpio[lock]]);
    advance(1); assert(!gpio[10] && !gpio[3]);
    assert(keyvalet_device_unlock_reserve(lock, true, 0) != ESP_OK); /* RECOVERY */
    advance(2000);
    start(lock, true, 5); /* positive timer is irrelevant, not a door lifecycle */
    advance(100000); update_timers(now_ms());
    assert(!ordinary_count && !urgent_count && !s_status.lock_operation_active[lock]);
    assert(!s_reminder_deadline[lock]);
    start(lock, false, 0); advance(2350);
    configuration.sensor_monitoring_enabled[lock] = true; apply();
    state(lock, false, false); state(lock, true, false); update_timers(now_ms());
    assert(urgent_count == 1 && !ordinary_count); /* independently monitored */
}
static void dependency_changes(unsigned lock)
{
    state(lock, false, true);
    assert(keyvalet_device_unlock_reserve(lock, true, 5) == ESP_OK);
    configuration.lock_sensor_required[lock] = false; apply();
    assert(keyvalet_device_unlock_commit(lock) != ESP_OK);
    keyvalet_device_unlock_cancel(lock);
    assert(!gpio[10] && !gpio[3]);
    configuration.lock_sensor_required[lock] = true; apply();
    start(lock, true, 5);
    /* Admin change neither cancels nor stretches the finite hardware pulse. */
    configuration.lock_sensor_required[lock] = false; apply();
    assert(gpio[profile.lock_gpio[lock]] && !s_status.lock_operation_active[lock]);
    advance(350); assert(!gpio[10] && !gpio[3]);
    state(lock, true, false); update_timers(now_ms());
    assert(urgent_count && !ordinary_count && !s_reminder_deadline[lock]);
}
static void monitoring_and_invalid(unsigned lock)
{
    configuration.sensor_monitoring_enabled[lock] = false; apply();
    state(lock, true, true); advance(100000); update_timers(now_ms());
    assert(!ordinary_count && !urgent_count);
    assert(keyvalet_device_unlock_reserve(lock, true, 0) != ESP_OK);
    state(lock, false, false);
    for (unsigned timeout = 1; timeout <= 4; ++timeout)
        assert(keyvalet_device_unlock_reserve(lock, true, timeout) != ESP_OK);
    assert(keyvalet_device_unlock_reserve(lock, true, 3601) != ESP_OK);
    start(lock, true, 5); advance(100); state(lock, true, false);
    advance(100000); update_timers(now_ms()); assert(!ordinary_count && !urgent_count);
    /* Optional sensor-only inputs retain their independent monitoring policy. */
    configuration.sensor_monitoring_enabled[2] = false; apply();
    sensor_only_state(KEYVALET_SENSOR_3, true, false, now_ms()); update_timers(now_ms());
    assert(!urgent_count);
    sensor_only_state(KEYVALET_SENSOR_4, true, false, now_ms()); update_timers(now_ms());
    assert(urgent_count && s_status.sensor_alarm_active[3]);
    assert(!s_status.sensor_alarm_active[2] && !s_status.sensor_alarm_active[lock]);
}
int main(void)
{
    void (*cases[])(unsigned) = {required_zero, unexpected_zero, positive_and_default,
        independent, dependency_changes, monitoring_and_invalid};
    for (size_t test = 0; test < sizeof(cases)/sizeof(cases[0]); ++test) {
        for (unsigned lock = 0; lock < 2; ++lock) {
            const pid_t child = fork(); assert(child >= 0);
            if (!child) {
                assert(keyvalet_hardware_io_init(&profile) == ESP_OK);
                assert(keyvalet_device_controller_start(&profile) == ESP_OK);
                cases[test](lock);
                _exit(0);
            }
            int status; assert(waitpid(child, &status, 0) == child);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                fprintf(stderr, "controller case %zu failed on lock_%u\n", test, lock+1);
            assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }
    puts("controller configuration tests passed: both locks, dependency/zero/monitoring/races/pulse+recovery");
    return 0;
}
