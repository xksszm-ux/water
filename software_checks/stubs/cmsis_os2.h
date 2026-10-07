#pragma once
#include <stdint.h>
typedef void *osMessageQueueId_t;
typedef void *osMutexId_t;
typedef void *osThreadId_t;
typedef int osStatus_t;
#define osOK 0
uint32_t osKernelGetTickCount(void);
osStatus_t osMessageQueueGet(osMessageQueueId_t, void *, uint8_t *, uint32_t);
osStatus_t osDelayUntil(uint32_t);
#include <stddef.h>
#define osFlagsWaitAny 0
uint32_t osThreadFlagsClear(uint32_t);
uint32_t osThreadFlagsWait(uint32_t,uint32_t,uint32_t);
osThreadId_t osThreadGetId(void);
