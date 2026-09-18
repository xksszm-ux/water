#include "battery_monitor.h"

static BatteryMonitorState_t monitor_state;
static uint8_t safe_count;
static uint8_t low_count;
static uint8_t recovery_count;

void BatteryMonitor_Init(void)
{
  monitor_state = BATTERY_MONITOR_UNKNOWN;
  safe_count = 0U;
  low_count = 0U;
  recovery_count = 0U;
}

BatteryMonitorState_t BatteryMonitor_Update(uint16_t voltage_mv)
{
  if (voltage_mv <= BATTERY_CRITICAL_MV) {
    monitor_state = BATTERY_MONITOR_LOW;
    safe_count = 0U;
    low_count = 0U;
    recovery_count = 0U;
    return monitor_state;
  }

  if (monitor_state == BATTERY_MONITOR_LOW) {
    low_count = 0U;
    safe_count = 0U;
    if (voltage_mv >= BATTERY_LOW_RECOVERY_MV) {
      if (recovery_count < BATTERY_RECOVERY_CONFIRM_COUNT) ++recovery_count;
      if (recovery_count >= BATTERY_RECOVERY_CONFIRM_COUNT) {
        monitor_state = BATTERY_MONITOR_NORMAL;
        recovery_count = 0U;
      }
    } else {
      recovery_count = 0U;
    }
    return monitor_state;
  }

  recovery_count = 0U;
  if (voltage_mv <= BATTERY_LOW_TRIP_MV) {
    safe_count = 0U;
    if (low_count < BATTERY_LOW_CONFIRM_COUNT) ++low_count;
  } else {
    low_count = 0U;
    if (monitor_state == BATTERY_MONITOR_UNKNOWN) {
      if (safe_count < BATTERY_VALID_CONFIRM_COUNT) ++safe_count;
    }
  }

  if (low_count >= BATTERY_LOW_CONFIRM_COUNT) {
    monitor_state = BATTERY_MONITOR_LOW;
    low_count = 0U;
  } else if ((monitor_state == BATTERY_MONITOR_UNKNOWN) &&
             (safe_count >= BATTERY_VALID_CONFIRM_COUNT)) {
    monitor_state = BATTERY_MONITOR_NORMAL;
  }
  return monitor_state;
}

void BatteryMonitor_Invalidate(void)
{
  safe_count = 0U;
  low_count = 0U;
  recovery_count = 0U;
  /* Never clear an already confirmed low-voltage latch because data vanished. */
  if (monitor_state != BATTERY_MONITOR_LOW) {
    monitor_state = BATTERY_MONITOR_UNKNOWN;
  }
}

BatteryMonitorState_t BatteryMonitor_GetState(void)
{
  return monitor_state;
}

bool BatteryMonitor_IsLow(void)
{
  return monitor_state == BATTERY_MONITOR_LOW;
}
