#pragma once
void vTaskDelay(unsigned ticks);
unsigned uxTaskGetStackHighWaterMark(void *task);
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                void *argument, unsigned priority, void *handle);
