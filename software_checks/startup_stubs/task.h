#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
uint32_t xPortGetFreeHeapSize(void);
uint32_t xPortGetMinimumEverFreeHeapSize(void);
