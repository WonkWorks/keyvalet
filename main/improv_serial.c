#include "improv_serial.h"

#include <string.h>

int keyvalet_improv_feed(keyvalet_improv_parser_t *parser, uint8_t byte, int64_t now_ms)
{
    static const uint8_t header[] = "IMPROV";
    const bool expired = parser->used && now_ms - parser->last_byte_ms > 1000;
    if (expired) memset(parser, 0, sizeof(*parser));
    parser->last_byte_ms = now_ms;
    if (parser->used < 6) {
        if (byte != header[parser->used]) {
            memset(parser, 0, sizeof(*parser));
            if (byte == 'I') { parser->bytes[0] = byte; parser->used = 1; parser->last_byte_ms = now_ms; }
            return expired ? -1 : 0;
        }
    } else if (parser->used == 6 && byte != 1) {
        memset(parser, 0, sizeof(*parser));
        return -1;
    }
    parser->bytes[parser->used++] = byte;
    /* RPC 0x82 carries binary TLS lengths/hashes, which can legitimately
     * contain the sync sequence. Keep UTF-8 Improv recovery unchanged, but
     * consume binary vendor frames by their declared bounded length. */
    const bool binary_tls = parser->used >= 10 && parser->bytes[7] == 3 && parser->bytes[9] == 0x82;
    if (!binary_tls && parser->used >= 14 && !memcmp(parser->bytes + parser->used - 7, "IMPROV\x01", 7)) {
        memset(parser->bytes, 0, parser->used - 7);
        memcpy(parser->bytes, "IMPROV\x01", 7);
        parser->used = 7;
        return -1;
    }
    if (parser->used >= 10 && parser->used == (size_t)parser->bytes[8] + 10U) {
        uint8_t checksum = 0;
        for (size_t i = 0; i < parser->used - 1; ++i) checksum += parser->bytes[i];
        parser->used = 0;
        if (checksum != byte) { memset(parser, 0, sizeof(*parser)); return -1; }
        return 1;
    }
    return expired ? -1 : 0;
}

bool keyvalet_improv_credentials(const uint8_t *payload, size_t length,
                                 keyvalet_wifi_credentials_t *credentials)
{
    keyvalet_wifi_credentials_clear(credentials);
    if (length < 4 || payload[0] != 1 || payload[1] != length - 2) return false;
    const size_t ssid = payload[2];
    if (ssid == 0 || ssid > 32 || 3 + ssid >= length) return false;
    const size_t password = payload[3 + ssid];
    if (password > 64 || 4 + ssid + password != length) return false;
    memcpy(credentials->ssid, payload + 3, ssid);
    memcpy(credentials->password, payload + 4 + ssid, password);
    if (strlen(credentials->ssid) != ssid || strlen(credentials->password) != password ||
        !keyvalet_wifi_credentials_valid(credentials)) {
        keyvalet_wifi_credentials_clear(credentials);
        return false;
    }
    return true;
}

size_t keyvalet_improv_frame(uint8_t *output, uint8_t type,
                              const uint8_t *payload, size_t length)
{
    if (length > 255) return 0;
    memcpy(output, "IMPROV", 6);
    output[6] = 1; output[7] = type; output[8] = (uint8_t)length;
    if (length) memcpy(output + 9, payload, length);
    uint8_t checksum = 0;
    for (size_t i = 0; i < length + 9; ++i) checksum += output[i];
    output[length + 9] = checksum;
    return length + 10;
}

size_t keyvalet_improv_result(uint8_t *output, uint8_t command,
                               const char *const *strings, size_t count)
{
    uint8_t payload[255] = {command, 0};
    size_t used = 2;
    for (size_t i = 0; i < count; ++i) {
        const size_t length = strlen(strings[i]);
        if (length > 255 || used + 1 + length > sizeof(payload)) return 0;
        payload[used++] = (uint8_t)length;
        memcpy(payload + used, strings[i], length); used += length;
    }
    payload[1] = (uint8_t)(used - 2);
    return keyvalet_improv_frame(output, 4, payload, used);
}
