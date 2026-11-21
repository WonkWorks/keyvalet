#include <assert.h>
#include <stdio.h>
#include "settings_schema.h"

int main(void)
{
    assert(keyvalet_settings_values_valid(KEYVALET_SETTINGS_SCHEMA_VERSION, 90));
    assert(keyvalet_settings_values_valid(KEYVALET_SETTINGS_SCHEMA_VERSION, 5));
    assert(keyvalet_settings_values_valid(KEYVALET_SETTINGS_SCHEMA_VERSION, 3600));
    assert(!keyvalet_settings_values_valid(0, 90));
    assert(!keyvalet_settings_values_valid(KEYVALET_SETTINGS_SCHEMA_VERSION, 4));
    assert(!keyvalet_settings_values_valid(KEYVALET_SETTINGS_SCHEMA_VERSION, 3601));
    assert(keyvalet_settings_revision_matches(1, 1));
    assert(!keyvalet_settings_revision_matches(2, 1));
    assert(!keyvalet_settings_revision_matches(1, 2));
    assert(!keyvalet_settings_revision_matches(UINT32_MAX, UINT32_MAX));
    puts("settings schema tests passed");
    return 0;
}
