#include <string.h>
#include "../../stm32/Drivers/HC_SR04/hc_sr04.c"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
GPIO_TypeDef mock_gpio_a, mock_gpio_b;
TIM_TypeDef mock_tim2;
RCC_TypeDef mock_rcc;
EXTI_TypeDef mock_exti;
uint32_t mock_primask, mock_basepri, mock_ipsr;
static TIM_HandleTypeDef timer;
static uint32_t timer_step, counter_reads;
static unsigned timer_fail;
uint32_t MockMaskInterrupts(void) {uint32_t p=mock_basepri;mock_basepri=5;return p;}
void MockRestoreInterrupts(uint32_t p) {mock_basepri=p;}
uint32_t MockGetCounter(TIM_HandleTypeDef *t) {++counter_reads;t->Instance->CNT=(t->Instance->CNT+timer_step)&0xffff;return t->Instance->CNT;}
void MockSetCompare(TIM_HandleTypeDef *t,uint32_t c,uint32_t v) {if(c==1)t->Instance->CCR1=v;else t->Instance->CCR2=v;}
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,GPIO_PinState s) {if(s)p->ODR|=pin;else p->ODR &= ~(uint32_t)pin;}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p,uint16_t pin) {return p->IDR&pin ? GPIO_PIN_SET:GPIO_PIN_RESET;}
uint32_t HAL_RCC_GetPCLK1Freq(void) {return 36000000;}
void HAL_NVIC_ClearPendingIRQ(int i) {(void)i;}
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *t) {t->Instance->CR1=0;return timer_fail==1 ? HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *t) {t->Instance->CR1=TIM_CR1_CEN;return timer_fail==2 ? HAL_ERROR:HAL_OK;}
static void Reset(void) {
  memset(&mock_tim2,0,sizeof(mock_tim2)); memset(&mock_gpio_a,0,sizeof(mock_gpio_a));
  memset(&mock_gpio_b,0,sizeof(mock_gpio_b)); memset(&mock_exti,0,sizeof(mock_exti));
  timer.Instance=TIM2;timer.Init.Period=65535;timer.Init.Prescaler=71;timer.Channel=2;
  mock_rcc.CFGR=RCC_CFGR_PPRE1; timer_fail=0;timer_step=1;counter_reads=0;
  mock_primask=mock_basepri=mock_ipsr=0;driver_phase=HC_SR04_PHASE_UNINITIALIZED;driver_timer=NULL;
}
static void Edge(uint16_t count,bool high) {timer_step=0;mock_tim2.CNT=count;mock_gpio_a.IDR=high ? HC_ECHO_Pin:0;HAL_GPIO_EXTI_Callback(HC_ECHO_Pin);}
int UltrasonicChecks_Run(void) {
  HC_SR04_Measurement_t m;
  Reset(); CHECK(HC_SR04_Trigger(0)==HC_SR04_TRIGGER_NOT_INITIALIZED);
  CHECK(HC_SR04_Init(NULL)==HAL_ERROR); timer.Init.Period=100;
  CHECK(HC_SR04_Init(&timer)==HAL_ERROR); Reset();timer.Init.Prescaler=70;
  CHECK(HC_SR04_Init(&timer)==HAL_ERROR);
  for(unsigned fail=1;fail<=2;++fail) {Reset();timer_fail=fail;CHECK(HC_SR04_Init(&timer)==HAL_ERROR && driver_timer==NULL);}
  Reset();CHECK(HC_SR04_Init(&timer)==HAL_OK && !HC_SR04_GetLatest(&m));
  mock_ipsr=1;CHECK(HC_SR04_Trigger(0)==HC_SR04_TRIGGER_CONTEXT_ERROR);mock_ipsr=0;
  mock_primask=1;CHECK(HC_SR04_Trigger(0)==HC_SR04_TRIGGER_CONTEXT_ERROR);mock_primask=0;
  mock_basepri=5;CHECK(HC_SR04_Trigger(0)==HC_SR04_TRIGGER_CONTEXT_ERROR);mock_basepri=0;
  mock_gpio_a.IDR=HC_ECHO_Pin;CHECK(HC_SR04_Trigger(0)==HC_SR04_TRIGGER_ECHO_HIGH);mock_gpio_a.IDR=0;
  CHECK(HC_SR04_Trigger(100)==HC_SR04_TRIGGER_ACCEPTED && HC_SR04_IsBusy());
  CHECK(!(mock_gpio_b.ODR&HC_TRIG_Pin) && (mock_exti.IMR&HC_ECHO_Pin));
  CHECK(HC_SR04_Trigger(100)==HC_SR04_TRIGGER_BUSY);
  uint16_t start=trigger_low_counter;
  Edge(start+100,true);Edge(start+1100,false);
  CHECK(driver_phase==HC_SR04_PHASE_RAW_READY && !HC_SR04_GetLatest(&m));
  HC_SR04_Service(500); CHECK(HC_SR04_GetLatest(&m) && m.status==HC_SR04_RESULT_VALID && m.distance_mm==172 && m.completed_at_ms==102);
  CHECK(!HC_SR04_IsBusy() && !(mock_exti.IMR&HC_ECHO_Pin) && !(mock_tim2.DIER&TIM_IT_CC2));
  CHECK(HC_SR04_Trigger(159)==HC_SR04_TRIGGER_TOO_SOON);
  const uint16_t widths[]={100,117,23323,25000,31000};
  const HC_SR04_ResultStatus_t expected[]={HC_SR04_RESULT_INVALID,HC_SR04_RESULT_VALID,
    HC_SR04_RESULT_VALID,HC_SR04_RESULT_INVALID,HC_SR04_RESULT_TIMEOUT};
  for(unsigned index=0;index<5;++index) {
    Reset();CHECK(HC_SR04_Init(&timer)==HAL_OK && HC_SR04_Trigger(100)==HC_SR04_TRIGGER_ACCEPTED);
    start=trigger_low_counter;Edge(start+10,true);Edge(start+10+widths[index],false);
    HC_SR04_Service(200);CHECK(HC_SR04_GetLatest(&m) && m.status==expected[index]);
    CHECK(!HC_SR04_IsBusy());
  }
  Reset();CHECK(HC_SR04_Init(&timer)==HAL_OK);mock_tim2.CNT=65000;
  CHECK(HC_SR04_Trigger(UINT32_MAX-10)==HC_SR04_TRIGGER_ACCEPTED);
  start=trigger_low_counter;Edge((uint16_t)(start+100),true);Edge((uint16_t)(start+2100),false);
  HC_SR04_Service(50);CHECK(HC_SR04_GetLatest(&m) && m.echo_time_us==2000 && m.distance_mm==343);
  for(unsigned order=0;order<2;++order) {
    Reset();CHECK(HC_SR04_Init(&timer)==HAL_OK && HC_SR04_Trigger(100)==HC_SR04_TRIGGER_ACCEPTED);
    Edge(trigger_low_counter+10,true);mock_tim2.SR|=TIM_FLAG_CC2;
    if(order==0) {Edge(trigger_low_counter+1000,false);HAL_TIM_OC_DelayElapsedCallback(&timer);}
    else {HAL_TIM_OC_DelayElapsedCallback(&timer);Edge(trigger_low_counter+1000,false);}
    CHECK(HC_SR04_GetLatest(&m) && m.status==HC_SR04_RESULT_TIMEOUT && m.sequence==1 && m.completed_at_ms==130);
  }
  Reset();CHECK(HC_SR04_Init(&timer)==HAL_OK && HC_SR04_Trigger(100)==HC_SR04_TRIGGER_ACCEPTED);
  start=trigger_low_counter;for(unsigned pulse=0;pulse<4;++pulse) {Edge(start+100+pulse*100,true);Edge(start+110+pulse*100,false);}
  HC_SR04_Service(101);CHECK(HC_SR04_GetLatest(&m) && m.status==HC_SR04_RESULT_INVALID && echo_edge_events==8);
  CHECK(!HC_SR04_IsBusy());timer_step=1;CHECK(HC_SR04_Trigger(160)==HC_SR04_TRIGGER_ACCEPTED);
  for(unsigned edge=0;edge<9;++edge) Edge(trigger_low_counter+10,false);
  HC_SR04_Service(161);CHECK(HC_SR04_GetLatest(&m) && m.status==HC_SR04_RESULT_INVALID && m.sequence==2);
  Reset();CHECK(HC_SR04_Init(&timer)==HAL_OK && HC_SR04_Trigger(100)==HC_SR04_TRIGGER_ACCEPTED);
  HC_SR04_Service(131);CHECK(HC_SR04_IsBusy());HC_SR04_Service(132);
  CHECK(HC_SR04_GetLatest(&m) && m.status==HC_SR04_RESULT_TIMEOUT && !HC_SR04_IsBusy());
  Reset();CHECK(HC_SR04_Init(&timer)==HAL_OK);timer_step=0;
  CHECK(HC_SR04_Trigger(100)==HC_SR04_TRIGGER_TIMER_ERROR && counter_reads<=1026);
  CHECK(HC_SR04_GetLatest(&m) && m.status==HC_SR04_RESULT_DRIVER_ERROR && mock_basepri==0);
  CHECK(!(mock_exti.IMR&HC_ECHO_Pin) && !(mock_gpio_b.ODR&HC_TRIG_Pin));
  timer_step=1;CHECK(HC_SR04_Trigger(160)==HC_SR04_TRIGGER_ACCEPTED);
  return 0;
}
