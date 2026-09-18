#include "robot_state.h"

#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

#define BATTERY_DATA_TIMEOUT_MS 1500U
#define SENSOR_DATA_TIMEOUT_MS   200U

static RobotStatus_t robot_status;

void RobotState_Init(void)
{
  taskENTER_CRITICAL();
  memset(&robot_status, 0, sizeof(robot_status));
  robot_status.mode = ROBOT_MODE_BLE;
  robot_status.error_status = ROBOT_ERROR_BATTERY_ADC | ROBOT_ERROR_CAN |
                              ROBOT_ERROR_UART;
  taskEXIT_CRITICAL();
}

void RobotState_UpdateSensor(const SensorMessage_t *sensor)
{
  if (sensor == NULL) return;
  taskENTER_CRITICAL();
  robot_status.sensor_updated_at_ms = sensor->timestamp_ms;
  robot_status.updated_at_ms = sensor->timestamp_ms;
  robot_status.distance_mm = sensor->distance_mm;
  robot_status.sensor_valid_mask = sensor->valid_mask;
  memcpy(robot_status.accel_mg, sensor->accel_mg, sizeof(robot_status.accel_mg));
  memcpy(robot_status.gyro_mdps, sensor->gyro_mdps, sizeof(robot_status.gyro_mdps));
  taskEXIT_CRITICAL();
}

void RobotState_InvalidateSensorIfStale(uint32_t now_ms)
{
  taskENTER_CRITICAL();
  if ((robot_status.sensor_updated_at_ms != 0U) &&
      ((uint32_t)(now_ms - robot_status.sensor_updated_at_ms) >
       SENSOR_DATA_TIMEOUT_MS)) {
    robot_status.sensor_valid_mask = SENSOR_VALID_NONE;
    robot_status.error_status |= ROBOT_ERROR_SENSOR;
    robot_status.updated_at_ms = now_ms;
  }
  taskEXIT_CRITICAL();
}

void RobotState_UpdateBattery(uint16_t battery_mv, uint16_t adc_raw,
                              bool battery_low, uint32_t timestamp_ms)
{
  taskENTER_CRITICAL();
  robot_status.battery_mv = battery_mv;
  robot_status.battery_adc_raw = adc_raw;
  robot_status.battery_valid = true;
  robot_status.battery_updated_at_ms = timestamp_ms;
  robot_status.error_status &= (uint8_t)(~ROBOT_ERROR_BATTERY_ADC);
  if (battery_low) robot_status.error_status |= ROBOT_ERROR_LOW_BATTERY;
  else robot_status.error_status &= (uint8_t)(~ROBOT_ERROR_LOW_BATTERY);
  robot_status.updated_at_ms = timestamp_ms;
  taskEXIT_CRITICAL();
}

void RobotState_InvalidateBattery(uint32_t timestamp_ms)
{
  taskENTER_CRITICAL();
  robot_status.battery_valid = false;
  robot_status.error_status |= ROBOT_ERROR_BATTERY_ADC;
  robot_status.updated_at_ms = timestamp_ms;
  taskEXIT_CRITICAL();
}

void RobotState_InvalidateBatteryIfStale(uint32_t now_ms)
{
  taskENTER_CRITICAL();
  if (robot_status.battery_valid &&
      ((uint32_t)(now_ms - robot_status.battery_updated_at_ms) >
       BATTERY_DATA_TIMEOUT_MS)) {
    robot_status.battery_valid = false;
    robot_status.error_status |= ROBOT_ERROR_BATTERY_ADC;
    robot_status.updated_at_ms = now_ms;
  }
  taskEXIT_CRITICAL();
}

bool RobotState_IsMotorPowerAllowed(uint32_t now_ms)
{
  bool allowed;
  taskENTER_CRITICAL();
  allowed = robot_status.battery_valid &&
      ((uint32_t)(now_ms - robot_status.battery_updated_at_ms) <=
       BATTERY_DATA_TIMEOUT_MS) &&
      ((robot_status.error_status &
        (ROBOT_ERROR_LOW_BATTERY | ROBOT_ERROR_BATTERY_ADC)) == 0U);
  taskEXIT_CRITICAL();
  return allowed;
}

void RobotState_UpdateMotorOutput(int16_t left_permille, int16_t right_permille,
                                  uint32_t timestamp_ms)
{
  taskENTER_CRITICAL();
  robot_status.left_output_permille = left_permille;
  robot_status.right_output_permille = right_permille;
  robot_status.updated_at_ms = timestamp_ms;
  taskEXIT_CRITICAL();
}

void RobotState_SetMode(RobotMode_t mode, uint32_t timestamp_ms)
{
  taskENTER_CRITICAL();
  robot_status.mode = mode;
  robot_status.updated_at_ms = timestamp_ms;
  taskEXIT_CRITICAL();
}

void RobotState_SetError(uint8_t error_mask, bool active, uint32_t timestamp_ms)
{
  taskENTER_CRITICAL();
  const uint8_t previous = robot_status.error_status;
  if (active) robot_status.error_status |= error_mask;
  else robot_status.error_status &= (uint8_t)(~error_mask);
  if (robot_status.error_status != previous) {
    robot_status.updated_at_ms = timestamp_ms;
  }
  taskEXIT_CRITICAL();
}

void RobotState_GetSnapshot(RobotStatus_t *snapshot)
{
  if (snapshot == NULL) return;
  taskENTER_CRITICAL();
  *snapshot = robot_status;
  taskEXIT_CRITICAL();
}
