#pragma once

#define KEYVALET_LOCK_COUNT 2U
#define KEYVALET_SENSOR_COUNT 4U

typedef enum {
    KEYVALET_LOCK_1 = 0,
    KEYVALET_LOCK_2 = 1,
} keyvalet_lock_id_t;

typedef enum {
    KEYVALET_SENSOR_1 = 0,
    KEYVALET_SENSOR_2 = 1,
    KEYVALET_SENSOR_3 = 2,
    KEYVALET_SENSOR_4 = 3,
} keyvalet_sensor_id_t;
