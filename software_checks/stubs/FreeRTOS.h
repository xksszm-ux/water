#pragma once
#include <stdint.h>
/* Sequential host boundary, not a scheduler or interrupt model. */
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
void vTaskSuspendAll(void);
int xTaskResumeAll(void);
