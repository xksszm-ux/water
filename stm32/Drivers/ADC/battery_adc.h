#ifndef BATTERY_ADC_H
#define BATTERY_ADC_H

#include "battery_adc_conversion.h"
#include "stm32f1xx_hal.h"

HAL_StatusTypeDef BatteryAdc_Init(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef BatteryAdc_Read(BatteryAdcReading_t *reading,
                                  uint32_t sample_timeout_ms);
bool BatteryAdc_IsReady(void);

#endif
