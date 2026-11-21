#include "time_sync.h"

#include <stdatomic.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

#define KEYVALET_EARLIEST_VALID_UTC 1767225600LL

static const char *TAG = "time_sync";
static atomic_bool s_time_valid;
static bool s_initialized;

static void time_sync_notification(struct timeval *time_value)
{
    if (time_value == NULL || time_value->tv_sec < KEYVALET_EARLIEST_VALID_UTC) {
        atomic_store(&s_time_valid, false);
        ESP_LOGE(TAG, "SNTP supplied an invalid UTC value");
        return;
    }
    atomic_store(&s_time_valid, true);
    ESP_LOGI(TAG, "UTC synchronized: %lld", (long long)time_value->tv_sec);
}
esp_err_t keyvalet_time_sync_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = time_sync_notification;
    const esp_err_t result = esp_netif_sntp_init(&config);
    if (result == ESP_OK) {
        s_initialized = true;
        ESP_LOGI(TAG, "SNTP initialized; UTC remains invalid until synchronization");
    }
    return result;
}

bool keyvalet_time_get_utc(int64_t *utc_seconds)
{
    if (utc_seconds == NULL || !atomic_load(&s_time_valid)) {
        return false;
    }
    const time_t now = time(NULL);
    if ((int64_t)now < KEYVALET_EARLIEST_VALID_UTC) {
        atomic_store(&s_time_valid, false);
        ESP_LOGE(TAG, "System UTC became invalid; new journal timestamps disabled");
        return false;
    }
    *utc_seconds = (int64_t)now;
    return true;
}

bool keyvalet_time_is_valid(void)
{
    int64_t ignored;
    return keyvalet_time_get_utc(&ignored);
}

esp_err_t keyvalet_time_set_usb_utc(int64_t utc_seconds)
{
    if (utc_seconds < KEYVALET_EARLIEST_VALID_UTC || utc_seconds > 4102444800LL)
        return ESP_ERR_INVALID_ARG;
    struct timeval value = {.tv_sec = (time_t)utc_seconds, .tv_usec = 0};
    if (settimeofday(&value, NULL)) return ESP_FAIL;
    atomic_store(&s_time_valid, true);
    return ESP_OK;
}
