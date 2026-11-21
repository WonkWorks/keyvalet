#include "hardware_io.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lock_arbiter.h"

static SemaphoreHandle_t s_output_lock;
static bool s_led_on;
static bool s_led_manual_override;
static esp_timer_handle_t s_solenoid_timer;
static bool s_safe_outputs_ready;
static bool s_maintenance;
static const keyvalet_hardware_profile_t *s_profile;
static keyvalet_lock_arbiter_t s_arbiter;
static int64_t s_recovery_deadline_us;

esp_err_t keyvalet_lock_flash_begin(void)
{
    if (s_output_lock == NULL) return ESP_ERR_INVALID_STATE;
    for (;;) {
        xSemaphoreTake(s_output_lock, portMAX_DELAY);
        if (s_arbiter.phase != KEYVALET_LOCK_PHASE_ACTIVE) return ESP_OK;
        xSemaphoreGive(s_output_lock);
        /* The timer task can acquire s_output_lock and drive both outputs LOW. */
        vTaskDelay(1);
    }
}

void keyvalet_lock_flash_end(void)
{
    xSemaphoreGive(s_output_lock);
}

esp_err_t keyvalet_lock_maintenance_begin(void)
{
    if (!s_output_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    const bool allowed = !s_maintenance && s_arbiter.phase != KEYVALET_LOCK_PHASE_ACTIVE &&
        s_arbiter.phase != KEYVALET_LOCK_PHASE_RESERVED;
    if (allowed) s_maintenance = true;
    xSemaphoreGive(s_output_lock);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void keyvalet_lock_maintenance_end(void)
{
    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    s_maintenance = false;
    xSemaphoreGive(s_output_lock);
}

static uint32_t output_level(bool active_high, bool active)
{
    return active == active_high ? 1U : 0U;
}

static esp_err_t preload_solenoids_inactive(
    const keyvalet_hardware_profile_t *profile)
{
    const uint32_t inactive = output_level(profile->solenoid_active_high, false);
    esp_err_t first_error = ESP_OK;
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index) {
        const esp_err_t result = gpio_set_level(profile->lock_gpio[index], inactive);
        if (first_error == ESP_OK && result != ESP_OK) first_error = result;
    }
    return first_error;
}

static esp_err_t force_solenoids_inactive_locked(
    const keyvalet_hardware_profile_t *profile)
{
    const esp_err_t result = preload_solenoids_inactive(profile);
    if (result != ESP_OK) {
        s_safe_outputs_ready = false;
        keyvalet_lock_arbiter_fault(&s_arbiter);
    }
    return result;
}

esp_err_t keyvalet_status_led_set(const keyvalet_hardware_profile_t *profile, bool on)
{
    if (profile == NULL || s_output_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    const esp_err_t result = gpio_set_level(
        profile->status_led_gpio, output_level(profile->status_led_active_high, on));
    if (result == ESP_OK) {
        s_led_on = on;
    }
    xSemaphoreGive(s_output_lock);
    return result;
}

esp_err_t keyvalet_status_led_set_automatic(
    const keyvalet_hardware_profile_t *profile, bool on)
{
    if (profile == NULL || s_output_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    if (s_led_manual_override) {
        xSemaphoreGive(s_output_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = gpio_set_level(
        profile->status_led_gpio, output_level(profile->status_led_active_high, on));
    if (result == ESP_OK) {
        s_led_on = on;
    }
    xSemaphoreGive(s_output_lock);
    return result;
}

esp_err_t keyvalet_status_led_toggle(const keyvalet_hardware_profile_t *profile, bool *is_on)
{
    if (profile == NULL || is_on == NULL || s_output_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    const bool next_state = !s_led_on;
    s_led_manual_override = true;
    const esp_err_t result = gpio_set_level(
        profile->status_led_gpio, output_level(profile->status_led_active_high, next_state));
    if (result == ESP_OK) {
        s_led_on = next_state;
        *is_on = next_state;
    }
    xSemaphoreGive(s_output_lock);
    return result;
}

bool keyvalet_status_led_is_on(void)
{
    if (s_output_lock == NULL) {
        return false;
    }
    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    const bool is_on = s_led_on;
    xSemaphoreGive(s_output_lock);
    return is_on;
}

esp_err_t keyvalet_solenoid_force_inactive(const keyvalet_hardware_profile_t *profile)
{
    if (profile == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_output_lock == NULL || profile != s_profile) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    const esp_err_t result = force_solenoids_inactive_locked(profile);
    xSemaphoreGive(s_output_lock);
    return result;
}

static void solenoid_timer_callback(void *context)
{
    (void)context;
    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    if (s_arbiter.phase == KEYVALET_LOCK_PHASE_ACTIVE) {
        if (force_solenoids_inactive_locked(s_profile) != ESP_OK) {
            xSemaphoreGive(s_output_lock);
            return;
        }
        keyvalet_lock_arbiter_pulse_complete(&s_arbiter, s_profile->lock_recovery_ms);
        if (s_arbiter.phase == KEYVALET_LOCK_PHASE_RECOVERY &&
            s_profile->lock_recovery_ms > 0) {
            s_recovery_deadline_us = esp_timer_get_time() +
                                     s_profile->lock_recovery_ms * 1000LL;
            if (esp_timer_start_once(s_solenoid_timer,
                                     s_profile->lock_recovery_ms * 1000ULL) != ESP_OK) {
                s_recovery_deadline_us = 0;
                keyvalet_lock_arbiter_fault(&s_arbiter);
                s_safe_outputs_ready = false;
            }
        }
    } else if (s_arbiter.phase == KEYVALET_LOCK_PHASE_RECOVERY) {
        keyvalet_lock_arbiter_recovery_complete(&s_arbiter);
        s_recovery_deadline_us = 0;
    }
    xSemaphoreGive(s_output_lock);
}

static bool lock_request_valid(const keyvalet_hardware_profile_t *profile,
                               keyvalet_lock_id_t lock)
{
    return profile != NULL && profile == s_profile && s_output_lock != NULL &&
           s_solenoid_timer != NULL && lock < KEYVALET_LOCK_COUNT &&
           profile->lock_gpio[lock] != GPIO_NUM_NC;
}

static esp_err_t start_lock_pulse_locked(
    const keyvalet_hardware_profile_t *profile, keyvalet_lock_id_t lock)
{
    esp_err_t result = force_solenoids_inactive_locked(profile);
    if (result == ESP_OK)
        result = gpio_set_level(
            profile->lock_gpio[lock], output_level(profile->solenoid_active_high, true));
    if (result == ESP_OK)
        result = esp_timer_start_once(s_solenoid_timer,
                                      profile->solenoid_pulse_ms * 1000ULL);
    if (result != ESP_OK) {
        (void)force_solenoids_inactive_locked(profile);
        s_safe_outputs_ready = false;
        keyvalet_lock_arbiter_fault(&s_arbiter);
    }
    return result;
}

esp_err_t keyvalet_lock_pulse(const keyvalet_hardware_profile_t *profile,
                             keyvalet_lock_id_t lock)
{
    if (!lock_request_valid(profile, lock)) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    if (s_maintenance || !s_safe_outputs_ready || !keyvalet_lock_arbiter_request(&s_arbiter, lock)) {
        xSemaphoreGive(s_output_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = start_lock_pulse_locked(profile, lock);
    xSemaphoreGive(s_output_lock);
    return result;
}

esp_err_t keyvalet_lock_reserve(const keyvalet_hardware_profile_t *profile,
                               keyvalet_lock_id_t lock)
{
    if (!lock_request_valid(profile, lock)) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    const bool reserved = !s_maintenance && s_safe_outputs_ready &&
                          keyvalet_lock_arbiter_reserve(&s_arbiter, lock);
    xSemaphoreGive(s_output_lock);
    return reserved ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t keyvalet_lock_pulse_reserved(
    const keyvalet_hardware_profile_t *profile, keyvalet_lock_id_t lock)
{
    if (!lock_request_valid(profile, lock)) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    if (s_maintenance || !s_safe_outputs_ready ||
        !keyvalet_lock_arbiter_start_reserved(&s_arbiter, lock)) {
        xSemaphoreGive(s_output_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = start_lock_pulse_locked(profile, lock);
    xSemaphoreGive(s_output_lock);
    return result;
}

esp_err_t keyvalet_lock_cancel_reservation(
    const keyvalet_hardware_profile_t *profile, keyvalet_lock_id_t lock)
{
    if (!lock_request_valid(profile, lock)) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    const bool cancelled =
        keyvalet_lock_arbiter_cancel_reservation(&s_arbiter, lock);
    xSemaphoreGive(s_output_lock);
    return cancelled ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t keyvalet_lock_status_get(keyvalet_lock_status_t *status)
{
    if (status == NULL || s_output_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    status->phase = s_arbiter.phase;
    status->selected = s_arbiter.selected;
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index)
        status->active[index] = s_arbiter.phase == KEYVALET_LOCK_PHASE_ACTIVE &&
                                s_arbiter.selected == index;
    const int64_t remaining_us = s_recovery_deadline_us - esp_timer_get_time();
    status->recovery_remaining_ms =
        s_arbiter.phase == KEYVALET_LOCK_PHASE_RECOVERY && remaining_us > 0
            ? (uint32_t)((remaining_us + 999) / 1000) : 0;
    xSemaphoreGive(s_output_lock);
    return ESP_OK;
}

esp_err_t keyvalet_sensor_configure(const keyvalet_hardware_profile_t *profile,
                                   keyvalet_sensor_id_t sensor)
{
    if (profile == NULL || sensor >= KEYVALET_SENSOR_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << profile->sensor_gpio[sensor],
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = profile->sensor_internal_pullup[sensor]
                          ? GPIO_PULLUP_ENABLE
                          : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

esp_err_t keyvalet_hardware_io_init(const keyvalet_hardware_profile_t *profile)
{
    if (profile == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_output_lock != NULL ||
        profile->lock_gpio[KEYVALET_LOCK_1] == GPIO_NUM_NC ||
        profile->lock_gpio[KEYVALET_LOCK_2] == GPIO_NUM_NC ||
        profile->lock_gpio[KEYVALET_LOCK_1] == profile->lock_gpio[KEYVALET_LOCK_2]) {
        return ESP_ERR_INVALID_STATE;
    }

    s_output_lock = xSemaphoreCreateMutex();
    if (s_output_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_profile = profile;
    s_safe_outputs_ready = false;
    keyvalet_lock_arbiter_init(&s_arbiter);

    // Preload every lock latch before enabling either output driver.
    ESP_RETURN_ON_ERROR(preload_solenoids_inactive(profile), "hardware_io",
                        "Unable to preload lock outputs");

    uint64_t lock_mask = 0;
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index)
        lock_mask |= 1ULL << profile->lock_gpio[index];
    const gpio_config_t solenoid_config = {
        .pin_bit_mask = lock_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&solenoid_config), "hardware_io",
                        "Unable to configure lock outputs");
    ESP_RETURN_ON_ERROR(force_solenoids_inactive_locked(profile), "hardware_io",
                        "Unable to hold lock outputs inactive");

    const esp_timer_create_args_t solenoid_timer_config = {
        .callback = solenoid_timer_callback,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "solenoid_off",
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&solenoid_timer_config, &s_solenoid_timer),
                        "hardware_io", "Unable to create solenoid timer");

    ESP_RETURN_ON_ERROR(gpio_set_level(profile->status_led_gpio,
                                       output_level(profile->status_led_active_high, false)),
                        "hardware_io", "Unable to preload status LED output");
    const gpio_config_t led_config = {
        .pin_bit_mask = 1ULL << profile->status_led_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&led_config), "hardware_io",
                        "Unable to configure status LED output");
    ESP_RETURN_ON_ERROR(keyvalet_status_led_set(profile, false), "hardware_io",
                        "Unable to hold status LED inactive");

    const gpio_config_t input_config = {
        .pin_bit_mask = 1ULL << profile->button_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = profile->button_internal_pullup
                          ? GPIO_PULLUP_ENABLE
                          : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input_config), "hardware_io",
                        "Unable to configure button input");
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index) {
        ESP_RETURN_ON_ERROR(keyvalet_sensor_configure(profile, index),
                            "hardware_io", "Unable to configure sensor input");
    }

    s_safe_outputs_ready = true;
    return ESP_OK;
}
