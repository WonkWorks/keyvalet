#include "settings_schema.h"

bool keyvalet_settings_values_valid(uint32_t schema_version,
                                    uint32_t default_door_open_timeout_s)
{
    return schema_version == KEYVALET_SETTINGS_SCHEMA_VERSION &&
           default_door_open_timeout_s >= KEYVALET_DOOR_OPEN_TIMEOUT_MIN_S &&
           default_door_open_timeout_s <= KEYVALET_DOOR_OPEN_TIMEOUT_MAX_S;
}

bool keyvalet_settings_revision_matches(uint32_t current_revision,
                                        uint32_t expected_revision)
{
    return expected_revision == current_revision && expected_revision != UINT32_MAX;
}
