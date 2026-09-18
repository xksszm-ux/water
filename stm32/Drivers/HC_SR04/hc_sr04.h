#ifndef HC_SR04_H
#define HC_SR04_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HC_SR04_MIN_TRIGGER_INTERVAL_MS 60U
#define HC_SR04_ECHO_TIMEOUT_MS         30U
#define HC_SR04_TRIGGER_PULSE_US        10U

typedef enum {
  HC_SR04_RESULT_NONE = 0U,
  HC_SR04_RESULT_VALID,
  HC_SR04_RESULT_INVALID,
  HC_SR04_RESULT_TIMEOUT,
  HC_SR04_RESULT_DRIVER_ERROR
} HC_SR04_ResultStatus_t;

typedef enum {
  HC_SR04_TRIGGER_ACCEPTED = 0U,
  HC_SR04_TRIGGER_NOT_INITIALIZED,
  HC_SR04_TRIGGER_BUSY,
  HC_SR04_TRIGGER_TOO_SOON,
  HC_SR04_TRIGGER_ECHO_HIGH,
  HC_SR04_TRIGGER_CONTEXT_ERROR,
  HC_SR04_TRIGGER_TIMER_ERROR
} HC_SR04_TriggerResult_t;

typedef struct {
  /* Incremented for every valid measurement, timeout, or driver error.
   * Sequence wrap is intentional; callers only need to compare for change. */
  uint32_t sequence;
  uint32_t completed_at_ms;
  uint32_t echo_time_us;
  uint16_t distance_mm;
  HC_SR04_ResultStatus_t status;
} HC_SR04_Measurement_t;

/* timer_1mhz must be the existing free-running TIM2 configured for a
 * 1 MHz counter clock and a 0xffff auto-reload value. */
HAL_StatusTypeDef HC_SR04_Init(TIM_HandleTypeDef *timer_1mhz);

/* Task-context API. now_ms must use the application's monotonic millisecond
 * time base (osKernelGetTickCount in the current project). It performs only a
 * bounded 10 us Trigger pulse; Echo waiting remains fully asynchronous. Do not
 * call from an ISR or while interrupts are masked. */
HC_SR04_TriggerResult_t HC_SR04_Trigger(uint32_t now_ms);

/* Task-context processing and fallback. It converts an ISR-latched raw Echo
 * width into millimetres; the TIM2 compare interrupt handles the 30 ms timeout.
 * Call periodically so a missing interrupt can also be recovered safely. */
void HC_SR04_Service(uint32_t now_ms);

/* Non-consuming snapshot of the latest completed result. The sequence field
 * lets a task distinguish a new result from one it has already processed. */
bool HC_SR04_GetLatest(HC_SR04_Measurement_t *measurement);
bool HC_SR04_IsBusy(void);

/* ISR routing entry. The driver also provides HAL_GPIO_EXTI_Callback() for the
 * current project, which forwards PA15 events here and ignores all other pins. */
void HC_SR04_HandleExti(uint16_t gpio_pin);

#ifdef __cplusplus
}
#endif

#endif
