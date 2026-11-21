#include <assert.h>
#include <stdio.h>

#include "audio_sequence.h"

int main(void)
{
    keyvalet_audio_step_t steps[32];
    size_t count = keyvalet_ip_octet_sequence(105, steps, 32);
    assert(count == 13);
    assert(steps[0].frequency_hz == 1800 && steps[0].duration_ms == 180);
    assert(steps[1].frequency_hz == 0 && steps[1].duration_ms == 800);
    assert(steps[2].frequency_hz == 2600 && steps[2].duration_ms == 600);
    assert(steps[3].frequency_hz == 0 && steps[3].duration_ms == 800);
    for (size_t index = 4; index < count; ++index) {
        if ((index - 4U) % 2U == 0)
            assert(steps[index].frequency_hz == 1800 &&
                   steps[index].duration_ms == 180);
        else
            assert(steps[index].frequency_hz == 0 &&
                   steps[index].duration_ms == 180);
    }
    assert(keyvalet_ip_octet_sequence(255, steps, 1) == 0);
    assert(keyvalet_ip_octet_sequence(0, steps, 32) == 1);
    assert(steps[0].frequency_hz == 2600 && steps[0].duration_ms == 600);
    puts("audio sequence tests passed");
    return 0;
}
