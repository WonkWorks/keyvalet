#include "piezo.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "audio_sequence.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "piezo";
static const ledc_mode_t PIEZO_SPEED_MODE = LEDC_LOW_SPEED_MODE;
static const ledc_timer_t PIEZO_TIMER = LEDC_TIMER_0;
static const ledc_channel_t PIEZO_CHANNEL = LEDC_CHANNEL_0;
static const uint32_t PIEZO_HALF_DUTY_8_BIT = 128;

typedef keyvalet_audio_step_t tone_step_t;

static const keyvalet_hardware_profile_t *s_profile;
static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_step_timer;
#define PIEZO_MAX_STEPS 64U
static tone_step_t s_steps[PIEZO_MAX_STEPS];
static size_t s_step_count;
static size_t s_step_index;
static keyvalet_piezo_activity_t s_activity = KEYVALET_PIEZO_NONE;
static keyvalet_piezo_completion_t s_completion;
static void *s_completion_context;

static esp_err_t piezo_silence_locked(void)
{
    return ledc_stop(PIEZO_SPEED_MODE, PIEZO_CHANNEL, 0);
}

static esp_err_t piezo_apply_step_locked(const tone_step_t *step)
{
    if (step->frequency_hz == 0) {
        return piezo_silence_locked();
    }
    ESP_RETURN_ON_ERROR(ledc_set_freq(PIEZO_SPEED_MODE, PIEZO_TIMER,
                                      step->frequency_hz),
                        TAG, "Unable to set piezo frequency");
    ESP_RETURN_ON_ERROR(ledc_set_duty(PIEZO_SPEED_MODE, PIEZO_CHANNEL,
                                      PIEZO_HALF_DUTY_8_BIT),
                        TAG, "Unable to set piezo duty");
    return ledc_update_duty(PIEZO_SPEED_MODE, PIEZO_CHANNEL);
}

static void finish_sequence_locked(keyvalet_piezo_activity_t *completed_activity,
                                   keyvalet_piezo_completion_t *completion,
                                   void **completion_context)
{
    (void)piezo_silence_locked();
    *completed_activity = s_activity;
    *completion = s_completion;
    *completion_context = s_completion_context;
    s_activity = KEYVALET_PIEZO_NONE;
    s_step_count = 0;
    s_step_index = 0;
    s_completion = NULL;
    s_completion_context = NULL;
}

static void step_timer_callback(void *context)
{
    (void)context;
    keyvalet_piezo_activity_t completed_activity = KEYVALET_PIEZO_NONE;
    keyvalet_piezo_completion_t completion = NULL;
    void *completion_context = NULL;
    esp_err_t result = ESP_OK;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    ++s_step_index;
    if (s_step_index >= s_step_count) {
        finish_sequence_locked(&completed_activity, &completion, &completion_context);
    } else {
        result = piezo_apply_step_locked(&s_steps[s_step_index]);
        if (result == ESP_OK) {
            result = esp_timer_start_once(s_step_timer,
                                           s_steps[s_step_index].duration_ms * 1000ULL);
        }
        if (result != ESP_OK) {
            finish_sequence_locked(&completed_activity, &completion, &completion_context);
        }
    }
    xSemaphoreGive(s_lock);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Tone sequence stopped after error: %s", esp_err_to_name(result));
        completion = NULL;
    }
    if (completion != NULL) {
        completion(completed_activity, completion_context);
    }
}

static esp_err_t play_sequence(keyvalet_piezo_activity_t activity,
                               const tone_step_t *steps, size_t step_count,
                               keyvalet_piezo_completion_t completion, void *context)
{
    if (s_profile == NULL || steps == NULL || step_count == 0 ||
        step_count > PIEZO_MAX_STEPS ||
        activity == KEYVALET_PIEZO_NONE) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_activity != KEYVALET_PIEZO_NONE) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t index = 0; index < step_count; ++index) {
        s_steps[index] = steps[index];
    }
    s_step_count = step_count;
    s_step_index = 0;
    s_activity = activity;
    s_completion = completion;
    s_completion_context = context;

    esp_err_t result = piezo_apply_step_locked(&s_steps[0]);
    if (result == ESP_OK) {
        result = esp_timer_start_once(s_step_timer, s_steps[0].duration_ms * 1000ULL);
    }
    if (result != ESP_OK) {
        keyvalet_piezo_activity_t unused_activity;
        keyvalet_piezo_completion_t unused_completion;
        void *unused_context;
        finish_sequence_locked(&unused_activity, &unused_completion, &unused_context);
    }
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t keyvalet_piezo_init(const keyvalet_hardware_profile_t *profile)
{
    if (profile == NULL || s_profile != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = PIEZO_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = PIEZO_TIMER,
        .freq_hz = profile->startup_tone_1_hz,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG,
                        "Unable to configure piezo timer");

    const ledc_channel_config_t channel_config = {
        .gpio_num = profile->piezo_gpio,
        .speed_mode = PIEZO_SPEED_MODE,
        .channel = PIEZO_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PIEZO_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags.output_invert = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG,
                        "Unable to configure piezo channel");

    const esp_timer_create_args_t timer_config_args = {
        .callback = step_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "piezo_step",
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_config_args, &s_step_timer), TAG,
                        "Unable to create piezo timer");

    s_profile = profile;
    return ledc_stop(PIEZO_SPEED_MODE, PIEZO_CHANNEL, 0);
}

esp_err_t keyvalet_piezo_play_startup(keyvalet_piezo_activity_t activity,
                                     keyvalet_piezo_completion_t completion,
                                     void *context)
{
    if (s_profile == NULL ||
        (activity != KEYVALET_PIEZO_STARTUP && activity != KEYVALET_PIEZO_DIAGNOSTIC)) {
        return ESP_ERR_INVALID_ARG;
    }
    const tone_step_t steps[] = {
        {s_profile->startup_tone_1_hz, s_profile->startup_tone_1_ms},
        {0, s_profile->startup_tone_gap_ms},
        {s_profile->startup_tone_2_hz, s_profile->startup_tone_2_ms},
    };
    return play_sequence(activity, steps, 3, completion, context);
}

esp_err_t keyvalet_piezo_play_alarm_chirp(void)
{
    if (s_profile == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const tone_step_t steps[] = {
        {s_profile->alarm_tone_hz, s_profile->alarm_tone_ms},
        {0, s_profile->alarm_tone_gap_ms},
        {s_profile->alarm_tone_hz, s_profile->alarm_tone_ms},
    };
    return play_sequence(KEYVALET_PIEZO_REMINDER_ALARM, steps, 3, NULL, NULL);
}

esp_err_t keyvalet_piezo_play_hold_indication(void)
{
    if (s_profile == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const tone_step_t step = {s_profile->hold_tone_hz, s_profile->hold_tone_ms};
    return play_sequence(KEYVALET_PIEZO_HOLD_INDICATION, &step, 1, NULL, NULL);
}

esp_err_t keyvalet_piezo_play_wifi_connected(void)
{
    const tone_step_t steps[] = {
        {1500, 80}, {0, 70}, {2000, 100}, {0, 60}, {2100, 220},
    };
    return play_sequence(KEYVALET_PIEZO_WIFI_CONNECTED, steps, 5, NULL, NULL);
}

esp_err_t keyvalet_piezo_play_ip_octet(uint8_t octet)
{
    tone_step_t steps[PIEZO_MAX_STEPS];
    const size_t step_count = keyvalet_ip_octet_sequence(
        octet, steps, PIEZO_MAX_STEPS);
    if (step_count == 0) return ESP_ERR_INVALID_ARG;
    return play_sequence(KEYVALET_PIEZO_IP_ANNOUNCEMENT, steps, step_count,
                         NULL, NULL);
}

esp_err_t keyvalet_piezo_play_pairing_cue(void)
{
    const tone_step_t steps[] = {
        {2300, 70}, {0, 70}, {2300, 70}, {0, 400},
        {2300, 70}, {0, 70}, {2300, 70}, {0, 400},
        {2300, 70}, {0, 70}, {2300, 70},
    };
    return play_sequence(KEYVALET_PIEZO_PAIRING, steps, 11, NULL, NULL);
}

esp_err_t keyvalet_piezo_play_urgent_alarm(void)
{
    const tone_step_t steps[] = {
        {3000, 180}, {0, 100}, {3000, 180},
    };
    return play_sequence(KEYVALET_PIEZO_URGENT_ALARM, steps, 3, NULL, NULL);
}

esp_err_t keyvalet_piezo_preempt_for_alarm(void)
{
    const keyvalet_piezo_activity_t activity = keyvalet_piezo_activity();
    if (activity == KEYVALET_PIEZO_NONE ||
        activity == KEYVALET_PIEZO_REMINDER_ALARM ||
        activity == KEYVALET_PIEZO_URGENT_ALARM) {
        return ESP_OK;
    }
    return keyvalet_piezo_stop(activity);
}

esp_err_t keyvalet_piezo_stop(keyvalet_piezo_activity_t activity)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_activity != activity) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t timer_result = esp_timer_stop(s_step_timer);
    const esp_err_t silence_result = piezo_silence_locked();
    s_activity = KEYVALET_PIEZO_NONE;
    s_step_count = 0;
    s_step_index = 0;
    s_completion = NULL;
    s_completion_context = NULL;
    xSemaphoreGive(s_lock);

    if (timer_result != ESP_OK && timer_result != ESP_ERR_INVALID_STATE) {
        return timer_result;
    }
    return silence_result;
}

bool keyvalet_piezo_is_active(void)
{
    return keyvalet_piezo_activity() != KEYVALET_PIEZO_NONE;
}

keyvalet_piezo_activity_t keyvalet_piezo_activity(void)
{
    if (s_lock == NULL) {
        return KEYVALET_PIEZO_NONE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const keyvalet_piezo_activity_t activity = s_activity;
    xSemaphoreGive(s_lock);
    return activity;
}
