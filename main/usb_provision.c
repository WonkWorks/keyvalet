#include "usb_provision.h"

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "improv_serial.h"
#include "network_manager.h"
#include "sdkconfig.h"
#include "security.h"
#include "tls_identity.h"
#include "tls_usb.h"
#ifdef CONFIG_KEYVALET_DIAGNOSTICS
#include "diagnostic_console.h"
#endif

static const char *TAG = "usb_wifi";
static const keyvalet_hardware_profile_t *s_profile;

/* KeyValet-only read query; standard Improv URL fields remain URLs. */
#define KEYVALET_CURRENT_NETWORK_RPC 0x80
#define KEYVALET_STAGE_ENROLLMENT_KEY_RPC 0x81

static void send_bytes(const uint8_t *bytes, size_t length)
{
    /* Bypass VFS LF->CRLF conversion. One ring-buffer write enqueues the whole
     * packet or none; no per-byte VFS timeout when a host stops reading. IDF
     * logs and diagnostics use stdout, whose lock excludes those writers. */
    if (!usb_serial_jtag_is_connected()) return;
    uint8_t packet[KEYVALET_IMPROV_MAX_FRAME + 1];
    if (!length || length > KEYVALET_IMPROV_MAX_FRAME) return;
    memcpy(packet, bytes, length); packet[length] = '\n';
    flockfile(stdout);
    (void)usb_serial_jtag_write_bytes(packet, length + 1, pdMS_TO_TICKS(50));
    funlockfile(stdout);
}

static void single_byte(uint8_t type, uint8_t value)
{
    uint8_t frame[11];
    send_bytes(frame, keyvalet_improv_frame(frame, type, &value, 1));
}

static void result(uint8_t command, const char *const *strings, size_t count)
{
    uint8_t frame[KEYVALET_IMPROV_MAX_FRAME];
    const size_t length = keyvalet_improv_result(frame, command, strings, count);
    if (length) send_bytes(frame, length);
    else single_byte(2, 0xff);
}

static uint8_t state(keyvalet_network_status_t *status)
{
    if (keyvalet_network_status_get(status) != ESP_OK || !status->available) return 0;
    if (status->provisioning) return 3;
    return status->connected ? 4 : 2;
}

static void connection_result(uint8_t command, const keyvalet_network_status_t *status)
{
    (void)status;
    keyvalet_tls_status_t tls; keyvalet_tls_status_get(&tls);
    if (!tls.ready) { result(command, NULL, 0); return; }
    char url[KEYVALET_TLS_HOST_MAX + 32];
    /* The device has no root web UI. Supply an actually implemented resource. */
    snprintf(url, sizeof(url), "https://%s/api/v1/status", tls.hostname);
    if (strlen(url) > 240) { result(command, NULL, 0); return; }
    const char *strings[] = {url};
    result(command, strings, 1);
}

static void tls_reply(const char *const *strings, size_t count)
{
    result(KEYVALET_TLS_USB_RPC, strings, count);
}

static void network_result(keyvalet_network_provision_event_t event,
    keyvalet_network_error_t error, const keyvalet_network_scan_result_t *entry)
{
    if (event == KEYVALET_NETWORK_PROVISIONING) { single_byte(2, 0); single_byte(1, 3); return; }
    if (event == KEYVALET_NETWORK_SCAN_RESULT) {
        char rssi[8]; snprintf(rssi, sizeof(rssi), "%d", entry->rssi);
        const char *strings[] = {entry->ssid, rssi, entry->secured ? "YES" : "NO", entry->authentication};
        result(4, strings, 4); return;
    }
    if (error != KEYVALET_NETWORK_OK) {
        if (event == KEYVALET_NETWORK_PROVISION_RESULT) single_byte(1, 2);
        single_byte(2, error == KEYVALET_NETWORK_STORAGE_FAILED ||
                       error == KEYVALET_NETWORK_UNAVAILABLE ? 0xff : 3);
        return;
    }
    if (event == KEYVALET_NETWORK_SCAN_DONE) { result(4, NULL, 0); return; }
    keyvalet_network_status_t status = {0};
    if (state(&status) == 4) { single_byte(1, 4); connection_result(1, &status); }
}

static void handle_improv(const uint8_t *bytes)
{
    const size_t length = bytes[8];
    const uint8_t *payload = bytes + 9;
    if (bytes[7] != 3 || length < 2 || payload[1] != length - 2) {
        single_byte(2, 1); return;
    }
    const uint8_t command = payload[0];
    if (command == KEYVALET_TLS_USB_RPC) {
        if (!keyvalet_tls_usb_handle(payload + 2, length - 2, tls_reply)) single_byte(2, 1);
        return;
    }
    if (command == 1) {
        keyvalet_wifi_credentials_t credentials;
        if (!keyvalet_improv_credentials(payload, length, &credentials)) {
            single_byte(2, 1); return;
        }
        const esp_err_t accepted = keyvalet_network_configure(&credentials);
        keyvalet_wifi_credentials_clear(&credentials);
        if (accepted != ESP_OK) single_byte(2, 1); /* Busy/unavailable: no queueing. */
        return;
    }
    if (command == KEYVALET_STAGE_ENROLLMENT_KEY_RPC) {
        /* Vendor-only RPC, one lowercase SEC1 public-key hex string. No
         * private key, trust persistence or enrollment completion over USB. */
        if (length != 133 || payload[2] != 130 || memchr(payload + 3, 0, 130)) {
            single_byte(2, 1); return;
        }
        char public_key[131], fingerprint[65], remaining[12];
        memcpy(public_key, payload + 3, 130); public_key[130] = 0;
        uint32_t seconds;
        if (keyvalet_security_stage_enrollment_key(public_key, fingerprint, &seconds) != ESP_OK) {
            single_byte(2, 1); return;
        }
        snprintf(remaining, sizeof(remaining), "%" PRIu32, seconds);
        const char *strings[] = {"STAGED", fingerprint, remaining};
        single_byte(2, 0); result(command, strings, 3); return;
    }
    if (length != 2) { single_byte(2, 1); return; }
    keyvalet_network_status_t status = {0};
    const uint8_t current = state(&status);
    single_byte(2, 0);
    switch (command) {
    case 2:
        single_byte(1, current);
        if (current == 4) connection_result(2, &status);
        break;
    case 3: {
        const char *strings[] = {"KeyValet", esp_app_get_description()->version,
            "ESP32-C6", keyvalet_network_device_id()};
        result(3, strings, 4); break;
    }
    case 4:
        if (keyvalet_network_scan() != ESP_OK) single_byte(2, 1);
        break;
    case 7: {
        const char *flags[] = {status.connected ? "3" : "2"};
        keyvalet_tls_status_t tls; keyvalet_tls_status_get(&tls);
        char url[KEYVALET_TLS_HOST_MAX + 32];
        snprintf(url, sizeof(url), "https://%s/api/v1/status", tls.hostname);
        const char *strings[] = {flags[0], url};
        result(7, strings, status.connected && tls.ready && strlen(url) <= 240 ? 2 : 1); break;
    }
    case KEYVALET_CURRENT_NETWORK_RPC: {
        const char *connection = current == 0 ? "unavailable" :
            current == 3 ? "provisioning" : current == 4 ? "connected" :
            status.configured ? "disconnected" : "unconfigured";
        const char *strings[] = {connection, current == 4 ? status.ssid : ""};
        result(command, strings, 2); break;
    }
    default: single_byte(2, 2); break;
    }
}

static void usb_task(void *context)
{
    (void)context;
    keyvalet_improv_parser_t parser = {0};
#ifdef CONFIG_KEYVALET_DIAGNOSTICS
    char line[96];
    size_t length = 0;
    bool line_start = true, discard_line = false;
#endif
    for (;;) {
        uint8_t byte;
        if (usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(100)) <= 0) {
            keyvalet_tls_usb_expire();
            if (parser.used && esp_timer_get_time() / 1000 - parser.last_byte_ms > 1000)
                memset(&parser, 0, sizeof(parser));
            continue;
        }
        const bool binary = parser.used || (byte == 'I'
#ifdef CONFIG_KEYVALET_DIAGNOSTICS
            && length == 0
#endif
        );
        const int parsed = binary ? keyvalet_improv_feed(&parser, byte, esp_timer_get_time() / 1000) : 0;
        if (parsed == 1) {
            handle_improv(parser.bytes);
            memset(&parser, 0, sizeof(parser)); /* Never retain credentials. */
        } else if (parsed < 0) single_byte(2, 1);
#ifdef CONFIG_KEYVALET_DIAGNOSTICS
        if (binary || parsed) { length = 0; discard_line = true; line_start = false; continue; }
        if (byte == '\r') continue;
        if (byte == '\n') {
            if (!discard_line && length) {
                line[length] = 0; keyvalet_diagnostic_console_handle(line);
            }
            length = 0; discard_line = false; line_start = true;
        } else if (line_start && byte != 'K') { discard_line = true; line_start = false; }
        else if (!discard_line) {
            line_start = false;
            if (byte < 32 || byte > 126 || length + 1 >= sizeof(line)) discard_line = true;
            else line[length++] = (char)byte;
        }
#else
        (void)binary;
#endif
    }
}

esp_err_t keyvalet_usb_provision_start(const keyvalet_hardware_profile_t *profile)
{
    if (!profile || s_profile) return ESP_ERR_INVALID_STATE;
#ifdef CONFIG_KEYVALET_DIAGNOSTICS
    ESP_RETURN_ON_ERROR(keyvalet_diagnostic_console_init(profile), TAG, "Diagnostic initialization failed");
#endif
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    config.tx_buffer_size = 1024; /* Accommodates a complete maximum-size frame. */
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&config), TAG, "USB driver unavailable");
    usb_serial_jtag_vfs_use_driver();
    s_profile = profile;
    keyvalet_network_set_provision_callback(network_result);
    if (xTaskCreate(usb_task, "usb_provision", 12288, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Improv Serial Wi-Fi provisioning ready; physical USB access required");
    return ESP_OK;
}
