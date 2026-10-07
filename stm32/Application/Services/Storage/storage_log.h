#ifndef STORAGE_LOG_H
#define STORAGE_LOG_H

#include <stdint.h>
#include "robot_state.h"
#include "stm32f1xx_hal.h"

extern volatile uint32_t g_storage_record_count;

HAL_StatusTypeDef StorageLog_Init(void);
HAL_StatusTypeDef StorageLog_Append(const RobotStatus_t *status);
uint32_t StorageLog_GetRecordCount(void);

#endif
