#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEYVALET_TLS_USB_RPC 0x82
/* Vendor operation, carried by the existing receiver. Results use the existing
 * Improv result framing; standard Improv commands are unchanged. */
typedef void (*keyvalet_tls_usb_reply_t)(const char *const *strings, size_t count);
bool keyvalet_tls_usb_handle(const uint8_t *body, size_t length, keyvalet_tls_usb_reply_t reply);
void keyvalet_tls_usb_expire(void);
