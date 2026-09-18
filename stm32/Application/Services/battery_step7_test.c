#include "battery_step7_test.h"

#include "battery_adc_conversion.h"
#include "battery_monitor.h"

volatile bool g_battery_step7_self_test_passed;

static bool InRange(uint16_t value, uint16_t minimum, uint16_t maximum)
{
  return (value >= minimum) && (value <= maximum);
}

static bool TestAdcConversion(void)
{
  BatteryAdcReading_t reading;
  const uint16_t count = BATTERY_ADC_BURST_SAMPLES;

  if (!BatteryAdc_ConvertSums(2792U * count, 1489U * count,
                              count, &reading)) return false;
  if (!reading.measurement_valid ||
      !InRange(reading.voltage_mv, 4490U, 4510U) ||
      !InRange(reading.safety_voltage_mv, 4340U, 4360U) ||
      !InRange(reading.vdda_mv, 3290U, 3310U)) return false;

  /* A collapsed 3.3 V rail must be rejected, even if the battery ratio can
   * still be calculated correctly. */
  if (BatteryAdc_ConvertSums(2992U * count, 1890U * count,
                             count, &reading)) return false;
  if (BatteryAdc_ConvertSums(2792U * count, 0U,
                             count, &reading)) return false;
  if (BatteryAdc_ConvertSums(4095U * count, 1489U * count,
                             count, &reading)) return false;
  if (BatteryAdc_ConvertSums(3600U * count, 1489U * count,
                             count, &reading)) return false;
  return true;
}

static bool TestBatteryMonitor(void)
{
  /* UNKNOWN requires two consecutive safe samples. Invalidate restarts the
   * qualification window. */
  BatteryMonitor_Init();
  if (BatteryMonitor_GetState() != BATTERY_MONITOR_UNKNOWN) return false;
  if (BatteryMonitor_Update(BATTERY_LOW_TRIP_MV + 1U) !=
      BATTERY_MONITOR_UNKNOWN) return false;
  BatteryMonitor_Invalidate();
  if (BatteryMonitor_Update(BATTERY_LOW_TRIP_MV + 1U) !=
      BATTERY_MONITOR_UNKNOWN) return false;
  if (BatteryMonitor_Update(BATTERY_LOW_TRIP_MV + 1U) !=
      BATTERY_MONITOR_NORMAL) return false;

  /* Exactly TRIP counts as low; one safe sample cancels that count. */
  if (BatteryMonitor_Update(BATTERY_LOW_TRIP_MV) !=
      BATTERY_MONITOR_NORMAL) return false;
  if (BatteryMonitor_Update(BATTERY_LOW_TRIP_MV + 1U) !=
      BATTERY_MONITOR_NORMAL) return false;
  if (BatteryMonitor_Update(BATTERY_LOW_TRIP_MV) !=
      BATTERY_MONITOR_NORMAL) return false;
  if (BatteryMonitor_Update(BATTERY_LOW_TRIP_MV) !=
      BATTERY_MONITOR_LOW) return false;

  /* Recovery is three consecutive samples; 3999 mV and invalid data reset it. */
  if (BatteryMonitor_Update(BATTERY_LOW_RECOVERY_MV) !=
      BATTERY_MONITOR_LOW) return false;
  if (BatteryMonitor_Update(BATTERY_LOW_RECOVERY_MV - 1U) !=
      BATTERY_MONITOR_LOW) return false;
  if (BatteryMonitor_Update(BATTERY_LOW_RECOVERY_MV) !=
      BATTERY_MONITOR_LOW) return false;
  BatteryMonitor_Invalidate();
  if (BatteryMonitor_GetState() != BATTERY_MONITOR_LOW) return false;
  if (BatteryMonitor_Update(BATTERY_LOW_RECOVERY_MV) !=
      BATTERY_MONITOR_LOW) return false;
  if (BatteryMonitor_Update(BATTERY_LOW_RECOVERY_MV) !=
      BATTERY_MONITOR_LOW) return false;
  if (BatteryMonitor_Update(BATTERY_LOW_RECOVERY_MV) !=
      BATTERY_MONITOR_NORMAL) return false;

  /* 3501 mV is not critical; exactly 3500 mV is. */
  BatteryMonitor_Init();
  if (BatteryMonitor_Update(BATTERY_CRITICAL_MV + 1U) !=
      BATTERY_MONITOR_UNKNOWN) return false;
  BatteryMonitor_Init();
  if (BatteryMonitor_Update(BATTERY_CRITICAL_MV) !=
      BATTERY_MONITOR_LOW) return false;

  BatteryMonitor_Init();
  if (BatteryMonitor_Update(4200U) != BATTERY_MONITOR_UNKNOWN) return false;
  if (BatteryMonitor_Update(4200U) != BATTERY_MONITOR_NORMAL) return false;
  BatteryMonitor_Invalidate();
  if (BatteryMonitor_GetState() != BATTERY_MONITOR_UNKNOWN) return false;
  return true;
}

bool BatteryStep7Test_Run(void)
{
  const bool passed = TestAdcConversion() && TestBatteryMonitor();
  BatteryMonitor_Init();
  g_battery_step7_self_test_passed = passed;
  return passed;
}
