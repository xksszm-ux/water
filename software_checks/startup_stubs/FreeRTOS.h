#pragma once
#include <stdint.h>
typedef uint32_t StackType_t;
typedef struct { int unused; } StaticTask_t;
typedef struct { int unused; } StaticQueue_t;
typedef struct { int unused; } StaticSemaphore_t;
#define configTICK_RATE_HZ 1000
#define pdPASS 1
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define taskDISABLE_INTERRUPTS() ((void)0)
