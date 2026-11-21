#include "wifi_store.h"

#include <string.h>
#include "esp_crc.h"
#include "hardware_io.h"
#include "nvs.h"

static nvs_handle_t s_nvs;
static bool s_writable;

/* Printable UTF-8; reject controls, overlong encodings and surrogate scalars.
 * SSIDs that cannot be represented safely in the standard UTF-8 UI are skipped. */
bool keyvalet_wifi_text_valid(const uint8_t *text, size_t length)
{
    for (size_t i = 0; i < length;) {
        uint32_t value = text[i++];
        if (value < 0x80) {
            if (value < 0x20 || value == 0x7f) return false;
            continue;
        }
        unsigned tail;
        uint32_t minimum;
        if (value >= 0xc2 && value <= 0xdf) { tail = 1; minimum = 0x80; value &= 31; }
        else if (value >= 0xe0 && value <= 0xef) { tail = 2; minimum = 0x800; value &= 15; }
        else if (value >= 0xf0 && value <= 0xf4) { tail = 3; minimum = 0x10000; value &= 7; }
        else return false;
        if (i + tail > length) return false;
        while (tail--) {
            const uint8_t byte = text[i++];
            if ((byte & 0xc0) != 0x80) return false;
            value = (value << 6) | (byte & 63);
        }
        if (value < minimum || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff) ||
            (value >= 0x80 && value <= 0x9f)) return false;
    }
    return true;
}

bool keyvalet_wifi_credentials_valid(const keyvalet_wifi_credentials_t *credentials)
{
    if (!credentials) return false;
    const size_t ssid = strnlen(credentials->ssid, sizeof(credentials->ssid));
    const size_t password = strnlen(credentials->password, sizeof(credentials->password));
    if (ssid == 0 || ssid > 32 || password > 64 ||
        !keyvalet_wifi_text_valid((const uint8_t *)credentials->ssid, ssid) ||
        !keyvalet_wifi_text_valid((const uint8_t *)credentials->password, password)) return false;
    if (password == 0) return true; /* Open network, explicitly selected over USB. */
    if (password < 8) return false;
    if (password == 64) {
        for (size_t i = 0; i < password; ++i) {
            const char c = credentials->password[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F'))) return false;
        }
    }
    return true;
}

void keyvalet_wifi_credentials_clear(keyvalet_wifi_credentials_t *credentials)
{
    volatile uint8_t *bytes = (volatile uint8_t *)credentials;
    for (size_t i = 0; i < sizeof(*credentials); ++i) bytes[i] = 0;
}

esp_err_t keyvalet_wifi_store_load(keyvalet_wifi_credentials_t *credentials)
{
    uint8_t raw[112] = {0};
    keyvalet_wifi_credentials_clear(credentials);
    s_writable = false;
    esp_err_t result = nvs_open("keyvalet_wifi", NVS_READWRITE, &s_nvs);
    if (result != ESP_OK) return result;
    s_writable = true;
    size_t length = sizeof(raw);
    result = nvs_get_blob(s_nvs, "station", raw, &length);
    if (result == ESP_OK) {
        const uint8_t header[] = {'K', 'V', 'W', 'F', 1, 0, 112, 0};
        uint32_t crc;
        memcpy(&crc, raw + 108, sizeof(crc));
        if (length != sizeof(raw) || memcmp(raw, header, sizeof(header)) ||
            raw[106] || raw[107] || crc != esp_crc32_le(0, raw, 108)) result = ESP_ERR_INVALID_CRC;
        else {
            memcpy(credentials->ssid, raw + 8, 33);
            memcpy(credentials->password, raw + 41, 65);
            if (!keyvalet_wifi_credentials_valid(credentials)) result = ESP_ERR_INVALID_CRC;
        }
    }
    volatile uint8_t *secret = raw;
    for (size_t i = 0; i < sizeof(raw); ++i) secret[i] = 0;
    if (result != ESP_OK) keyvalet_wifi_credentials_clear(credentials);
    return result;
}

esp_err_t keyvalet_wifi_store_save(const keyvalet_wifi_credentials_t *credentials)
{
    if (!s_writable) return ESP_ERR_INVALID_STATE;
    if (!keyvalet_wifi_credentials_valid(credentials)) return ESP_ERR_INVALID_ARG;
    uint8_t raw[112] = {'K', 'V', 'W', 'F', 1, 0, 112, 0};
    memcpy(raw + 8, credentials->ssid, strlen(credentials->ssid));
    memcpy(raw + 41, credentials->password, strlen(credentials->password));
    const uint32_t crc = esp_crc32_le(0, raw, 108);
    memcpy(raw + 108, &crc, sizeof(crc));
    esp_err_t result = keyvalet_lock_flash_begin();
    if (result == ESP_OK) {
        result = nvs_set_blob(s_nvs, "station", raw, sizeof(raw));
        if (result == ESP_OK) result = nvs_commit(s_nvs);
        keyvalet_lock_flash_end();
        /* An error may follow a durable commit. Never overwrite uncertain state
         * from an older RAM copy; reboot reloads the actual durable credentials. */
        if (result != ESP_OK) s_writable = false;
    }
    volatile uint8_t *secret = raw;
    for (size_t i = 0; i < sizeof(raw); ++i) secret[i] = 0;
    return result;
}
