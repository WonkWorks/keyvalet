#include "lock_arbiter.h"

#include <stddef.h>

void keyvalet_lock_arbiter_init(keyvalet_lock_arbiter_t *arbiter)
{
    if (arbiter == NULL) return;
    arbiter->phase = KEYVALET_LOCK_PHASE_READY;
    arbiter->selected = KEYVALET_LOCK_1;
}

bool keyvalet_lock_arbiter_request(keyvalet_lock_arbiter_t *arbiter,
                                  keyvalet_lock_id_t lock)
{
    if (arbiter == NULL || arbiter->phase != KEYVALET_LOCK_PHASE_READY ||
        lock >= KEYVALET_LOCK_COUNT) return false;
    arbiter->selected = lock;
    arbiter->phase = KEYVALET_LOCK_PHASE_ACTIVE;
    return true;
}

bool keyvalet_lock_arbiter_reserve(keyvalet_lock_arbiter_t *arbiter,
                                  keyvalet_lock_id_t lock)
{
    if (arbiter == NULL || arbiter->phase != KEYVALET_LOCK_PHASE_READY ||
        lock >= KEYVALET_LOCK_COUNT) return false;
    arbiter->selected = lock;
    arbiter->phase = KEYVALET_LOCK_PHASE_RESERVED;
    return true;
}

bool keyvalet_lock_arbiter_start_reserved(keyvalet_lock_arbiter_t *arbiter,
                                         keyvalet_lock_id_t lock)
{
    if (arbiter == NULL || arbiter->phase != KEYVALET_LOCK_PHASE_RESERVED ||
        arbiter->selected != lock) return false;
    arbiter->phase = KEYVALET_LOCK_PHASE_ACTIVE;
    return true;
}

bool keyvalet_lock_arbiter_cancel_reservation(keyvalet_lock_arbiter_t *arbiter,
                                             keyvalet_lock_id_t lock)
{
    if (arbiter == NULL || arbiter->phase != KEYVALET_LOCK_PHASE_RESERVED ||
        arbiter->selected != lock) return false;
    arbiter->phase = KEYVALET_LOCK_PHASE_READY;
    return true;
}

void keyvalet_lock_arbiter_pulse_complete(keyvalet_lock_arbiter_t *arbiter,
                                         uint32_t recovery_ms)
{
    if (arbiter == NULL || arbiter->phase != KEYVALET_LOCK_PHASE_ACTIVE) return;
    arbiter->phase = recovery_ms == 0 ? KEYVALET_LOCK_PHASE_READY
                                     : KEYVALET_LOCK_PHASE_RECOVERY;
}

void keyvalet_lock_arbiter_recovery_complete(keyvalet_lock_arbiter_t *arbiter)
{
    if (arbiter != NULL && arbiter->phase == KEYVALET_LOCK_PHASE_RECOVERY)
        arbiter->phase = KEYVALET_LOCK_PHASE_READY;
}

void keyvalet_lock_arbiter_fault(keyvalet_lock_arbiter_t *arbiter)
{
    if (arbiter != NULL) arbiter->phase = KEYVALET_LOCK_PHASE_FAULT;
}

const char *keyvalet_lock_phase_name(keyvalet_lock_phase_t phase)
{
    switch (phase) {
    case KEYVALET_LOCK_PHASE_READY: return "READY";
    case KEYVALET_LOCK_PHASE_RESERVED: return "RESERVED";
    case KEYVALET_LOCK_PHASE_ACTIVE: return "ACTIVE";
    case KEYVALET_LOCK_PHASE_RECOVERY: return "RECOVERY";
    case KEYVALET_LOCK_PHASE_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}
