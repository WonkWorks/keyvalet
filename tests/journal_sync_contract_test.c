#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "journal_sync_contract.h"

int main(void)
{
    keyvalet_journal_ack_payload_t payload = {
        .device_id = "keyvalet-001122334455",
        .highest_contiguous_sequence = 42,
        .operation_id = "operation-1",
        .nonce_hex = "0123456789abcdef",
        .issued_at = 100,
        .expires_at = 160,
    };
    char message[512];
    const int length = keyvalet_journal_ack_canonical(
        message, sizeof(message), &payload);
    assert(length > 0);
    assert(strcmp(message,
        "KEYVALET-JOURNAL-ACK-V1\nkeyvalet-001122334455\nJOURNAL_ACK\n"
        "42\noperation-1\n0123456789abcdef\n100\n160\n") == 0);
    assert(keyvalet_journal_ack_decide(10, 20, 11) ==
           KEYVALET_JOURNAL_ACK_ADVANCE);
    assert(keyvalet_journal_ack_decide(10, 20, 10) ==
           KEYVALET_JOURNAL_ACK_ALREADY_ACKNOWLEDGED);
    assert(keyvalet_journal_ack_decide(10, 20, 9) ==
           KEYVALET_JOURNAL_ACK_ALREADY_ACKNOWLEDGED);
    assert(keyvalet_journal_ack_decide(10, 20, 21) ==
           KEYVALET_JOURNAL_ACK_BEYOND_LATEST);
    keyvalet_journal_cursor_blob_t cursor;
    uint64_t recovered = 0;
    keyvalet_journal_cursor_encode(&cursor, 1234);
    assert(keyvalet_journal_cursor_decode(&cursor, &recovered));
    assert(recovered == 1234);
    cursor.crc32 ^= 1U;
    assert(!keyvalet_journal_cursor_decode(&cursor, &recovered));
    puts("journal sync contract tests passed");
    return 0;
}
