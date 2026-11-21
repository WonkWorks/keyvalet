#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define ESP_ERR_WIFI_NOT_STARTED 100
#define WIFI_MODE_STA 1
#define WIFI_IF_STA 0
#define WIFI_STORAGE_RAM 0
#define WIFI_SCAN_TYPE_ACTIVE 0
#define WPA3_SAE_PWE_BOTH 3
enum {WIFI_EVENT_STA_START=1, WIFI_EVENT_STA_STOP, WIFI_EVENT_STA_DISCONNECTED, WIFI_EVENT_SCAN_DONE};
typedef enum {WIFI_AUTH_OPEN, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_WPA2_ENTERPRISE, WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK} wifi_auth_mode_t;
typedef struct {int dummy;} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() {0}
typedef struct {struct {uint8_t ssid[32], password[64]; struct {wifi_auth_mode_t authmode;} threshold;
    struct {bool capable;} pmf_cfg; int sae_pwe_h2e;} sta;} wifi_config_t;
typedef struct {uint8_t ssid[33]; int8_t rssi; wifi_auth_mode_t authmode;} wifi_ap_record_t;
typedef struct {bool show_hidden; int scan_type; union {struct {uint32_t min,max;} active;} scan_time;} wifi_scan_config_t;
typedef struct {uint32_t status;} wifi_event_sta_scan_done_t;
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *config);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *records);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *record);
