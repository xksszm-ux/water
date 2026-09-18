#include "battery_adc.h"
#include "cmsis_os2.h"
#include <limits.h>
#include <string.h>

#define ADC_DONE_FLAG  (1UL << 0)
#define ADC_ERROR_FLAG (1UL << 1)
#define ADC_MAX_VALUE  4095UL
#define ADC_VREFINT_STARTUP_DELAY_MS 1U
#define ADC_BATTERY_CHANNEL ADC_CHANNEL_0
#define ADC_BATTERY_SAMPLE_TIME ADC_SAMPLETIME_239CYCLES_5
#define ADC_VREFINT_SAMPLE_TIME ADC_SAMPLETIME_239CYCLES_5

#if (BATTERY_DIVIDER_DENOMINATOR == 0U)
#error "BATTERY_DIVIDER_DENOMINATOR must not be zero"
#endif

static ADC_HandleTypeDef *battery_adc;
static volatile osThreadId_t waiting_task;
static volatile uint16_t dma_sample;
static volatile bool conversion_pending;
static bool adc_ready;

static HAL_StatusTypeDef ConfigureChannel(ADC_HandleTypeDef *adc,
                                          uint32_t channel_number,
                                          uint32_t sampling_time)
{
  ADC_ChannelConfTypeDef channel = {0};
  channel.Channel = channel_number;
  channel.Rank = ADC_REGULAR_RANK_1;
  channel.SamplingTime = sampling_time;
  return HAL_ADC_ConfigChannel(adc, &channel);
}

static HAL_StatusTypeDef RecoverPeripheral(ADC_HandleTypeDef *adc)
{
  if (HAL_ADC_DeInit(adc) != HAL_OK) return HAL_ERROR;
  if (HAL_ADC_Init(adc) != HAL_OK) return HAL_ERROR;
  return ConfigureChannel(adc, ADC_BATTERY_CHANNEL,
                          ADC_BATTERY_SAMPLE_TIME);
}

static HAL_StatusTypeDef ReadSingleSample(uint16_t *sample,
                                          uint32_t sample_timeout_ms)
{
  if (sample == NULL) return HAL_ERROR;

  (void)osThreadFlagsClear(ADC_DONE_FLAG | ADC_ERROR_FLAG);
  dma_sample = UINT16_MAX;
  conversion_pending = true;
  if (HAL_ADC_Start_DMA(battery_adc, (uint32_t *)&dma_sample, 1U) != HAL_OK) {
    conversion_pending = false;
    (void)HAL_ADC_Stop_DMA(battery_adc);
    return HAL_ERROR;
  }

  const uint32_t flags = osThreadFlagsWait(ADC_DONE_FLAG | ADC_ERROR_FLAG,
                                            osFlagsWaitAny,
                                            sample_timeout_ms);
  conversion_pending = false;
  const HAL_StatusTypeDef stop_result = HAL_ADC_Stop_DMA(battery_adc);
  if ((flags & osFlagsError) != 0U) {
    return (flags == osFlagsErrorTimeout) ? HAL_TIMEOUT : HAL_ERROR;
  }
  if (((flags & ADC_ERROR_FLAG) != 0U) ||
      ((flags & ADC_DONE_FLAG) == 0U) || (stop_result != HAL_OK) ||
      (dma_sample > ADC_MAX_VALUE)) {
    return HAL_ERROR;
  }

  *sample = dma_sample;
  return HAL_OK;
}

static HAL_StatusTypeDef ReadChannelSum(uint32_t channel_number,
                                        uint32_t sampling_time,
                                        uint32_t sample_timeout_ms,
                                        uint32_t *sum)
{
  uint16_t sample;
  if ((sum == NULL) ||
      (ConfigureChannel(battery_adc, channel_number, sampling_time) != HAL_OK)) {
    return HAL_ERROR;
  }

  /* Discard the first conversion after a channel switch. */
  HAL_StatusTypeDef result = ReadSingleSample(&sample, sample_timeout_ms);
  if (result != HAL_OK) return result;

  *sum = 0U;
  for (uint32_t index = 0U; index < BATTERY_ADC_BURST_SAMPLES; ++index) {
    result = ReadSingleSample(&sample, sample_timeout_ms);
    if (result != HAL_OK) return result;
    *sum += sample;
  }
  return HAL_OK;
}

HAL_StatusTypeDef BatteryAdc_Init(ADC_HandleTypeDef *adc)
{
  adc_ready = false;
  conversion_pending = false;
  waiting_task = NULL;
  if (adc == NULL) return HAL_ERROR;
  battery_adc = adc;
  /* Recover a DMA/ADC transaction left busy by a previous timeout. */
  const uint32_t state_before_stop = battery_adc->State;
  const HAL_StatusTypeDef stop_result = HAL_ADC_Stop_DMA(battery_adc);
  if ((stop_result != HAL_OK) ||
      ((state_before_stop &
        (HAL_ADC_STATE_ERROR_INTERNAL | HAL_ADC_STATE_ERROR_DMA)) != 0U) ||
      ((battery_adc->State &
        (HAL_ADC_STATE_ERROR_INTERNAL | HAL_ADC_STATE_ERROR_DMA)) != 0U)) {
    if (RecoverPeripheral(battery_adc) != HAL_OK) return HAL_ERROR;
  }
  if (HAL_ADCEx_Calibration_Start(battery_adc) != HAL_OK) return HAL_ERROR;
  /* Selecting VREFINT enables TSVREFE. Give the internal path time to settle,
   * then leave Rank 1 restored to PA0 for a known idle configuration. */
  if (ConfigureChannel(battery_adc, ADC_CHANNEL_VREFINT,
                       ADC_VREFINT_SAMPLE_TIME) != HAL_OK) return HAL_ERROR;
  if (osDelay(ADC_VREFINT_STARTUP_DELAY_MS) != osOK) return HAL_ERROR;
  if (ConfigureChannel(battery_adc, ADC_BATTERY_CHANNEL,
                       ADC_BATTERY_SAMPLE_TIME) != HAL_OK) return HAL_ERROR;
  adc_ready = true;
  return HAL_OK;
}

HAL_StatusTypeDef BatteryAdc_Read(BatteryAdcReading_t *reading,
                                  uint32_t sample_timeout_ms)
{
  uint32_t battery_sum = 0U;
  uint32_t vrefint_sum = 0U;
  if (!adc_ready || (reading == NULL) || (sample_timeout_ms == 0U)) {
    return HAL_ERROR;
  }
  memset(reading, 0, sizeof(*reading));
  waiting_task = osThreadGetId();
  if (waiting_task == NULL) return HAL_ERROR;

  HAL_StatusTypeDef result = ReadChannelSum(
      ADC_CHANNEL_VREFINT, ADC_VREFINT_SAMPLE_TIME,
      sample_timeout_ms, &vrefint_sum);
  if (result == HAL_OK) {
    result = ReadChannelSum(ADC_BATTERY_CHANNEL, ADC_BATTERY_SAMPLE_TIME,
                            sample_timeout_ms, &battery_sum);
  }

  /* Always leave the generated PA0 channel selected. */
  const HAL_StatusTypeDef restore_result = ConfigureChannel(
      battery_adc, ADC_BATTERY_CHANNEL, ADC_BATTERY_SAMPLE_TIME);
  conversion_pending = false;
  waiting_task = NULL;
  if ((result != HAL_OK) || (restore_result != HAL_OK)) {
    adc_ready = false;
    return result != HAL_OK ? result : HAL_ERROR;
  }

  (void)BatteryAdc_ConvertSums(battery_sum, vrefint_sum,
                               BATTERY_ADC_BURST_SAMPLES, reading);
  return HAL_OK;
}

bool BatteryAdc_IsReady(void)
{
  return adc_ready;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
{
  if ((adc == battery_adc) && conversion_pending && (waiting_task != NULL)) {
    (void)osThreadFlagsSet(waiting_task, ADC_DONE_FLAG);
  }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc)
{
  if ((adc == battery_adc) && conversion_pending && (waiting_task != NULL)) {
    (void)osThreadFlagsSet(waiting_task, ADC_ERROR_FLAG);
  }
}
