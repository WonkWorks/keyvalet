#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef void esp_netif_t;
typedef struct {esp_ip4_addr_t ip;} esp_netif_ip_info_t;
typedef struct { struct {esp_ip4_addr_t ip;} ip_info; } ip_event_got_ip_t;
#define IP_EVENT_STA_GOT_IP 1
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) ((ip)->addr & 255), (((ip)->addr >> 8) & 255), (((ip)->addr >> 16) & 255), (((ip)->addr >> 24) & 255)
esp_err_t esp_netif_init(void);
void *esp_netif_create_default_wifi_sta(void);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info);
