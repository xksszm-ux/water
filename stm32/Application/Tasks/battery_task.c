#include "app_tasks.h"
#include "adc.h"
#include "battery_adc.h"
#include "battery_monitor.h"
#include "battery_step7_test.h"
#include "power_supply_guard.h"
#include "task_entries.h"
#include "cmsis_os2.h"

#define BATTERY_TASK_PERIOD_MS 500U

void BatteryTask_Entry(void *argument)
{
  (void)argument;
  uint32_t next_wake = osKernelGetTickCount();
  uint32_t last_init_attempt = osKernelGetTickCount() - 1000U;
  bool adc_ready = false;
  BatteryAdcReading_t reading;
  if (!BatteryStep7Test_Run()) Error_Handler();
  BatteryMonitor_Init();
  RobotState_InvalidateBattery(osKernelGetTickCount());
  for (;;) {
    AppTasks_Heartbeat(APP_TASK_BATTERY);
    const uint32_t now = osKernelGetTickCount();
    if (!PowerSupplyGuard_IsSafe()) {
      BatteryMonitor_Invalidate();
      RobotState_InvalidateBattery(now);
    } else if (!adc_ready &&
               ((uint32_t)(now - last_init_attempt) >= 1000U)) {
      last_init_attempt = now;
      adc_ready = BatteryAdc_Init(&hadc1) == HAL_OK;
      next_wake = osKernelGetTickCount();
    }

    if (PowerSupplyGuard_IsSafe() && adc_ready) {
      if (BatteryAdc_Read(&reading, 20U) == HAL_OK) {
        if (reading.measurement_valid && PowerSupplyGuard_IsSafe()) {
          const BatteryMonitorState_t state =
              BatteryMonitor_Update(reading.safety_voltage_mv);
          if (state == BATTERY_MONITOR_UNKNOWN) {
            RobotState_InvalidateBattery(osKernelGetTickCount());
          } else {
            RobotState_UpdateBattery(reading.voltage_mv, reading.raw_average,
                                     state == BATTERY_MONITOR_LOW,
                                     osKernelGetTickCount());
          }
        } else {
          BatteryMonitor_Invalidate();
          RobotState_InvalidateBattery(osKernelGetTickCount());
        }
      } else {
        adc_ready = false;
        BatteryMonitor_Invalidate();
        RobotState_InvalidateBattery(osKernelGetTickCount());
      }
    } else if (PowerSupplyGuard_IsSafe()) {
      BatteryMonitor_Invalidate();
      RobotState_InvalidateBattery(osKernelGetTickCount());
    }

    next_wake += BATTERY_TASK_PERIOD_MS;
    if (osDelayUntil(next_wake) != osOK) next_wake = osKernelGetTickCount();
  }
}
