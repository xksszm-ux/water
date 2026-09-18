#include "hc_sr04.h"

#include <limits.h>

#include "FreeRTOS.h"
#include "main.h"

#define HC_SR04_ECHO_TIMEOUT_US       30000U
#define HC_SR04_TIMER_FREQUENCY_HZ  1000000U
#define HC_SR04_SERVICE_GRACE_MS          1U
#define HC_SR04_MIN_ECHO_US              100U
#define HC_SR04_TRIGGER_SPIN_LIMIT      1024U
#define HC_SR04_MAX_EDGE_EVENTS            8U
#define HC_SR04_MIN_DISTANCE_MM            20U
#define HC_SR04_MAX_DISTANCE_MM          4000U

typedef enum {
  HC_SR04_PHASE_UNINITIALIZED = 0U,
  HC_SR04_PHASE_IDLE,
  HC_SR04_PHASE_TRIGGER_HIGH,
  HC_SR04_PHASE_WAIT_RISE,
  HC_SR04_PHASE_WAIT_FALL,
  HC_SR04_PHASE_RAW_READY,
  HC_SR04_PHASE_PROCESSING
} HC_SR04_Phase_t;

typedef enum {
  HC_SR04_RAW_NONE = 0U,
  HC_SR04_RAW_ECHO,
  HC_SR04_RAW_EDGE_BUDGET
} HC_SR04_RawKind_t;

static TIM_HandleTypeDef *driver_timer;
static volatile HC_SR04_Phase_t driver_phase;
static volatile uint32_t trigger_started_ms;
static volatile uint32_t last_trigger_ms;
static volatile uint16_t trigger_low_counter;
static volatile uint16_t echo_rise_counter;
static volatile uint8_t echo_edge_events;
static volatile bool has_triggered;
static volatile HC_SR04_RawKind_t pending_raw_kind;
static volatile uint16_t pending_echo_time_us;
static volatile uint16_t pending_trigger_to_fall_us;
static volatile HC_SR04_Measurement_t latest_measurement;

static uint32_t EnterCritical(void)
{
  /* Mask TIM2/EXTI and the scheduler while still allowing the priority-4 PVD
   * brownout interrupt to execute its emergency stop. */
  return portSET_INTERRUPT_MASK_FROM_ISR();
}

static void ExitCritical(uint32_t interrupt_mask)
{
  portCLEAR_INTERRUPT_MASK_FROM_ISR(interrupt_mask);
}

static uint16_t TimerCounter(void)
{
  return (uint16_t)__HAL_TIM_GET_COUNTER(driver_timer);
}

static uint16_t CounterElapsed(uint16_t started_at, uint16_t ended_at)
{
  /* TIM2 uses ARR=0xffff, so unsigned 16-bit subtraction is the exact modulo
   * delta even when the counter wraps between the two Echo edges. */
  return (uint16_t)(ended_at - started_at);
}

static bool TimerConfigurationIsValid(const TIM_HandleTypeDef *timer)
{
  if ((timer == NULL) || (timer->Instance != TIM2) ||
      (timer->Init.Period != UINT16_MAX)) {
    return false;
  }

  uint32_t timer_clock_hz = HAL_RCC_GetPCLK1Freq();
  if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U) {
    /* STM32F1 timers receive twice PCLK when the APB prescaler is not one. */
    timer_clock_hz *= 2U;
  }
  const uint32_t divider = timer->Init.Prescaler + 1U;
  return (divider != 0U) && ((timer_clock_hz % divider) == 0U) &&
         ((timer_clock_hz / divider) == HC_SR04_TIMER_FREQUENCY_HZ);
}

static void DisableCompareInterrupts(void)
{
  if (driver_timer == NULL) return;
  __HAL_TIM_DISABLE_IT(driver_timer, TIM_IT_CC1 | TIM_IT_CC2);
  __HAL_TIM_CLEAR_FLAG(driver_timer, TIM_FLAG_CC1 | TIM_FLAG_CC2);
}

static void DisableEchoInterrupt(void)
{
  EXTI->IMR &= ~(uint32_t)HC_ECHO_Pin;
  __HAL_GPIO_EXTI_CLEAR_IT(HC_ECHO_Pin);
}

static void EnableEchoInterrupt(void)
{
  __HAL_GPIO_EXTI_CLEAR_IT(HC_ECHO_Pin);
  EXTI->IMR |= (uint32_t)HC_ECHO_Pin;
}

static void PublishResult(HC_SR04_ResultStatus_t status,
                          uint32_t echo_time_us,
                          uint16_t distance_mm,
                          uint32_t completed_at_ms)
{
  const uint32_t next_sequence = latest_measurement.sequence + 1U;
  latest_measurement.status = status;
  latest_measurement.completed_at_ms = completed_at_ms;
  latest_measurement.echo_time_us = echo_time_us;
  latest_measurement.distance_mm = distance_mm;
  __DMB();
  /* Publish the sequence last so a debugger also sees a complete record. */
  latest_measurement.sequence = next_sequence;
}

static void CompleteTimeout(uint32_t now_ms)
{
  DisableEchoInterrupt();
  DisableCompareInterrupts();
  HAL_GPIO_WritePin(HC_TRIG_GPIO_Port, HC_TRIG_Pin, GPIO_PIN_RESET);
  driver_phase = HC_SR04_PHASE_IDLE;
  PublishResult(HC_SR04_RESULT_TIMEOUT, 0U, 0U, now_ms);
}

static void CompleteDriverError(uint32_t now_ms)
{
  DisableEchoInterrupt();
  DisableCompareInterrupts();
  HAL_GPIO_WritePin(HC_TRIG_GPIO_Port, HC_TRIG_Pin, GPIO_PIN_RESET);
  driver_phase = HC_SR04_PHASE_IDLE;
  PublishResult(HC_SR04_RESULT_DRIVER_ERROR, 0U, 0U, now_ms);
}

static void LatchRawResultFromIsr(HC_SR04_RawKind_t kind,
                                  uint16_t echo_time_us,
                                  uint16_t trigger_to_fall_us)
{
  DisableEchoInterrupt();
  __HAL_TIM_DISABLE_IT(driver_timer, TIM_IT_CC2);
  __HAL_TIM_CLEAR_FLAG(driver_timer, TIM_FLAG_CC2);
  pending_raw_kind = kind;
  pending_echo_time_us = echo_time_us;
  pending_trigger_to_fall_us = trigger_to_fall_us;
  __DMB();
  /* Publish the phase last. SensorTask performs all distance arithmetic. */
  driver_phase = HC_SR04_PHASE_RAW_READY;
}

static void ArmCompareInterrupt(uint32_t channel, uint16_t compare,
                                uint32_t interrupt, uint32_t flag)
{
  __HAL_TIM_DISABLE_IT(driver_timer, interrupt);
  __HAL_TIM_SET_COMPARE(driver_timer, channel, compare);
  __HAL_TIM_CLEAR_FLAG(driver_timer, flag);
  __HAL_TIM_ENABLE_IT(driver_timer, interrupt);
}

HAL_StatusTypeDef HC_SR04_Init(TIM_HandleTypeDef *timer_1mhz)
{
  DisableEchoInterrupt();
  HAL_GPIO_WritePin(HC_TRIG_GPIO_Port, HC_TRIG_Pin, GPIO_PIN_RESET);

  driver_phase = HC_SR04_PHASE_UNINITIALIZED;
  driver_timer = NULL;
  if (!TimerConfigurationIsValid(timer_1mhz)) return HAL_ERROR;

  driver_timer = timer_1mhz;
  DisableCompareInterrupts();
  if (HAL_TIM_Base_Stop(driver_timer) != HAL_OK) {
    driver_timer = NULL;
    return HAL_ERROR;
  }
  __HAL_TIM_SET_COUNTER(driver_timer, 0U);
  if (HAL_TIM_Base_Start(driver_timer) != HAL_OK) {
    driver_timer = NULL;
    return HAL_ERROR;
  }

  const uint32_t interrupt_mask = EnterCritical();
  trigger_started_ms = 0U;
  last_trigger_ms = 0U;
  trigger_low_counter = 0U;
  echo_rise_counter = 0U;
  echo_edge_events = 0U;
  has_triggered = false;
  pending_raw_kind = HC_SR04_RAW_NONE;
  pending_echo_time_us = 0U;
  pending_trigger_to_fall_us = 0U;
  latest_measurement.sequence = 0U;
  latest_measurement.completed_at_ms = 0U;
  latest_measurement.echo_time_us = 0U;
  latest_measurement.distance_mm = 0U;
  latest_measurement.status = HC_SR04_RESULT_NONE;
  driver_phase = HC_SR04_PHASE_IDLE;
  __DMB();
  ExitCritical(interrupt_mask);

  HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);
  return HAL_OK;
}

HC_SR04_TriggerResult_t HC_SR04_Trigger(uint32_t now_ms)
{
  /* The bounded Trigger pulse must run in task context while priority-4 PVD
   * remains able to preempt it, so reject ISR or pre-masked callers. */
  if ((__get_IPSR() != 0U) || (__get_PRIMASK() != 0U) ||
      (__get_BASEPRI() != 0U)) {
    return HC_SR04_TRIGGER_CONTEXT_ERROR;
  }

  HC_SR04_Service(now_ms);
  const uint32_t interrupt_mask = EnterCritical();

  if ((driver_phase == HC_SR04_PHASE_UNINITIALIZED) ||
      (driver_timer == NULL)) {
    ExitCritical(interrupt_mask);
    return HC_SR04_TRIGGER_NOT_INITIALIZED;
  }
  if ((driver_timer->Instance->CR1 & TIM_CR1_CEN) == 0U) {
    ExitCritical(interrupt_mask);
    return HC_SR04_TRIGGER_TIMER_ERROR;
  }
  if (driver_phase != HC_SR04_PHASE_IDLE) {
    ExitCritical(interrupt_mask);
    return HC_SR04_TRIGGER_BUSY;
  }
  if (has_triggered &&
      ((uint32_t)(now_ms - last_trigger_ms) <
       HC_SR04_MIN_TRIGGER_INTERVAL_MS)) {
    ExitCritical(interrupt_mask);
    return HC_SR04_TRIGGER_TOO_SOON;
  }
  if (HAL_GPIO_ReadPin(HC_ECHO_GPIO_Port, HC_ECHO_Pin) == GPIO_PIN_SET) {
    ExitCritical(interrupt_mask);
    return HC_SR04_TRIGGER_ECHO_HIGH;
  }

  DisableCompareInterrupts();
  DisableEchoInterrupt();
  trigger_started_ms = now_ms;
  last_trigger_ms = now_ms;
  has_triggered = true;
  echo_edge_events = 0U;
  pending_raw_kind = HC_SR04_RAW_NONE;
  pending_echo_time_us = 0U;
  pending_trigger_to_fall_us = 0U;
  driver_phase = HC_SR04_PHASE_TRIGGER_HIGH;
  __DMB();
  HAL_GPIO_WritePin(HC_TRIG_GPIO_Port, HC_TRIG_Pin, GPIO_PIN_SET);
  const uint16_t pulse_started = TimerCounter();
  uint32_t spin_count = 0U;
  while (CounterElapsed(pulse_started, TimerCounter()) <
         HC_SR04_TRIGGER_PULSE_US) {
    if (++spin_count >= HC_SR04_TRIGGER_SPIN_LIMIT) {
      HAL_GPIO_WritePin(HC_TRIG_GPIO_Port, HC_TRIG_Pin, GPIO_PIN_RESET);
      CompleteDriverError(now_ms);
      ExitCritical(interrupt_mask);
      return HC_SR04_TRIGGER_TIMER_ERROR;
    }
  }
  HAL_GPIO_WritePin(HC_TRIG_GPIO_Port, HC_TRIG_Pin, GPIO_PIN_RESET);
  trigger_low_counter = TimerCounter();
  const uint16_t echo_deadline =
      (uint16_t)(trigger_low_counter + HC_SR04_ECHO_TIMEOUT_US);
  driver_phase = HC_SR04_PHASE_WAIT_RISE;
  __DMB();
  ArmCompareInterrupt(TIM_CHANNEL_2, echo_deadline,
                      TIM_IT_CC2, TIM_FLAG_CC2);
  EnableEchoInterrupt();
  ExitCritical(interrupt_mask);
  return HC_SR04_TRIGGER_ACCEPTED;
}

void HC_SR04_Service(uint32_t now_ms)
{
  HC_SR04_RawKind_t raw_kind = HC_SR04_RAW_NONE;
  uint16_t echo_time_us = 0U;
  uint16_t trigger_to_fall_us = 0U;
  uint32_t started_ms = 0U;
  bool process_raw = false;

  if ((driver_timer == NULL) ||
      (driver_phase == HC_SR04_PHASE_UNINITIALIZED)) {
    return;
  }

  const uint32_t interrupt_mask = EnterCritical();
  if (driver_phase == HC_SR04_PHASE_RAW_READY) {
    raw_kind = pending_raw_kind;
    echo_time_us = pending_echo_time_us;
    trigger_to_fall_us = pending_trigger_to_fall_us;
    started_ms = trigger_started_ms;
    driver_phase = HC_SR04_PHASE_PROCESSING;
    process_raw = true;
  } else if ((driver_phase == HC_SR04_PHASE_TRIGGER_HIGH) &&
      ((uint32_t)(now_ms - trigger_started_ms) >
       HC_SR04_SERVICE_GRACE_MS)) {
    CompleteDriverError(now_ms);
  } else if (((driver_phase == HC_SR04_PHASE_WAIT_RISE) ||
              (driver_phase == HC_SR04_PHASE_WAIT_FALL)) &&
             ((uint32_t)(now_ms - trigger_started_ms) >
              (HC_SR04_ECHO_TIMEOUT_MS + HC_SR04_SERVICE_GRACE_MS))) {
    CompleteTimeout(trigger_started_ms + HC_SR04_ECHO_TIMEOUT_MS);
  }
  ExitCritical(interrupt_mask);

  if (!process_raw) return;

  HC_SR04_ResultStatus_t result_status = HC_SR04_RESULT_INVALID;
  uint16_t distance_mm = 0U;
  uint32_t completed_at_ms = started_ms;
  if (raw_kind == HC_SR04_RAW_EDGE_BUDGET) {
    completed_at_ms = started_ms +
        (((uint32_t)trigger_to_fall_us + 999U) / 1000U);
  } else if ((raw_kind != HC_SR04_RAW_ECHO) ||
             (echo_time_us > HC_SR04_ECHO_TIMEOUT_US) ||
             (trigger_to_fall_us > HC_SR04_ECHO_TIMEOUT_US)) {
    result_status = (raw_kind == HC_SR04_RAW_ECHO) ?
        HC_SR04_RESULT_TIMEOUT : HC_SR04_RESULT_DRIVER_ERROR;
    completed_at_ms = started_ms + HC_SR04_ECHO_TIMEOUT_MS;
  } else {
    const uint32_t calculated_distance_mm =
        ((((uint32_t)echo_time_us * 343U) + 1000U) / 2000U);
    completed_at_ms = started_ms +
        (((uint32_t)trigger_to_fall_us + 999U) / 1000U);
    if ((echo_time_us >= HC_SR04_MIN_ECHO_US) &&
        (calculated_distance_mm >= HC_SR04_MIN_DISTANCE_MM) &&
        (calculated_distance_mm <= HC_SR04_MAX_DISTANCE_MM)) {
      result_status = HC_SR04_RESULT_VALID;
      distance_mm = (uint16_t)calculated_distance_mm;
    }
  }

  const uint32_t publish_mask = EnterCritical();
  if (driver_phase == HC_SR04_PHASE_PROCESSING) {
    PublishResult(result_status, echo_time_us, distance_mm,
                  completed_at_ms);
    pending_raw_kind = HC_SR04_RAW_NONE;
    __DMB();
    driver_phase = HC_SR04_PHASE_IDLE;
  }
  ExitCritical(publish_mask);
}

bool HC_SR04_GetLatest(HC_SR04_Measurement_t *measurement)
{
  if (measurement == NULL) return false;

  const uint32_t interrupt_mask = EnterCritical();
  if (latest_measurement.status == HC_SR04_RESULT_NONE) {
    ExitCritical(interrupt_mask);
    return false;
  }
  measurement->status = latest_measurement.status;
  measurement->completed_at_ms = latest_measurement.completed_at_ms;
  measurement->echo_time_us = latest_measurement.echo_time_us;
  measurement->distance_mm = latest_measurement.distance_mm;
  measurement->sequence = latest_measurement.sequence;
  ExitCritical(interrupt_mask);
  return true;
}

bool HC_SR04_IsBusy(void)
{
  const uint32_t interrupt_mask = EnterCritical();
  const bool busy = (driver_phase == HC_SR04_PHASE_TRIGGER_HIGH) ||
                    (driver_phase == HC_SR04_PHASE_WAIT_RISE) ||
                    (driver_phase == HC_SR04_PHASE_WAIT_FALL) ||
                    (driver_phase == HC_SR04_PHASE_RAW_READY) ||
                    (driver_phase == HC_SR04_PHASE_PROCESSING);
  ExitCritical(interrupt_mask);
  return busy;
}

void HC_SR04_HandleExti(uint16_t gpio_pin)
{
  if ((gpio_pin != HC_ECHO_Pin) || (driver_timer == NULL)) return;

  /* If normal interrupt service was delayed until both IRQs were pending,
   * timeout wins over a late Echo edge regardless of NVIC tie-breaking. */
  if ((__HAL_TIM_GET_FLAG(driver_timer, TIM_FLAG_CC2) != RESET) &&
      (__HAL_TIM_GET_IT_SOURCE(driver_timer, TIM_IT_CC2) != RESET)) {
    CompleteTimeout(trigger_started_ms + HC_SR04_ECHO_TIMEOUT_MS);
    return;
  }

  if ((driver_phase != HC_SR04_PHASE_WAIT_RISE) &&
      (driver_phase != HC_SR04_PHASE_WAIT_FALL)) {
    return;
  }

  if (echo_edge_events < UINT8_MAX) ++echo_edge_events;
  if (echo_edge_events > HC_SR04_MAX_EDGE_EVENTS) {
    const uint16_t edge_at = TimerCounter();
    LatchRawResultFromIsr(
        HC_SR04_RAW_EDGE_BUDGET, 0U,
        CounterElapsed(trigger_low_counter, edge_at));
    return;
  }

  const GPIO_PinState level =
      HAL_GPIO_ReadPin(HC_ECHO_GPIO_Port, HC_ECHO_Pin);
  if ((driver_phase == HC_SR04_PHASE_WAIT_RISE) &&
      (level == GPIO_PIN_SET)) {
    echo_rise_counter = TimerCounter();
    __DMB();
    driver_phase = HC_SR04_PHASE_WAIT_FALL;
    return;
  }

  if ((driver_phase != HC_SR04_PHASE_WAIT_FALL) ||
      (level != GPIO_PIN_RESET)) {
    return;
  }

  const uint16_t ended_at = TimerCounter();
  const uint16_t echo_time_us =
      CounterElapsed(echo_rise_counter, ended_at);
  const uint16_t trigger_to_fall_us =
      CounterElapsed(trigger_low_counter, ended_at);
  if ((echo_time_us > HC_SR04_ECHO_TIMEOUT_US) ||
      (trigger_to_fall_us > HC_SR04_ECHO_TIMEOUT_US)) {
    CompleteTimeout(trigger_started_ms + HC_SR04_ECHO_TIMEOUT_MS);
    return;
  }

  if (echo_time_us < HC_SR04_MIN_ECHO_US) {
    if (echo_edge_events >= HC_SR04_MAX_EDGE_EVENTS) {
      LatchRawResultFromIsr(HC_SR04_RAW_EDGE_BUDGET, echo_time_us,
                            trigger_to_fall_us);
    } else {
      /* Treat a short pulse as noise and keep waiting inside the original
       * 30 ms measurement window. */
      driver_phase = HC_SR04_PHASE_WAIT_RISE;
    }
    return;
  }

  LatchRawResultFromIsr(HC_SR04_RAW_ECHO, echo_time_us,
                        trigger_to_fall_us);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  /* Encoder EXTI lines also arrive here in the current CubeMX project. They
   * are intentionally ignored because V1 has no encoder driver. */
  HC_SR04_HandleExti(GPIO_Pin);
}

void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *timer)
{
  if ((timer != driver_timer) || (timer->Instance != TIM2)) return;

  if (timer->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
    __HAL_TIM_DISABLE_IT(driver_timer, TIM_IT_CC2);
    if ((driver_phase == HC_SR04_PHASE_WAIT_RISE) ||
        (driver_phase == HC_SR04_PHASE_WAIT_FALL)) {
      CompleteTimeout(trigger_started_ms + HC_SR04_ECHO_TIMEOUT_MS);
    }
  }
}
