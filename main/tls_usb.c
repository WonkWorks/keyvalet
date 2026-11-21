#include "tls_usb.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "network_manager.h"
#include "time_sync.h"
#include "tls_identity.h"
#include "web_server.h"

static uint8_t *s_transfer;
static size_t s_total, s_used;
static unsigned s_kind;
static uint8_t s_digest[32];
static int64_t s_deadline;
static char s_export[2048];
static unsigned s_export_kind;

static void clear_transfer(void)
{
    free(s_transfer); s_transfer = NULL; s_total = s_used = s_kind = 0;
}

void keyvalet_tls_usb_expire(void)
{
    if (s_transfer && esp_timer_get_time() >= s_deadline) clear_transfer();
}

static esp_err_t configure_candidate(const char *json)
{
    /* Commissioning names/Origins are canonical ASCII. Escaped NULs must not
     * turn a longer host-supplied value into a silently accepted prefix. */
    if (strchr(json, '\\')) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    cJSON *host = cJSON_GetObjectItemCaseSensitive(root, "hostname");
    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "origins");
    cJSON *rotate = cJSON_GetObjectItemCaseSensitive(root, "rotate_key");
    esp_err_t result = ESP_ERR_INVALID_ARG;
    char origins[KEYVALET_TLS_ORIGIN_COUNT][KEYVALET_TLS_ORIGIN_MAX] = {0};
    const int count = cJSON_GetArraySize(list);
    if (cJSON_IsObject(root) && cJSON_GetArraySize(root) == 3 && cJSON_IsString(host) &&
        cJSON_IsArray(list) && count >= 1 && count <= KEYVALET_TLS_ORIGIN_COUNT && cJSON_IsBool(rotate)) {
        bool valid = true;
        for (int i = 0; i < count; ++i) {
            cJSON *origin = cJSON_GetArrayItem(list, i);
            if (!cJSON_IsString(origin) || !keyvalet_tls_origin_valid(origin->valuestring)) { valid = false; break; }
            strcpy(origins[i], origin->valuestring);
            for (int j = 0; j < i; ++j) if (!strcmp(origins[j], origins[i])) valid = false;
        }
        if (valid) result = keyvalet_tls_prepare(host->valuestring, origins, cJSON_IsTrue(rotate));
    }
    cJSON_Delete(root); return result;
}

static bool export_object(unsigned kind)
{
    memset(s_export, 0, sizeof(s_export)); s_export_kind = 0;
    esp_err_t result;
    if (kind == 1 || kind == 4) result = keyvalet_tls_export(kind == 1, s_export, sizeof(s_export), NULL);
    else if (kind == 2 || kind == 5) result = keyvalet_tls_export(kind == 2, NULL, 0, s_export);
    else if (kind == 3) {
        keyvalet_tls_status_t status; keyvalet_tls_status_get(&status);
        cJSON *root = cJSON_CreateObject(), *origins = cJSON_CreateArray();
        if (!root || !origins) { cJSON_Delete(root); cJSON_Delete(origins); return false; }
        cJSON_AddStringToObject(root, "device_id", keyvalet_network_device_id());
        cJSON_AddStringToObject(root, "device_uid", keyvalet_tls_device_uid());
        cJSON_AddStringToObject(root, "state", status.state);
        cJSON_AddStringToObject(root, "hostname", status.hostname);
        cJSON_AddNumberToObject(root, "generation", status.generation);
        cJSON_AddBoolToObject(root, "candidate", status.candidate);
        cJSON_AddBoolToObject(root, "ready", status.ready);
        cJSON_AddNumberToObject(root, "not_after", (double)status.not_after);
        cJSON_AddStringToObject(root, "activation", keyvalet_web_server_tls_activation_state());
        cJSON_AddStringToObject(root, "maintenance", "NOT_IMPLEMENTED");
        cJSON_AddNumberToObject(root, "usb_stack_free_bytes", uxTaskGetStackHighWaterMark(NULL));
        cJSON_AddItemToObject(root, "origins", origins);
        for (unsigned i = 0; i < KEYVALET_TLS_ORIGIN_COUNT; ++i)
            if (status.origins[i][0]) cJSON_AddItemToArray(origins, cJSON_CreateString(status.origins[i]));
        result = cJSON_PrintPreallocated(root, s_export, sizeof(s_export), false) ? ESP_OK : ESP_ERR_NO_MEM;
        cJSON_Delete(root);
    } else return false;
    if (result == ESP_OK) s_export_kind = kind;
    return result == ESP_OK;
}

bool keyvalet_tls_usb_handle(const uint8_t *body, size_t length, keyvalet_tls_usb_reply_t reply)
{
    keyvalet_tls_usb_expire();
    if (!body || !length) return false;
    esp_err_t result = ESP_ERR_INVALID_ARG;
    const unsigned action = body[0];
    if (action != 0 && action != 4 && keyvalet_web_server_tls_activation_pending()) return false;
    if (action == 0 && length == 1) {
        keyvalet_tls_status_t status; keyvalet_tls_status_get(&status);
        const char *strings[] = {status.state, status.candidate ? "CANDIDATE" : "NO_CANDIDATE",
            keyvalet_web_server_tls_activation_state()};
        reply(strings, 3); return true;
    }
    if (action == 1 && length == 36) {
        clear_transfer(); s_export_kind = 0;
        const size_t total = body[2] | ((size_t)body[3] << 8);
        const unsigned kind = body[1];
        if (!total || (kind != 1 && kind != 2) || total > (kind == 1 ? 1024 : KEYVALET_TLS_CHAIN_MAX)) return false;
        s_transfer = calloc(1, total + 1);
        if (!s_transfer) return false;
        s_total = total; s_kind = kind; memcpy(s_digest, body + 4, 32);
        s_deadline = esp_timer_get_time() + 30000000;
        result = ESP_OK;
    } else if (action == 2 && length > 3 && length <= 223 && s_transfer) {
        const size_t offset = body[1] | ((size_t)body[2] << 8), chunk = length - 3;
        if (offset != s_used || chunk > s_total - s_used) { clear_transfer(); return false; }
        memcpy(s_transfer + s_used, body + 3, chunk); s_used += chunk;
        /* Fixed total deadline, not attacker-extendable by individual chunks. */
        result = ESP_OK;
    } else if (action == 3 && length == 1 && s_transfer) {
        uint8_t digest[32]; mbedtls_sha256(s_transfer, s_used, digest, 0);
        if (s_used == s_total && !memcmp(digest, s_digest, 32) && !memchr(s_transfer, 0, s_used))
            result = s_kind == 1 ? configure_candidate((const char *)s_transfer) :
                keyvalet_tls_install((const char *)s_transfer, s_used);
        clear_transfer(); s_export_kind = 0;
    } else if (action == 4 && length == 4) {
        const unsigned kind = body[1];
        const size_t offset = body[2] | ((size_t)body[3] << 8);
        if ((!offset && !export_object(kind)) || s_export_kind != kind) return false;
        const size_t total = strlen(s_export);
        if (offset > total) return false;
        size_t chunk = total - offset; if (chunk > 100) chunk = 100;
        char hex[201], total_text[8], offset_text[8];
        static const char digits[] = "0123456789abcdef";
        for (size_t i = 0; i < chunk; ++i) { const uint8_t c = s_export[offset + i]; hex[i * 2] = digits[c >> 4]; hex[i * 2 + 1] = digits[c & 15]; }
        hex[chunk * 2] = 0;
        snprintf(total_text, sizeof(total_text), "%u", (unsigned)total);
        snprintf(offset_text, sizeof(offset_text), "%u", (unsigned)offset);
        const char *strings[] = {total_text, offset_text, hex}; reply(strings, 3); return true;
    } else if (action == 5 && length == 1) result = keyvalet_web_server_request_tls_activation();
    else if (action == 6 && length == 1) { clear_transfer(); s_export_kind = 0; result = ESP_OK; }
    else if (action == 7 && length == 1) { clear_transfer(); s_export_kind = 0; result = keyvalet_tls_discard_candidate(); }
    else if (action == 8 && length == 9) {
        uint64_t utc = 0; for (unsigned i = 0; i < 8; ++i) utc |= (uint64_t)body[i + 1] << (i * 8);
        if (utc <= INT64_MAX) result = keyvalet_time_set_usb_utc((int64_t)utc);
    }
    if (result != ESP_OK) return false;
    const char *strings[] = {action == 5 ? "PENDING" : "OK"}; reply(strings, 1); return true;
}
