#pragma once
#include <stdint.h>
#include <stddef.h>
typedef void *osMessageQueueId_t;
typedef void *osMutexId_t;
typedef void *osThreadId_t;
typedef int osPriority_t;
#define osPriorityLow 16
#define osPriorityBelowNormal 20
#define osPriorityNormal 24
#define osPriorityAboveNormal 28
typedef struct {
  const char *name;
  void *cb_mem;
  uint32_t cb_size;
  void *mq_mem;
  uint32_t mq_size;
} osMessageQueueAttr_t;
typedef struct {
  const char *name;
  void *cb_mem;
  uint32_t cb_size;
} osMutexAttr_t;
typedef struct {
  const char *name;
  void *cb_mem;
  uint32_t cb_size;
  void *stack_mem;
  uint32_t stack_size;
  osPriority_t priority;
} osThreadAttr_t;
osMessageQueueId_t osMessageQueueNew(uint32_t count, uint32_t size,
                                    const osMessageQueueAttr_t *attributes);
osMutexId_t osMutexNew(const osMutexAttr_t *attributes);
osThreadId_t osThreadNew(void (*entry)(void *), void *argument,
                       const osThreadAttr_t *attributes);
uint32_t osKernelGetTickCount(void);
