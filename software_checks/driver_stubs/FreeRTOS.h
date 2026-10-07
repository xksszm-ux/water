#pragma once
#include "stm32f1xx_hal.h"
#define taskENTER_CRITICAL() MockEnterCritical()
#define taskEXIT_CRITICAL() MockExitCritical()
