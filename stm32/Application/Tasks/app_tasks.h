#ifndef APP_TASKS_H
#define APP_TASKS_H

#include <stdbool.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "robot_state.h"

extern osMessageQueueId_t g_sensor_queue;
extern osMessageQueueId_t g_motor_command_queue;
extern osMutexId_t g_i2c1_mutex;

extern osThreadId_t g_sensor_task_handle;
extern osThreadId_t g_motor_task_handle;
extern osThreadId_t g_battery_task_handle;
extern osThreadId_t g_can_task_handle;
extern osThreadId_t g_communication_task_handle;
extern osThreadId_t g_display_task_handle;
extern osThreadId_t g_storage_task_handle;

typedef struct {
  uint32_t free_heap_bytes;
  uint32_t minimum_free_heap_bytes;
  uint32_t sensor_stack_free_bytes;
  uint32_t motor_stack_free_bytes;
  uint32_t battery_stack_free_bytes;
  uint32_t can_stack_free_bytes;
  uint32_t communication_stack_free_bytes;
  uint32_t display_stack_free_bytes;
  uint32_t storage_stack_free_bytes;
  uint32_t sensor_queue_drop_count;
  uint32_t storage_record_count;
  uint32_t heartbeat[7];
} RtosDiagnostics_t;

typedef enum {
  APP_TASK_SENSOR = 0,
  APP_TASK_MOTOR,
  APP_TASK_BATTERY,
  APP_TASK_CAN,
  APP_TASK_COMMUNICATION,
  APP_TASK_DISPLAY,
  APP_TASK_STORAGE,
  APP_TASK_COUNT
} AppTaskId_t;

bool AppTasks_Init(void);

typedef enum {
  APP_MOTOR_STOP_USER = (1U << 0),
  APP_MOTOR_STOP_SOURCE_FAULT = (1U << 1),
  APP_MOTOR_STOP_HANDOFF = (1U << 2)
} AppMotorStopReason_t;

/* Internal sink used by the source-aware control arbiter and disabled tests. */
bool AppTasks_SubmitMotorCommandInternal(const MotorCommand_t *command);
void AppTasks_RequestMotorStop(uint8_t reasons);
uint8_t AppTasks_ConsumeMotorStopRequest(void);
void AppTasks_GetDiagnostics(RtosDiagnostics_t *diagnostics);
void AppTasks_Heartbeat(AppTaskId_t task_id);
void AppTasks_RecordSensorQueueDrop(void);

#endif
