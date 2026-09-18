#include "motor.h"

#include "main.h"
#include "power_supply_guard.h"
#include "tim.h"

/* Change these after the first wheels-off-ground polarity test if required. */
#define MOTOR_LEFT_INVERTED  0
#define MOTOR_RIGHT_INVERTED 0

static MotorDriverState_t motor_state;
volatile bool g_motor_emergency_stop_latched;

static uint32_t EnterAllInterruptCritical(void)
{
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  __DSB();
  return primask;
}

static void ExitAllInterruptCritical(uint32_t primask)
{
  __DSB();
  if (primask == 0U) __enable_irq();
}

static void ForceHardwareOff(void)
{
  if (__HAL_RCC_GPIOA_IS_CLK_ENABLED()) {
    TB6612_STBY_GPIO_Port->BRR = TB6612_STBY_Pin;
  }
  if (__HAL_RCC_TIM3_IS_CLK_ENABLED()) {
    TIM3->CCR1 = 0U;
    TIM3->CCR2 = 0U;
  }
  if (__HAL_RCC_GPIOB_IS_CLK_ENABLED()) {
    TB6612_AIN1_GPIO_Port->BRR = TB6612_AIN1_Pin | TB6612_AIN2_Pin |
                                 TB6612_BIN1_Pin | TB6612_BIN2_Pin;
  }
  motor_state.left_output_permille = 0;
  motor_state.right_output_permille = 0;
  motor_state.stop_mode = MOTOR_STOP_STANDBY;
  motor_state.enabled = false;
}

static int16_t ClampOutput(int32_t output)
{
  if (output > MOTOR_OUTPUT_MAX_PERMILLE) return MOTOR_OUTPUT_MAX_PERMILLE;
  if (output < -MOTOR_OUTPUT_MAX_PERMILLE) return -MOTOR_OUTPUT_MAX_PERMILLE;
  return (int16_t)output;
}

static int8_t OutputSign(int16_t output)
{
  if (output > 0) return 1;
  if (output < 0) return -1;
  return 0;
}

static uint16_t OutputToCompare(int16_t output)
{
  uint32_t magnitude = (output < 0) ? (uint32_t)(-output) : (uint32_t)output;
  const uint32_t period_counts = __HAL_TIM_GET_AUTORELOAD(&htim3) + 1U;
  return (uint16_t)((period_counts * magnitude) / MOTOR_OUTPUT_MAX_PERMILLE);
}

static void SetLeftDirection(int16_t output)
{
  bool forward = output > 0;
#if MOTOR_LEFT_INVERTED
  forward = !forward;
#endif
  if (output == 0) {
    HAL_GPIO_WritePin(TB6612_AIN1_GPIO_Port, TB6612_AIN1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TB6612_AIN2_GPIO_Port, TB6612_AIN2_Pin, GPIO_PIN_RESET);
  } else {
    HAL_GPIO_WritePin(TB6612_AIN1_GPIO_Port, TB6612_AIN1_Pin,
                      forward ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TB6612_AIN2_GPIO_Port, TB6612_AIN2_Pin,
                      forward ? GPIO_PIN_RESET : GPIO_PIN_SET);
  }
}

static void SetRightDirection(int16_t output)
{
  bool forward = output > 0;
#if MOTOR_RIGHT_INVERTED
  forward = !forward;
#endif
  if (output == 0) {
    HAL_GPIO_WritePin(TB6612_BIN1_GPIO_Port, TB6612_BIN1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TB6612_BIN2_GPIO_Port, TB6612_BIN2_Pin, GPIO_PIN_RESET);
  } else {
    HAL_GPIO_WritePin(TB6612_BIN1_GPIO_Port, TB6612_BIN1_Pin,
                      forward ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TB6612_BIN2_GPIO_Port, TB6612_BIN2_Pin,
                      forward ? GPIO_PIN_RESET : GPIO_PIN_SET);
  }
}

HAL_StatusTypeDef MotorDriver_Init(void)
{
  if (g_motor_emergency_stop_latched || !PowerSupplyGuard_IsSafe()) {
    ForceHardwareOff();
    return HAL_ERROR;
  }
  HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin, GPIO_PIN_RESET);
  SetLeftDirection(0);
  SetRightDirection(0);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0U);

  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) != HAL_OK) return HAL_ERROR;
  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2) != HAL_OK) {
    (void)HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
    return HAL_ERROR;
  }

  motor_state.left_output_permille = 0;
  motor_state.right_output_permille = 0;
  motor_state.stop_mode = MOTOR_STOP_STANDBY;
  motor_state.initialized = true;
  motor_state.enabled = false;
  return HAL_OK;
}

bool MotorDriver_SetOutput(int16_t left_permille, int16_t right_permille)
{
  if (!motor_state.initialized) return false;
  left_permille = ClampOutput(left_permille);
  right_permille = ClampOutput(right_permille);

  if ((left_permille == 0) && (right_permille == 0)) {
    MotorDriver_Stop(MOTOR_STOP_STANDBY);
    return true;
  }

  /* PVD is priority 4 and can pre-empt every RTOS-aware peripheral IRQ. Mask
   * all maskable IRQs across the final safety check and bridge enable writes,
   * so an emergency-stop ISR can never return into code that re-enables STBY. */
  const uint32_t primask = EnterAllInterruptCritical();
  if (g_motor_emergency_stop_latched || !PowerSupplyGuard_IsSafe()) {
    ForceHardwareOff();
    ExitAllInterruptCritical(primask);
    return false;
  }

  const bool direction_changed =
      (OutputSign(left_permille) != OutputSign(motor_state.left_output_permille)) ||
      (OutputSign(right_permille) != OutputSign(motor_state.right_output_permille));

  if (direction_changed) {
    /* Disable the bridge before changing either H-bridge direction. */
    HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin, GPIO_PIN_RESET);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0U);
    SetLeftDirection(left_permille);
    SetRightDirection(right_permille);
  }
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, OutputToCompare(left_permille));
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, OutputToCompare(right_permille));
  if (g_motor_emergency_stop_latched || !PowerSupplyGuard_IsSafe()) {
    ForceHardwareOff();
    ExitAllInterruptCritical(primask);
    return false;
  }
  if (direction_changed || !motor_state.enabled) {
    HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin, GPIO_PIN_SET);
  }
  if (g_motor_emergency_stop_latched || !PowerSupplyGuard_IsSafe()) {
    ForceHardwareOff();
    ExitAllInterruptCritical(primask);
    return false;
  }

  motor_state.left_output_permille = left_permille;
  motor_state.right_output_permille = right_permille;
  motor_state.stop_mode = MOTOR_STOP_COAST;
  motor_state.enabled = true;
  ExitAllInterruptCritical(primask);
  return true;
}

void MotorDriver_Drive(int16_t linear_permille, int16_t turn_left_permille)
{
  int32_t left = (int32_t)ClampOutput(linear_permille) - ClampOutput(turn_left_permille);
  int32_t right = (int32_t)ClampOutput(linear_permille) + ClampOutput(turn_left_permille);
  int32_t maximum = (left < 0) ? -left : left;
  const int32_t right_abs = (right < 0) ? -right : right;
  if (right_abs > maximum) maximum = right_abs;
  if (maximum > MOTOR_OUTPUT_MAX_PERMILLE) {
    left = (left * MOTOR_OUTPUT_MAX_PERMILLE) / maximum;
    right = (right * MOTOR_OUTPUT_MAX_PERMILLE) / maximum;
  }
  (void)MotorDriver_SetOutput((int16_t)left, (int16_t)right);
}

void MotorDriver_Stop(MotorStopMode_t mode)
{
  const uint32_t primask = EnterAllInterruptCritical();
  if (!motor_state.initialized) {
    ForceHardwareOff();
    ExitAllInterruptCritical(primask);
    return;
  }

  if (g_motor_emergency_stop_latched ||
      ((mode != MOTOR_STOP_STANDBY) && !PowerSupplyGuard_IsSafe())) {
    ForceHardwareOff();
    ExitAllInterruptCritical(primask);
    return;
  }

  HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin, GPIO_PIN_RESET);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0U);

  if (mode == MOTOR_STOP_BRAKE) {
    HAL_GPIO_WritePin(TB6612_AIN1_GPIO_Port, TB6612_AIN1_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TB6612_AIN2_GPIO_Port, TB6612_AIN2_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TB6612_BIN1_GPIO_Port, TB6612_BIN1_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TB6612_BIN2_GPIO_Port, TB6612_BIN2_Pin, GPIO_PIN_SET);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1,
                          __HAL_TIM_GET_AUTORELOAD(&htim3) + 1U);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2,
                          __HAL_TIM_GET_AUTORELOAD(&htim3) + 1U);
    HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin, GPIO_PIN_SET);
    motor_state.enabled = true;
  } else {
    SetLeftDirection(0);
    SetRightDirection(0);
    if (mode == MOTOR_STOP_COAST) {
      /* TB6612 coast requires IN1=IN2=L and PWM=H. PWM=L is short brake. */
      __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1,
                            __HAL_TIM_GET_AUTORELOAD(&htim3) + 1U);
      __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2,
                            __HAL_TIM_GET_AUTORELOAD(&htim3) + 1U);
      HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin, GPIO_PIN_SET);
      motor_state.enabled = true;
    } else {
      motor_state.enabled = false;
    }
  }

  if ((mode != MOTOR_STOP_STANDBY) &&
      (g_motor_emergency_stop_latched || !PowerSupplyGuard_IsSafe())) {
    ForceHardwareOff();
    ExitAllInterruptCritical(primask);
    return;
  }

  motor_state.left_output_permille = 0;
  motor_state.right_output_permille = 0;
  motor_state.stop_mode = mode;
  ExitAllInterruptCritical(primask);
}

void MotorDriver_EmergencyStop(void)
{
  /* Keep this path independent of RTOS state, HAL locks and initialization. */
  g_motor_emergency_stop_latched = true;
  ForceHardwareOff();
}

MotorDriverState_t MotorDriver_GetState(void)
{
  const uint32_t primask = EnterAllInterruptCritical();
  const MotorDriverState_t state = motor_state;
  ExitAllInterruptCritical(primask);
  return state;
}
