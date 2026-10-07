/* Exercise production AppTasks_Init with only CMSIS resource creation replaced.
 * These fakes never schedule a task or model memory exhaustion. */
#include "../../stm32/Application/Tasks/task_health.c"
#include "../../stm32/Application/Tasks/app_tasks.c"

static int fail_at;
static int creation_step;
static int state_init_calls;
static int arbiter_init_calls;
static uint8_t resource_slots[10];

static void *CreateResource(void)
{
  const int step = ++creation_step;
  return step == fail_at ? NULL : &resource_slots[step - 1];
}

osMessageQueueId_t osMessageQueueNew(uint32_t count, uint32_t size,
                                    const osMessageQueueAttr_t *attributes)
{
  (void)count; (void)size; (void)attributes;
  return CreateResource();
}

osMutexId_t osMutexNew(const osMutexAttr_t *attributes)
{
  (void)attributes;
  return CreateResource();
}

osThreadId_t osThreadNew(void (*entry)(void *), void *argument,
                       const osThreadAttr_t *attributes)
{
  (void)entry; (void)argument; (void)attributes;
  return CreateResource();
}

uint32_t osKernelGetTickCount(void) { return 0U; }
int xQueueOverwrite(QueueHandle_t queue, const void *item)
{ (void)queue; (void)item; return pdPASS; }
uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{ (void)task; return 0U; }
uint32_t xPortGetFreeHeapSize(void) { return 0U; }
uint32_t xPortGetMinimumEverFreeHeapSize(void) { return 0U; }
void RobotState_Init(void) { ++state_init_calls; }
void ControlArbiter_Init(void) { ++arbiter_init_calls; }
uint32_t StorageLog_GetRecordCount(void) { return 0U; }
void MotorDriver_EmergencyStop(void) { }

#define ENTRY(name) void name(void *argument) { (void)argument; }
ENTRY(SensorTask_Entry)
ENTRY(MotorTask_Entry)
ENTRY(BatteryTask_Entry)
ENTRY(CanTask_Entry)
ENTRY(CommunicationTask_Entry)
ENTRY(DisplayTask_Entry)
ENTRY(StorageTask_Entry)

int AppTasksInitChecks_Run(void)
{
  for (int failed_resource = 1; failed_resource <= 10; ++failed_resource)
  {
    fail_at = failed_resource;
    creation_step = 0;
    state_init_calls = arbiter_init_calls = 0;
    g_sensor_queue = g_motor_command_queue = NULL;
    g_i2c1_mutex = NULL;
    if (AppTasks_Init()) return __LINE__;
    if (failed_resource >= 4 &&
        (state_init_calls != 1 || arbiter_init_calls != 1)) return __LINE__;
  }

  fail_at = 0;
  creation_step = 0;
  if (!AppTasks_Init() || creation_step != 10) return __LINE__;
  return 0;
}
