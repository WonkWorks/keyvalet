#include <inttypes.h>
#include <stdatomic.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "connectivity_indicator.h"
#include "build_identity.h"
#include "device_controller.h"
#include "hardware_io.h"
#include "hardware_profile.h"
#include "input_monitor.h"
#include "journal.h"
#include "journal_sync.h"
#include "network_manager.h"
#include "nvs_flash.h"
#include "piezo.h"
#include "security.h"
#include "settings.h"
#include "sdkconfig.h"
#include "web_server.h"
#include "usb_provision.h"
#include "tls_identity.h"

static const char *TAG = "keyvalet";
static atomic_bool s_wifi_tone_pending;

static void network_connection_changed(bool connected, void *context)
{
    (void)context;
    keyvalet_connectivity_indicator_set_wifi(connected);
    keyvalet_web_server_set_network_available(connected);
    if (connected) {
        const esp_err_t tone_result = keyvalet_piezo_play_wifi_connected();
        if (tone_result == ESP_ERR_INVALID_STATE &&
            keyvalet_piezo_activity() == KEYVALET_PIEZO_STARTUP) {
            atomic_store(&s_wifi_tone_pending, true);
        } else if (tone_result != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi acquisition tone unavailable: %s",
                     esp_err_to_name(tone_result));
        }
    } else {
        atomic_store(&s_wifi_tone_pending, false);
    }
}

static void startup_chirp_complete(keyvalet_piezo_activity_t activity, void *context)
{
    (void)activity;
    (void)context;
    ESP_LOGI(TAG, "Startup chirp complete; piezo inactive");
    if (atomic_exchange(&s_wifi_tone_pending, false)) {
        (void)keyvalet_piezo_play_wifi_connected();
    }
}

static void log_boot_diagnostics(const keyvalet_hardware_profile_t *profile)
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "KeyValet firmware %s starting", app->version);
    ESP_LOGI(TAG, "Build identity: %s | variant: %s",
             keyvalet_build_identity(), keyvalet_build_variant());
    ESP_LOGI(TAG, "Project: %s | IDF: %s | target: %s",
             app->project_name, app->idf_ver, CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "Chip revision: v%d.%d | cores: %d | reset reason: %d",
             chip_info.revision / 100, chip_info.revision % 100,
             chip_info.cores, esp_reset_reason());
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Detected flash: %" PRIu32 " bytes (%" PRIu32 " MiB)",
                 flash_size, flash_size / (1024U * 1024U));
    } else {
        ESP_LOGW(TAG, "Unable to read detected flash size");
    }

    ESP_LOGI(TAG, "Hardware profile: %s", profile->name);
    ESP_LOGI(TAG, "Profile GPIOs: LED=%d button=%d sensors=%d/%d/%d/%d",
             profile->status_led_gpio, profile->button_gpio,
             profile->sensor_gpio[0], profile->sensor_gpio[1],
             profile->sensor_gpio[2], profile->sensor_gpio[3]);
    ESP_LOGI(TAG, "Profile GPIOs: locks=%d/%d piezo=%d reserved=%d recovery=%" PRIu32 "ms",
             profile->lock_gpio[0], profile->lock_gpio[1],
             profile->piezo_gpio, profile->reserved_gpio,
             profile->lock_recovery_ms);
#ifdef CONFIG_KEYVALET_DIAGNOSTICS
    ESP_LOGW(TAG, "Diagnostic mode: ENABLED (development build)");
#else
    ESP_LOGI(TAG, "Diagnostic mode: DISABLED (production build)");
#endif
}

void app_main(void)
{
    const keyvalet_hardware_profile_t *profile = keyvalet_hardware_profile();

    // The first hardware operation establishes both solenoids' inactive state.
    ESP_ERROR_CHECK(keyvalet_hardware_io_init(profile));
    ESP_LOGI(TAG, "Both solenoids initialized inactive; shared arbiter ready");

    log_boot_diagnostics(profile);

    const esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result != ESP_OK) {
        ESP_LOGE(TAG, "NVS unavailable; settings and authorized control disabled: %s",
                 esp_err_to_name(nvs_result));
    }
    const esp_err_t settings_result = keyvalet_settings_init();
    if (settings_result != ESP_OK) {
        ESP_LOGE(TAG, "Persistent settings unavailable or invalid; safe defaults active: %s",
                 esp_err_to_name(settings_result));
    }
    keyvalet_settings_t boot_settings;
    if (keyvalet_settings_get(&boot_settings) == ESP_OK) {
        ESP_LOGI(TAG, "Sensor monitoring settings: 1=%s 2=%s 3=%s 4=%s",
                 boot_settings.sensor_monitoring_enabled[0] ? "ON" : "OFF",
                 boot_settings.sensor_monitoring_enabled[1] ? "ON" : "OFF",
                 boot_settings.sensor_monitoring_enabled[2] ? "ON" : "OFF",
                 boot_settings.sensor_monitoring_enabled[3] ? "ON" : "OFF");
    }
    const esp_err_t security_result = keyvalet_security_init();
    if (security_result != ESP_OK) {
        ESP_LOGE(TAG, "Enrollment and authorized control disabled: %s",
                 esp_err_to_name(security_result));
    }

    const esp_err_t journal_result = keyvalet_journal_init();
    if (journal_result == ESP_OK) {
        const esp_err_t sync_result = keyvalet_journal_sync_init();
        if (sync_result != ESP_OK) {
            ESP_LOGE(TAG, "Durable journal synchronization cursor unavailable: %s",
                     esp_err_to_name(sync_result));
        }
        const esp_err_t boot_result = keyvalet_journal_append(
            KEYVALET_EVENT_DEVICE_BOOT, 0, (uint32_t)esp_reset_reason());
        if (boot_result != ESP_OK) {
            ESP_LOGE(TAG, "Unable to persist DEVICE_BOOT: %s",
                     esp_err_to_name(boot_result));
        }
        const esp_err_t heartbeat_result = keyvalet_journal_start_heartbeat();
        if (heartbeat_result != ESP_OK) {
            ESP_LOGE(TAG, "Unable to start journal heartbeat: %s",
                     esp_err_to_name(heartbeat_result));
        }
    } else {
        ESP_LOGE(TAG, "Journal unavailable; events will not be durable: %s",
                 esp_err_to_name(journal_result));
    }

    const esp_err_t piezo_init_result = keyvalet_piezo_init(profile);
    if (piezo_init_result != ESP_OK) {
        ESP_LOGE(TAG, "Piezo initialization failed; audible indications unavailable: %s",
                 esp_err_to_name(piezo_init_result));
    }
    ESP_ERROR_CHECK(keyvalet_device_controller_start(profile));
    ESP_ERROR_CHECK(keyvalet_input_monitor_start(profile,
                                                  keyvalet_device_input_callbacks()));
    const esp_err_t apply_settings_result = keyvalet_device_controller_apply_settings();
    if (apply_settings_result != ESP_OK) {
        ESP_LOGE(TAG, "Unable to apply persistent input settings: %s",
                 esp_err_to_name(apply_settings_result));
    }

    const esp_err_t indicator_result = keyvalet_connectivity_indicator_start(profile);
    if (indicator_result != ESP_OK) {
        ESP_LOGE(TAG, "Connectivity LED unavailable: %s",
                 esp_err_to_name(indicator_result));
    }
    const esp_err_t tls_result = keyvalet_tls_identity_init();
    if (tls_result != ESP_OK) ESP_LOGE(TAG, "TLS identity unavailable; USB recovery required: %s", esp_err_to_name(tls_result));
    const esp_err_t web_result = keyvalet_web_server_init();
    if (web_result != ESP_OK) {
        ESP_LOGE(TAG, "Local HTTPS/WSS service unavailable: %s",
                 esp_err_to_name(web_result));
    } else if (journal_result == ESP_OK) {
        const esp_err_t listener_result = keyvalet_journal_set_commit_listener(
            keyvalet_web_server_publish_record, NULL);
        if (listener_result != ESP_OK) {
            ESP_LOGE(TAG, "Live journal publication unavailable: %s",
                     esp_err_to_name(listener_result));
        }
    }
    if (piezo_init_result == ESP_OK) {
        const esp_err_t chirp_result = keyvalet_piezo_play_startup(
            KEYVALET_PIEZO_STARTUP, startup_chirp_complete, NULL);
        if (chirp_result == ESP_OK) {
            ESP_LOGI(TAG, "Startup chirp scheduled; autonomous input controller active");
        } else {
            ESP_LOGE(TAG, "Startup chirp failed; audible indications may be unavailable: %s",
                     esp_err_to_name(chirp_result));
        }
    }

    const esp_err_t network_result = keyvalet_network_manager_start(
        network_connection_changed, NULL);
    if (network_result != ESP_OK) {
        ESP_LOGE(TAG, "Networking unavailable; autonomous operation continues: %s",
                 esp_err_to_name(network_result));
    }

    ESP_ERROR_CHECK(keyvalet_usb_provision_start(profile));
}
