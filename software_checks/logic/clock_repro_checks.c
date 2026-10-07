/* Production RobotState with preemption injected only at the lock boundary.
 * It is sequential ordering, not actual RTOS/ISR scheduling. */
#include "FreeRTOS.h"
static void EnterCritical(void);
static void ExitCritical(void);
#undef taskENTER_CRITICAL
#undef taskEXIT_CRITICAL
#define taskENTER_CRITICAL() EnterCritical()
#define taskEXIT_CRITICAL() ExitCritical()
#include "../../stm32/Application/Services/State/robot_state.c"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static uint32_t current_tick;
static unsigned critical_depth, unlocked_time_reads;
static bool inject_sensor, inject_battery;

uint32_t osKernelGetTickCount(void)
{
  if (critical_depth == 0U) ++unlocked_time_reads;
  return current_tick;
}

static void EnterCritical(void)
{
  if (critical_depth == 0U && (inject_sensor || inject_battery)) {
    const bool sensor = inject_sensor;
    const bool battery = inject_battery;
    inject_sensor = inject_battery = false;
    ++current_tick;
    if (sensor) {
      const SensorMessage_t sample = {.timestamp_ms = current_tick,
          .valid_mask = SENSOR_VALID_DISTANCE, .distance_mm = 300U};
      RobotState_UpdateSensor(&sample);
    }
    if (battery) RobotState_UpdateBattery(4500U, 10U, false, current_tick);
  }
  ++critical_depth;
}

static void ExitCritical(void) { --critical_depth; }

int ClockRepro_Run(void)
{
  RobotStatus_t snapshot;
  SensorMessage_t sensor = {.timestamp_ms = 4990U,
      .valid_mask = SENSOR_VALID_DISTANCE, .distance_mm = 300U};
  critical_depth = unlocked_time_reads = 0U;
  inject_sensor = inject_battery = false;
  current_tick = 5000U;
  RobotState_Init();
  RobotState_UpdateSensor(&sensor);
  inject_sensor = true;
  RobotState_InvalidateSensorIfStale();
  RobotState_GetSnapshot(&snapshot);
  CHECK(snapshot.sensor_valid_mask == SENSOR_VALID_DISTANCE &&
        snapshot.sensor_updated_at_ms == 5001U);
  current_tick = 5201U;
  RobotState_InvalidateSensorIfStale();
  RobotState_GetSnapshot(&snapshot);
  CHECK(snapshot.sensor_valid_mask == SENSOR_VALID_DISTANCE);
  current_tick = 5202U;
  RobotState_InvalidateSensorIfStale();
  RobotState_GetSnapshot(&snapshot);
  CHECK(snapshot.sensor_valid_mask == SENSOR_VALID_NONE &&
        (snapshot.error_status & ROBOT_ERROR_SENSOR) != 0U);

  /* A boundary preemption can cross the real 32-bit tick rollover. */
  current_tick = UINT32_MAX;
  inject_sensor = true;
  RobotState_InvalidateSensorIfStale();
  CHECK(current_tick == 0U);
  current_tick = 200U;
  RobotState_InvalidateSensorIfStale();
  RobotState_GetSnapshot(&snapshot);
  CHECK(snapshot.sensor_valid_mask == SENSOR_VALID_DISTANCE);
  current_tick = 201U;
  RobotState_InvalidateSensorIfStale();
  RobotState_GetSnapshot(&snapshot);
  CHECK(snapshot.sensor_valid_mask == SENSOR_VALID_NONE);
  /* Do not hide genuinely very old samples with a signed-age shortcut. */
  sensor.timestamp_ms = 0U;
  RobotState_UpdateSensor(&sensor);
  current_tick = UINT32_MAX;
  RobotState_InvalidateSensorIfStale();
  RobotState_GetSnapshot(&snapshot);
  CHECK(snapshot.sensor_valid_mask == SENSOR_VALID_NONE);

  current_tick = 6000U;
  RobotState_UpdateBattery(4500U, 10U, false, 5990U);
  inject_battery = true;
  RobotState_InvalidateBatteryIfStale();
  RobotState_GetSnapshot(&snapshot);
  CHECK(snapshot.battery_valid && snapshot.battery_updated_at_ms == 6001U);
  current_tick = 6010U;
  inject_battery = true;
  CHECK(RobotState_IsMotorPowerAllowed());
  current_tick = 7511U;
  CHECK(RobotState_IsMotorPowerAllowed());
  current_tick = 7512U;
  CHECK(!RobotState_IsMotorPowerAllowed());
  RobotState_InvalidateBatteryIfStale();
  RobotState_GetSnapshot(&snapshot);
  CHECK(!snapshot.battery_valid && (snapshot.error_status & ROBOT_ERROR_BATTERY_ADC));
  RobotState_UpdateBattery(3500U, 10U, true, current_tick);
  CHECK(!RobotState_IsMotorPowerAllowed());
  CHECK(unlocked_time_reads == 0U && critical_depth == 0U);
  return 0;
}
