#pragma once
#include <stddef.h>
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned length, size_t item_size);
int xQueueSend(QueueHandle_t queue, const void *item, unsigned timeout);
int xQueueReceive(QueueHandle_t queue, void *item, unsigned timeout);
