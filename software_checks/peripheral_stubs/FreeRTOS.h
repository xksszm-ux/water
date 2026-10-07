#pragma once
#include "stm32f1xx_hal.h"
uint32_t MockMaskInterrupts(void);
void MockRestoreInterrupts(uint32_t);
#define portSET_INTERRUPT_MASK_FROM_ISR() MockMaskInterrupts()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(m) MockRestoreInterrupts(m)
