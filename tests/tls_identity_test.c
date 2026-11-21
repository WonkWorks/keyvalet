/* Host fixture: actual identity/store/USB code and actual mbedTLS. Only IDF
 * services, clock and NVS durability are simulated; never opens a device. */
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include "../main/tls_identity.c"
#include "../main/tls_usb.c"

typedef struct { bool present; size_t length; uint8_t bytes[8192]; } cell_t;
static cell_t durable[3], pending[3];
static int failure;
static int64_t utc, micros;
static bool connected = true, clock_valid = true;
static char answer[1024];
static unsigned flash_depth;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{ SemaphoreHandle_t lock = malloc(sizeof(*lock)); assert(lock && !pthread_mutex_init(lock, NULL)); return lock; }
int xSemaphoreTake(SemaphoreHandle_t lock, unsigned timeout)
{ (void)timeout; assert(!pthread_mutex_lock(lock)); return 1; }
int xSemaphoreGive(SemaphoreHandle_t lock) { assert(!pthread_mutex_unlock(lock)); return 1; }
void vSemaphoreDelete(SemaphoreHandle_t lock) { pthread_mutex_destroy(lock); free(lock); }
void esp_fill_random(void *output, size_t length) { assert(getrandom(output, length, 0) == (ssize_t)length); }
bool keyvalet_time_get_utc(int64_t *output) { *output = utc; return clock_valid; }
int64_t esp_timer_get_time(void) { return micros; }
unsigned uxTaskGetStackHighWaterMark(void *task) { (void)task; return 12000; }
esp_err_t keyvalet_time_set_usb_utc(int64_t value) { if (value < 1767225600 || value > 4102444800LL) return ESP_ERR_INVALID_ARG; utc = value; clock_valid = true; return ESP_OK; }
esp_err_t keyvalet_network_status_get(keyvalet_network_status_t *status)
{ memset(status, 0, sizeof(*status)); status->connected = connected; return ESP_OK; }
const char *keyvalet_network_device_id(void) { return "keyvalet-030405"; }
esp_err_t esp_read_mac(uint8_t *bytes, int type)
{ (void)type; const uint8_t mac[] = {0, 1, 2, 3, 4, 5}; memcpy(bytes, mac, 6); return ESP_OK; }
esp_err_t keyvalet_lock_flash_begin(void) { assert(!flash_depth++); return ESP_OK; }
void keyvalet_lock_flash_end(void) { assert(flash_depth-- == 1); }
bool keyvalet_web_server_tls_activation_pending(void) { return false; }
const char *keyvalet_web_server_tls_activation_state(void) { return "IDLE"; }
esp_err_t keyvalet_web_server_request_tls_activation(void) { return ESP_OK; }
esp_err_t nvs_flash_init_partition(const char *partition) { assert(!strcmp(partition, "tls_nvs")); return ESP_OK; }
esp_err_t nvs_open_from_partition(const char *partition, const char *space, int mode, nvs_handle_t *handle)
{ assert(!strcmp(partition, "tls_nvs") && !strcmp(space, "tls_identity") && mode == NVS_READWRITE); *handle = 1; return ESP_OK; }
static unsigned cell(const char *key)
{ if (!strcmp(key, "identity0")) return 0; if (!strcmp(key, "identity1")) return 1; assert(!strcmp(key, "selected")); return 2; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *length)
{ assert(handle == 1); cell_t *c = &durable[cell(key)]; if (!c->present) return ESP_ERR_NVS_NOT_FOUND; assert(*length >= c->length); memcpy(data, c->bytes, c->length); *length = c->length; return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length)
{ assert(handle == 1 && flash_depth && length <= 8192); if (failure == 1) return ESP_FAIL; cell_t *c = &pending[cell(key)]; c->present = true; c->length = length; memcpy(c->bytes, data, length); return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{ assert(handle == 1 && flash_depth); pending[cell(key)].present = false; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t handle)
{ assert(handle == 1 && flash_depth); if (failure == 2) return ESP_FAIL; memcpy(durable, pending, sizeof(durable)); return failure == 3 ? ESP_FAIL : ESP_OK; }

int test_reboot(void)
{
    if (s_lock) vSemaphoreDelete(s_lock);
    s_lock = NULL; s_nvs = 0; s_active = s_candidate = -1; s_generation = 0;
    memset(&s_status, 0, sizeof(s_status));
    failure = 0; memcpy(pending, durable, sizeof(pending));
    clear_transfer(); s_export_kind = 0;
    return keyvalet_tls_identity_init();
}
void test_reset(void) { memset(durable, 0, sizeof(durable)); assert(test_reboot() == ESP_OK); }
void test_failure(int value) { failure = value; }
void test_clock(int64_t value, int valid) { utc = value; clock_valid = valid; }
void test_micros(int64_t value) { micros = value; }
void test_network(int value) { connected = value; }
int test_prepare(const char *hostname, int rotate)
{
    const char origins[3][192] = {"https://staff.example.com", "http://localhost:8765", "https://localhost:8765"};
    return keyvalet_tls_prepare(hostname, origins, rotate);
}
int test_export(int candidate, char *csr, char *public_key)
{ return keyvalet_tls_export(candidate, csr, 2048, public_key); }
int test_ready(void) { keyvalet_tls_status_t status; keyvalet_tls_status_get(&status); return status.ready; }
int test_writable(void) { keyvalet_tls_status_t status; keyvalet_tls_status_get(&status); return status.writable; }
unsigned test_generation(void) { keyvalet_tls_status_t status; keyvalet_tls_status_get(&status); return status.generation; }
static void capture(const char *const *values, size_t count)
{
    cJSON *array = cJSON_CreateStringArray(values, (int)count);
    assert(array && cJSON_PrintPreallocated(array, answer, sizeof(answer), false)); cJSON_Delete(array);
}
const char *test_usb(const uint8_t *body, size_t length)
{ memset(answer, 0, sizeof(answer)); return keyvalet_tls_usb_handle(body, length, capture) ? answer : NULL; }
