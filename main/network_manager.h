#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "wifi_store.h"

#define KEYVALET_DEVICE_ID_MAX 24
#define KEYVALET_IPV4_TEXT_MAX 16

typedef struct {
    bool connected;
    char ip_address[KEYVALET_IPV4_TEXT_MAX];
    bool rssi_valid;
    int8_t rssi;
    bool available;
    bool configured;
    bool provisioning;
    bool scanning;
    bool persistence_uncertain;
    char ssid[33];
} keyvalet_network_status_t;

#define KEYVALET_WIFI_SCAN_LIMIT 32U
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secured;
    char authentication[25];
} keyvalet_network_scan_result_t;

typedef enum {
    KEYVALET_NETWORK_PROVISIONING,
    KEYVALET_NETWORK_PROVISION_RESULT,
    KEYVALET_NETWORK_SCAN_RESULT,
    KEYVALET_NETWORK_SCAN_DONE,
} keyvalet_network_provision_event_t;

typedef enum {
    KEYVALET_NETWORK_OK,
    KEYVALET_NETWORK_CONNECT_FAILED,
    KEYVALET_NETWORK_TIMEOUT,
    KEYVALET_NETWORK_STORAGE_FAILED,
    KEYVALET_NETWORK_SCAN_FAILED,
    KEYVALET_NETWORK_UNAVAILABLE,
} keyvalet_network_error_t;

typedef void (*keyvalet_network_provision_callback_t)(
    keyvalet_network_provision_event_t event, keyvalet_network_error_t error,
    const keyvalet_network_scan_result_t *result);

typedef void (*keyvalet_network_connection_callback_t)(bool connected, void *context);

esp_err_t keyvalet_network_manager_start(
    keyvalet_network_connection_callback_t callback, void *context);
esp_err_t keyvalet_network_status_get(keyvalet_network_status_t *status);
esp_err_t keyvalet_network_last_ipv4_octet(uint8_t *octet);
const char *keyvalet_network_device_id(void);
void keyvalet_network_set_provision_callback(keyvalet_network_provision_callback_t callback);
esp_err_t keyvalet_network_scan(void);
esp_err_t keyvalet_network_configure(const keyvalet_wifi_credentials_t *credentials);
