/* Compile the production CubeMX bootstrap against an injected RTOS boundary.
 * This checks the error decision only; it does not simulate scheduling. */
#define Error_Handler StartupTest_Error_Handler
#include "../../stm32/Core/Src/freertos.c"
#undef Error_Handler

static int fail_creation;
static int fail_app_init;
static int error_calls;
static int kernel_ready;
static int app_init_calls;

osKernelState_t osKernelGetState(void) { return kernel_ready ? osKernelReady : 0; }

osThreadId_t osThreadNew(void (*entry)(void *), void *argument,
                       const osThreadAttr_t *attributes)
{
  (void)entry;
  (void)argument;
  (void)attributes;
  return fail_creation ? NULL : (osThreadId_t)&defaultTaskControlBlock;
}

bool AppTasks_Init(void) { ++app_init_calls; return !fail_app_init; }
void osThreadExit(void) { }
void StartupTest_Error_Handler(void) { ++error_calls; }

int StartupChecks_Run(void)
{
  error_calls = 0;
  kernel_ready = 1;
  fail_creation = 1;
  fail_app_init = 0;
  app_init_calls = 0;
  MX_FREERTOS_Init();
  /* The host Error_Handler stub returns; the device handler never does. */
  if (defaultTaskHandle != NULL || error_calls != 1)
    return __LINE__;

  error_calls = 0;
  fail_creation = 0;
  app_init_calls = 0;
  MX_FREERTOS_Init();
  if (defaultTaskHandle == NULL || error_calls != 0 || app_init_calls != 1)
    return __LINE__;

  error_calls = 0;
  fail_app_init = 1;
  app_init_calls = 0;
  MX_FREERTOS_Init();
  if (error_calls != 1 || app_init_calls != 1) return __LINE__;

  error_calls = 0;
  kernel_ready = 0;
  fail_app_init = 0;
  MX_FREERTOS_Init();
  if (error_calls != 1) return __LINE__;
  return 0;
}
