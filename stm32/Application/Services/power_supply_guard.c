#include "power_supply_guard.h"

#include "motor.h"
#include "stm32f1xx_hal.h"

/* Priority 4 deliberately cannot call FreeRTOS APIs. It can pre-empt the
 * priority-5 peripheral IRQs and only performs register-level emergency stop. */
#define POWER_SUPPLY_PVD_IRQ_PRIORITY 4U

volatile bool g_power_supply_fault_latched = true;
static volatile bool guard_initialized;

static void LatchSupplyFault(void)
{
  g_power_supply_fault_latched = true;
  MotorDriver_EmergencyStop();
}

bool PowerSupplyGuard_Init(void)
{
  PWR_PVDTypeDef configuration = {0};
  guard_initialized = false;
  g_power_supply_fault_latched = true;

  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_NVIC_DisableIRQ(PVD_IRQn);
  HAL_PWR_DisablePVD();
  __HAL_PWR_PVD_EXTI_CLEAR_FLAG();
  HAL_NVIC_ClearPendingIRQ(PVD_IRQn);

  configuration.PVDLevel = PWR_PVDLEVEL_7;
  configuration.Mode = PWR_PVD_MODE_IT_RISING;
  HAL_PWR_ConfigPVD(&configuration);
  HAL_PWR_EnablePVD();
  __DSB();
  __ISB();

  guard_initialized = true;
  if ((__HAL_PWR_GET_FLAG(PWR_FLAG_PVDO) != RESET) ||
      (__HAL_PWR_PVD_EXTI_GET_FLAG() != RESET)) {
    LatchSupplyFault();
  } else {
    g_power_supply_fault_latched = false;
  }

  HAL_NVIC_SetPriority(PVD_IRQn, POWER_SUPPLY_PVD_IRQ_PRIORITY, 0U);
  HAL_NVIC_EnableIRQ(PVD_IRQn);
  return !g_power_supply_fault_latched;
}

bool PowerSupplyGuard_IsSafe(void)
{
  if (!guard_initialized) return false;
  if ((__HAL_PWR_GET_FLAG(PWR_FLAG_PVDO) != RESET) ||
      (__HAL_PWR_PVD_EXTI_GET_FLAG() != RESET)) {
    LatchSupplyFault();
  }
  return !g_power_supply_fault_latched;
}

void PVD_IRQHandler(void)
{
  HAL_PWR_PVD_IRQHandler();
}

void HAL_PWR_PVDCallback(void)
{
  /* Only the falling-supply (PVDO rising) edge is enabled. Reaching this
   * callback therefore proves that a brownout happened, even if the rail has
   * already recovered before the ISR reads PVDO. Never discard that event. */
  LatchSupplyFault();
}
