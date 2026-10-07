#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Sequential register/API boundary: not an electrical or NVIC simulation. */
typedef int HAL_StatusTypeDef;
enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT, RESET=0 };
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { volatile uint32_t BRR, ODR, IDR; } GPIO_TypeDef;
typedef struct { volatile uint32_t CR1, CCR1, CCR2, ARR, CNT, DIER, SR; } TIM_TypeDef;
typedef struct { TIM_TypeDef *Instance; struct { uint32_t Period, Prescaler; } Init; uint32_t Channel; } TIM_HandleTypeDef;
typedef struct { volatile uint32_t CFGR; } RCC_TypeDef;
typedef struct { volatile uint32_t IMR, PR; } EXTI_TypeDef;
typedef struct { uint32_t State; } ADC_HandleTypeDef;
typedef struct { uint32_t Channel, Rank, SamplingTime; } ADC_ChannelConfTypeDef;
typedef struct { int unused; } I2C_HandleTypeDef;
typedef struct { int unused; } SPI_HandleTypeDef;
typedef struct { uint32_t PVDLevel, Mode; } PWR_PVDTypeDef;
extern GPIO_TypeDef mock_gpio_a, mock_gpio_b;
extern TIM_TypeDef mock_tim2, mock_tim3;
extern RCC_TypeDef mock_rcc;
extern EXTI_TypeDef mock_exti;
extern uint32_t mock_primask, mock_basepri, mock_ipsr, mock_pvdo, mock_pvd_pending;
extern bool mock_gpio_a_clock, mock_gpio_b_clock, mock_tim3_clock;
#define TIM2 (&mock_tim2)
#define TIM3 (&mock_tim3)
#define RCC (&mock_rcc)
#define EXTI (&mock_exti)
enum { TIM_CHANNEL_1=1, TIM_CHANNEL_2=2, TIM_IT_CC1=2, TIM_IT_CC2=4,
 TIM_FLAG_CC1=2, TIM_FLAG_CC2=4, TIM_CR1_CEN=1, HAL_TIM_ACTIVE_CHANNEL_2=2,
 RCC_CFGR_PPRE1=0x700, PVD_IRQn=1, TIM2_IRQn=2, PWR_PVDLEVEL_7=7,
 PWR_PVD_MODE_IT_RISING=1, PWR_FLAG_PVDO=1,
 ADC_CHANNEL_0=0, ADC_CHANNEL_VREFINT=17, ADC_REGULAR_RANK_1=1,
 ADC_SAMPLETIME_239CYCLES_5=239, HAL_ADC_STATE_ERROR_INTERNAL=1,
 HAL_ADC_STATE_ERROR_DMA=2, I2C_MEMADD_SIZE_8BIT=1 };
#define __get_PRIMASK() mock_primask
#define __get_BASEPRI() mock_basepri
#define __get_IPSR() mock_ipsr
#define __disable_irq() (mock_primask=1U)
void MockEnableIrq(void);
#define __enable_irq() MockEnableIrq()
#define __DSB() ((void)0)
#define __ISB() ((void)0)
#define __DMB() ((void)0)
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOA_IS_CLK_ENABLED() mock_gpio_a_clock
#define __HAL_RCC_GPIOB_IS_CLK_ENABLED() mock_gpio_b_clock
#define __HAL_RCC_TIM3_IS_CLK_ENABLED() mock_tim3_clock
#define __HAL_PWR_GET_FLAG(f) ((void)(f), mock_pvdo)
#define __HAL_PWR_PVD_EXTI_GET_FLAG() mock_pvd_pending
#define __HAL_PWR_PVD_EXTI_CLEAR_FLAG() (mock_pvd_pending=0U)
#define __HAL_GPIO_EXTI_CLEAR_IT(p) (mock_exti.PR &= ~(uint32_t)(p))
#define __HAL_TIM_GET_AUTORELOAD(h) ((h)->Instance->ARR)
void MockSetCompare(TIM_HandleTypeDef *, uint32_t, uint32_t);
uint32_t MockGetCounter(TIM_HandleTypeDef *);
#define __HAL_TIM_SET_COMPARE(h,c,v) MockSetCompare(h,c,v)
#define __HAL_TIM_GET_COUNTER(h) MockGetCounter(h)
#define __HAL_TIM_SET_COUNTER(h,v) ((h)->Instance->CNT=(v))
#define __HAL_TIM_DISABLE_IT(h,i) ((h)->Instance->DIER &= ~(uint32_t)(i))
#define __HAL_TIM_ENABLE_IT(h,i) ((h)->Instance->DIER |= (i))
#define __HAL_TIM_CLEAR_FLAG(h,f) ((h)->Instance->SR &= ~(uint32_t)(f))
#define __HAL_TIM_GET_FLAG(h,f) ((h)->Instance->SR & (f))
#define __HAL_TIM_GET_IT_SOURCE(h,i) ((h)->Instance->DIER & (i))
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t);
void HAL_NVIC_DisableIRQ(int);
void HAL_NVIC_EnableIRQ(int);
void HAL_NVIC_ClearPendingIRQ(int);
void HAL_NVIC_SetPriority(int,uint32_t,uint32_t);
void HAL_PWR_DisablePVD(void);
void HAL_PWR_EnablePVD(void);
void HAL_PWR_ConfigPVD(const PWR_PVDTypeDef *);
void HAL_PWR_PVD_IRQHandler(void);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *,uint32_t);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *,uint32_t);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *);
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *);
uint32_t HAL_RCC_GetPCLK1Freq(void);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *,const ADC_ChannelConfTypeDef *);
HAL_StatusTypeDef HAL_ADC_DeInit(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *,uint32_t *,uint32_t);
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *,uint16_t,uint32_t,uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *,uint16_t,uint16_t,uint16_t,uint8_t *,uint16_t,uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *,uint16_t,uint16_t,uint16_t,uint8_t *,uint16_t,uint32_t);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *,uint16_t,uint8_t *,uint16_t,uint32_t);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *,uint8_t *,uint16_t,uint32_t);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *,uint8_t *,uint16_t,uint32_t);
uint32_t HAL_GetTick(void);
