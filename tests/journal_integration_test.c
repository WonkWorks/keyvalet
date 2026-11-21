#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_partition.h"
#include "esp_timer.h"
#include "hardware_io.h"
#include "journal.h"
/* Compile the real cursor module; reset only its volatile state to model reboot. */
#include "../main/journal_sync.c"

static uint8_t flash[8192];
static const esp_partition_t partition = {.size = sizeof(flash)};
static struct {
    uint8_t durable[64], pending[64];
    size_t length, pending_length;
} nvs[3];
static int commit_failure;
static int64_t simulated_us, timer_due;
static esp_timer_create_args_t timer_args;
static unsigned gpio[32], erase_count, write_count, waited_ticks;
static atomic_bool pulse_request_started, pulse_request_finished;
static const keyvalet_hardware_profile_t profile = {
    .lock_gpio = {10, 3}, .sensor_gpio = {6, 7, 18, 19},
    .status_led_gpio = 4, .button_gpio = 2,
    .solenoid_active_high = true, .solenoid_pulse_ms = 350,
    .lock_recovery_ms = 2000,
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
int64_t esp_timer_get_time(void) { return simulated_us; }
void vTaskDelay(unsigned ticks)
{
    waited_ticks += ticks;
    simulated_us += ticks * 1000;
    if (timer_due && simulated_us >= timer_due) {
        timer_due = 0;
        timer_args.callback(timer_args.arg);
    }
}
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle)
{ (void)task; (void)name; (void)stack; (void)arg; (void)priority; (void)handle; return 1; }
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *timer)
{ timer_args = *args; *timer = &timer_args; return ESP_OK; }
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout)
{ (void)timer; timer_due = simulated_us + (int64_t)timeout; return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level)
{ gpio[pin] = level; return ESP_OK; }
esp_err_t gpio_config(const gpio_config_t *config)
{ (void)config; return ESP_OK; }
bool keyvalet_time_get_utc(int64_t *utc)
{ *utc = 1800000000; return true; }

const esp_partition_t *esp_partition_find_first(int type,
    esp_partition_subtype_t subtype, const char *label)
{ (void)type; (void)subtype; assert(strcmp(label, "journal") == 0); return &partition; }
esp_err_t esp_partition_read(const esp_partition_t *p, size_t offset, void *data, size_t length)
{ assert(p == &partition && offset + length <= sizeof(flash)); memcpy(data, flash + offset, length); return ESP_OK; }
esp_err_t esp_partition_write(const esp_partition_t *p, size_t offset, const void *data, size_t length)
{
    assert(p == &partition && offset + length <= sizeof(flash));
    assert(!gpio[10] && !gpio[3]); /* real gate excludes active pulse for all writes */
    for (size_t i = 0; i < length; ++i) {
        const uint8_t byte = ((const uint8_t *)data)[i];
        assert((flash[offset + i] & byte) == byte);
        flash[offset + i] &= byte;
    }
    ++write_count; simulated_us += 100;
    return ESP_OK;
}
esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t offset, size_t length)
{
    assert(p == &partition && offset + length <= sizeof(flash));
    assert(length == 4096U && offset % 4096U == 0);
    assert(!gpio[10] && !gpio[3]);
    memset(flash + offset, 0xff, length);
    ++erase_count; simulated_us += 25000; /* explicit simulated latency */
    return ESP_OK;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{ (void)mode; *handle = strcmp(name, "keyvalet_sync") == 0 ? 2U : 1U; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *length)
{
    (void)key;
    if (nvs[handle].length == 0) return ESP_ERR_NVS_NOT_FOUND;
    assert(*length >= nvs[handle].length);
    memcpy(data, nvs[handle].durable, nvs[handle].length);
    *length = nvs[handle].length;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length)
{
    (void)key;
    if (handle == 1) assert(!gpio[10] && !gpio[3]);
    assert(length <= sizeof(nvs[handle].pending));
    memcpy(nvs[handle].pending, data, length);
    nvs[handle].pending_length = length;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    if (commit_failure == 1 && handle == 2) return ESP_FAIL;
    nvs[handle].length = nvs[handle].pending_length;
    memcpy(nvs[handle].durable, nvs[handle].pending, nvs[handle].length);
    return commit_failure == 2 && handle == 2 ? ESP_FAIL : ESP_OK;
}

static void reboot_cursor(void)
{
    vSemaphoreDelete(s_lock);
    s_lock = NULL; s_ready = false; s_acknowledged_sequence = 0;
    commit_failure = 0;
    assert(keyvalet_journal_sync_init() == ESP_OK);
}

static void *pulse_request(void *argument)
{
    atomic_store(&pulse_request_started, true);
    /* RECOVERY rejects after acquiring the gate; must not finish while held. */
    (void)keyvalet_lock_pulse(&profile, (keyvalet_lock_id_t)(uintptr_t)argument);
    atomic_store(&pulse_request_finished, true);
    return NULL;
}

static void flash_gate(void)
{
    for (unsigned lock = 0; lock < 2; ++lock) {
        if (timer_due) vTaskDelay(2000);
        assert(keyvalet_lock_pulse(&profile, lock) == ESP_OK);
        assert(gpio[profile.lock_gpio[lock]] == 1);
        const unsigned before = waited_ticks;
        assert(keyvalet_lock_flash_begin() == ESP_OK);
        assert(waited_ticks - before == 350U);
        assert(!gpio[10] && !gpio[3]);
        pthread_t thread;
        atomic_store(&pulse_request_started, false);
        atomic_store(&pulse_request_finished, false);
        assert(pthread_create(&thread, NULL, pulse_request, (void *)(uintptr_t)lock) == 0);
        while (!atomic_load(&pulse_request_started)) { }
        const struct timespec delay = {.tv_nsec = 1000000};
        nanosleep(&delay, NULL);
        assert(!atomic_load(&pulse_request_finished));
        keyvalet_lock_flash_end();
        assert(pthread_join(thread, NULL) == 0);
        assert(atomic_load(&pulse_request_finished));
    }
}

int main(void)
{
    memset(flash, 0xff, sizeof(flash));
    assert(keyvalet_hardware_io_init(&profile) == ESP_OK);
    flash_gate();
    assert(keyvalet_journal_init() == ESP_OK);
    assert(keyvalet_journal_sync_init() == ESP_OK);
    bool already;
    for (unsigned seq = 1; seq <= 128; ++seq)
        assert(keyvalet_journal_append(KEYVALET_EVENT_SENSOR_OPEN, 0, 1) == ESP_OK);
    assert(keyvalet_journal_sync_advance(100, &already) == ESP_OK && !already);
    for (unsigned seq = 129; seq <= 193; ++seq) {
        if (seq == 193) {
            vTaskDelay(2000);
            assert(keyvalet_lock_pulse(&profile, KEYVALET_LOCK_1) == ESP_OK);
        }
        assert(keyvalet_journal_append(KEYVALET_EVENT_SENSOR_CLOSED, 0, 1) == ESP_OK);
    }
    keyvalet_journal_sync_status_t status;
    assert(keyvalet_journal_sync_get_status(&status) == ESP_OK);
    assert(status.oldest_available_sequence == 129 && status.latest_available_sequence == 193);
    assert(status.acknowledged_sequence == 100 && status.unrecoverable_gap);
    assert(status.gap_start_sequence == 101 && status.gap_end_sequence == 128);
    keyvalet_journal_record_t batch[8];
    keyvalet_journal_stats_t snapshot;
    size_t count;
    uint64_t next, gap_start, gap_end;
    bool more;
    assert(keyvalet_journal_read_batch(101, batch, 8, &count, &next, &more,
        &gap_start, &gap_end, &snapshot) == ESP_OK);
    assert(count == 0 && more && next == 129 && gap_start == 101 && gap_end == 128);
    assert(snapshot.oldest_sequence == 129 && snapshot.latest_sequence == 193);
    assert(keyvalet_journal_read_batch(129, batch, 8, &count, &next, &more,
        &gap_start, &gap_end, &snapshot) == ESP_OK);
    assert(count == 8 && next == 137 && batch[0].sequence == 129 && batch[7].sequence == 136);
    assert(keyvalet_journal_read_batch(129, batch, 9, &count, &next, &more,
        &gap_start, &gap_end, &snapshot) == ESP_ERR_INVALID_ARG);
    reboot_cursor();
    assert(keyvalet_journal_sync_get_status(&status) == ESP_OK && status.unrecoverable_gap);
    commit_failure = 1;
    assert(keyvalet_journal_sync_advance(180, &already) == ESP_FAIL);
    assert(keyvalet_journal_sync_advance(150, &already) == ESP_ERR_INVALID_STATE);
    reboot_cursor();
    assert(keyvalet_journal_sync_get_status(&status) == ESP_OK && status.acknowledged_sequence == 100);
    commit_failure = 2; /* persistence succeeded, response failed/lost */
    assert(keyvalet_journal_sync_advance(180, &already) == ESP_FAIL);
    commit_failure = 0;
    assert(keyvalet_journal_sync_advance(150, &already) == ESP_ERR_INVALID_STATE);
    assert(keyvalet_journal_sync_get_status(&status) == ESP_ERR_INVALID_STATE);
    reboot_cursor();
    assert(keyvalet_journal_sync_get_status(&status) == ESP_OK && status.acknowledged_sequence == 180);
    assert(!status.unrecoverable_gap && status.unsynchronized_records);
    assert(keyvalet_journal_sync_advance(180, &already) == ESP_OK && already);
    assert(keyvalet_journal_sync_advance(99, &already) == ESP_OK && already);
    assert(keyvalet_journal_sync_advance(194, &already) == ESP_ERR_INVALID_ARG);
    assert(keyvalet_journal_sync_advance(193, &already) == ESP_OK && !already);
    assert(keyvalet_journal_sync_get_status(&status) == ESP_OK && !status.unsynchronized_records);
    keyvalet_journal_stats_t stats;
    assert(keyvalet_journal_get_stats(&stats) == ESP_OK);
    assert(stats.max_erase_us == 25000 && stats.max_append_us >= 25000);
    /* Real journal adapter must wait for either diagnostic pulse before writing. */
    vTaskDelay(2000);
    assert(keyvalet_lock_pulse(&profile, KEYVALET_LOCK_2) == ESP_OK);
    const unsigned before = write_count;
    assert(keyvalet_journal_append(KEYVALET_EVENT_SENSOR_OPEN, 0, 2) == ESP_OK);
    assert(!gpio[10] && !gpio[3] && write_count == before + 2U);
    printf("journal integration tests passed: real adapter/cursor/gate, %u sector erases; "
           "simulated erase=25ms; both lock channels wait 350ms for pulse completion\n", erase_count);
    return 0;
}
