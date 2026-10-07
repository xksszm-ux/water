#pragma once
#include <stddef.h>
#include <stdint.h>
typedef void *osThreadId_t;
typedef int osPriority_t;
#define osPriorityNormal 24
typedef int osKernelState_t;
#define osKernelReady 1
osKernelState_t osKernelGetState(void);
typedef struct {
  const char *name;
  void *cb_mem;
  uint32_t cb_size;
  void *stack_mem;
  uint32_t stack_size;
  osPriority_t priority;
} osThreadAttr_t;
osThreadId_t osThreadNew(void (*entry)(void *), void *argument,
                       const osThreadAttr_t *attributes);
void osThreadExit(void);
