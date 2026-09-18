#include "app_tasks.h"
#include "task_entries.h"
#include "hc_sr04.h"
#include "i2c.h"
#include "mpu6050.h"
#include "tim.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <string.h>

#define SENSOR_PERIOD_MS        50U
#define SENSOR_STALE_TIMEOUT_MS 200U
#define HC_TRIGGER_PERIOD_MS    100U
#define SENSOR_REINIT_PERIOD_MS 1000U

void SensorTask_Entry(void *argument)
{
  (void)argument;
  uint32_t next_wake = osKernelGetTickCount();
  SensorMessage_t message;
  Mpu6050Data_t mpu_data;
  HC_SR04_Measurement_t distance_measurement;
  bool mpu_ready = false;
  bool hc_sr04_ready = false;
  bool have_mpu_sample = false;
  bool have_distance_sample = false;
  uint8_t mpu_read_failures = 0U;
  uint32_t last_mpu_init_attempt =
      osKernelGetTickCount() - SENSOR_REINIT_PERIOD_MS;
  uint32_t last_hc_init_attempt =
      osKernelGetTickCount() - SENSOR_REINIT_PERIOD_MS;
  uint32_t last_hc_trigger_attempt =
      osKernelGetTickCount() - HC_TRIGGER_PERIOD_MS;
  uint32_t last_mpu_success = 0U;
  uint32_t last_distance_success = 0U;
  uint32_t last_distance_sequence = 0U;
  memset(&message, 0, sizeof(message));
  for (;;) {
    AppTasks_Heartbeat(APP_TASK_SENSOR);
    uint32_t now = osKernelGetTickCount();

    if (!hc_sr04_ready &&
        ((uint32_t)(now - last_hc_init_attempt) >=
         SENSOR_REINIT_PERIOD_MS)) {
      last_hc_init_attempt = now;
      if (HC_SR04_Init(&htim2) == HAL_OK) {
        hc_sr04_ready = true;
        /* A reinitialized driver starts a new measurement generation. Never
           revive a successful sample produced before the fault. */
        have_distance_sample = false;
        last_distance_sequence = 0U;
        last_hc_trigger_attempt = now - HC_TRIGGER_PERIOD_MS;
      }
    }

    if (hc_sr04_ready) {
      HC_SR04_Service(now);
      if (HC_SR04_GetLatest(&distance_measurement) &&
          (distance_measurement.sequence != last_distance_sequence)) {
        last_distance_sequence = distance_measurement.sequence;
        if (distance_measurement.status == HC_SR04_RESULT_VALID) {
          message.distance_mm = distance_measurement.distance_mm;
          last_distance_success = distance_measurement.completed_at_ms;
          have_distance_sample = true;
        } else {
          /* A newer invalid/timeout result must not leave an older distance
             advertised as safe. */
          have_distance_sample = false;
          if (distance_measurement.status == HC_SR04_RESULT_DRIVER_ERROR) {
            hc_sr04_ready = false;
          }
        }
      }
    }

    if (!mpu_ready &&
        ((uint32_t)(now - last_mpu_init_attempt) >=
         SENSOR_REINIT_PERIOD_MS)) {
      last_mpu_init_attempt = now;
      if (osMutexAcquire(g_i2c1_mutex, 50U) == osOK) {
        mpu_ready = MPU6050_Init(&hi2c1) == HAL_OK;
        mpu_read_failures = 0U;
        (void)osMutexRelease(g_i2c1_mutex);
        next_wake = osKernelGetTickCount();
      }
    }

    if (mpu_ready) {
      if (osMutexAcquire(g_i2c1_mutex, 25U) == osOK) {
        if (MPU6050_Read(&mpu_data) == HAL_OK) {
          memcpy(message.accel_mg, mpu_data.accel_mg, sizeof(message.accel_mg));
          memcpy(message.gyro_mdps, mpu_data.gyro_mdps, sizeof(message.gyro_mdps));
          have_mpu_sample = true;
          mpu_read_failures = 0U;
          last_mpu_success = osKernelGetTickCount();
        } else if (++mpu_read_failures >= 3U) {
          mpu_ready = false;
        }
        (void)osMutexRelease(g_i2c1_mutex);
      }
    }

    now = osKernelGetTickCount();
    if (hc_sr04_ready && !HC_SR04_IsBusy() &&
        ((uint32_t)(now - last_hc_trigger_attempt) >=
         HC_TRIGGER_PERIOD_MS)) {
      const HC_SR04_TriggerResult_t trigger_result = HC_SR04_Trigger(now);
      if ((trigger_result == HC_SR04_TRIGGER_ACCEPTED) ||
          (trigger_result == HC_SR04_TRIGGER_ECHO_HIGH)) {
        /* A stuck-high Echo is retried at a controlled 100 ms rate. */
        last_hc_trigger_attempt = now;
        if (trigger_result == HC_SR04_TRIGGER_ECHO_HIGH) {
          have_distance_sample = false;
        }
      } else if ((trigger_result == HC_SR04_TRIGGER_NOT_INITIALIZED) ||
                 (trigger_result == HC_SR04_TRIGGER_CONTEXT_ERROR) ||
                 (trigger_result == HC_SR04_TRIGGER_TIMER_ERROR)) {
        have_distance_sample = false;
        hc_sr04_ready = false;
      }
    }

    now = osKernelGetTickCount();
    const bool mpu_fresh = have_mpu_sample && mpu_ready &&
        ((uint32_t)(now - last_mpu_success) <= SENSOR_STALE_TIMEOUT_MS);
    const bool distance_fresh = have_distance_sample && hc_sr04_ready &&
        ((uint32_t)(now - last_distance_success) <=
         SENSOR_STALE_TIMEOUT_MS);
    message.timestamp_ms = now;
    message.valid_mask = SENSOR_VALID_NONE;
    if (mpu_fresh) message.valid_mask |= SENSOR_VALID_MPU6050;
    if (distance_fresh) message.valid_mask |= SENSOR_VALID_DISTANCE;

    RobotState_SetError(ROBOT_ERROR_SENSOR,
                        !mpu_fresh || !distance_fresh, now);
    RobotState_UpdateSensor(&message);
    if (xQueueOverwrite((QueueHandle_t)g_sensor_queue, &message) != pdPASS) {
      AppTasks_RecordSensorQueueDrop();
    }
    next_wake += SENSOR_PERIOD_MS;
    if (osDelayUntil(next_wake) != osOK) next_wake = osKernelGetTickCount();
  }
}
