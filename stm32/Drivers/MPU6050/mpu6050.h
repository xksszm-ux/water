#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f1xx_hal.h"

typedef struct {
  int16_t accel_mg[3];
  int32_t gyro_mdps[3];
  int16_t temperature_centi_c;
} Mpu6050Data_t;

HAL_StatusTypeDef MPU6050_Init(I2C_HandleTypeDef *i2c);
HAL_StatusTypeDef MPU6050_Read(Mpu6050Data_t *data);
bool MPU6050_IsReady(void);
uint8_t MPU6050_GetAddress(void);

#endif
