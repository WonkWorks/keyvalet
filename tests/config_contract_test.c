#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "config_contract.h"

int main(void)
{
    const keyvalet_config_v3_payload_t payload = {
        .device_id = "keyvalet-a1b2c3",
        .operation_id = "operation-7",
        .nonce_hex = "0011",
        .issued_at = 100,
        .expires_at = 160,
        .expected_revision = 9,
        .default_door_open_timeout_s = 120,
        .lock_sensor_required = {true, false},
        .sensor_monitoring_enabled = {true, false, true, false},
    };
    const char expected[] =
        "KEYVALET-CONFIG-V3\nkeyvalet-a1b2c3\nUPDATE_CONFIG\noperation-7\n"
        "0011\n100\n160\n9\n120\n2\nlock_1.sensor_required=1\nlock_2.sensor_required=0\n4\nsensor_1=1\nsensor_2=0\n"
        "sensor_3=1\nsensor_4=0\n";
    char output[512];
    const int length = keyvalet_config_v3_canonical(
        output, sizeof(output), &payload);
    assert(length == (int)strlen(expected));
    assert(strcmp(output, expected) == 0);
    assert(keyvalet_config_v3_canonical(output, 8, &payload) < 0);
    puts("config contract tests passed");
    return 0;
}
