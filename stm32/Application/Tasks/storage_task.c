#include "app_tasks.h"
#include "robot_state.h"
#include "spi.h"
#include "storage_log.h"
#include "task_entries.h"
#include "w25q64.h"

void StorageTask_Entry(void *argument)
{
  (void)argument;
  SensorMessage_t sensor;
  RobotStatus_t snapshot;
  bool storage_ready = false;
  uint32_t last_sensor_received = 0U;
  uint32_t last_init_attempt = osKernelGetTickCount() - 1000U;
  uint32_t last_log_time = osKernelGetTickCount();
  for (;;) {
    AppTasks_Heartbeat(APP_TASK_STORAGE);
    const uint32_t now = osKernelGetTickCount();
    if (!storage_ready && ((uint32_t)(now - last_init_attempt) >= 1000U)) {
      last_init_attempt = now;
      storage_ready = (W25Q64_Init(&hspi2) == HAL_OK) &&
                      (StorageLog_Init() == HAL_OK);
      if (storage_ready) last_log_time = now;
    }

    if (osMessageQueueGet(g_sensor_queue, &sensor, NULL, 50U) == osOK) {
      last_sensor_received = sensor.timestamp_ms;
    }

    const uint32_t current_time = osKernelGetTickCount();
    RobotState_InvalidateSensorIfStale(current_time);
    RobotState_GetSnapshot(&snapshot);

    if (storage_ready && (last_sensor_received != 0U) &&
        ((uint32_t)(current_time - last_sensor_received) <= 1000U) &&
        ((uint32_t)(osKernelGetTickCount() - last_log_time) >= 5000U)) {
      if (StorageLog_Append(&snapshot) == HAL_OK) {
        last_log_time = osKernelGetTickCount();
      } else {
        storage_ready = false;
      }
    }
    RobotState_SetError(ROBOT_ERROR_STORAGE, !storage_ready,
                        osKernelGetTickCount());
  }
}
