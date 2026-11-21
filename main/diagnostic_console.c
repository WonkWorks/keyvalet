#include "diagnostic_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "build_identity.h"
#include "device_controller.h"
#include "diagnostic_solenoid.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardware_io.h"
#include "journal.h"
#include "piezo.h"
#include "sdkconfig.h"

static const char *TAG = "diagnostic";
static const keyvalet_hardware_profile_t *s_profile;

static void send_response(const char *response)
{
    printf("KVRESP %s\n", response);
    fflush(stdout);
}

static void chirp_complete(keyvalet_piezo_activity_t activity, void *context)
{
    (void)activity;
    (void)context;
    ESP_LOGW(TAG, "Diagnostic piezo test COMPLETED; piezo inactive");
}

static void report_status(void)
{
    keyvalet_device_status_t device_status;
    if (keyvalet_device_status_get(&device_status) != ESP_OK) {
        send_response("ERR STATUS_UNAVAILABLE");
        return;
    }

    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    const char *sensor[KEYVALET_SENSOR_COUNT];
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index) {
        if (!device_status.sensor_monitoring_enabled[index]) sensor[index] = "DISABLED";
        else if (!device_status.sensor_known[index]) sensor[index] = "UNKNOWN";
        else sensor[index] = device_status.sensor_open[index] ? "OPEN" : "CLOSED";
    }
    const char *button = device_status.button_known
                             ? (device_status.button_pressed ? "PRESSED" : "RELEASED")
                             : "UNKNOWN";
    keyvalet_lock_status_t lock_status = {0};
    if (keyvalet_lock_status_get(&lock_status) != ESP_OK) {
        send_response("ERR STATUS_UNAVAILABLE");
        return;
    }

    printf("KVRESP OK STATUS firmware=%s build_id=%s variant=%s "
           "target=%s chip_model=%d "
           "chip_revision=%d.%d cores=%d sensor_1=%s sensor_2=%s "
           "sensor_3=%s sensor_4=%s button=%s lock_1=%s lock_2=%s "
           "arbiter=%s recovery_remaining_ms=%" PRIu32 " piezo=%s led=%s diagnostics=ENABLED\n",
           app->version, keyvalet_build_identity(), keyvalet_build_variant(),
           CONFIG_IDF_TARGET, chip_info.model,
           chip_info.revision / 100, chip_info.revision % 100, chip_info.cores,
           sensor[0], sensor[1], sensor[2], sensor[3], button,
           lock_status.active[0] ? "ACTIVE" : "INACTIVE",
           lock_status.active[1] ? "ACTIVE" : "INACTIVE",
           keyvalet_lock_phase_name(lock_status.phase),
           lock_status.recovery_remaining_ms,
           keyvalet_piezo_is_active() ? "ACTIVE" : "INACTIVE",
           keyvalet_status_led_is_on() ? "ON" : "OFF");
    fflush(stdout);
}

static void report_recent_journal(void)
{
    keyvalet_journal_record_t records[20];
    size_t count = 0;
    const esp_err_t result = keyvalet_journal_read_recent(
        records, sizeof(records) / sizeof(records[0]), &count);
    if (result != ESP_OK) {
        send_response("ERR JOURNAL_UNAVAILABLE");
        return;
    }

    printf("KVRESP OK JOURNAL RECENT count=%u\n", (unsigned)count);
    for (size_t index = 0; index < count; ++index) {
        const keyvalet_journal_record_t *record = &records[index];
        printf("KVRESP OK JOURNAL EVENT seq=%" PRIu64 " type=%s uptime_ms=%" PRIu64,
               record->sequence, keyvalet_event_type_name(record->type),
               record->uptime_ms);
        if ((record->flags & KEYVALET_EVENT_FLAG_WALL_TIME_VALID) != 0) {
            printf(" wall_time_s=%" PRId64, record->wall_time_s);
        } else {
            printf(" wall_time=INVALID");
        }
        printf(" flags=0x%04x detail=%" PRIu32 "\n",
               record->flags, record->detail);
    }
    fflush(stdout);
}

static void report_journal_stats(void)
{
    keyvalet_journal_stats_t stats;
    if (keyvalet_journal_get_stats(&stats) != ESP_OK) {
        send_response("ERR JOURNAL_UNAVAILABLE");
        return;
    }
    printf("KVRESP OK JOURNAL STATS format=%" PRIu32 " capacity_bytes=%u "
           "capacity_records=%u valid=%u corrupt=%u gaps=%u oldest=%" PRIu64 " "
           "latest=%" PRIu64 " used_bytes=%u available_bytes=%u health=%s "
           "produced=%" PRIu64 " retired_through=%" PRIu64 " "
           "max_append_us=%" PRIu64 " max_erase_us=%" PRIu64 " sync=SIGNED_ACK_V1\n",
           stats.format_version, (unsigned)stats.partition_size,
           (unsigned)stats.record_capacity, (unsigned)stats.valid_record_count,
           (unsigned)stats.corrupt_record_count, (unsigned)stats.gap_count,
           stats.oldest_sequence, stats.latest_sequence,
           (unsigned)stats.used_bytes, (unsigned)stats.available_bytes,
           keyvalet_journal_health_name(stats.health), stats.produced_sequence,
           stats.retired_through_sequence, stats.max_append_us, stats.max_erase_us);
    fflush(stdout);
}

static void request_reboot(void)
{
    const esp_err_t result = keyvalet_solenoid_force_inactive(s_profile);
    if (result != ESP_OK) {
        send_response("ERR REBOOT_SOLENOID_NOT_SAFE");
        return;
    }
    send_response("OK REBOOT REQUESTED");
    ESP_LOGW(TAG, "Diagnostic software reboot requested; solenoid inactive");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

void keyvalet_diagnostic_console_handle(const char *command)
{
    if (strcmp(command, "KVTEST PULSE LOCK_1") == 0) {
        const esp_err_t result = keyvalet_diagnostic_lock_pulse(KEYVALET_LOCK_1);
        send_response(result == ESP_OK ? "OK PULSE LOCK_1 ACCEPTED" : "ERR PULSE_BUSY");
        return;
    }
    if (strcmp(command, "KVTEST PULSE LOCK_2") == 0) {
        const esp_err_t result = keyvalet_diagnostic_lock_pulse(KEYVALET_LOCK_2);
        send_response(result == ESP_OK ? "OK PULSE LOCK_2 ACCEPTED" : "ERR PULSE_BUSY");
        return;
    }
    if (strcmp(command, "KVTEST CHIRP") == 0) {
        const esp_err_t result = keyvalet_piezo_play_startup(
            KEYVALET_PIEZO_DIAGNOSTIC, chirp_complete, NULL);
        if (result == ESP_OK) {
            ESP_LOGW(TAG, "Diagnostic piezo test STARTED");
            send_response("OK CHIRP ACCEPTED");
        } else {
            send_response("ERR PIEZO_BUSY");
        }
        return;
    }
    if (strcmp(command, "KVTEST LED") == 0) {
        bool is_on = false;
        if (keyvalet_status_led_toggle(s_profile, &is_on) == ESP_OK) {
            ESP_LOGW(TAG, "Diagnostic status LED toggled: %s", is_on ? "ON" : "OFF");
            send_response(is_on ? "OK LED ON" : "OK LED OFF");
        } else {
            send_response("ERR LED_FAILED");
        }
        return;
    }
    if (strcmp(command, "KVTEST STATUS") == 0) {
        report_status();
        return;
    }
    if (strcmp(command, "KVTEST REBOOT") == 0) {
        request_reboot();
        return;
    }
    if (strcmp(command, "KVTEST JOURNAL RECENT") == 0) {
        report_recent_journal();
        return;
    }
    if (strcmp(command, "KVTEST JOURNAL STATS") == 0) {
        report_journal_stats();
        return;
    }

    send_response(strncmp(command, "KVTEST ", 7) == 0 ? "ERR UNKNOWN_COMMAND"
                                                       : "ERR MALFORMED_COMMAND");
}

esp_err_t keyvalet_diagnostic_console_init(const keyvalet_hardware_profile_t *profile)
{
    if (profile == NULL || s_profile != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(keyvalet_diagnostic_solenoid_init(profile), TAG,
                        "Unable to initialize diagnostic solenoid control");
    s_profile = profile;
    ESP_LOGW(TAG, "DEVELOPMENT DIAGNOSTIC COMMANDS ENABLED on USB Serial/JTAG");
    return ESP_OK;
}
