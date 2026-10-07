#include <string.h>
#include "../../stm32/Drivers/ADC/battery_adc.c"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
static ADC_HandleTypeDef adc, other_adc;
static unsigned starts, stops, sets, fail_sample, config_calls, config_fail, init_fail;
static uint32_t flags, channel, wait_result;
static bool stop_fail, no_task, late_on_stop, wrong_handle;
static uint16_t *sample_buffer;
osThreadId_t osThreadGetId(void) { return no_task ? NULL : (void *)1; }
uint32_t osThreadFlagsClear(uint32_t f) {flags &= ~f; return 0;}
uint32_t osThreadFlagsSet(osThreadId_t t,uint32_t f) {(void)t; ++sets; flags |= f; return flags;}
uint32_t osThreadFlagsWait(uint32_t f,uint32_t options,uint32_t timeout) {
  (void)f; (void)options;
  if(timeout!=20) return osFlagsError;
  if(starts==fail_sample && wait_result) return wait_result;
  *sample_buffer=(channel==ADC_CHANNEL_VREFINT ? 1489:2792);
  if(wrong_handle) HAL_ADC_ConvCpltCallback(&other_adc);
  else if(starts==fail_sample) HAL_ADC_ErrorCallback(&adc);
  else HAL_ADC_ConvCpltCallback(&adc);
  return flags ? flags:osFlagsErrorTimeout;
}
osStatus_t osDelay(uint32_t n) {(void)n;return init_fail==6 ? -1:osOK;}
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *a,const ADC_ChannelConfTypeDef *c) {
  (void)a; channel=c->Channel; ++config_calls;
  if(config_calls==config_fail || (init_fail==5 && channel==ADC_CHANNEL_VREFINT) || (init_fail==7 && channel==ADC_CHANNEL_0)) return HAL_ERROR;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_DeInit(ADC_HandleTypeDef *a) {(void)a;return init_fail==2 ? HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *a) {a->State=0;return init_fail==3 ? HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *a) {(void)a;return init_fail==4 ? HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *a,uint32_t *p,uint32_t n) {
  (void)a; ++starts; sample_buffer=(uint16_t *)p;
  return n!=1 || (starts==fail_sample && wait_result==1) ? HAL_BUSY:HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *a) {
  ++stops;
  if(late_on_stop) {HAL_ADC_ConvCpltCallback(a);HAL_ADC_ErrorCallback(a);}
  return stop_fail || init_fail==1 || init_fail==2 || init_fail==3 ? HAL_ERROR:HAL_OK;
}
static void Reset(void) {
  battery_adc=NULL; adc_ready=conversion_pending=false; waiting_task=NULL;
  starts=stops=sets=fail_sample=config_calls=config_fail=init_fail=0;
  flags=channel=wait_result=0; stop_fail=no_task=late_on_stop=wrong_handle=false;
  adc.State=other_adc.State=0;
}
int AdcDmaChecks_Run(void) {
  BatteryAdcReading_t reading;
  Reset(); CHECK(BatteryAdc_Init(NULL)==HAL_ERROR && !BatteryAdc_IsReady());
  for(unsigned fail=2;fail<=7;++fail) {
    Reset(); init_fail=fail; CHECK(BatteryAdc_Init(&adc)==HAL_ERROR && !adc_ready && waiting_task==NULL);
  }
  Reset(); init_fail=1; CHECK(BatteryAdc_Init(&adc)==HAL_OK); /* recovery succeeds */
  Reset(); adc.State=HAL_ADC_STATE_ERROR_DMA; CHECK(BatteryAdc_Init(&adc)==HAL_OK && adc.State==0);
  CHECK(channel==ADC_CHANNEL_0 && BatteryAdc_Read(NULL,20)==HAL_ERROR);
  CHECK(BatteryAdc_Read(&reading,0)==HAL_ERROR);
  CHECK(BatteryAdc_Read(&reading,20)==HAL_OK && reading.measurement_valid);
  CHECK(starts==18 && sets==18 && channel==ADC_CHANNEL_0 && waiting_task==NULL && !conversion_pending);
  unsigned old_sets=sets; HAL_ADC_ConvCpltCallback(&adc); HAL_ADC_ErrorCallback(&adc);
  CHECK(sets==old_sets); /* callbacks after completion cannot notify the old task */
  for(unsigned sample=1;sample<=18;++sample) {
    for(unsigned error=0;error<3;++error) {
      Reset(); CHECK(BatteryAdc_Init(&adc)==HAL_OK);
      fail_sample=sample; wait_result=error==0 ? osFlagsErrorTimeout : error==1 ? 1:0;
      late_on_stop=true;
      CHECK(BatteryAdc_Read(&reading,20)==(error==0 ? HAL_TIMEOUT:HAL_ERROR));
      CHECK(!reading.measurement_valid && !adc_ready && !conversion_pending && waiting_task==NULL);
      CHECK(channel==ADC_CHANNEL_0 && starts==sample && sets==sample-(error<2 ? 1U:0U));
      old_sets=sets; HAL_ADC_ConvCpltCallback(&adc); CHECK(sets==old_sets);
      fail_sample=wait_result=0; CHECK(BatteryAdc_Init(&adc)==HAL_OK);
      CHECK(BatteryAdc_Read(&reading,20)==HAL_OK && reading.measurement_valid);
    }
  }
  Reset(); CHECK(BatteryAdc_Init(&adc)==HAL_OK); stop_fail=true;
  CHECK(BatteryAdc_Read(&reading,20)==HAL_ERROR && !reading.measurement_valid && !adc_ready);
  Reset(); CHECK(BatteryAdc_Init(&adc)==HAL_OK); wrong_handle=true;
  CHECK(BatteryAdc_Read(&reading,20)==HAL_TIMEOUT && sets==0);
  Reset(); CHECK(BatteryAdc_Init(&adc)==HAL_OK); no_task=true;
  CHECK(BatteryAdc_Read(&reading,20)==HAL_ERROR && !reading.measurement_valid && waiting_task==NULL);
  Reset(); CHECK(BatteryAdc_Init(&adc)==HAL_OK); init_fail=7;
  CHECK(BatteryAdc_Read(&reading,20)==HAL_ERROR && !reading.measurement_valid && !adc_ready && waiting_task==NULL);
  Reset(); CHECK(BatteryAdc_Init(&adc)==HAL_OK); config_fail=config_calls+3;
  CHECK(BatteryAdc_Read(&reading,20)==HAL_ERROR && starts==18 && !reading.measurement_valid && !adc_ready && waiting_task==NULL);
  return 0;
}
