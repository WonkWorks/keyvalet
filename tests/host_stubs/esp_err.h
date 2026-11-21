#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_ERR_NVS_NOT_FOUND 5
#define ESP_ERR_INVALID_CRC 6
#define ESP_ERR_INVALID_SIZE 7
#define ESP_ERR_NOT_SUPPORTED 8
#define ESP_ERR_TIMEOUT 9
#define ESP_ERR_NVS_INVALID_LENGTH 10
#define ESP_ERR_NOT_ALLOWED 11
#define ESP_ERR_INVALID_RESPONSE 12
static inline const char *esp_err_to_name(esp_err_t error)
{ (void)error; return "host-error"; }
