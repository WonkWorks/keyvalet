#include "web_server.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "connectivity_indicator.h"
#include "cJSON.h"
#include "build_identity.h"
#include "device_controller.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "journal.h"
#include "journal_sync.h"
#include "journal_sync_contract.h"
#include "hardware_io.h"
#include "hardware_profile.h"
#include "network_manager.h"
#include "security.h"
#include "settings.h"
#include "sdkconfig.h"
#include "time_sync.h"
#include "tls_identity.h"

#define EVENT_QUEUE_LENGTH 32U
#define JSON_BUFFER_SIZE 4096U
#define REQUEST_BUFFER_SIZE 2048U

static const char *TAG = "web_server";
static httpd_handle_t s_server;
static SemaphoreHandle_t s_client_lock;
static QueueHandle_t s_event_queue;
static TaskHandle_t s_worker_task;
static int s_client_fds[KEYVALET_WEBSOCKET_MAX_CLIENTS];
static size_t s_client_count;
static atomic_bool s_network_available;
static atomic_bool s_activate_requested;
static atomic_int s_activation_result;
static keyvalet_tls_credentials_t *s_credentials;
static atomic_uint s_tls_created, s_tls_closed, s_tls_errors;
static atomic_uint s_http_stack_free;
static atomic_uint s_https_sessions;
static atomic_bool s_listening;
static int64_t s_last_rx[KEYVALET_WEBSOCKET_MAX_CLIENTS];
static int64_t s_last_ping;
static bool s_events_registered;

static void tls_session_event(esp_https_server_user_cb_arg_t *event)
{
    if (event->user_cb_state == HTTPD_SSL_USER_CB_SESS_CREATE) {
        atomic_fetch_add(&s_tls_created, 1); atomic_fetch_add(&s_https_sessions, 1);
    } else {
        atomic_fetch_add(&s_tls_closed, 1); atomic_fetch_sub(&s_https_sessions, 1);
    }
    atomic_store(&s_http_stack_free, uxTaskGetStackHighWaterMark(NULL));
    ESP_LOGI(TAG, "TLS session=%s free=%u min=%u largest=%u stack=%u",
        event->user_cb_state == HTTPD_SSL_USER_CB_SESS_CREATE ? "open" : "close",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static void tls_error_event(void *context, esp_event_base_t base, int32_t id, void *data)
{
    (void)context; (void)base; (void)id; (void)data;
    atomic_fetch_add(&s_tls_errors, 1);
    ESP_LOGW(TAG, "TLS error: free=%u min=%u largest=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

typedef struct {
    httpd_handle_t server;
    char *payload;
} async_send_context_t;

static esp_err_t set_console_cors(httpd_req_t *request);

static size_t client_count_locked(void)
{
    return s_client_count;
}
size_t keyvalet_web_server_client_count(void)
{
    if (s_client_lock == NULL) {
        return 0;
    }
    xSemaphoreTake(s_client_lock, portMAX_DELAY);
    const size_t count = client_count_locked();
    xSemaphoreGive(s_client_lock);
    return count;
}

static bool add_client(int fd)
{
    bool added = false;
    size_t count;
    xSemaphoreTake(s_client_lock, portMAX_DELAY);
    for (size_t index = 0; index < s_client_count; ++index) {
        if (s_client_fds[index] == fd) {
            added = true;
            break;
        }
    }
    if (!added && s_client_count < KEYVALET_WEBSOCKET_MAX_CLIENTS) {
        s_client_fds[s_client_count] = fd;
        s_last_rx[s_client_count++] = esp_timer_get_time();
        added = true;
    }
    count = s_client_count;
    xSemaphoreGive(s_client_lock);
    keyvalet_connectivity_indicator_set_clients(count);
    return added;
}

static void remove_client(int fd)
{
    bool removed = false;
    size_t count;
    xSemaphoreTake(s_client_lock, portMAX_DELAY);
    for (size_t index = 0; index < s_client_count; ++index) {
        if (s_client_fds[index] == fd) {
            s_client_fds[index] = s_client_fds[s_client_count - 1U];
            s_last_rx[index] = s_last_rx[s_client_count - 1U];
            --s_client_count;
            removed = true;
            break;
        }
    }
    count = s_client_count;
    xSemaphoreGive(s_client_lock);
    if (removed) {
        ESP_LOGI(TAG, "WebSocket client disconnected; clients=%u", (unsigned)count);
    }
    keyvalet_connectivity_indicator_set_clients(count);
}

static int build_status_json(char *output, size_t capacity, const char *message_type)
{
    keyvalet_network_status_t network = {0};
    keyvalet_device_status_t device = {0};
    keyvalet_journal_stats_t journal = {0};
    const bool network_status_available = keyvalet_network_status_get(&network) == ESP_OK;
    const bool device_status_available = keyvalet_device_status_get(&device) == ESP_OK;
    const bool journal_status_available = keyvalet_journal_get_stats(&journal) == ESP_OK;
    keyvalet_journal_sync_status_t journal_sync = {0};
    const bool journal_sync_available =
        keyvalet_journal_sync_get_status(&journal_sync) == ESP_OK;
    const bool utc_valid = keyvalet_time_is_valid();
    const keyvalet_hardware_profile_t *profile = keyvalet_hardware_profile();
    keyvalet_lock_status_t lock_status = {0};
    const bool lock_status_available = keyvalet_lock_status_get(&lock_status) == ESP_OK;
    char locks[512] = "{";
    char sensors[1024] = "{";
    size_t locks_used = 1, sensors_used = 1;
    bool first_lock = true, first_sensor = true;
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index) {
        const char *state = "INACTIVE";
        if (lock_status_available && lock_status.active[index]) state = "ACTIVE";
        else if (lock_status_available &&
                 lock_status.phase == KEYVALET_LOCK_PHASE_RESERVED &&
                 lock_status.selected == index) state = "RESERVED";
        const int added = snprintf(locks + locks_used, sizeof(locks) - locks_used,
            "%s\"lock_%u\":{\"state\":\"%s\",\"sensor_id\":\"sensor_%u\","
            "\"sensor_required\":%s,\"operation_active\":%s}", first_lock ? "" : ",",
            (unsigned)index + 1U, state, (unsigned)index + 1U,
            !device_status_available || device.lock_sensor_required[index]
                ? "true" : "false",
            device_status_available && device.lock_operation_active[index]
                ? "true" : "false");
        if (added < 0 || (size_t)added >= sizeof(locks) - locks_used) return -1;
        locks_used += (size_t)added; first_lock = false;
    }
    locks[locks_used++] = '}'; locks[locks_used] = '\0';
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index) {
        const char *state = !device_status_available || !device.sensor_known[index]
            ? "UNKNOWN" : (device.sensor_open[index] ? "OPEN" : "CLOSED");
        const int added = snprintf(sensors + sensors_used,
            sizeof(sensors) - sensors_used,
            "%s\"sensor_%u\":{\"state\":\"%s\",\"monitoring_enabled\":%s,"
            "\"alarm_active\":%s,\"alarm_silenced\":%s,"
            "\"unexpected_open\":%s}", first_sensor ? "" : ",",
            (unsigned)index + 1U, state,
            device_status_available && device.sensor_monitoring_enabled[index]
                ? "true" : "false",
            device_status_available && device.sensor_alarm_active[index]
                ? "true" : "false",
            device_status_available && device.sensor_alarm_silenced[index]
                ? "true" : "false",
            device_status_available && device.sensor_unexpected_open[index]
                ? "true" : "false");
        if (added < 0 || (size_t)added >= sizeof(sensors) - sensors_used) return -1;
        sensors_used += (size_t)added; first_sensor = false;
    }
    sensors[sensors_used++] = '}'; sensors[sensors_used] = '\0';
    char ip_json[32];
    char rssi_json[16];
    if (network_status_available && network.connected) {
        snprintf(ip_json, sizeof(ip_json), "\"%s\"", network.ip_address);
        if (network.rssi_valid) {
            snprintf(rssi_json, sizeof(rssi_json), "%d", network.rssi);
        } else {
            snprintf(rssi_json, sizeof(rssi_json), "null");
        }
    } else {
        snprintf(ip_json, sizeof(ip_json), "null");
        snprintf(rssi_json, sizeof(rssi_json), "null");
    }
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const int length = snprintf(
        output, capacity,
        "{\"type\":\"%s\",\"device_id\":\"%s\",\"firmware\":\"%s\","
        "\"build_id\":\"%s\",\"build_variant\":\"%s\","
        "\"target\":\"%s\",\"chip_model\":%d,\"wifi_connected\":%s,"
        "\"ip\":%s,\"rssi_dbm\":%s,\"utc_valid\":%s,"
        "\"hardware_profile\":\"%s\",\"locks\":%s,\"sensors\":%s,"
        "\"lock_arbiter\":\"%s\",\"lock_recovery_remaining_ms\":%u,"
        "\"latest_sequence\":%" PRIu64 ",\"journal_health\":\"%s\","
        "\"websocket_clients\":%u,\"enrolled\":%s,\"pairing_active\":%s,"
        "\"pairing_seconds_remaining\":%u,"
        "\"journal_sync\":{\"protocol_version\":%u,"
        "\"oldest_available_sequence\":%" PRIu64 ","
        "\"latest_available_sequence\":%" PRIu64 ","
        "\"highest_produced_sequence\":%" PRIu64 ","
        "\"acknowledged_sequence\":%" PRIu64 ","
        "\"unsynchronized_records\":%s,\"unrecoverable_gap\":%s,"
        "\"gap_start_sequence\":%" PRIu64 ","
        "\"gap_end_sequence\":%" PRIu64 ",\"max_batch_size\":%u,"
        "\"available\":%s}}",
        message_type, keyvalet_network_device_id(), app->version,
        keyvalet_build_identity(), keyvalet_build_variant(), CONFIG_IDF_TARGET,
        chip.model,
        network_status_available && network.connected ? "true" : "false",
        ip_json, rssi_json, utc_valid ? "true" : "false", profile->name,
        locks, sensors,
        lock_status_available ? keyvalet_lock_phase_name(lock_status.phase)
                              : "UNAVAILABLE",
        lock_status_available ? (unsigned)lock_status.recovery_remaining_ms : 0U,
        journal_status_available ? journal.latest_sequence : 0,
        journal_status_available ? keyvalet_journal_health_name(journal.health)
                                 : "UNAVAILABLE",
        (unsigned)keyvalet_web_server_client_count(),
        keyvalet_security_is_enrolled() ? "true" : "false",
        keyvalet_security_pairing_active() ? "true" : "false",
        (unsigned)keyvalet_security_pairing_seconds_remaining(),
        KEYVALET_JOURNAL_SYNC_PROTOCOL_VERSION,
        journal_sync_available ? journal_sync.oldest_available_sequence : 0,
        journal_sync_available ? journal_sync.latest_available_sequence : 0,
        journal_sync_available ? journal_sync.highest_produced_sequence : 0,
        journal_sync_available ? journal_sync.acknowledged_sequence : 0,
        journal_sync_available && journal_sync.unsynchronized_records
            ? "true" : "false",
        journal_sync_available && journal_sync.unrecoverable_gap
            ? "true" : "false",
        journal_sync_available ? journal_sync.gap_start_sequence : 0,
        journal_sync_available ? journal_sync.gap_end_sequence : 0,
        KEYVALET_JOURNAL_SYNC_MAX_BATCH,
        journal_sync_available ? "true" : "false");
    if (length < 0 || (size_t)length >= capacity) return -1;
    cJSON *root = cJSON_Parse(output);
    if (!root) return -1;
    keyvalet_tls_status_t tls; keyvalet_tls_status_get(&tls);
    cJSON_AddStringToObject(root, "ssid", network_status_available && network.connected ? network.ssid : "");
    cJSON_AddStringToObject(root, "device_uid", keyvalet_tls_device_uid());
    cJSON_AddBoolToObject(root, "enrollment_available", keyvalet_security_is_available());
    cJSON_AddBoolToObject(root, "wifi_available", network_status_available && network.available);
    cJSON_AddNumberToObject(root, "websocket_max_clients", KEYVALET_WEBSOCKET_MAX_CLIENTS);
    cJSON *identity = cJSON_AddObjectToObject(root, "tls");
    if (!identity) { cJSON_Delete(root); return -1; }
    cJSON_AddStringToObject(identity, "state", tls.state);
    cJSON_AddBoolToObject(identity, "ready", tls.ready);
    cJSON_AddBoolToObject(identity, "listening", atomic_load(&s_listening));
    cJSON_AddStringToObject(identity, "hostname", tls.hostname);
    cJSON_AddNumberToObject(identity, "generation", tls.generation);
    cJSON_AddNumberToObject(identity, "certificate_expires_at", (double)tls.not_after);
    cJSON_AddBoolToObject(identity, "candidate_available", tls.candidate);
    cJSON_AddStringToObject(identity, "activation", keyvalet_web_server_tls_activation_state());
    cJSON_AddStringToObject(identity, "maintenance_state", "NOT_IMPLEMENTED");
    cJSON *resources = cJSON_AddObjectToObject(root, "resources");
    if (!resources) { cJSON_Delete(root); return -1; }
    cJSON_AddNumberToObject(resources, "free_heap_bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(resources, "minimum_free_heap_bytes", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(resources, "largest_free_block_bytes", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(resources, "https_stack_free_bytes", atomic_load(&s_http_stack_free));
    cJSON_AddNumberToObject(resources, "worker_stack_free_bytes", uxTaskGetStackHighWaterMark(s_worker_task));
    cJSON_AddNumberToObject(resources, "tls_sessions_created", atomic_load(&s_tls_created));
    cJSON_AddNumberToObject(resources, "tls_sessions_closed", atomic_load(&s_tls_closed));
    cJSON_AddNumberToObject(resources, "tls_errors", atomic_load(&s_tls_errors));
    cJSON_AddNumberToObject(resources, "tls_sessions_current", atomic_load(&s_https_sessions));
    const bool printed = cJSON_PrintPreallocated(root, output, capacity, false);
    cJSON_Delete(root);
    return printed ? (int)strlen(output) : -1;
}

static int build_event_json(char *output, size_t capacity,
                            const keyvalet_journal_record_t *record)
{
    char utc_json[32];
    const bool utc_valid =
        (record->flags & KEYVALET_EVENT_FLAG_WALL_TIME_VALID) != 0;
    if (utc_valid) {
        snprintf(utc_json, sizeof(utc_json), "%" PRId64, record->wall_time_s);
    } else {
        snprintf(utc_json, sizeof(utc_json), "null");
    }
    const char *lock_id =
        record->type == KEYVALET_EVENT_LOCK_UNLOCK_AUTHORIZED
            ? (record->detail == 1U ? "lock_1" :
               (record->detail == 2U ? "lock_2" : NULL))
            : NULL;
    const char *sensor_id =
        record->type >= KEYVALET_EVENT_SENSOR_OPEN &&
        record->type <= KEYVALET_EVENT_SENSOR_ALARM_SILENCED &&
        record->detail >= 1U && record->detail <= KEYVALET_SENSOR_COUNT
            ? (record->detail == 1U ? "sensor_1" :
               record->detail == 2U ? "sensor_2" :
               record->detail == 3U ? "sensor_3" : "sensor_4") : NULL;
    if (lock_id != NULL || sensor_id != NULL) {
        return snprintf(
            output, capacity,
            "{\"type\":\"event\",\"device_id\":\"%s\",\"sequence\":%" PRIu64
            ",\"event\":\"%s\",%s\"%s\",\"uptime_ms\":%" PRIu64
            ",\"utc_valid\":%s,\"utc_s\":%s,\"flags\":%u,\"detail\":%" PRIu32 "}",
            keyvalet_network_device_id(), record->sequence,
            keyvalet_event_type_name(record->type),
            lock_id != NULL ? "\"lock_id\":" : "\"sensor_id\":",
            lock_id != NULL ? lock_id : sensor_id, record->uptime_ms,
            utc_valid ? "true" : "false", utc_json, record->flags, record->detail);
    }
    return snprintf(
        output, capacity,
        "{\"type\":\"event\",\"device_id\":\"%s\",\"sequence\":%" PRIu64
        ",\"event\":\"%s\",\"uptime_ms\":%" PRIu64
        ",\"utc_valid\":%s,\"utc_s\":%s,\"flags\":%u,\"detail\":%" PRIu32 "}",
        keyvalet_network_device_id(), record->sequence,
        keyvalet_event_type_name(record->type), record->uptime_ms,
        utc_valid ? "true" : "false", utc_json, record->flags, record->detail);
}

static esp_err_t status_handler(httpd_req_t *request)
{
    char response[JSON_BUFFER_SIZE];
    const int length = build_status_json(response, sizeof(response), "status");
    if (length < 0 || (size_t)length >= sizeof(response)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "status response unavailable");
    }
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (set_console_cors(request) != ESP_OK)
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "origin not permitted");
    return httpd_resp_send(request, response, length);
}

static esp_err_t send_read_only_error(httpd_req_t *request)
{
    static const char response[] =
        "{\"type\":\"error\",\"code\":\"READ_ONLY\","
        "\"message\":\"unsupported or unsigned network command\"}";
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)response,
        .len = sizeof(response) - 1U,
    };
    return httpd_ws_send_frame(request, &frame);
}

static const char *json_string(cJSON *object, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool json_i64(cJSON *object, const char *name, int64_t *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 ||
        item->valuedouble > 9007199254740991.0) {
        return false;
    }
    *value = (int64_t)item->valuedouble;
    return (double)*value == item->valuedouble;
}

static bool json_u32(cJSON *object, const char *name, uint32_t *value)
{
    int64_t parsed;
    if (!json_i64(object, name, &parsed) || parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool json_u64(cJSON *object, const char *name, uint64_t *value)
{
    int64_t parsed;
    if (!json_i64(object, name, &parsed)) return false;
    *value = (uint64_t)parsed;
    return true;
}

static esp_err_t ws_send_json(httpd_req_t *request, cJSON *response)
{
    char *payload = cJSON_PrintUnformatted(response);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    httpd_ws_frame_t frame = {
        .final = true,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)payload,
        .len = strlen(payload),
    };
    const esp_err_t result = httpd_ws_send_frame(request, &frame);
    cJSON_free(payload);
    return result;
}

static esp_err_t ws_result(httpd_req_t *request, const char *operation_id,
                           bool accepted, const char *code)
{
    cJSON *response = cJSON_CreateObject();
    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(response, "type", "command_result");
    if (operation_id != NULL) cJSON_AddStringToObject(response, "operation_id", operation_id);
    cJSON_AddBoolToObject(response, "accepted", accepted);
    cJSON_AddStringToObject(response, "code", code);
    const esp_err_t result = ws_send_json(request, response);
    cJSON_Delete(response);
    return result;
}

static esp_err_t ws_unlock_result(httpd_req_t *request, const char *operation_id,
                                  const char *lock_id, bool accepted,
                                  const char *code)
{
    cJSON *response = cJSON_CreateObject();
    if (response == NULL) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(response, "type", "command_result");
    if (operation_id != NULL)
        cJSON_AddStringToObject(response, "operation_id", operation_id);
    if (lock_id != NULL) cJSON_AddStringToObject(response, "lock_id", lock_id);
    cJSON_AddBoolToObject(response, "accepted", accepted);
    cJSON_AddStringToObject(response, "code", code);
    const esp_err_t result = ws_send_json(request, response);
    cJSON_Delete(response);
    return result;
}

static bool parse_authorization(cJSON *root, keyvalet_authorization_t *authorization)
{
    authorization->operation_id = json_string(root, "operation_id");
    authorization->nonce_hex = json_string(root, "nonce");
    authorization->signature_hex = json_string(root, "signature");
    return authorization->operation_id != NULL && authorization->nonce_hex != NULL &&
           authorization->signature_hex != NULL &&
           json_i64(root, "issued_at", &authorization->issued_at) &&
           json_i64(root, "expires_at", &authorization->expires_at);
}

static esp_err_t handle_enroll_begin(httpd_req_t *request)
{
    char challenge[65];
    uint32_t remaining;
    const esp_err_t result = keyvalet_security_begin_enrollment(challenge, &remaining);
    if (result != ESP_OK) {
        return ws_result(request, NULL, false, esp_err_to_name(result));
    }
    cJSON *response = cJSON_CreateObject();
    if (response == NULL) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(response, "type", "enrollment_challenge");
    cJSON_AddStringToObject(response, "device_id", keyvalet_network_device_id());
    cJSON_AddStringToObject(response, "challenge", challenge);
    cJSON_AddNumberToObject(response, "expires_in_s", remaining);
    const esp_err_t send_result = ws_send_json(request, response);
    cJSON_Delete(response);
    return send_result;
}

static esp_err_t handle_enroll_complete(httpd_req_t *request, cJSON *root)
{
    const char *challenge = json_string(root, "challenge");
    const char *public_key = json_string(root, "operational_public_key");
    const char *signature = json_string(root, "signature");
    int64_t expires_at;
    if (challenge == NULL || public_key == NULL || signature == NULL ||
        !json_i64(root, "expires_at", &expires_at)) {
        keyvalet_security_close_pairing_window();
        return ws_result(request, NULL, false, "MALFORMED");
    }
    esp_err_t result = keyvalet_security_complete_enrollment(
        challenge, public_key, expires_at, signature);
    if (result == ESP_OK) {
        const esp_err_t journal_result = keyvalet_journal_append(
            KEYVALET_EVENT_ENROLLMENT_ACCEPTED, 0, 0);
        if (journal_result != ESP_OK) {
            ESP_LOGE(TAG, "Enrollment persisted but could not be journaled: %s",
                     esp_err_to_name(journal_result));
        }
    }
    return ws_result(request, NULL, result == ESP_OK,
                     result == ESP_OK ? "ENROLLED" : esp_err_to_name(result));
}

static esp_err_t handle_targeted_unlock(httpd_req_t *request, cJSON *root)
{
    keyvalet_authorization_t authorization = {0};
    int64_t protocol_version;
    const char *device_id = json_string(root, "device_id");
    const char *action = json_string(root, "action");
    const char *lock_name = json_string(root, "lock_id");
    keyvalet_lock_id_t lock;
    if (!parse_authorization(root, &authorization) ||
        !json_i64(root, "protocol_version", &protocol_version) ||
        protocol_version != KEYVALET_UNLOCK_PROTOCOL_VERSION ||
        device_id == NULL || action == NULL || strcmp(action, "UNLOCK") != 0 ||
        lock_name == NULL) {
        return ws_unlock_result(request, authorization.operation_id, lock_name,
                                false, "MALFORMED");
    }
    if (!keyvalet_lock_id_parse(lock_name, &lock)) {
        return ws_unlock_result(request, authorization.operation_id, lock_name,
                                false, "INVALID_LOCK_ID");
    }
    const cJSON *timeout_item =
        cJSON_GetObjectItemCaseSensitive(root, "door_open_timeout_s");
    const bool timeout_present = timeout_item != NULL;
    uint32_t timeout_s = 0;
    if ((timeout_present && !json_u32(root, "door_open_timeout_s", &timeout_s)) ||
        !keyvalet_unlock_timeout_valid(timeout_present, timeout_s)) {
        return ws_unlock_result(request, authorization.operation_id, lock_name,
                                false, "INVALID_TIMEOUT");
    }
    keyvalet_unlock_authorization_payload_t payload = {
        .device_id = device_id,
        .lock = lock,
        .timeout_present = timeout_present,
        .door_open_timeout_s = timeout_s,
        .operation_id = authorization.operation_id,
        .nonce_hex = authorization.nonce_hex,
        .issued_at = authorization.issued_at,
        .expires_at = authorization.expires_at,
    };
    esp_err_t result = keyvalet_device_unlock_reserve(
        lock, timeout_present, timeout_s);
    if (result != ESP_OK) {
        return ws_unlock_result(request, authorization.operation_id, lock_name,
                                false, "BUSY_UNAVAILABLE_OR_OPEN");
    }
    result = keyvalet_security_authorize_and_consume(
        KEYVALET_AUTH_UNLOCK, &authorization, NULL, &payload, NULL);
    if (result == ESP_OK) {
        result = keyvalet_journal_append(
            KEYVALET_EVENT_LOCK_UNLOCK_AUTHORIZED, 0, (uint32_t)lock + 1U);
    }
    if (result == ESP_OK) result = keyvalet_device_unlock_commit(lock);
    if (result != ESP_OK) keyvalet_device_unlock_cancel(lock);
    return ws_unlock_result(
        request, authorization.operation_id, lock_name, result == ESP_OK,
        result == ESP_OK ? "ACCEPTED" : esp_err_to_name(result));
}

static bool add_journal_record_json(cJSON *records,
                                    const keyvalet_journal_record_t *record)
{
    static const char digits[] = "0123456789abcdef";
    char metadata_hex[sizeof(record->metadata) * 2U + 1U];
    for (size_t index = 0; index < sizeof(record->metadata); ++index) {
        metadata_hex[index * 2U] = digits[record->metadata[index] >> 4];
        metadata_hex[index * 2U + 1U] = digits[record->metadata[index] & 0x0f];
    }
    metadata_hex[sizeof(record->metadata) * 2U] = '\0';
    cJSON *item = cJSON_CreateObject();
    if (item == NULL) return false;
    const bool complete =
        cJSON_AddStringToObject(item, "device_id", keyvalet_network_device_id()) &&
        cJSON_AddNumberToObject(item, "sequence", (double)record->sequence) &&
        cJSON_AddNumberToObject(item, "event_id", (double)record->type) &&
        cJSON_AddStringToObject(item, "event", keyvalet_event_type_name(record->type)) &&
        cJSON_AddNumberToObject(item, "uptime_ms", (double)record->uptime_ms) &&
        ((record->flags & KEYVALET_EVENT_FLAG_WALL_TIME_VALID) != 0
            ? cJSON_AddNumberToObject(item, "utc_s", (double)record->wall_time_s)
            : cJSON_AddNullToObject(item, "utc_s")) &&
        cJSON_AddNumberToObject(item, "flags", record->flags) &&
        cJSON_AddNumberToObject(item, "detail", record->detail) &&
        cJSON_AddStringToObject(item, "metadata_hex", metadata_hex);
    if (!complete || !cJSON_AddItemToArray(records, item)) {
        cJSON_Delete(item);
        return false;
    }
    return true;
}

static esp_err_t handle_journal_get(httpd_req_t *request, cJSON *root)
{
    int64_t protocol_version;
    uint64_t from_sequence;
    uint32_t limit;
    const char *request_id = json_string(root, "request_id");
    if (!json_i64(root, "protocol_version", &protocol_version) ||
        protocol_version != KEYVALET_JOURNAL_SYNC_PROTOCOL_VERSION ||
        !json_u64(root, "from_sequence", &from_sequence) ||
        !json_u32(root, "limit", &limit) || limit == 0 ||
        limit > KEYVALET_JOURNAL_SYNC_MAX_BATCH || request_id == NULL) {
        return ws_result(request, request_id, false, "MALFORMED");
    }
    keyvalet_journal_record_t records[KEYVALET_JOURNAL_SYNC_MAX_BATCH];
    size_t count;
    uint64_t next_sequence, gap_start, gap_end;
    keyvalet_journal_stats_t snapshot;
    bool more;
    esp_err_t result = keyvalet_journal_read_batch(
        from_sequence, records, limit, &count, &next_sequence, &more,
        &gap_start, &gap_end, &snapshot);
    if (result != ESP_OK)
        return ws_result(request, request_id, false, esp_err_to_name(result));
    cJSON *response = cJSON_CreateObject();
    cJSON *items = response == NULL ? NULL : cJSON_AddArrayToObject(response, "records");
    cJSON *gap = response == NULL ? NULL : cJSON_AddObjectToObject(response, "gap");
    if (response == NULL || items == NULL || gap == NULL) {
        cJSON_Delete(response);
        return ESP_ERR_NO_MEM;
    }
    const bool complete =
        cJSON_AddStringToObject(response, "type", "journal_batch") &&
        cJSON_AddNumberToObject(response, "protocol_version",
                                KEYVALET_JOURNAL_SYNC_PROTOCOL_VERSION) &&
        cJSON_AddStringToObject(response, "request_id", request_id) &&
        cJSON_AddStringToObject(response, "device_id", keyvalet_network_device_id()) &&
        cJSON_AddNumberToObject(response, "requested_from_sequence", (double)from_sequence) &&
        cJSON_AddNumberToObject(response, "next_sequence", (double)next_sequence) &&
        cJSON_AddNumberToObject(response, "returned_count", (double)count) &&
        cJSON_AddBoolToObject(response, "more", more) &&
        cJSON_AddNumberToObject(response, "oldest_available_sequence",
                                (double)snapshot.oldest_sequence) &&
        cJSON_AddNumberToObject(response, "latest_available_sequence",
                                (double)snapshot.latest_sequence) &&
        cJSON_AddNumberToObject(response, "highest_produced_sequence",
                                (double)snapshot.produced_sequence) &&
        cJSON_AddBoolToObject(gap, "exists", gap_start != 0) &&
        cJSON_AddNumberToObject(gap, "start_sequence", (double)gap_start) &&
        cJSON_AddNumberToObject(gap, "end_sequence", (double)gap_end);
    if (!complete) {
        cJSON_Delete(response);
        return ESP_ERR_NO_MEM;
    }
    for (size_t index = 0; index < count; ++index) {
        if (!add_journal_record_json(items, &records[index])) {
            cJSON_Delete(response);
            return ESP_ERR_NO_MEM;
        }
    }
    result = ws_send_json(request, response);
    cJSON_Delete(response);
    return result;
}

static esp_err_t handle_journal_ack(httpd_req_t *request, cJSON *root)
{
    keyvalet_authorization_t authorization = {0};
    int64_t protocol_version;
    uint64_t sequence;
    const char *device_id = json_string(root, "device_id");
    const char *action = json_string(root, "action");
    if (!parse_authorization(root, &authorization) ||
        !json_i64(root, "protocol_version", &protocol_version) ||
        protocol_version != KEYVALET_JOURNAL_SYNC_PROTOCOL_VERSION ||
        !json_u64(root, "highest_contiguous_sequence", &sequence) ||
        device_id == NULL || action == NULL ||
        strcmp(action, "JOURNAL_ACK") != 0) {
        return ws_result(request, authorization.operation_id, false, "MALFORMED");
    }
    keyvalet_journal_sync_status_t status;
    esp_err_t result = keyvalet_journal_sync_get_status(&status);
    if (result != ESP_OK || sequence > status.highest_produced_sequence)
        return ws_result(request, authorization.operation_id, false,
                         result == ESP_OK ? "BEYOND_LATEST" : esp_err_to_name(result));
    keyvalet_journal_ack_payload_t payload = {
        .device_id = device_id,
        .highest_contiguous_sequence = sequence,
        .operation_id = authorization.operation_id,
        .nonce_hex = authorization.nonce_hex,
        .issued_at = authorization.issued_at,
        .expires_at = authorization.expires_at,
    };
    result = keyvalet_security_authorize_and_consume(
        KEYVALET_AUTH_JOURNAL_ACK, &authorization, NULL, NULL, &payload);
    bool already = false;
    if (result == ESP_OK) result = keyvalet_journal_sync_advance(sequence, &already);
    cJSON *response = cJSON_CreateObject();
    if (response == NULL) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(response, "type", "journal_ack_result");
    if (authorization.operation_id != NULL)
        cJSON_AddStringToObject(response, "operation_id", authorization.operation_id);
    cJSON_AddBoolToObject(response, "accepted", result == ESP_OK);
    cJSON_AddStringToObject(response, "code",
        result == ESP_OK ? (already ? "ALREADY_ACKNOWLEDGED" : "ACKNOWLEDGED")
                         : esp_err_to_name(result));
    if (result == ESP_OK)
        cJSON_AddNumberToObject(response, "acknowledged_sequence",
            (double)(already ? status.acknowledged_sequence : sequence));
    const esp_err_t send_result = ws_send_json(request, response);
    cJSON_Delete(response);
    return send_result;
}

static esp_err_t websocket_pre_handshake(httpd_req_t *request)
{
    if (set_console_cors(request) != ESP_OK) return ESP_ERR_NOT_ALLOWED;
    if (keyvalet_web_server_client_count() >= KEYVALET_WEBSOCKET_MAX_CLIENTS) {
        ESP_LOGW(TAG, "Rejecting WebSocket client: four-client limit reached");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t websocket_post_handshake(httpd_req_t *request)
{
    const int fd = httpd_req_to_sockfd(request);
    if (!add_client(fd)) {
        return ESP_FAIL;
    }
    char welcome[JSON_BUFFER_SIZE];
    const int length = build_status_json(welcome, sizeof(welcome), "welcome");
    if (length < 0 || (size_t)length >= sizeof(welcome)) {
        remove_client(fd);
        return ESP_FAIL;
    }
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)welcome,
        .len = (size_t)length,
    };
    const esp_err_t result = httpd_ws_send_frame(request, &frame);
    if (result != ESP_OK) {
        remove_client(fd);
        return result;
    }
    ESP_LOGI(TAG, "WebSocket client connected; clients=%u",
             (unsigned)keyvalet_web_server_client_count());
    return ESP_OK;
}

static esp_err_t websocket_handler(httpd_req_t *request)
{
    keyvalet_tls_status_t tls; keyvalet_tls_status_get(&tls);
    if (!tls.ready) return ESP_FAIL;
    httpd_ws_frame_t frame = {0};
    esp_err_t result = httpd_ws_recv_frame(request, &frame, 0);
    if (result != ESP_OK) {
        return result;
    }
    if (frame.len > REQUEST_BUFFER_SIZE - 1U) {
        ESP_LOGW(TAG, "Rejecting oversized WebSocket client message");
        return ESP_FAIL;
    }
    uint8_t *payload = malloc(frame.len + 1U);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    frame.payload = payload;
    /* The length probe already consumed an empty frame (including its mask).
     * IDF treats frame.len == 0 as a new header read, not a payload read. */
    if (frame.len > 0) {
        result = httpd_ws_recv_frame(request, &frame, frame.len);
        if (result != ESP_OK) {
            free(payload);
            return result;
        }
    }
    payload[frame.len] = '\0';
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        free(payload);
        return ESP_FAIL;
    }
    xSemaphoreTake(s_client_lock, portMAX_DELAY);
    for (size_t i = 0; i < s_client_count; ++i)
        if (s_client_fds[i] == httpd_req_to_sockfd(request)) s_last_rx[i] = esp_timer_get_time();
    xSemaphoreGive(s_client_lock);
    atomic_store(&s_http_stack_free, uxTaskGetStackHighWaterMark(NULL));
    if (frame.type == HTTPD_WS_TYPE_PONG || frame.type == HTTPD_WS_TYPE_PING) {
        if (frame.type == HTTPD_WS_TYPE_PING) { frame.type = HTTPD_WS_TYPE_PONG; result = httpd_ws_send_frame(request, &frame); }
        free(payload); return result;
    }
    if (frame.type != HTTPD_WS_TYPE_TEXT) {
        free(payload);
        return send_read_only_error(request);
    }
    cJSON *root = cJSON_ParseWithLength((char *)payload, frame.len);
    if (root == NULL) {
        free(payload);
        return ws_result(request, NULL, false, "MALFORMED_JSON");
    }
    const char *type = json_string(root, "type");
    esp_err_t response;
    if (type != NULL && strcmp(type, "enroll_begin") == 0) {
        response = handle_enroll_begin(request);
    } else if (type != NULL && strcmp(type, "enroll_complete") == 0) {
        response = handle_enroll_complete(request, root);
    } else if (type != NULL && strcmp(type, "unlock") == 0) {
        response = handle_targeted_unlock(request, root);
    } else if (type != NULL && strcmp(type, "journal_get") == 0) {
        response = handle_journal_get(request, root);
    } else if (type != NULL && strcmp(type, "journal_ack") == 0) {
        response = handle_journal_ack(request, root);
    } else {
        response = send_read_only_error(request);
    }
    cJSON_Delete(root);
    free(payload);
    return response;
}

static void session_closed(httpd_handle_t server, int fd)
{
    (void)server;
    remove_client(fd);
    close(fd);
}

static esp_err_t set_console_cors(httpd_req_t *request)
{
    keyvalet_tls_status_t tls; keyvalet_tls_status_get(&tls);
    if (!tls.ready) return ESP_ERR_NOT_ALLOWED;
    char origin[KEYVALET_TLS_ORIGIN_MAX];
    const size_t length = httpd_req_get_hdr_value_len(request, "Origin");
    if (length == 0) return ESP_ERR_NOT_ALLOWED;
    if (length >= sizeof(origin) ||
        httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof(origin)) != ESP_OK)
        return ESP_ERR_INVALID_ARG;
    const char *allowed = keyvalet_tls_allowed_origin(origin);
    if (!allowed) return ESP_ERR_NOT_ALLOWED;
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(
        request, "Access-Control-Allow-Origin", allowed), TAG,
        "Unable to set CORS origin");
    return httpd_resp_set_hdr(request, "Vary", "Origin");
}

static esp_err_t config_get_handler(httpd_req_t *request)
{
    if (set_console_cors(request) != ESP_OK)
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "origin not permitted");
    keyvalet_settings_t settings;
    if (keyvalet_settings_get(&settings) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "settings unavailable");
    }
    char sensors[384] = "{";
    size_t used = 1;
    bool first = true;
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index) {
        const int added = snprintf(sensors + used, sizeof(sensors) - used,
            "%s\"sensor_%u\":{\"monitoring_enabled\":%s}", first ? "" : ",",
            (unsigned)index + 1U,
            settings.sensor_monitoring_enabled[index] ? "true" : "false");
        if (added < 0 || (size_t)added >= sizeof(sensors) - used)
            return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "settings response unavailable");
        used += (size_t)added; first = false;
    }
    sensors[used++] = '}'; sensors[used] = '\0';
    char locks[192] = "{";
    size_t locks_used = 1;
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index) {
        const int added = snprintf(locks + locks_used, sizeof(locks) - locks_used,
            "%s\"lock_%u\":{\"sensor_required\":%s}", index == 0 ? "" : ",",
            (unsigned)index + 1U, settings.lock_sensor_required[index] ? "true" : "false");
        if (added < 0 || (size_t)added >= sizeof(locks) - locks_used)
            return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "settings response unavailable");
        locks_used += (size_t)added;
    }
    locks[locks_used++] = '}'; locks[locks_used] = '\0';
    char response[768];
    const int length = snprintf(response, sizeof(response),
        "{\"protocol_version\":%u,\"schema_version\":%u,\"revision\":%u,"
        "\"default_door_open_timeout_s\":%u,\"locks\":%s,\"sensors\":%s}",
        KEYVALET_CONFIG_PROTOCOL_VERSION,
        (unsigned)settings.schema_version, (unsigned)settings.revision,
        (unsigned)settings.default_door_open_timeout_s, locks, sensors);
    if (length < 0 || (size_t)length >= sizeof(response))
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "settings response unavailable");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, length);
}

static esp_err_t config_patch_handler(httpd_req_t *request)
{
    if (set_console_cors(request) != ESP_OK)
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "origin not permitted");
    if (request->content_len <= 0 || request->content_len >= REQUEST_BUFFER_SIZE) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid body size");
    }
    char *body = malloc((size_t)request->content_len + 1U);
    if (body == NULL) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "request allocation failed");
    }
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        const int result = httpd_req_recv(request, body + received,
                                          request->content_len - received);
        if (result <= 0) {
            free(body);
            return ESP_FAIL;
        }
        received += (size_t)result;
    }
    body[received] = '\0';
    cJSON *root = cJSON_ParseWithLength(body, received);
    if (root == NULL) {
        free(body);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "malformed JSON");
    }
    keyvalet_authorization_t authorization = {0};
    keyvalet_config_v3_payload_t payload = {0};
    uint32_t protocol_version;
    cJSON *sensors = cJSON_GetObjectItemCaseSensitive(root, "sensors");
    cJSON *locks = cJSON_GetObjectItemCaseSensitive(root, "locks");
    const char *action = json_string(root, "action");
    bool valid = parse_authorization(root, &authorization) &&
        json_u32(root, "protocol_version", &protocol_version) &&
        protocol_version == KEYVALET_CONFIG_PROTOCOL_VERSION &&
        action != NULL && strcmp(action, "UPDATE_CONFIG") == 0 &&
        json_u32(root, "expected_revision", &payload.expected_revision) &&
        json_u32(root, "default_door_open_timeout_s",
                 &payload.default_door_open_timeout_s) && cJSON_IsObject(sensors) &&
        cJSON_IsObject(locks);
    payload.device_id = json_string(root, "device_id");
    payload.operation_id = authorization.operation_id;
    payload.nonce_hex = authorization.nonce_hex;
    payload.issued_at = authorization.issued_at;
    payload.expires_at = authorization.expires_at;
    valid = valid &&
        (size_t)cJSON_GetArraySize(sensors) == KEYVALET_SENSOR_COUNT &&
        (size_t)cJSON_GetArraySize(locks) == KEYVALET_LOCK_COUNT;
    for (size_t index = 0; valid && index < KEYVALET_LOCK_COUNT; ++index) {
        const char *id = keyvalet_lock_id_name(index);
        cJSON *entry = cJSON_GetObjectItemCaseSensitive(locks, id);
        cJSON *required = entry == NULL ? NULL :
            cJSON_GetObjectItemCaseSensitive(entry, "sensor_required");
        valid = cJSON_IsObject(entry) && cJSON_IsBool(required);
        payload.lock_sensor_required[index] = cJSON_IsTrue(required);
    }
    for (size_t index = 0; valid && index < KEYVALET_SENSOR_COUNT; ++index) {
        char id[16];
        snprintf(id, sizeof(id), "sensor_%u", (unsigned)index + 1U);
        cJSON *entry = cJSON_GetObjectItemCaseSensitive(sensors, id);
        cJSON *enabled = entry == NULL ? NULL :
            cJSON_GetObjectItemCaseSensitive(entry, "monitoring_enabled");
        valid = cJSON_IsObject(entry) && cJSON_IsBool(enabled);
        payload.sensor_monitoring_enabled[index] = cJSON_IsTrue(enabled);
    }
    esp_err_t result = valid ? keyvalet_security_authorize_and_consume(
        KEYVALET_AUTH_UPDATE_CONFIG, &authorization, &payload, NULL, NULL)
        : ESP_ERR_INVALID_ARG;
    keyvalet_settings_t settings = {
        .schema_version = KEYVALET_SETTINGS_SCHEMA_VERSION,
        .default_door_open_timeout_s = payload.default_door_open_timeout_s,
    };
    for (size_t index = 0; index < KEYVALET_LOCK_COUNT; ++index)
        settings.lock_sensor_required[index] = payload.lock_sensor_required[index];
    for (size_t index = 0; index < KEYVALET_SENSOR_COUNT; ++index)
        settings.sensor_monitoring_enabled[index] =
            payload.sensor_monitoring_enabled[index];
    if (result == ESP_OK) {
        result = keyvalet_settings_update(&settings, payload.expected_revision);
    }
    if (result == ESP_OK) {
        const esp_err_t apply_result = keyvalet_device_controller_apply_settings();
        if (apply_result != ESP_OK) {
            ESP_LOGE(TAG, "Settings persisted but live application failed: %s",
                     esp_err_to_name(apply_result));
        }
    }
    if (result == ESP_OK) {
        const esp_err_t journal_result = keyvalet_journal_append(
            KEYVALET_EVENT_CONFIG_UPDATED, 0, payload.expected_revision + 1U);
        if (journal_result != ESP_OK) {
            ESP_LOGE(TAG, "Settings persisted but CONFIG_UPDATED could not be journaled: %s",
                     esp_err_to_name(journal_result));
        }
    }
    cJSON_Delete(root);
    free(body);
    char response[192];
    const int length = snprintf(response, sizeof(response),
        "{\"type\":\"config_result\",\"accepted\":%s,\"code\":\"%s\"}",
        result == ESP_OK ? "true" : "false",
        result == ESP_OK ? "ACCEPTED" : esp_err_to_name(result));
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_status(request, result == ESP_OK ? "200 OK" : "400 Bad Request");
    return httpd_resp_send(request, response, length);
}

static esp_err_t config_options_handler(httpd_req_t *request)
{
    if (set_console_cors(request) != ESP_OK)
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "origin not permitted");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", "GET, PATCH, OPTIONS");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Content-Type");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t start_server(void)
{
    if (!s_events_registered) {
        esp_err_t registered = esp_event_handler_register(ESP_HTTPS_SERVER_EVENT,
            HTTPS_SERVER_EVENT_ERROR, tls_error_event, NULL);
        if (registered != ESP_OK) return registered;
        s_events_registered = true;
    }
    esp_err_t result = keyvalet_tls_credentials_load(&s_credentials);
    if (result != ESP_OK) return result;
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd.stack_size = 16384;
    /* Four WSS clients plus one short HTTPS request. Never evict a healthy
     * operational client to admit an additional handshake. */
    config.httpd.max_open_sockets = KEYVALET_WEBSOCKET_MAX_CLIENTS + 1;
    config.httpd.lru_purge_enable = false;
    config.httpd.recv_wait_timeout = 2;
    config.httpd.send_wait_timeout = 2;
    config.httpd.keep_alive_enable = true;
    config.httpd.keep_alive_idle = 15;
    config.httpd.keep_alive_interval = 5;
    config.httpd.keep_alive_count = 3;
    config.httpd.close_fn = session_closed;
    config.tls_handshake_timeout_ms = 3000;
    config.user_cb = tls_session_event;
    config.servercert = (const uint8_t *)s_credentials->chain;
    config.servercert_len = s_credentials->chain_length + 1;
    config.prvtkey_pem = s_credentials->key; /* ESP-TLS accepts DER too. */
    config.prvtkey_len = s_credentials->key_length;
    result = httpd_ssl_start(&s_server, &config);
    if (result != ESP_OK) {
        s_server = NULL;
        keyvalet_tls_credentials_free(s_credentials); s_credentials = NULL;
        return result;
    }

    const httpd_uri_t status_uri = {
        .uri = KEYVALET_HTTP_STATUS_PATH,
        .method = HTTP_GET,
        .handler = status_handler,
    };
    const httpd_uri_t websocket_uri = {
        .uri = KEYVALET_WEBSOCKET_PATH,
        .method = HTTP_GET,
        .handler = websocket_handler,
        .is_websocket = true,
        .handle_ws_control_frames = true,
        .ws_pre_handshake_cb = websocket_pre_handshake,
        .ws_post_handshake_cb = websocket_post_handshake,
    };
    const httpd_uri_t config_get_uri = {
        .uri = "/api/v1/config", .method = HTTP_GET,
        .handler = config_get_handler,
    };
    const httpd_uri_t config_patch_uri = {
        .uri = "/api/v1/config", .method = HTTP_PATCH,
        .handler = config_patch_handler,
    };
    const httpd_uri_t config_options_uri = {
        .uri = "/api/v1/config", .method = HTTP_OPTIONS,
        .handler = config_options_handler,
    };
    result = httpd_register_uri_handler(s_server, &status_uri);
    if (result == ESP_OK) {
        result = httpd_register_uri_handler(s_server, &websocket_uri);
    }
    if (result == ESP_OK) result = httpd_register_uri_handler(s_server, &config_get_uri);
    if (result == ESP_OK) result = httpd_register_uri_handler(s_server, &config_patch_uri);
    if (result == ESP_OK) result = httpd_register_uri_handler(s_server, &config_options_uri);
    if (result != ESP_OK) {
        httpd_ssl_stop(s_server);
        s_server = NULL;
        keyvalet_tls_credentials_free(s_credentials); s_credentials = NULL;
        return result;
    }
    ESP_LOGI(TAG, "HTTPS/WSS ready on port 443; hostname=%s; WSS clients=4, HTTPS overlap=1", s_credentials->hostname);
    atomic_store(&s_listening, true);
    return ESP_OK;
}

static void async_send_complete(esp_err_t result, int fd, void *argument)
{
    async_send_context_t *context = argument;
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Live event send failed for fd=%d: %s", fd,
                 esp_err_to_name(result));
        (void)httpd_sess_trigger_close(context->server, fd);
    }
    free(context->payload);
    free(context);
}

static void send_event_to_client(httpd_handle_t server, int fd, const char *json)
{
    async_send_context_t *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        ESP_LOGW(TAG, "Dropped live notification for fd=%d: no memory", fd);
        return;
    }
    context->payload = strdup(json);
    context->server = server;
    if (context->payload == NULL) {
        free(context);
        ESP_LOGW(TAG, "Dropped live notification for fd=%d: no memory", fd);
        return;
    }
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)context->payload,
        .len = strlen(context->payload),
    };
    const esp_err_t result = httpd_ws_send_data_async(
        server, fd, &frame, async_send_complete, context);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Dropped live notification for fd=%d: %s", fd,
                 esp_err_to_name(result));
        free(context->payload);
        free(context);
    }
}

static void broadcast_record(const keyvalet_journal_record_t *record)
{
    if (s_server == NULL) {
        return;
    }
    int clients[KEYVALET_WEBSOCKET_MAX_CLIENTS];
    size_t count;
    xSemaphoreTake(s_client_lock, portMAX_DELAY);
    count = s_client_count;
    memcpy(clients, s_client_fds, count * sizeof(clients[0]));
    xSemaphoreGive(s_client_lock);
    if (count == 0) {
        return;
    }

    char json[JSON_BUFFER_SIZE];
    const int length = build_event_json(json, sizeof(json), record);
    if (length < 0 || (size_t)length >= sizeof(json)) {
        ESP_LOGW(TAG, "Dropped live notification for seq=%" PRIu64
                      ": JSON buffer exceeded",
                 record->sequence);
        return;
    }
    for (size_t index = 0; index < count; ++index) {
        if (httpd_ws_get_fd_info(s_server, clients[index]) ==
            HTTPD_WS_CLIENT_WEBSOCKET) {
            send_event_to_client(s_server, clients[index], json);
        } else {
            remove_client(clients[index]);
        }
    }
}

static bool stop_server(void)
{
    if (!s_server) return true;
    if (httpd_ssl_stop(s_server) != ESP_OK) return false;
    atomic_store(&s_listening, false);
    s_server = NULL;
    keyvalet_tls_credentials_free(s_credentials); s_credentials = NULL;
    keyvalet_connectivity_indicator_set_clients(0);
    return true;
}

static void websocket_liveness(void *context)
{
    (void)context;
    const int64_t now = esp_timer_get_time();
    int clients[KEYVALET_WEBSOCKET_MAX_CLIENTS];
    int64_t seen[KEYVALET_WEBSOCKET_MAX_CLIENTS];
    xSemaphoreTake(s_client_lock, portMAX_DELAY);
    const size_t count = s_client_count;
    memcpy(clients, s_client_fds, count * sizeof(*clients));
    memcpy(seen, s_last_rx, count * sizeof(*seen));
    xSemaphoreGive(s_client_lock);
    httpd_ws_frame_t ping = {.final = true, .type = HTTPD_WS_TYPE_PING, .len = 0};
    for (size_t i = 0; i < count; ++i) {
        if (now - seen[i] > 60000000 || httpd_ws_send_frame_async(s_server, clients[i], &ping) != ESP_OK)
            (void)httpd_sess_trigger_close(s_server, clients[i]);
    }
    /* Close idle short HTTPS sessions without purging operational WSS clients. */
    int sockets[KEYVALET_WEBSOCKET_MAX_CLIENTS + 1]; size_t total = sizeof(sockets) / sizeof(*sockets);
    if (httpd_get_client_list(s_server, &total, sockets) == ESP_OK)
        for (size_t i = 0; i < total; ++i)
            if (httpd_ws_get_fd_info(s_server, sockets[i]) == HTTPD_WS_CLIENT_HTTP)
                (void)httpd_sess_trigger_close(s_server, sockets[i]);
}

static void web_worker_task(void *context)
{
    (void)context;
    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
        keyvalet_tls_status_t tls; keyvalet_tls_status_get(&tls);
        const bool should_run = atomic_load(&s_network_available) && tls.ready;
        const bool activate = atomic_load(&s_activate_requested);
        bool maintenance = false;
        if ((activate || (!should_run && s_server)) && keyvalet_lock_maintenance_begin() == ESP_OK) {
            maintenance = true;
            const bool stopped = stop_server();
            if (activate) {
                const esp_err_t result = stopped ? keyvalet_tls_activate() : ESP_FAIL;
                atomic_store(&s_activation_result, result == ESP_OK ? 1 : -1);
                atomic_store(&s_activate_requested, false);
                keyvalet_tls_status_get(&tls);
            }
        }
        if (atomic_load(&s_network_available) && tls.ready && s_server == NULL) {
            if (maintenance || keyvalet_lock_maintenance_begin() == ESP_OK) {
                maintenance = true;
                const esp_err_t result = start_server();
                if (result != ESP_OK) {
                    ESP_LOGE(TAG, "Unable to start local server: %s", esp_err_to_name(result));
                }
            }
        }
        if (maintenance) keyvalet_lock_maintenance_end();
        if (s_server && esp_timer_get_time() - s_last_ping >= 15000000) {
            if (httpd_queue_work(s_server, websocket_liveness, NULL) == ESP_OK) s_last_ping = esp_timer_get_time();
        }

        keyvalet_journal_record_t record;
        while (xQueueReceive(s_event_queue, &record, 0) == pdTRUE) {
            broadcast_record(&record);
        }
    }
}

esp_err_t keyvalet_web_server_init(void)
{
    if (s_worker_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_client_lock = xSemaphoreCreateMutex();
    s_event_queue = xQueueCreate(EVENT_QUEUE_LENGTH,
                                 sizeof(keyvalet_journal_record_t));
    if (s_client_lock == NULL || s_event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(web_worker_task, "web_worker", 8192, NULL, 4,
                    &s_worker_task) != pdPASS) {
        s_worker_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t keyvalet_web_server_request_tls_activation(void)
{
    if (!s_worker_task) return ESP_ERR_INVALID_STATE;
    keyvalet_tls_status_t status; keyvalet_tls_status_get(&status);
    bool expected = false;
    if (!status.candidate || !status.writable || !atomic_compare_exchange_strong(&s_activate_requested, &expected, true))
        return ESP_ERR_INVALID_STATE;
    atomic_store(&s_activation_result, 0);
    xTaskNotifyGive(s_worker_task); return ESP_OK;
}

bool keyvalet_web_server_tls_activation_pending(void) { return atomic_load(&s_activate_requested); }
const char *keyvalet_web_server_tls_activation_state(void)
{
    return atomic_load(&s_activate_requested) ? "PENDING" : atomic_load(&s_activation_result) > 0 ? "ACTIVATED" :
        atomic_load(&s_activation_result) < 0 ? "FAILED" : "IDLE";
}

void keyvalet_web_server_set_network_available(bool available)
{
    atomic_store(&s_network_available, available);
    if (s_worker_task != NULL) {
        xTaskNotifyGive(s_worker_task);
    }
}

void keyvalet_web_server_publish_record(
    const keyvalet_journal_record_t *record, void *context)
{
    (void)context;
    if (record == NULL || s_event_queue == NULL ||
        xQueueSend(s_event_queue, record, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Dropped live notification before network queueing");
        return;
    }
    xTaskNotifyGive(s_worker_task);
}
