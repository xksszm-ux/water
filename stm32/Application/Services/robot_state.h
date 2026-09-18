#ifndef ROBOT_STATE_H
#define ROBOT_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  ROBOT_MODE_BLE = 0,
  ROBOT_MODE_AUTO = 1
} RobotMode_t;

typedef enum {
  ROBOT_ERROR_NONE = 0U,
  ROBOT_ERROR_COMM_TIMEOUT = (1U << 0),
  ROBOT_ERROR_LOW_BATTERY = (1U << 1),
  ROBOT_ERROR_SENSOR = (1U << 2),
  ROBOT_ERROR_DISPLAY = (1U << 3),
  ROBOT_ERROR_STORAGE = (1U << 4),
  ROBOT_ERROR_BATTERY_ADC = (1U << 5),
  ROBOT_ERROR_CAN = (1U << 6),
  ROBOT_ERROR_UART = (1U << 7)
} RobotError_t;

typedef enum {
  SENSOR_VALID_NONE = 0U,
  SENSOR_VALID_MPU6050 = (1U << 0),
  SENSOR_VALID_DISTANCE = (1U << 1)
} SensorValidMask_t;

typedef struct {
  uint32_t timestamp_ms;
  int16_t accel_mg[3];
  int32_t gyro_mdps[3];
  uint16_t distance_mm;
  uint8_t valid_mask;
} SensorMessage_t;

typedef struct {
  /* Step 4 open-loop command: -1000..1000 equals -100.0%..100.0% PWM. */
  int16_t left_output_permille;
  int16_t right_output_permille;
  RobotMode_t mode;
  bool enable;
  uint32_t issued_at_ms;
} MotorCommand_t;

typedef struct {
  uint32_t updated_at_ms;
  uint32_t sensor_updated_at_ms;
  uint32_t battery_updated_at_ms;
  uint16_t battery_mv;
  uint16_t battery_adc_raw;
  int16_t left_output_permille;
  int16_t right_output_permille;
  uint16_t distance_mm;
  int16_t accel_mg[3];
  int32_t gyro_mdps[3];
  RobotMode_t mode;
  uint8_t error_status;
  uint8_t sensor_valid_mask;
  bool battery_valid;
} RobotStatus_t;

void RobotState_Init(void);
void RobotState_UpdateSensor(const SensorMessage_t *sensor);
void RobotState_InvalidateSensorIfStale(uint32_t now_ms);
void RobotState_UpdateBattery(uint16_t battery_mv, uint16_t adc_raw,
                              bool battery_low, uint32_t timestamp_ms);
void RobotState_InvalidateBattery(uint32_t timestamp_ms);
void RobotState_InvalidateBatteryIfStale(uint32_t now_ms);
bool RobotState_IsMotorPowerAllowed(uint32_t now_ms);
void RobotState_UpdateMotorOutput(int16_t left_permille, int16_t right_permille,
                                  uint32_t timestamp_ms);
void RobotState_SetMode(RobotMode_t mode, uint32_t timestamp_ms);
void RobotState_SetError(uint8_t error_mask, bool active, uint32_t timestamp_ms);
void RobotState_GetSnapshot(RobotStatus_t *snapshot);

#endif
