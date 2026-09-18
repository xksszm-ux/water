#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

/* Provisional thresholds for the stated 4.5 V input. Calibrate after the
 * battery chemistry and regulator dropout are confirmed with a multimeter. */
#ifndef BATTERY_LOW_TRIP_MV
#define BATTERY_LOW_TRIP_MV 3800U
#endif

#ifndef BATTERY_LOW_RECOVERY_MV
#define BATTERY_LOW_RECOVERY_MV 4000U
#endif

#ifndef BATTERY_CRITICAL_MV
#define BATTERY_CRITICAL_MV 3500U
#endif

#define BATTERY_LOW_CONFIRM_COUNT      2U
#define BATTERY_RECOVERY_CONFIRM_COUNT 3U
#define BATTERY_VALID_CONFIRM_COUNT    2U

#if (BATTERY_CRITICAL_MV > BATTERY_LOW_TRIP_MV)
#error "BATTERY_CRITICAL_MV must not exceed BATTERY_LOW_TRIP_MV"
#endif

#if (BATTERY_LOW_TRIP_MV >= BATTERY_LOW_RECOVERY_MV)
#error "BATTERY_LOW_RECOVERY_MV must exceed BATTERY_LOW_TRIP_MV"
#endif

typedef enum {
  BATTERY_MONITOR_UNKNOWN = 0U,
  BATTERY_MONITOR_NORMAL,
  BATTERY_MONITOR_LOW
} BatteryMonitorState_t;

void BatteryMonitor_Init(void);
BatteryMonitorState_t BatteryMonitor_Update(uint16_t voltage_mv);
void BatteryMonitor_Invalidate(void);
BatteryMonitorState_t BatteryMonitor_GetState(void);
bool BatteryMonitor_IsLow(void);

#endif
