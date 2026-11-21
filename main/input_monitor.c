#include "input_monitor.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor_debounce.h"

static const char *TAG = "inputs";

typedef struct {
    gpio_num_t gpio;
    keyvalet_sensor_debounce_t debounce;
} debounced_input_t;

static const keyvalet_hardware_profile_t *s_profile;
static keyvalet_input_callbacks_t s_callbacks;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool debounce_sample(debounced_input_t *input, int64_t sample_time_ms,
                            uint32_t debounce_ms)
{
    return keyvalet_sensor_debounce_sample(
        &input->debounce, gpio_get_level(input->gpio), sample_time_ms, debounce_ms);
}

static void report_button_change(int level)
{
    const bool is_pressed = (level == 0) == s_profile->button_active_low;
    ESP_LOGI(TAG, "Physical button changed: %s", is_pressed ? "PRESSED" : "RELEASED");
    s_callbacks.button_state(is_pressed, false);
}

static void report_sensor_change(keyvalet_sensor_id_t sensor, int level, bool initial)
{
    const bool is_open = (level != 0) == s_profile->sensor_open_high;
    ESP_LOGI(TAG, "sensor_%u changed: %s", (unsigned)sensor + 1U,
             is_open ? "OPEN" : "CLOSED");
    s_callbacks.sensor_state(sensor, is_open, initial);
}

static void input_monitor_task(void *context)
{
    const keyvalet_hardware_profile_t *profile = context;
    const int64_t initial_time_ms = now_ms();
    debounced_input_t button = {
        .gpio = profile->button_gpio,
    };
    keyvalet_sensor_debounce_init(&button.debounce,
                                  gpio_get_level(profile->button_gpio),
                                  initial_time_ms);

    const bool button_pressed =
        (button.debounce.stable_level == 0) == profile->button_active_low;
    ESP_LOGI(TAG, "Physical button initial observed state: %s",
             button_pressed ? "PRESSED" : "RELEASED");
    s_callbacks.button_state(button_pressed, true);

    debounced_input_t sensors[KEYVALET_SENSOR_COUNT] = {0};
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index) {
        sensors[index].gpio = profile->sensor_gpio[index];
        keyvalet_sensor_debounce_init(
            &sensors[index].debounce, gpio_get_level(sensors[index].gpio),
            initial_time_ms);
        report_sensor_change(index, sensors[index].debounce.stable_level, true);
    }

    while (true) {
        const int64_t sample_time_ms = now_ms();
        if (debounce_sample(&button, sample_time_ms, profile->input_debounce_ms))
            report_button_change(button.debounce.stable_level);
        for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index) {
            if (debounce_sample(&sensors[index], sample_time_ms,
                                profile->input_debounce_ms)) {
                report_sensor_change(index,
                                     sensors[index].debounce.stable_level, false);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t keyvalet_input_monitor_start(const keyvalet_hardware_profile_t *profile,
                                       const keyvalet_input_callbacks_t *callbacks)
{
    if (profile == NULL || callbacks == NULL || callbacks->sensor_state == NULL ||
        callbacks->button_state == NULL ||
        s_profile != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_profile = profile;
    s_callbacks = *callbacks;
    if (xTaskCreate(input_monitor_task, "input_monitor", 3072, (void *)profile, 5, NULL) != pdPASS) {
        s_profile = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
