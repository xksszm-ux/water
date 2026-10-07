/* Production resource creation and health snapshot boundary checks.
 * The injected preemption order is sequential, not a real scheduler. */
#include "FreeRTOS.h"
static void EnterCritical(void);
static void ExitCritical(void);
#undef taskENTER_CRITICAL
#undef taskEXIT_CRITICAL
#define taskENTER_CRITICAL() EnterCritical()
#define taskEXIT_CRITICAL() ExitCritical()
#include "../../stm32/Application/Tasks/task_health.c"
#include "../../stm32/Application/Tasks/app_tasks.c"

static int fail_at;
static int creation_step;
static int state_init_calls;
static int arbiter_init_calls;
static uint8_t resource_slots[10];
static uint32_t current_tick;
static unsigned critical_depth;
static unsigned unlocked_time_reads;
static bool require_locked_time;
static bool inject_motor_poll;

static void EnterCritical(void)
{
  if (critical_depth == 0U && inject_motor_poll) {
    /* Motor runs just before Comm actually masks the RTOS-aware interrupts. */
    inject_motor_poll = false;
    ++current_tick;
    AppTasks_Heartbeat(APP_TASK_MOTOR);
    (void)AppTasks_HealthPoll();
  }
  ++critical_depth;
}

static void ExitCritical(void) { --critical_depth; }

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

uint32_t osKernelGetTickCount(void)
{
  if (require_locked_time && critical_depth == 0U) ++unlocked_time_reads;
  return current_tick;
}
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
  current_tick = 0U;
  require_locked_time = inject_motor_poll = false;
  critical_depth = unlocked_time_reads = 0U;
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

  TaskHealth_Init(&task_health, 0U);
  for (unsigned i = 0U; i < APP_TASK_COUNT; ++i) task_heartbeat[i] = 1U;
  current_tick = 4990U;
  require_locked_time = true;
  if (AppTasks_HealthPoll() != 0U) return __LINE__;
  current_tick = 5000U;
  inject_motor_poll = true;
  RtosDiagnostics_t diagnostics;
  AppTasks_GetDiagnostics(&diagnostics);
  if (diagnostics.health_fault_mask != 0U ||
      task_health.last_progress_ms[APP_TASK_MOTOR] != 5001U ||
      unlocked_time_reads != 0U || critical_depth != 0U) return __LINE__;
  /* A genuine late heartbeat still faults and remains latched on recovery. */
  current_tick = 5101U;
  if (AppTasks_HealthPoll() != 0U) return __LINE__;
  current_tick = 5102U;
  if (AppTasks_HealthPoll() != (1U << APP_TASK_MOTOR)) return __LINE__;
  AppTasks_Heartbeat(APP_TASK_MOTOR);
  if (AppTasks_HealthPoll() != (1U << APP_TASK_MOTOR)) return __LINE__;

  TaskHealth_Init(&task_health, UINT32_MAX - 3100U);
  current_tick = UINT32_MAX - 1U;
  if (AppTasks_HealthPoll() != 0U) return __LINE__;
  current_tick = UINT32_MAX;
  inject_motor_poll = true;
  AppTasks_GetDiagnostics(&diagnostics);
  if (diagnostics.health_fault_mask != 0U || current_tick != 0U ||
      unlocked_time_reads != 0U || critical_depth != 0U) return __LINE__;
  require_locked_time = false;
  return 0;
}
