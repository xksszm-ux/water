#include "battery_adc_conversion.h"
#include <string.h>

#define ADC_MAX_VALUE 4095UL

#if (BATTERY_DIVIDER_DENOMINATOR == 0U)
#error "BATTERY_DIVIDER_DENOMINATOR must not be zero"
#endif

bool BatteryAdc_ConvertSums(uint32_t battery_sum, uint32_t vrefint_sum,
                            uint16_t sample_count,
                            BatteryAdcReading_t *reading)
{
  if (reading == NULL) return false;
  memset(reading, 0, sizeof(*reading));
  if ((sample_count == 0U) || (vrefint_sum == 0U) ||
      (battery_sum > (ADC_MAX_VALUE * sample_count)) ||
      (vrefint_sum > (ADC_MAX_VALUE * sample_count))) {
    return false;
  }

  const uint32_t battery_average =
      (battery_sum + (sample_count / 2U)) / sample_count;
  const uint32_t vrefint_average =
      (vrefint_sum + (sample_count / 2U)) / sample_count;
  if ((battery_average >= BATTERY_ADC_SATURATION_RAW) ||
      (vrefint_average == 0U) ||
      (vrefint_average >= BATTERY_ADC_SATURATION_RAW)) {
    return false;
  }

  const uint64_t vdda_numerator =
      (uint64_t)BATTERY_VREFINT_CALIBRATED_MV * ADC_MAX_VALUE * sample_count;
  const uint64_t vdda_result =
      (vdda_numerator + (vrefint_sum / 2U)) / vrefint_sum;

  const uint64_t voltage_denominator =
      (uint64_t)vrefint_sum * BATTERY_DIVIDER_DENOMINATOR;
  const uint64_t nominal_numerator =
      (uint64_t)battery_sum * BATTERY_VREFINT_CALIBRATED_MV *
      BATTERY_DIVIDER_NUMERATOR;
  const uint64_t safety_numerator =
      (uint64_t)battery_sum * BATTERY_VREFINT_SAFETY_MV *
      BATTERY_DIVIDER_NUMERATOR;
  const uint64_t voltage_result =
      ((nominal_numerator + (voltage_denominator / 2U)) /
       voltage_denominator);
  const uint64_t safety_voltage_result =
      ((safety_numerator + (voltage_denominator / 2U)) /
       voltage_denominator);

  if ((vdda_result < BATTERY_ADC_MIN_SAFE_VDDA_MV) ||
      (vdda_result > BATTERY_ADC_MAX_SAFE_VDDA_MV) ||
      (voltage_result > BATTERY_INPUT_MAX_PLAUSIBLE_MV) ||
      (voltage_result > UINT16_MAX) ||
      (safety_voltage_result > UINT16_MAX)) {
    return false;
  }

  reading->raw_average = (uint16_t)battery_average;
  reading->vrefint_raw_average = (uint16_t)vrefint_average;
  reading->vdda_mv = (uint16_t)vdda_result;
  reading->voltage_mv = (uint16_t)voltage_result;
  reading->safety_voltage_mv = (uint16_t)safety_voltage_result;
  reading->measurement_valid = true;
  return true;
}

