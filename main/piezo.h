#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "hardware_profile.h"

typedef enum {
    KEYVALET_PIEZO_NONE,
    KEYVALET_PIEZO_STARTUP,
    KEYVALET_PIEZO_DIAGNOSTIC,
    KEYVALET_PIEZO_REMINDER_ALARM,
    KEYVALET_PIEZO_HOLD_INDICATION,
    KEYVALET_PIEZO_WIFI_CONNECTED,
    KEYVALET_PIEZO_IP_ANNOUNCEMENT,
    KEYVALET_PIEZO_PAIRING,
    KEYVALET_PIEZO_URGENT_ALARM,
} keyvalet_piezo_activity_t;

typedef void (*keyvalet_piezo_completion_t)(keyvalet_piezo_activity_t activity,
                                             void *context);

esp_err_t keyvalet_piezo_init(const keyvalet_hardware_profile_t *profile);
esp_err_t keyvalet_piezo_play_startup(keyvalet_piezo_activity_t activity,
                                     keyvalet_piezo_completion_t completion,
                                     void *context);
esp_err_t keyvalet_piezo_play_alarm_chirp(void);
esp_err_t keyvalet_piezo_play_hold_indication(void);
esp_err_t keyvalet_piezo_play_wifi_connected(void);
esp_err_t keyvalet_piezo_play_ip_octet(uint8_t octet);
esp_err_t keyvalet_piezo_play_pairing_cue(void);
esp_err_t keyvalet_piezo_play_urgent_alarm(void);
esp_err_t keyvalet_piezo_preempt_for_alarm(void);
esp_err_t keyvalet_piezo_stop(keyvalet_piezo_activity_t activity);
bool keyvalet_piezo_is_active(void);
keyvalet_piezo_activity_t keyvalet_piezo_activity(void);
