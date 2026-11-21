#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
typedef struct {unsigned tx_buffer_size;} usb_serial_jtag_driver_config_t;
#define USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT() {0}
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *config);
int usb_serial_jtag_read_bytes(void *data,size_t length,unsigned timeout);
int usb_serial_jtag_write_bytes(const void *data,size_t length,unsigned timeout);
bool usb_serial_jtag_is_connected(void);
