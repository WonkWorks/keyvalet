#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware_channels.h"

typedef enum {
    KEYVALET_LOCK_PHASE_READY,
    KEYVALET_LOCK_PHASE_RESERVED,
    KEYVALET_LOCK_PHASE_ACTIVE,
    KEYVALET_LOCK_PHASE_RECOVERY,
    KEYVALET_LOCK_PHASE_FAULT,
} keyvalet_lock_phase_t;

typedef struct {
    keyvalet_lock_phase_t phase;
    keyvalet_lock_id_t selected;
} keyvalet_lock_arbiter_t;

void keyvalet_lock_arbiter_init(keyvalet_lock_arbiter_t *arbiter);
bool keyvalet_lock_arbiter_request(keyvalet_lock_arbiter_t *arbiter,
                                  keyvalet_lock_id_t lock);
bool keyvalet_lock_arbiter_reserve(keyvalet_lock_arbiter_t *arbiter,
                                  keyvalet_lock_id_t lock);
bool keyvalet_lock_arbiter_start_reserved(keyvalet_lock_arbiter_t *arbiter,
                                         keyvalet_lock_id_t lock);
bool keyvalet_lock_arbiter_cancel_reservation(keyvalet_lock_arbiter_t *arbiter,
                                             keyvalet_lock_id_t lock);
void keyvalet_lock_arbiter_pulse_complete(keyvalet_lock_arbiter_t *arbiter,
                                         uint32_t recovery_ms);
void keyvalet_lock_arbiter_recovery_complete(keyvalet_lock_arbiter_t *arbiter);
void keyvalet_lock_arbiter_fault(keyvalet_lock_arbiter_t *arbiter);
const char *keyvalet_lock_phase_name(keyvalet_lock_phase_t phase);
