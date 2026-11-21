#include <assert.h>
#include <stdio.h>

#include "lock_arbiter.h"

int main(void)
{
    keyvalet_lock_arbiter_t arbiter;
    keyvalet_lock_arbiter_init(&arbiter);
    assert(arbiter.phase == KEYVALET_LOCK_PHASE_READY);
    assert(keyvalet_lock_arbiter_reserve(&arbiter, KEYVALET_LOCK_2));
    assert(!keyvalet_lock_arbiter_request(&arbiter, KEYVALET_LOCK_1));
    assert(!keyvalet_lock_arbiter_start_reserved(&arbiter, KEYVALET_LOCK_1));
    assert(keyvalet_lock_arbiter_cancel_reservation(&arbiter, KEYVALET_LOCK_2));
    assert(arbiter.phase == KEYVALET_LOCK_PHASE_READY);
    assert(keyvalet_lock_arbiter_reserve(&arbiter, KEYVALET_LOCK_1));
    assert(keyvalet_lock_arbiter_start_reserved(&arbiter, KEYVALET_LOCK_1));
    keyvalet_lock_arbiter_pulse_complete(&arbiter, 0);
    assert(arbiter.phase == KEYVALET_LOCK_PHASE_READY);
    assert(keyvalet_lock_arbiter_request(&arbiter, KEYVALET_LOCK_1));
    assert(!keyvalet_lock_arbiter_request(&arbiter, KEYVALET_LOCK_2));
    assert(arbiter.selected == KEYVALET_LOCK_1);
    keyvalet_lock_arbiter_pulse_complete(&arbiter, 2000);
    assert(arbiter.phase == KEYVALET_LOCK_PHASE_RECOVERY);
    assert(!keyvalet_lock_arbiter_request(&arbiter, KEYVALET_LOCK_2));
    keyvalet_lock_arbiter_recovery_complete(&arbiter);
    assert(keyvalet_lock_arbiter_request(&arbiter, KEYVALET_LOCK_2));
    assert(arbiter.selected == KEYVALET_LOCK_2);
    keyvalet_lock_arbiter_fault(&arbiter);
    assert(!keyvalet_lock_arbiter_request(&arbiter, KEYVALET_LOCK_1));

    keyvalet_lock_arbiter_init(&arbiter);
    assert(keyvalet_lock_arbiter_request(&arbiter, KEYVALET_LOCK_1));
    keyvalet_lock_arbiter_pulse_complete(&arbiter, 0);
    assert(arbiter.phase == KEYVALET_LOCK_PHASE_READY);
    puts("lock arbiter tests passed");
    return 0;
}
