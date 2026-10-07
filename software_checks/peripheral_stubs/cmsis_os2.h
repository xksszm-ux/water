#pragma once
#include "../stubs/cmsis_os2.h"
#define osFlagsError 0x80000000U
#define osFlagsErrorTimeout 0xFFFFFFFEU
uint32_t osThreadFlagsSet(osThreadId_t,uint32_t);
osStatus_t osDelay(uint32_t);
