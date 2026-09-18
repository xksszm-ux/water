#ifndef BATTERY_ADC_CONVERSION_H
#define BATTERY_ADC_CONVERSION_H

#include <stdbool.h>
#include <stdint.h>

#ifndef BATTERY_VREFINT_CALIBRATED_MV
/* STM32F103 has no per-device VREFINT calibration constant. 1200 mV is the
 * nominal value; replace it with a board-calibrated value after measurement. */
#define BATTERY_VREFINT_CALIBRATED_MV 1200U
#endif

#ifndef BATTERY_VREFINT_SAFETY_MV
/* Datasheet lower bound. Using it for protection avoids an optimistic
 * voltage estimate before board calibration. */
#define BATTERY_VREFINT_SAFETY_MV 1160U
#endif

#ifndef BATTERY_DIVIDER_NUMERATOR
#define BATTERY_DIVIDER_NUMERATOR 2U
#endif

#ifndef BATTERY_DIVIDER_DENOMINATOR
#define BATTERY_DIVIDER_DENOMINATOR 1U
#endif

#define BATTERY_ADC_BURST_SAMPLES 8U

#ifndef BATTERY_ADC_MIN_SAFE_VDDA_MV
#define BATTERY_ADC_MIN_SAFE_VDDA_MV 3000U
#endif

#ifndef BATTERY_ADC_MAX_SAFE_VDDA_MV
#define BATTERY_ADC_MAX_SAFE_VDDA_MV 3600U
#endif

#ifndef BATTERY_INPUT_MAX_PLAUSIBLE_MV
#define BATTERY_INPUT_MAX_PLAUSIBLE_MV 5500U
#endif

#ifndef BATTERY_ADC_SATURATION_RAW
#define BATTERY_ADC_SATURATION_RAW 4080U
#endif

typedef struct {
  uint16_t raw_average;
  uint16_t vrefint_raw_average;
  uint16_t vdda_mv;
  uint16_t voltage_mv;
  uint16_t safety_voltage_mv;
  bool measurement_valid;
} BatteryAdcReading_t;

bool BatteryAdc_ConvertSums(uint32_t battery_sum, uint32_t vrefint_sum,
                            uint16_t sample_count,
                            BatteryAdcReading_t *reading);

#endif
