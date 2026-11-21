#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "sensor_debounce.h"

int main(void)
{
    keyvalet_sensor_debounce_t sensors[4];
    for (size_t index = 0; index < 4; ++index)
        keyvalet_sensor_debounce_init(&sensors[index], 0, 1000);

    for (size_t index = 0; index < 4; ++index)
        assert(sensors[index].stable_level == 0);

    assert(!keyvalet_sensor_debounce_sample(&sensors[2], 1, 1010, 25));
    assert(!keyvalet_sensor_debounce_sample(&sensors[2], 1, 1034, 25));
    assert(keyvalet_sensor_debounce_sample(&sensors[2], 1, 1035, 25));
    assert(sensors[2].stable_level == 1);
    assert(sensors[0].stable_level == 0);
    assert(sensors[1].stable_level == 0);
    assert(sensors[3].stable_level == 0);

    assert(!keyvalet_sensor_debounce_sample(&sensors[2], 0, 1040, 25));
    assert(!keyvalet_sensor_debounce_sample(&sensors[2], 1, 1045, 25));
    assert(!keyvalet_sensor_debounce_sample(&sensors[2], 0, 1050, 25));
    assert(!keyvalet_sensor_debounce_sample(&sensors[2], 0, 1074, 25));
    assert(keyvalet_sensor_debounce_sample(&sensors[2], 0, 1075, 25));
    assert(sensors[2].stable_level == 0);

    puts("sensor debounce tests passed");
    return 0;
}
