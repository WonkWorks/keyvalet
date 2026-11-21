#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "hardware_profile.h"
#include "lock_arbiter.h"

/* Internal flash gate: excludes pulse start and waits for an ACTIVE pulse to
 * finish without holding the output mutex needed by its timer callback. */
esp_err_t keyvalet_lock_flash_begin(void);
void keyvalet_lock_flash_end(void);
/* Nonblocking, short transport transition. Rejects ACTIVE/RESERVED and inhibits
 * new pulses without holding the output mutex across server shutdown. */
esp_err_t keyvalet_lock_maintenance_begin(void);
void keyvalet_lock_maintenance_end(void);

typedef struct {
    keyvalet_lock_phase_t phase;
    keyvalet_lock_id_t selected;
    bool active[KEYVALET_LOCK_COUNT];
    uint32_t recovery_remaining_ms;
} keyvalet_lock_status_t;

esp_err_t keyvalet_hardware_io_init(const keyvalet_hardware_profile_t *profile);
esp_err_t keyvalet_status_led_set(const keyvalet_hardware_profile_t *profile, bool on);
esp_err_t keyvalet_status_led_set_automatic(
    const keyvalet_hardware_profile_t *profile, bool on);
esp_err_t keyvalet_status_led_toggle(const keyvalet_hardware_profile_t *profile, bool *is_on);
bool keyvalet_status_led_is_on(void);
esp_err_t keyvalet_solenoid_force_inactive(const keyvalet_hardware_profile_t *profile);
esp_err_t keyvalet_lock_pulse(const keyvalet_hardware_profile_t *profile,
                             keyvalet_lock_id_t lock);
esp_err_t keyvalet_lock_reserve(const keyvalet_hardware_profile_t *profile,
                               keyvalet_lock_id_t lock);
esp_err_t keyvalet_lock_pulse_reserved(
    const keyvalet_hardware_profile_t *profile, keyvalet_lock_id_t lock);
esp_err_t keyvalet_lock_cancel_reservation(
    const keyvalet_hardware_profile_t *profile, keyvalet_lock_id_t lock);
esp_err_t keyvalet_lock_status_get(keyvalet_lock_status_t *status);
esp_err_t keyvalet_sensor_configure(const keyvalet_hardware_profile_t *profile,
                                    keyvalet_sensor_id_t sensor);
