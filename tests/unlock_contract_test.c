#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "unlock_contract.h"

int main(void)
{
    keyvalet_lock_id_t lock;
    assert(keyvalet_lock_id_parse("lock_1", &lock) && lock == KEYVALET_LOCK_1);
    assert(keyvalet_lock_id_parse("lock_2", &lock) && lock == KEYVALET_LOCK_2);
    assert(!keyvalet_lock_id_parse("courtesy", &lock));
    assert(!keyvalet_lock_id_parse("LOCK_1", &lock));
    assert(keyvalet_unlock_timeout_valid(false, 0));
    assert(keyvalet_unlock_timeout_valid(true, 0));
    for (unsigned timeout = 1; timeout <= 4; ++timeout)
        assert(!keyvalet_unlock_timeout_valid(true, timeout));
    assert(!keyvalet_unlock_timeout_valid(true, 4));
    assert(keyvalet_unlock_timeout_valid(true, 5));
    assert(keyvalet_unlock_timeout_valid(true, 3600));
    assert(!keyvalet_unlock_timeout_valid(true, 3601));

    keyvalet_unlock_authorization_payload_t payload = {
        .device_id = "keyvalet-001122334455",
        .lock = KEYVALET_LOCK_2,
        .operation_id = "operation-1",
        .nonce_hex = "0123456789abcdef",
        .issued_at = 100,
        .expires_at = 160,
    };
    char message[512];
    int length = keyvalet_unlock_v2_canonical(message, sizeof(message), &payload);
    assert(length > 0);
    assert(strcmp(message,
        "KEYVALET-UNLOCK-V2\nkeyvalet-001122334455\nUNLOCK\nlock_2\nDEFAULT\n"
        "operation-1\n0123456789abcdef\n100\n160\n") == 0);
    payload.timeout_present = true;
    payload.door_open_timeout_s = 0;
    length = keyvalet_unlock_v2_canonical(message, sizeof(message), &payload);
    assert(length > 0 && strstr(message, "\nlock_2\n0\noperation-1\n") != NULL);
    assert(strstr(message, "DEFAULT") == NULL);
    payload.door_open_timeout_s = 240;
    length = keyvalet_unlock_v2_canonical(message, sizeof(message), &payload);
    assert(length > 0);
    assert(strstr(message, "\nlock_2\n240\noperation-1\n") != NULL);
    puts("unlock contract tests passed");
    return 0;
}
