#include <string.h>
#include "../../stm32/Drivers/Motor/motor.c"
#include "../../stm32/Application/Services/Power/power_supply_guard.c"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
GPIO_TypeDef mock_gpio_a, mock_gpio_b;
TIM_TypeDef mock_tim3;
TIM_HandleTypeDef htim3 = {.Instance=TIM3};
uint32_t mock_primask, mock_pvdo, mock_pvd_pending;
bool mock_gpio_a_clock, mock_gpio_b_clock, mock_tim3_clock;
static unsigned pwm_fail, pwm_calls, pwm_stops, irq_priority, pvd_mode;
static bool pending_irq, inject_supply_drop, unsafe_write;
void MockEnableIrq(void) {
  mock_primask=0;
  if (pending_irq) { pending_irq=false; HAL_PWR_PVDCallback(); }
}
void MockSetCompare(TIM_HandleTypeDef *t,uint32_t ch,uint32_t value) {
  if (motor_state.initialized && !mock_primask) unsafe_write=true;
  if (ch==TIM_CHANNEL_1) t->Instance->CCR1=value; else t->Instance->CCR2=value;
  if (inject_supply_drop && value) { inject_supply_drop=false; mock_pvdo=1; }
}
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,GPIO_PinState level) {
  if (motor_state.initialized && !mock_primask) unsafe_write=true;
  if (level) p->ODR |= pin; else p->ODR &= ~(uint32_t)pin;
}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *t,uint32_t ch) {
  (void)t; (void)ch; return ++pwm_calls==pwm_fail ? HAL_ERROR:HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *t,uint32_t ch) {
  (void)t; (void)ch; ++pwm_stops; return HAL_OK;
}
void HAL_NVIC_DisableIRQ(int i) { (void)i; }
void HAL_NVIC_EnableIRQ(int i) { (void)i; }
void HAL_NVIC_ClearPendingIRQ(int i) { (void)i; }
void HAL_NVIC_SetPriority(int i,uint32_t p,uint32_t s) {(void)i;(void)s;irq_priority=p;}
void HAL_PWR_DisablePVD(void) {}
void HAL_PWR_EnablePVD(void) {}
void HAL_PWR_ConfigPVD(const PWR_PVDTypeDef *p) {pvd_mode=p->Mode;}
void HAL_PWR_PVD_IRQHandler(void) { if(mock_pvd_pending) {mock_pvd_pending=0;HAL_PWR_PVDCallback();} }
static void Reset(void) {
  memset(&motor_state,0,sizeof(motor_state));
  memset(&mock_gpio_a,0,sizeof(mock_gpio_a)); memset(&mock_gpio_b,0,sizeof(mock_gpio_b));
  memset(&mock_tim3,0,sizeof(mock_tim3)); mock_tim3.ARR=999;
  mock_primask=mock_pvdo=mock_pvd_pending=0;
  mock_gpio_a_clock=mock_gpio_b_clock=mock_tim3_clock=true;
  guard_initialized=false; g_power_supply_fault_latched=true; g_motor_emergency_stop_latched=false;
  pwm_fail=pwm_calls=pwm_stops=0; unsafe_write=inject_supply_drop=pending_irq=false;
}
int PowerMotorChecks_Run(void) {
  Reset(); CHECK(!PowerSupplyGuard_IsSafe()); CHECK(MotorDriver_Init()==HAL_ERROR);
  CHECK(mock_gpio_a.BRR==TB6612_STBY_Pin && mock_tim3.CCR1==0);
  Reset(); mock_gpio_a_clock=mock_gpio_b_clock=mock_tim3_clock=false;
  MotorDriver_EmergencyStop(); CHECK(g_motor_emergency_stop_latched && mock_gpio_a.BRR==0);
  Reset(); mock_pvdo=1; CHECK(!PowerSupplyGuard_Init()); mock_pvdo=0;
  CHECK(!PowerSupplyGuard_IsSafe() && MotorDriver_Init()==HAL_ERROR);
  for (unsigned fail=1;fail<=2;++fail) {
    Reset(); CHECK(PowerSupplyGuard_Init()); pwm_fail=fail;
    CHECK(MotorDriver_Init()==HAL_ERROR && !motor_state.initialized);
    CHECK(pwm_stops==(fail==2 ? 1U:0U)); CHECK(!MotorDriver_SetOutput(1,1));
  }
  Reset(); CHECK(PowerSupplyGuard_Init() && irq_priority==4 && pvd_mode==PWR_PVD_MODE_IT_RISING);
  CHECK(MotorDriver_Init()==HAL_OK); CHECK(MotorDriver_SetOutput(2000,-2000));
  CHECK(mock_tim3.CCR1==1000 && mock_tim3.CCR2==1000 && mock_primask==0 && !unsafe_write);
  CHECK(MotorDriver_GetState().left_output_permille==1000);
  MotorDriver_Drive(1000,1000); CHECK(motor_state.left_output_permille==0 && motor_state.right_output_permille==1000);
  MotorDriver_Stop(MOTOR_STOP_BRAKE); CHECK(mock_tim3.CCR1==1000 && motor_state.enabled);
  MotorDriver_Stop(MOTOR_STOP_COAST); CHECK(mock_tim3.CCR2==1000 && motor_state.enabled);
  CHECK(MotorDriver_SetOutput(0,0)); CHECK(!motor_state.enabled && mock_tim3.CCR1==0);
  mock_primask=1; CHECK(MotorDriver_SetOutput(200,300)); CHECK(mock_primask==1); mock_primask=0;
  inject_supply_drop=true; CHECK(!MotorDriver_SetOutput(400,400)); CHECK(g_motor_emergency_stop_latched);
  CHECK(mock_tim3.CCR1==0 && mock_tim3.CCR2==0 && mock_gpio_a.BRR==TB6612_STBY_Pin);
  mock_pvdo=0; CHECK(!MotorDriver_SetOutput(400,400)); MotorDriver_Stop(MOTOR_STOP_BRAKE);
  CHECK(!motor_state.enabled && mock_tim3.CCR1==0);
  Reset(); CHECK(PowerSupplyGuard_Init() && MotorDriver_Init()==HAL_OK);
  /* Pending priority-4 IRQ is delivered on PRIMASK restoration, not inside it. */
  pending_irq=true; CHECK(MotorDriver_SetOutput(500,500));
  CHECK(g_motor_emergency_stop_latched && !motor_state.enabled && mock_tim3.CCR1==0);
  Reset(); CHECK(PowerSupplyGuard_Init() && MotorDriver_Init()==HAL_OK);
  CHECK(MotorDriver_SetOutput(500,500)); mock_pvd_pending=1; PVD_IRQHandler();
  CHECK(!PowerSupplyGuard_IsSafe() && !motor_state.enabled && mock_tim3.CCR2==0);
  CHECK(!unsafe_write); return 0;
}
