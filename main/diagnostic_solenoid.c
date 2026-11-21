#include "diagnostic_solenoid.h"

#include <inttypes.h>
#include "esp_log.h"
#include "device_controller.h"
#include "hardware_io.h"

static const char *TAG = "diagnostic";
static const keyvalet_hardware_profile_t *s_profile;

esp_err_t keyvalet_diagnostic_solenoid_init(const keyvalet_hardware_profile_t *profile)
{
    if (profile == NULL || s_profile != NULL) return ESP_ERR_INVALID_STATE;
    s_profile = profile;
    return ESP_OK;
}

esp_err_t keyvalet_diagnostic_lock_pulse(keyvalet_lock_id_t lock)
{
    if (s_profile == NULL) return ESP_ERR_INVALID_STATE;
    const esp_err_t result = keyvalet_lock_pulse(s_profile, lock);
    if (result == ESP_OK) {
        keyvalet_device_note_solenoid_activation(lock);
        ESP_LOGW(TAG, "Diagnostic lock_%u pulse STARTED: %" PRIu32 " ms",
                 (unsigned)lock + 1U, s_profile->solenoid_pulse_ms);
    }
    return result;
}
