#include "network_manager.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "time_sync.h"
#include "nvs.h"
#include "sdkconfig.h"

#define CANDIDATE_TIMEOUT_MS 30000LL
#define SCAN_TIMEOUT_MS 10000LL
#define STOP_TIMEOUT_MS 5000LL

/* One worker owns Wi-Fi operations. STOP is an event-order barrier before
 * changing configuration: queued old DHCP events cannot accept a candidate. */
typedef enum { REQUEST_CONFIGURE, REQUEST_SCAN, STA_START, STA_STOP,
               STA_DISCONNECTED, GOT_IP, SCAN_DONE } message_type_t;
typedef struct {
    message_type_t type;
    union { keyvalet_wifi_credentials_t credentials; esp_ip4_addr_t ip; bool scan_ok; } data;
} message_t;
typedef enum { NORMAL, STOPPING, CANDIDATE, SCANNING, RADIO_FAULT } phase_t;
typedef enum { USE_SAVED, USE_CANDIDATE, START_SCAN } after_stop_t;

static const char *TAG = "network";
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_queue;
static esp_netif_t *s_netif;
static keyvalet_network_status_t s_status;
static keyvalet_wifi_credentials_t s_saved, s_candidate;
static bool s_have_saved, s_busy, s_connecting, s_scan_started;
static phase_t s_phase;
static after_stop_t s_after_stop;
static int64_t s_deadline, s_reconnect_at, s_next_rssi;
static uint32_t s_backoff_ms = 1000;
static atomic_bool s_event_overflow;
static char s_device_id[KEYVALET_DEVICE_ID_MAX] = "keyvalet-unknown";
static keyvalet_network_connection_callback_t s_connection_callback;
static void *s_connection_context;
static keyvalet_network_provision_callback_t s_provision_callback;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void provision_event(keyvalet_network_provision_event_t event,
    keyvalet_network_error_t error, const keyvalet_network_scan_result_t *result)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    keyvalet_network_provision_callback_t callback = s_provision_callback;
    xSemaphoreGive(s_lock);
    if (callback) callback(event, error, result);
}

static void disconnected(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool changed = s_status.connected;
    s_status.connected = false; s_status.ip_address[0] = 0;
    s_status.ssid[0] = 0; s_status.rssi_valid = false;
    xSemaphoreGive(s_lock);
    if (changed && s_connection_callback) s_connection_callback(false, s_connection_context);
}

static void schedule_reconnect(void)
{
    s_connecting = false;
    s_reconnect_at = s_have_saved ? now_ms() + s_backoff_ms : 0;
    if (s_backoff_ms < 30000) {
        s_backoff_ms *= 2;
        if (s_backoff_ms > 30000) s_backoff_ms = 30000;
    }
}

static void release_request(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_busy = false; s_status.provisioning = false; s_status.scanning = false;
    xSemaphoreGive(s_lock);
}

static void apply_after_stop(void);

static bool switch_radio(after_stop_t next)
{
    s_phase = STOPPING; s_after_stop = next;
    s_deadline = now_ms() + STOP_TIMEOUT_MS;
    s_connecting = false; s_reconnect_at = 0;
    disconnected();
    const esp_err_t result = esp_wifi_stop();
    if (result == ESP_ERR_WIFI_NOT_STARTED) apply_after_stop();
    else if (result != ESP_OK) { s_phase = RADIO_FAULT; release_request(); return false; }
    return true;
}

static void candidate_failed(keyvalet_network_error_t error)
{
    if (error == KEYVALET_NETWORK_STORAGE_FAILED)
        ESP_LOGE(TAG, "Wi-Fi credential persistence failed; durable outcome uncertain; restoring previous runtime config, reboot required before further writes");
    keyvalet_wifi_credentials_clear(&s_candidate);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.provisioning = false;
    xSemaphoreGive(s_lock);
    /* Preserve old durable credentials. If commit failed ambiguously, reboot
     * may load the candidate; never mask that by rewriting an older RAM copy. */
    (void)switch_radio(USE_SAVED);
    provision_event(KEYVALET_NETWORK_PROVISION_RESULT, error, NULL);
}

static void attempt_connection(void)
{
    s_reconnect_at = 0; s_connecting = true;
    if (esp_wifi_connect() != ESP_OK) {
        if (s_phase == CANDIDATE) candidate_failed(KEYVALET_NETWORK_CONNECT_FAILED);
        else schedule_reconnect();
    }
}

static void scan_finished(keyvalet_network_error_t error)
{
    s_phase = NORMAL; s_scan_started = false;
    if (!s_status.connected) schedule_reconnect();
    provision_event(KEYVALET_NETWORK_SCAN_DONE, error, NULL);
    release_request();
}

static void start_scan(void)
{
    const wifi_scan_config_t scan = {
        .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 0, .max = 120},
    };
    s_deadline = now_ms() + SCAN_TIMEOUT_MS;
    s_scan_started = esp_wifi_scan_start(&scan, false) == ESP_OK;
    if (!s_scan_started) scan_finished(KEYVALET_NETWORK_SCAN_FAILED);
}

static void apply_after_stop(void)
{
    const keyvalet_wifi_credentials_t *credentials =
        s_after_stop == USE_CANDIDATE ? &s_candidate : &s_saved;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, credentials->ssid, strlen(credentials->ssid));
    memcpy(config.sta.password, credentials->password, strlen(credentials->password));
    config.sta.threshold.authmode = credentials->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
#ifdef CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
#endif
    s_phase = s_after_stop == USE_CANDIDATE ? CANDIDATE :
              s_after_stop == START_SCAN ? SCANNING : NORMAL;
    s_deadline = now_ms() + CANDIDATE_TIMEOUT_MS;
    const esp_err_t configured = esp_wifi_set_config(WIFI_IF_STA, &config);
    volatile uint8_t *secret = (volatile uint8_t *)&config;
    for (size_t i = 0; i < sizeof(config); ++i) secret[i] = 0;
    if (configured != ESP_OK || esp_wifi_start() != ESP_OK) {
        if (s_phase == CANDIDATE) candidate_failed(KEYVALET_NETWORK_UNAVAILABLE);
        else if (s_phase == SCANNING) scan_finished(KEYVALET_NETWORK_SCAN_FAILED);
        else { s_phase = RADIO_FAULT; release_request(); }
    } else if (s_phase == NORMAL) release_request();
}

static const char *authentication(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP (unsupported)";
    case WIFI_AUTH_WPA_PSK: return "WPA (unsupported)";
    case WIFI_AUTH_WPA2_PSK: return "WPA2-Personal";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2-Personal";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "Enterprise (unsupported)";
    case WIFI_AUTH_WPA3_PSK: return "WPA3-Personal";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3-Personal";
    default: return "Other (unsupported)";
    }
}

static int compare_ap(const void *left, const void *right)
{
    const wifi_ap_record_t *a = left, *b = right;
    if (a->rssi != b->rssi) return (int)b->rssi - a->rssi;
    const int name = strcmp((const char *)a->ssid, (const char *)b->ssid);
    return name ? name : (int)a->authmode - (int)b->authmode;
}

static void scan_results(void)
{
    uint16_t count = KEYVALET_WIFI_SCAN_LIMIT;
    wifi_ap_record_t *records = calloc(count, sizeof(*records));
    if (!records || esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        free(records); (void)esp_wifi_clear_ap_list();
        scan_finished(KEYVALET_NETWORK_SCAN_FAILED); return;
    }
    for (uint16_t i = 0; i < count; ++i) records[i].ssid[32] = 0;
    qsort(records, count, sizeof(*records), compare_ap);
    for (uint16_t i = 0; i < count; ++i) {
        const size_t length = strnlen((const char *)records[i].ssid, 32);
        if (!length || !keyvalet_wifi_text_valid(records[i].ssid, length)) continue;
        bool duplicate = false;
        for (uint16_t j = 0; j < i; ++j)
            if (records[i].authmode == records[j].authmode &&
                !strcmp((const char *)records[i].ssid, (const char *)records[j].ssid)) duplicate = true;
        if (duplicate) continue;
        keyvalet_network_scan_result_t result = {
            .rssi = records[i].rssi, .secured = records[i].authmode != WIFI_AUTH_OPEN,
        };
        memcpy(result.ssid, records[i].ssid, length);
        snprintf(result.authentication, sizeof(result.authentication), "%s", authentication(records[i].authmode));
        provision_event(KEYVALET_NETWORK_SCAN_RESULT, KEYVALET_NETWORK_OK, &result);
    }
    free(records);
    scan_finished(KEYVALET_NETWORK_OK);
}

static void connected(const esp_ip4_addr_t *ip)
{
    if (s_phase == STOPPING || s_phase == RADIO_FAULT) return;
    if (s_phase == CANDIDATE && now_ms() >= s_deadline) {
        candidate_failed(KEYVALET_NETWORK_TIMEOUT); return;
    }
    wifi_ap_record_t ap;
    esp_netif_ip_info_t current;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK || ip->addr == 0 ||
        esp_netif_get_ip_info(s_netif, &current) != ESP_OK || current.ip.addr != ip->addr) return;
    const keyvalet_wifi_credentials_t *expected = s_phase == CANDIDATE ? &s_candidate : &s_saved;
    if (memcmp(ap.ssid, expected->ssid, strlen(expected->ssid)) ||
        strnlen((const char *)ap.ssid, 32) != strlen(expected->ssid)) return;
    if (s_phase == CANDIDATE) {
        if ((s_status.persistence_uncertain || !s_have_saved || strcmp(s_saved.ssid, s_candidate.ssid) ||
             strcmp(s_saved.password, s_candidate.password)) &&
            keyvalet_wifi_store_save(&s_candidate) != ESP_OK) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_status.persistence_uncertain = true;
            xSemaphoreGive(s_lock);
            candidate_failed(KEYVALET_NETWORK_STORAGE_FAILED); return;
        }
        s_saved = s_candidate; s_have_saved = true;
        keyvalet_wifi_credentials_clear(&s_candidate);
        s_phase = NORMAL;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.configured = true;
        s_status.provisioning = false;
        xSemaphoreGive(s_lock);
    } else if (!s_have_saved) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool changed = !s_status.connected;
    s_status.connected = true;
    snprintf(s_status.ip_address, sizeof(s_status.ip_address), IPSTR, IP2STR(ip));
    snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", s_saved.ssid);
    s_status.rssi = ap.rssi; s_status.rssi_valid = true;
    xSemaphoreGive(s_lock);
    s_connecting = false; s_reconnect_at = 0; s_backoff_ms = 1000;
    if (changed && s_connection_callback) s_connection_callback(true, s_connection_context);
}

static void handle_message(message_t *message)
{
    switch (message->type) {
    case REQUEST_CONFIGURE:
        s_candidate = message->data.credentials;
        keyvalet_wifi_credentials_clear(&message->data.credentials);
        provision_event(KEYVALET_NETWORK_PROVISIONING, KEYVALET_NETWORK_OK, NULL);
        if (!switch_radio(USE_CANDIDATE)) candidate_failed(KEYVALET_NETWORK_UNAVAILABLE);
        break;
    case REQUEST_SCAN:
        if (s_status.connected || !s_connecting) {
            s_reconnect_at = 0; s_phase = SCANNING; start_scan();
        } else if (!switch_radio(START_SCAN)) scan_finished(KEYVALET_NETWORK_SCAN_FAILED);
        break;
    case STA_STOP:
        if (s_phase == STOPPING) apply_after_stop();
        break;
    case STA_START:
        if (s_phase == SCANNING && !s_scan_started) start_scan();
        else if (s_phase == CANDIDATE || (s_phase == NORMAL && s_have_saved)) attempt_connection();
        break;
    case STA_DISCONNECTED:
        disconnected(); s_connecting = false;
        if (s_phase == CANDIDATE) candidate_failed(KEYVALET_NETWORK_CONNECT_FAILED);
        else if (s_phase == NORMAL) schedule_reconnect();
        break;
    case GOT_IP: {
        const bool candidate = s_phase == CANDIDATE;
        connected(&message->data.ip);
        if (candidate && s_phase == NORMAL && s_status.connected) {
            provision_event(KEYVALET_NETWORK_PROVISION_RESULT, KEYVALET_NETWORK_OK, NULL);
            release_request();
        }
        break;
    }
    case SCAN_DONE:
        if (s_phase == SCANNING && s_scan_started) {
            if (message->data.scan_ok) scan_results();
            else { (void)esp_wifi_clear_ap_list(); scan_finished(KEYVALET_NETWORK_SCAN_FAILED); }
        }
        break;
    }
}

static void network_tick(void)
{
    const int64_t now = now_ms();
    if (atomic_exchange(&s_event_overflow, false)) {
        if (s_phase == CANDIDATE) candidate_failed(KEYVALET_NETWORK_UNAVAILABLE);
        else if (s_phase == SCANNING) {
            (void)esp_wifi_scan_stop(); (void)switch_radio(USE_SAVED);
            provision_event(KEYVALET_NETWORK_SCAN_DONE, KEYVALET_NETWORK_SCAN_FAILED, NULL);
        } else if (s_phase != STOPPING) schedule_reconnect();
    }
    if ((s_phase == CANDIDATE || s_phase == STOPPING) && now >= s_deadline) {
        if (s_phase == CANDIDATE || s_after_stop == USE_CANDIDATE)
            candidate_failed(KEYVALET_NETWORK_TIMEOUT);
        else if (s_after_stop == START_SCAN) {
            (void)switch_radio(USE_SAVED);
            provision_event(KEYVALET_NETWORK_SCAN_DONE, KEYVALET_NETWORK_SCAN_FAILED, NULL);
        } else { s_phase = RADIO_FAULT; release_request(); }
    } else if (s_phase == SCANNING && now >= s_deadline) {
        (void)esp_wifi_scan_stop(); (void)esp_wifi_clear_ap_list();
        (void)switch_radio(USE_SAVED);
        provision_event(KEYVALET_NETWORK_SCAN_DONE, KEYVALET_NETWORK_TIMEOUT, NULL);
    }
    if (s_phase == NORMAL && s_reconnect_at && now >= s_reconnect_at) attempt_connection();
    if (s_status.connected && now >= s_next_rssi) {
        wifi_ap_record_t ap;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.rssi_valid = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
        if (s_status.rssi_valid) s_status.rssi = ap.rssi;
        xSemaphoreGive(s_lock);
        s_next_rssi = now + 30000;
    }
}

static void network_task(void *context)
{
    (void)context;
    s_after_stop = USE_SAVED;
    apply_after_stop();
    message_t message;
    for (;;) {
        if (xQueueReceive(s_queue, &message, pdMS_TO_TICKS(100)) == pdTRUE) {
            handle_message(&message);
            memset(&message, 0, sizeof(message));
        }
        network_tick();
    }
}

static void network_event_handler(void *argument, esp_event_base_t base, int32_t id, void *data)
{
    (void)argument;
    message_t message = {0};
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) message.type = STA_START;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) message.type = STA_STOP;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) message.type = STA_DISCONNECTED;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        message.type = SCAN_DONE;
        message.data.scan_ok = data && ((wifi_event_sta_scan_done_t *)data)->status == 0;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        message.type = GOT_IP; message.data.ip = ((ip_event_got_ip_t *)data)->ip_info.ip;
    } else return;
    if (xQueueSend(s_queue, &message, 0) != pdTRUE) atomic_store(&s_event_overflow, true);
}

esp_err_t keyvalet_network_manager_start(keyvalet_network_connection_callback_t callback, void *context)
{
    if (s_lock) return ESP_ERR_INVALID_STATE;
    s_lock = xSemaphoreCreateMutex(); s_queue = xQueueCreate(16, sizeof(message_t));
    if (!s_lock || !s_queue) return ESP_ERR_NO_MEM;
    s_connection_callback = callback; s_connection_context = context;
    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "Device identity unavailable");
    snprintf(s_device_id, sizeof(s_device_id), "keyvalet-%02x%02x%02x", mac[3], mac[4], mac[5]);
    const esp_err_t stored = keyvalet_wifi_store_load(&s_saved);
    s_have_saved = stored == ESP_OK; s_status.configured = s_have_saved;
    if (stored != ESP_OK && stored != ESP_ERR_NVS_NOT_FOUND)
        ESP_LOGE(TAG, "Wi-Fi storage unavailable/invalid; USB reconfiguration available");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "TCP/IP initialization failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Event loop initialization failed");
    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) return ESP_ERR_NO_MEM;
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi initialization failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi RAM storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi station mode failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event_handler, NULL), TAG, "Wi-Fi events unavailable");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event_handler, NULL), TAG, "IP events unavailable");
    if (keyvalet_time_sync_init() != ESP_OK) ESP_LOGW(TAG, "SNTP unavailable; local provisioning still available");
    s_status.available = true;
    if (xTaskCreate(network_task, "wifi_manager", 6144, NULL, 3, NULL) != pdPASS) {
        s_status.available = false; return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Wi-Fi manager initialized (%s); USB provisioning available", s_have_saved ? "saved configuration" : "unconfigured");
    return ESP_OK;
}

esp_err_t keyvalet_network_status_get(keyvalet_network_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY); *status = s_status; xSemaphoreGive(s_lock);
    return ESP_OK;
}

const char *keyvalet_network_device_id(void) { return s_device_id; }

esp_err_t keyvalet_network_last_ipv4_octet(uint8_t *octet)
{
    keyvalet_network_status_t status;
    if (!octet || keyvalet_network_status_get(&status) != ESP_OK || !status.connected) return ESP_ERR_INVALID_STATE;
    unsigned a, b, c, d;
    if (sscanf(status.ip_address, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return ESP_ERR_INVALID_STATE;
    *octet = (uint8_t)d; return ESP_OK;
}

void keyvalet_network_set_provision_callback(keyvalet_network_provision_callback_t callback)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY); s_provision_callback = callback; xSemaphoreGive(s_lock);
}

static esp_err_t submit(message_t *message)
{
    if (!s_lock || !s_queue) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_busy || !s_status.available) { xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE; }
    s_busy = true;
    s_status.provisioning = message->type == REQUEST_CONFIGURE;
    s_status.scanning = message->type == REQUEST_SCAN;
    const bool sent = xQueueSend(s_queue, message, 0) == pdTRUE;
    if (!sent) { s_busy = false; s_status.provisioning = false; s_status.scanning = false; }
    xSemaphoreGive(s_lock);
    return sent ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t keyvalet_network_scan(void)
{
    message_t message = {.type = REQUEST_SCAN}; return submit(&message);
}

esp_err_t keyvalet_network_configure(const keyvalet_wifi_credentials_t *credentials)
{
    if (!credentials || !keyvalet_wifi_credentials_valid(credentials)) return ESP_ERR_INVALID_ARG;
    message_t message = {.type = REQUEST_CONFIGURE, .data.credentials = *credentials};
    const esp_err_t result = submit(&message);
    keyvalet_wifi_credentials_clear(&message.data.credentials);
    return result;
}
