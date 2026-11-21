#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "journal_format.h"

#define KEYVALET_HTTP_STATUS_PATH "/api/v1/status"
#define KEYVALET_WEBSOCKET_PATH "/api/v1/ws"
#define KEYVALET_WEBSOCKET_MAX_CLIENTS 4U

esp_err_t keyvalet_web_server_init(void);
void keyvalet_web_server_set_network_available(bool available);
void keyvalet_web_server_publish_record(
    const keyvalet_journal_record_t *record, void *context);
size_t keyvalet_web_server_client_count(void);
esp_err_t keyvalet_web_server_request_tls_activation(void);
bool keyvalet_web_server_tls_activation_pending(void);
const char *keyvalet_web_server_tls_activation_state(void);
