#ifndef MOTOR_H
#define MOTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f1xx_hal.h"

#define MOTOR_OUTPUT_MAX_PERMILLE 1000

typedef enum {
  MOTOR_STOP_COAST = 0,
  MOTOR_STOP_BRAKE,
  MOTOR_STOP_STANDBY
} MotorStopMode_t;

typedef struct {
  int16_t left_output_permille;
  int16_t right_output_permille;
  MotorStopMode_t stop_mode;
  bool initialized;
  bool enabled;
} MotorDriverState_t;

HAL_StatusTypeDef MotorDriver_Init(void);
bool MotorDriver_SetOutput(int16_t left_permille, int16_t right_permille);
void MotorDriver_Drive(int16_t linear_permille, int16_t turn_left_permille);
void MotorDriver_Stop(MotorStopMode_t mode);
void MotorDriver_EmergencyStop(void);
MotorDriverState_t MotorDriver_GetState(void);

extern volatile bool g_motor_emergency_stop_latched;

#endif
