#include "tls_material.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_random.h"
#include "mbedtls/asn1write.h"
#include "mbedtls/ecp.h"
#include "mbedtls/oid.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/x509_csr.h"

static int random_bytes(void *context, unsigned char *output, size_t length)
{
    (void)context;
    esp_fill_random(output, length);
    return 0;
}

bool keyvalet_tls_hostname_valid(const char *hostname)
{
    if (!hostname) return false;
    const size_t length = strnlen(hostname, KEYVALET_TLS_HOST_MAX + 1);
    if (!length || length > KEYVALET_TLS_HOST_MAX) return false;
    size_t label = 0;
    bool dot = false, alpha = false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = hostname[i];
        if (c == '.') {
            if (!label || hostname[i - 1] == '-') return false;
            label = 0; dot = true;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
            if ((!label && c == '-') || ++label > 63) return false;
            if (c >= 'a' && c <= 'z') alpha = true;
        }
    }
    return dot && alpha && label && hostname[length - 1] != '-' &&
        !(length >= 6 && !strcmp(hostname + length - 6, ".local"));
}

bool keyvalet_tls_origin_valid(const char *origin)
{
    if (!origin || strnlen(origin, KEYVALET_TLS_ORIGIN_MAX) >= KEYVALET_TLS_ORIGIN_MAX) return false;
    /* Explicitly provisioned localhost is a standard secure-context exception,
     * not an exception to device TLS validation or Origin admission. */
    if (!strcmp(origin, "http://localhost:8765") || !strcmp(origin, "http://127.0.0.1:8765")) return true;
    if (strncmp(origin, "https://", 8)) return false;
    const char *host = origin + 8;
    const char *colon = strchr(host, ':');
    size_t length = colon ? (size_t)(colon - host) : strlen(host);
    if (!length || length > KEYVALET_TLS_HOST_MAX) return false;
    char hostname[KEYVALET_TLS_HOST_MAX + 1];
    memcpy(hostname, host, length); hostname[length] = 0;
    if (!keyvalet_tls_hostname_valid(hostname) && strcmp(hostname, "localhost")) return false;
    if (colon) {
        unsigned port = 0;
        if (!colon[1] || colon[1] == '0') return false;
        for (const char *p = colon + 1; *p; ++p) {
            if (*p < '0' || *p > '9' || port > 6553) return false;
            port = port * 10 + (unsigned)(*p - '0');
        }
        if (!port || port > 65535 || port == 443) return false; /* canonical Origin */
    }
    return true;
}

static int parse_key(mbedtls_pk_context *pk, const uint8_t *key, size_t length)
{
    if (!key || !length || length > KEYVALET_TLS_KEY_MAX) return -1;
    int result = mbedtls_pk_parse_key(pk, key, length, NULL, 0, random_bytes, NULL);
    if (!result && (mbedtls_pk_get_type(pk) != MBEDTLS_PK_ECKEY || mbedtls_pk_get_bitlen(pk) != 256 ||
        mbedtls_pk_ec(*pk)->MBEDTLS_PRIVATE(grp).id != MBEDTLS_ECP_DP_SECP256R1)) result = -1;
    return result;
}

esp_err_t keyvalet_tls_key_generate(uint8_t key[KEYVALET_TLS_KEY_MAX], size_t *length)
{
    mbedtls_pk_context pk; mbedtls_pk_init(&pk);
    int result = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (!result) result = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk), random_bytes, NULL);
    if (!result) {
        result = mbedtls_pk_write_key_der(&pk, key, KEYVALET_TLS_KEY_MAX);
        if (result > 0) {
            *length = (size_t)result;
            memmove(key, key + KEYVALET_TLS_KEY_MAX - *length, *length);
            memset(key + *length, 0, KEYVALET_TLS_KEY_MAX - *length);
            result = 0;
        } else result = -1;
    }
    mbedtls_pk_free(&pk);
    return result ? ESP_FAIL : ESP_OK;
}

esp_err_t keyvalet_tls_public_material(const uint8_t *key, size_t length,
    const char *hostname, char *csr, size_t capacity, char public_key_hex[131])
{
    if (!keyvalet_tls_hostname_valid(hostname)) return ESP_ERR_INVALID_ARG;
    mbedtls_pk_context pk; mbedtls_pk_init(&pk);
    mbedtls_x509write_csr request; mbedtls_x509write_csr_init(&request);
    int result = parse_key(&pk, key, length);
    if (!result && public_key_hex) {
        uint8_t point[65]; size_t used;
        result = mbedtls_ecp_point_write_binary(&mbedtls_pk_ec(pk)->MBEDTLS_PRIVATE(grp),
            &mbedtls_pk_ec(pk)->MBEDTLS_PRIVATE(Q), MBEDTLS_ECP_PF_UNCOMPRESSED, &used, point, sizeof(point));
        if (!result && used == sizeof(point)) {
            static const char digits[] = "0123456789abcdef";
            for (size_t i = 0; i < used; ++i) { public_key_hex[i * 2] = digits[point[i] >> 4]; public_key_hex[i * 2 + 1] = digits[point[i] & 15]; }
            public_key_hex[130] = 0;
        } else result = -1;
    }
    if (!result && csr) {
        char subject[KEYVALET_TLS_HOST_MAX + 4]; snprintf(subject, sizeof(subject), "CN=%s", hostname);
        mbedtls_x509write_csr_set_md_alg(&request, MBEDTLS_MD_SHA256);
        mbedtls_x509write_csr_set_key(&request, &pk);
        result = mbedtls_x509write_csr_set_subject_name(&request, subject);
        unsigned char san[264], *p = san + sizeof(san);
        int used = mbedtls_asn1_write_raw_buffer(&p, san, (const unsigned char *)hostname, strlen(hostname));
        if (used >= 0) { int n = mbedtls_asn1_write_len(&p, san, (size_t)used); used = n < 0 ? n : used + n; }
        if (used >= 0) { int n = mbedtls_asn1_write_tag(&p, san, MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_X509_SAN_DNS_NAME); used = n < 0 ? n : used + n; }
        if (used >= 0) { int n = mbedtls_asn1_write_len(&p, san, (size_t)used); used = n < 0 ? n : used + n; }
        if (used >= 0) { int n = mbedtls_asn1_write_tag(&p, san, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE); used = n < 0 ? n : used + n; }
        if (!result && used >= 0) result = mbedtls_x509write_csr_set_extension(&request, MBEDTLS_OID_SUBJECT_ALT_NAME,
            MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME), 0, p, (size_t)used);
        else result = -1;
        if (!result) result = mbedtls_x509write_csr_pem(&request, (unsigned char *)csr, capacity, random_bytes, NULL);
    }
    mbedtls_x509write_csr_free(&request); mbedtls_pk_free(&pk);
    return result ? ESP_ERR_INVALID_ARG : ESP_OK;
}

/* Civil UTC conversion without local timezone/mktime or 32-bit time_t. */
static int64_t certificate_utc(const mbedtls_x509_time *t)
{
    int year = t->year - (t->mon <= 2);
    int era = year / 400, yoe = year - era * 400;
    int month = t->mon + (t->mon > 2 ? -3 : 9);
    int doy = (153 * month + 2) / 5 + t->day - 1;
    int days = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    return (int64_t)days * 86400 + t->hour * 3600 + t->min * 60 + t->sec;
}

static bool certificate_pem_only(const char *chain)
{
    static const char begin[] = "-----BEGIN CERTIFICATE-----";
    static const char end[] = "-----END CERTIFICATE-----";
    unsigned count = 0;
    const char *p = chain;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (!*p) return count > 0;
        if (++count > 4 || strncmp(p, begin, sizeof(begin) - 1)) return false;
        p += sizeof(begin) - 1;
        if (*p != '\r' && *p != '\n') return false;
        const char *last = strstr(p, end);
        if (!last) return false;
        for (; p < last; ++p)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                (*p >= '0' && *p <= '9') || *p == '+' || *p == '/' || *p == '=' || *p == '\r' || *p == '\n')) return false;
        p += sizeof(end) - 1;
    }
}

esp_err_t keyvalet_tls_material_validate(const uint8_t *key, size_t key_length,
    const char *chain, size_t chain_length, const char *hostname, int64_t now,
    int64_t *not_before, int64_t *not_after)
{
    if (!keyvalet_tls_hostname_valid(hostname) || !chain || !chain_length ||
        chain_length > KEYVALET_TLS_CHAIN_MAX || memchr(chain, 0, chain_length) || chain[chain_length] != 0 || !certificate_pem_only(chain))
        return ESP_ERR_INVALID_ARG;
    mbedtls_pk_context pk; mbedtls_pk_init(&pk);
    mbedtls_x509_crt crt; mbedtls_x509_crt_init(&crt);
    int result = parse_key(&pk, key, key_length);
    if (!result) result = mbedtls_x509_crt_parse(&crt, (const unsigned char *)chain, chain_length + 1);
    if (!result) result = mbedtls_pk_check_pair(&crt.pk, &pk, random_bytes, NULL);
    const mbedtls_x509_sequence *san = &crt.subject_alt_names;
    if (!result && (san->next || san->buf.tag != (MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_X509_SAN_DNS_NAME) ||
        san->buf.len != strlen(hostname) || memcmp(san->buf.p, hostname, san->buf.len) ||
        crt.MBEDTLS_PRIVATE(ca_istrue) || !crt.next ||
        mbedtls_x509_crt_check_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) ||
        mbedtls_x509_crt_check_extended_key_usage(&crt, MBEDTLS_OID_SERVER_AUTH, MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH)))) result = -1;
    int64_t start = certificate_utc(&crt.valid_from), end = certificate_utc(&crt.valid_to);
    mbedtls_x509_crt *last = &crt;
    unsigned count = 0;
    for (mbedtls_x509_crt *c = &crt; !result && c; c = c->next) {
        if (c != &crt && (!c->MBEDTLS_PRIVATE(ca_istrue) ||
            mbedtls_x509_crt_check_key_usage(c, MBEDTLS_X509_KU_KEY_CERT_SIGN))) result = -1;
        if (++count > 4 || (c->MBEDTLS_PRIVATE(sig_md) != MBEDTLS_MD_SHA256 && c->MBEDTLS_PRIVATE(sig_md) != MBEDTLS_MD_SHA384 && c->MBEDTLS_PRIVATE(sig_md) != MBEDTLS_MD_SHA512) ||
            (now > 0 && (now < certificate_utc(&c->valid_from) || now >= certificate_utc(&c->valid_to)))) result = -1;
        last = c;
        if (certificate_utc(&c->valid_from) > start) start = certificate_utc(&c->valid_from);
        if (certificate_utc(&c->valid_to) < end) end = certificate_utc(&c->valid_to);
    }
    if (!result) {
        uint32_t flags = 0;
        /* The supplied terminal CA checks chain consistency, not public trust.
         * The browser independently builds trust to its installed root store. */
        const int verified = mbedtls_x509_crt_verify(&crt, last, NULL, hostname, &flags, NULL, NULL);
        flags &= ~(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);
        if (flags || (verified && verified != MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)) result = -1;
    }
    mbedtls_x509_crt_free(&crt); mbedtls_pk_free(&pk);
    if (result || end <= start) return ESP_ERR_INVALID_ARG;
    if (not_before) *not_before = start;
    if (not_after) *not_after = end;
    return ESP_OK;
}
