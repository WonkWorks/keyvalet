#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KEYVALET_SETTINGS_SCHEMA_VERSION 4U
#define KEYVALET_DOOR_OPEN_TIMEOUT_MIN_S 5U
#define KEYVALET_DOOR_OPEN_TIMEOUT_MAX_S 3600U

bool keyvalet_settings_values_valid(uint32_t schema_version,
                                    uint32_t default_door_open_timeout_s);
bool keyvalet_settings_revision_matches(uint32_t current_revision,
                                        uint32_t expected_revision);
