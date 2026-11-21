#include "connectivity_indicator.h"

#include <stdbool.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hardware_io.h"

static const char *TAG = "connectivity_led";
#define INITIAL_WIFI_GRACE_US (30LL * 1000LL * 1000LL)

static const keyvalet_hardware_profile_t *s_profile;
static SemaphoreHandle_t s_lock;
static bool s_wifi_connected;
static bool s_wifi_has_connected;
static size_t s_client_count;

static void indicator_task(void *context)
{
    (void)context;
    unsigned phase = 0;
    const int64_t fault_indication_start =
        esp_timer_get_time() + INITIAL_WIFI_GRACE_US;
    while (true) {
        bool wifi_connected;
        bool wifi_has_connected;
        size_t client_count;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        wifi_connected = s_wifi_connected;
        wifi_has_connected = s_wifi_has_connected;
        client_count = s_client_count;
        xSemaphoreGive(s_lock);

        const bool show_wifi_fault =
            !wifi_connected &&
            (wifi_has_connected || esp_timer_get_time() >= fault_indication_start);
        const bool on = client_count > 0 || (show_wifi_fault && phase < 4U);
        const esp_err_t result = keyvalet_status_led_set_automatic(s_profile, on);
        if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Unable to update connectivity LED: %s", esp_err_to_name(result));
        }
        phase = (phase + 1U) % 8U;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
esp_err_t keyvalet_connectivity_indicator_start(
    const keyvalet_hardware_profile_t *profile)
{
    if (profile == NULL || s_profile != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_profile = profile;
    if (xTaskCreate(indicator_task, "connectivity_led", 2048, NULL, 2, NULL) != pdPASS) {
        s_profile = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void keyvalet_connectivity_indicator_set_wifi(bool connected)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_wifi_connected = connected;
    if (connected) {
        s_wifi_has_connected = true;
    }
    if (!connected) {
        s_client_count = 0;
    }
    xSemaphoreGive(s_lock);
}

void keyvalet_connectivity_indicator_set_clients(size_t client_count)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_client_count = client_count;
    xSemaphoreGive(s_lock);
}
