#include "app_tasks.h"
#include "control_arbiter.h"
#include "motor.h"
#include "storage_log.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "task_entries.h"

osMessageQueueId_t g_sensor_queue;
osMessageQueueId_t g_motor_command_queue;
osMutexId_t g_i2c1_mutex;
osThreadId_t g_sensor_task_handle;
osThreadId_t g_motor_task_handle;
osThreadId_t g_battery_task_handle;
osThreadId_t g_can_task_handle;
osThreadId_t g_communication_task_handle;
osThreadId_t g_display_task_handle;
osThreadId_t g_storage_task_handle;
static volatile uint32_t task_heartbeat[APP_TASK_COUNT];
static volatile uint32_t sensor_queue_drop_count;
static volatile uint8_t motor_stop_reasons;

#define TASK_STORAGE(name, bytes) static StaticTask_t name##_control; static StackType_t name##_stack[(bytes) / sizeof(StackType_t)]
TASK_STORAGE(sensor, 512U);
TASK_STORAGE(motor, 512U);
TASK_STORAGE(battery, 512U);
TASK_STORAGE(can, 512U);
TASK_STORAGE(communication, 640U);
TASK_STORAGE(display, 512U);
TASK_STORAGE(storage, 512U);

static StaticQueue_t sensor_queue_control;
static uint8_t sensor_queue_storage[sizeof(SensorMessage_t)];
static StaticQueue_t motor_queue_control;
static uint8_t motor_queue_storage[sizeof(MotorCommand_t)];
static StaticSemaphore_t i2c1_mutex_control;

#define THREAD_ATTR(stem, label, prio) static const osThreadAttr_t stem##_attributes = { .name = label, .cb_mem = &stem##_control, .cb_size = sizeof(stem##_control), .stack_mem = stem##_stack, .stack_size = sizeof(stem##_stack), .priority = prio }
THREAD_ATTR(sensor, "SensorTask", osPriorityNormal);
THREAD_ATTR(motor, "MotorTask", osPriorityAboveNormal);
THREAD_ATTR(battery, "BatteryTask", osPriorityBelowNormal);
THREAD_ATTR(can, "CanTask", osPriorityNormal);
THREAD_ATTR(communication, "CommTask", osPriorityNormal);
THREAD_ATTR(display, "DisplayTask", osPriorityLow);
THREAD_ATTR(storage, "StorageTask", osPriorityLow);

static bool CreateQueues(void)
{
  const osMessageQueueAttr_t sensor_attr = { .name = "SensorQueue", .cb_mem = &sensor_queue_control, .cb_size = sizeof(sensor_queue_control), .mq_mem = sensor_queue_storage, .mq_size = sizeof(sensor_queue_storage) };
  const osMessageQueueAttr_t motor_attr = { .name = "MotorCmdQueue", .cb_mem = &motor_queue_control, .cb_size = sizeof(motor_queue_control), .mq_mem = motor_queue_storage, .mq_size = sizeof(motor_queue_storage) };
  g_sensor_queue = osMessageQueueNew(1U, sizeof(SensorMessage_t), &sensor_attr);
  g_motor_command_queue = osMessageQueueNew(1U, sizeof(MotorCommand_t), &motor_attr);
  return (g_sensor_queue != NULL) && (g_motor_command_queue != NULL);
}

static bool CreateMutexes(void)
{
  const osMutexAttr_t i2c1_attr = {
    .name = "I2C1Mutex",
    .cb_mem = &i2c1_mutex_control,
    .cb_size = sizeof(i2c1_mutex_control)
  };
  g_i2c1_mutex = osMutexNew(&i2c1_attr);
  return g_i2c1_mutex != NULL;
}

bool AppTasks_Init(void)
{
  if (!CreateQueues() || !CreateMutexes()) return false;
  motor_stop_reasons = 0U;
  RobotState_Init();
  ControlArbiter_Init();
  g_sensor_task_handle = osThreadNew(SensorTask_Entry, NULL, &sensor_attributes);
  g_motor_task_handle = osThreadNew(MotorTask_Entry, NULL, &motor_attributes);
  g_battery_task_handle = osThreadNew(BatteryTask_Entry, NULL, &battery_attributes);
  g_can_task_handle = osThreadNew(CanTask_Entry, NULL, &can_attributes);
  g_communication_task_handle = osThreadNew(CommunicationTask_Entry, NULL, &communication_attributes);
  g_display_task_handle = osThreadNew(DisplayTask_Entry, NULL, &display_attributes);
  g_storage_task_handle = osThreadNew(StorageTask_Entry, NULL, &storage_attributes);
  return (g_sensor_task_handle != NULL) && (g_motor_task_handle != NULL) &&
         (g_battery_task_handle != NULL) && (g_can_task_handle != NULL) &&
         (g_communication_task_handle != NULL) && (g_display_task_handle != NULL) &&
         (g_storage_task_handle != NULL);
}

bool AppTasks_SubmitMotorCommandInternal(const MotorCommand_t *command)
{
  if ((command == NULL) || (g_motor_command_queue == NULL)) return false;
  MotorCommand_t latest = *command;
  if ((latest.mode != ROBOT_MODE_BLE) && (latest.mode != ROBOT_MODE_AUTO)) return false;
  if (latest.left_output_permille > MOTOR_OUTPUT_MAX_PERMILLE) {
    latest.left_output_permille = MOTOR_OUTPUT_MAX_PERMILLE;
  } else if (latest.left_output_permille < -MOTOR_OUTPUT_MAX_PERMILLE) {
    latest.left_output_permille = -MOTOR_OUTPUT_MAX_PERMILLE;
  }
  if (latest.right_output_permille > MOTOR_OUTPUT_MAX_PERMILLE) {
    latest.right_output_permille = MOTOR_OUTPUT_MAX_PERMILLE;
  } else if (latest.right_output_permille < -MOTOR_OUTPUT_MAX_PERMILLE) {
    latest.right_output_permille = -MOTOR_OUTPUT_MAX_PERMILLE;
  }
  latest.issued_at_ms = osKernelGetTickCount();
  if (!latest.enable ||
      ((latest.left_output_permille == 0) &&
       (latest.right_output_permille == 0))) {
    AppTasks_RequestMotorStop(APP_MOTOR_STOP_USER);
  }
  return xQueueOverwrite((QueueHandle_t)g_motor_command_queue, &latest) == pdPASS;
}

void AppTasks_RequestMotorStop(uint8_t reasons)
{
  if (reasons == 0U) return;
  taskENTER_CRITICAL();
  motor_stop_reasons |= reasons;
  taskEXIT_CRITICAL();
}

uint8_t AppTasks_ConsumeMotorStopRequest(void)
{
  uint8_t reasons;
  taskENTER_CRITICAL();
  reasons = motor_stop_reasons;
  motor_stop_reasons = 0U;
  taskEXIT_CRITICAL();
  return reasons;
}

static uint32_t StackFreeBytes(osThreadId_t handle)
{
  if (handle == NULL) return 0U;
  return (uint32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)handle) * sizeof(StackType_t);
}

void AppTasks_GetDiagnostics(RtosDiagnostics_t *diagnostics)
{
  if (diagnostics == NULL) return;
  diagnostics->free_heap_bytes = (uint32_t)xPortGetFreeHeapSize();
  diagnostics->minimum_free_heap_bytes = (uint32_t)xPortGetMinimumEverFreeHeapSize();
  diagnostics->sensor_stack_free_bytes = StackFreeBytes(g_sensor_task_handle);
  diagnostics->motor_stack_free_bytes = StackFreeBytes(g_motor_task_handle);
  diagnostics->battery_stack_free_bytes = StackFreeBytes(g_battery_task_handle);
  diagnostics->can_stack_free_bytes = StackFreeBytes(g_can_task_handle);
  diagnostics->communication_stack_free_bytes = StackFreeBytes(g_communication_task_handle);
  diagnostics->display_stack_free_bytes = StackFreeBytes(g_display_task_handle);
  diagnostics->storage_stack_free_bytes = StackFreeBytes(g_storage_task_handle);
  diagnostics->storage_record_count = StorageLog_GetRecordCount();
  taskENTER_CRITICAL();
  diagnostics->sensor_queue_drop_count = sensor_queue_drop_count;
  for (uint32_t index = 0U; index < APP_TASK_COUNT; ++index) {
    diagnostics->heartbeat[index] = task_heartbeat[index];
  }
  taskEXIT_CRITICAL();
}

void AppTasks_RecordSensorQueueDrop(void)
{
  taskENTER_CRITICAL();
  ++sensor_queue_drop_count;
  taskEXIT_CRITICAL();
}

void AppTasks_Heartbeat(AppTaskId_t task_id)
{
  if (task_id >= APP_TASK_COUNT) return;
  taskENTER_CRITICAL();
  ++task_heartbeat[task_id];
  taskEXIT_CRITICAL();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
  (void)task;
  (void)task_name;
  MotorDriver_EmergencyStop();
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}

void vApplicationMallocFailedHook(void)
{
  MotorDriver_EmergencyStop();
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}

void vApplicationAssertFailed(void)
{
  MotorDriver_EmergencyStop();
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}
