/* Real canonical settings/replay adapters; shared mock NVS survives forked
 * firmware boots, while all module statics begin pristine on every boot. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "freertos/semphr.h"
#include "nvs.h"
#include "replay_record.h"
#include "replay_store.h"
#include "settings.h"
#include "settings_schema.h"

typedef struct {
    unsigned handle;
    char key[16];
    size_t size;
    uint8_t data[128];
} entry_t;
static entry_t *durable;
static entry_t pending;
static int failure; /* 1: before commit, 2: durable but error returned */
static unsigned first, last;
static unsigned lock_bits, revision = 2;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t mutex = malloc(sizeof(*mutex));
    assert(mutex && pthread_mutex_init(mutex, NULL) == 0);
    return mutex;
}
int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{ (void)timeout; return pthread_mutex_lock(mutex) == 0; }
int xSemaphoreGive(SemaphoreHandle_t mutex)
{ return pthread_mutex_unlock(mutex) == 0; }
void vSemaphoreDelete(SemaphoreHandle_t mutex)
{ assert(pthread_mutex_destroy(mutex) == 0); free(mutex); }
uint32_t esp_crc32_le(uint32_t crc, const uint8_t *data, uint32_t length)
{ assert(crc == 0); return keyvalet_replay_crc32(data, length); }
esp_err_t nvs_flash_init_partition(const char *partition)
{ assert(strcmp(partition, "replay_nvs") == 0); return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{ assert(strcmp(name, "keyvalet") == 0 && mode == NVS_READWRITE); *handle = 1; return ESP_OK; }
esp_err_t nvs_open_from_partition(const char *partition, const char *name,
    int mode, nvs_handle_t *handle)
{
    assert(strcmp(partition, "replay_nvs") == 0 && strcmp(name, "replay") == 0);
    assert(mode == NVS_READWRITE); *handle = 2; return ESP_OK;
}
static entry_t *find(unsigned handle, const char *key, bool create)
{
    entry_t *empty = NULL;
    for (size_t i = 0; i < 170; ++i) {
        if (durable[i].size && durable[i].handle == handle &&
            strcmp(durable[i].key, key) == 0) return &durable[i];
        if (!durable[i].size) empty = &durable[i];
    }
    assert(!create || empty);
    return create ? empty : NULL;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *size)
{
    entry_t *entry = find(handle, key, false);
    if (!entry) return ESP_ERR_NVS_NOT_FOUND;
    const size_t capacity = *size;
    *size = entry->size;
    if (!data) return ESP_OK;
    if (capacity < entry->size) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(data, entry->data, entry->size);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size)
{
    assert(size <= sizeof(pending.data) && strlen(key) < sizeof(pending.key));
    pending = (entry_t){.handle = handle, .size = size};
    strcpy(pending.key, key);
    memcpy(pending.data, data, size);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == pending.handle);
    if (failure == 1) return ESP_FAIL;
    *find(handle, pending.key, true) = pending;
    return failure == 2 ? ESP_FAIL : ESP_OK;
}
static void boot(void (*test)(void))
{
    const pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) { test(); _exit(0); }
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static void settings_create(void)
{
    assert(keyvalet_settings_init() == ESP_OK);
    keyvalet_settings_t settings;
    assert(keyvalet_settings_get(&settings) == ESP_OK);
    assert(settings.schema_version == 4 && settings.revision == 1);
    assert(settings.default_door_open_timeout_s == 90);
    assert(settings.sensor_monitoring_enabled[0] && !settings.sensor_monitoring_enabled[1]);
    assert(settings.lock_sensor_required[0] && settings.lock_sensor_required[1]);
    settings.lock_sensor_required[0] = false;
    settings.default_door_open_timeout_s = 17;
    settings.sensor_monitoring_enabled[1] = true;
    assert(keyvalet_settings_update(&settings, 1) == ESP_OK);
    assert(keyvalet_settings_update(&settings, 1) == ESP_ERR_INVALID_STATE);
}
static void settings_reboot(void)
{
    assert(keyvalet_settings_init() == ESP_OK);
    keyvalet_settings_t settings;
    assert(keyvalet_settings_get(&settings) == ESP_OK);
    assert(settings.revision == 2 && settings.default_door_open_timeout_s == 17);
    assert(!settings.lock_sensor_required[0] && settings.lock_sensor_required[1]);
    for (size_t i = 0; i < 4; ++i) assert(settings.sensor_monitoring_enabled[i]);
}
static void settings_bad(void)
{ assert(keyvalet_settings_init() != ESP_OK); }
static void settings_flags_write(void)
{
    assert(keyvalet_settings_init() == ESP_OK);
    keyvalet_settings_t settings;
    assert(keyvalet_settings_get(&settings) == ESP_OK && settings.revision == revision);
    for (size_t lock = 0; lock < 2; ++lock)
        settings.lock_sensor_required[lock] = (lock_bits & (1U << lock)) != 0;
    const esp_err_t result = keyvalet_settings_update(&settings, revision);
    assert(result == (failure ? ESP_FAIL : ESP_OK));
    assert(keyvalet_settings_update(&settings, revision) != ESP_OK);
}
static void settings_flags_reboot(void)
{
    assert(keyvalet_settings_init() == ESP_OK);
    keyvalet_settings_t settings;
    assert(keyvalet_settings_get(&settings) == ESP_OK && settings.revision == revision);
    for (size_t lock = 0; lock < 2; ++lock)
        assert(settings.lock_sensor_required[lock] == ((lock_bits & (1U << lock)) != 0));
}
static void make_digests(unsigned sequence, uint8_t operation[16], uint8_t nonce[16])
{
    memset(operation, 0, 16); memset(nonce, 0xa5, 16);
    memcpy(operation, &sequence, sizeof(sequence));
    memcpy(nonce, &sequence, sizeof(sequence));
}
static void replay_create(void)
{
    keyvalet_replay_window_t window;
    assert(keyvalet_replay_store_init(false, &window) == ESP_OK);
    assert(window.count == 0 && window.last_accept_utc == 0);
}
static void replay_init_failure(void)
{
    keyvalet_replay_window_t window;
    assert(keyvalet_replay_store_init(false, &window) == ESP_FAIL);
    uint8_t operation[16], nonce[16];
    make_digests(1, operation, nonce);
    assert(keyvalet_replay_store_append(operation, nonce, 1001) == ESP_ERR_INVALID_STATE);
}
static void replay_cycle(void)
{
    keyvalet_replay_window_t window;
    assert(keyvalet_replay_store_init(true, &window) == ESP_OK);
    assert(window.count == (first > 160 ? 160 : first - 1));
    assert(window.last_accept_utc == (first == 1 ? 0 : (int64_t)first + 999));
    if (first > 1) {
        uint8_t operation[16], nonce[16];
        make_digests(first - 1, operation, nonce);
        assert(keyvalet_replay_window_contains(&window, operation, nonce));
    }
    for (unsigned sequence = first; sequence <= last; ++sequence) {
        uint8_t operation[16], nonce[16];
        make_digests(sequence, operation, nonce);
        const esp_err_t result = keyvalet_replay_store_append(operation, nonce, sequence + 1000);
        if (failure) {
            assert(result == ESP_FAIL);
            assert(keyvalet_replay_store_append(operation, nonce, sequence + 1001) == ESP_ERR_INVALID_STATE);
            return;
        }
        assert(result == ESP_OK);
    }
}
static void replay_bad_enrolled(void)
{ keyvalet_replay_window_t window; assert(keyvalet_replay_store_init(true, &window) != ESP_OK); }
static void replay_bad_unenrolled(void)
{ keyvalet_replay_window_t window; assert(keyvalet_replay_store_init(false, &window) != ESP_OK); }

int main(void)
{
    durable = mmap(NULL, 170 * sizeof(*durable), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(durable != MAP_FAILED);
    boot(settings_create);
    assert(find(1, "settings", false)->size == 28);
    boot(settings_reboot);
    entry_t *settings = find(1, "settings", false);
    settings->data[24] ^= 1;
    boot(settings_bad);
    settings->data[24] ^= 1;
    boot(settings_reboot);
    for (lock_bits = 0; lock_bits < 4; ++lock_bits) {
        boot(settings_flags_write); ++revision;
        boot(settings_flags_reboot);
    }
    lock_bits = 0; failure = 1;
    boot(settings_flags_write); /* no durable update; old flags/revision survive */
    lock_bits = 3; failure = 0; boot(settings_flags_reboot);
    lock_bits = 0; failure = 2;
    boot(settings_flags_write); /* durable despite error; further writes disabled */
    failure = 0; ++revision; boot(settings_flags_reboot);
    settings->data[16] |= 4U; /* reserved high mask bit, even with valid CRC */
    uint32_t crc = keyvalet_replay_crc32(settings->data, 24);
    memcpy(settings->data + 24, &crc, sizeof(crc));
    boot(settings_bad);

    boot(replay_bad_enrolled); /* trust cannot silently lose replay state */
    failure = 1;
    boot(replay_init_failure);
    assert(!find(2, "meta", false));
    failure = 2;
    boot(replay_init_failure);
    failure = 0;
    boot(replay_create);
    assert(find(2, "meta", false)->size == 12);
    for (first = 1; first <= 1000; first = last + 1) {
        last = first + 136;
        if (last > 1000) last = 1000;
        boot(replay_cycle);
    }
    first = 1001; last = 1001; failure = 1;
    boot(replay_cycle); /* commit failed, no record persisted */
    failure = 2;
    boot(replay_cycle); /* commit durable, error returned; RAM remains fail-closed */
    failure = 0; first = 1002; last = 1001;
    boot(replay_cycle); /* canonical reboot recovered the ambiguous commit */
    entry_t *marker = find(2, "meta", false);
    marker->data[8] ^= 1;
    boot(replay_bad_enrolled);
    marker->data[8] ^= 1;
    marker->size = 0;
    boot(replay_bad_enrolled);
    boot(replay_bad_unenrolled); /* orphan records are not a virgin partition */
    assert(munmap(durable, 170 * sizeof(*durable)) == 0);
    puts("canonical storage integration passed: blank/reboot/CRC/replay wraps/ambiguous commits");
    return 0;
}
