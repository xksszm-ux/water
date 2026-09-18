/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define STATUS_LED_Pin GPIO_PIN_13
#define STATUS_LED_GPIO_Port GPIOC
#define ENC_LF_Pin GPIO_PIN_1
#define ENC_LF_GPIO_Port GPIOA
#define ENC_LF_EXTI_IRQn EXTI1_IRQn
#define TB6612_STBY_Pin GPIO_PIN_4
#define TB6612_STBY_GPIO_Port GPIOA
#define ENC_RR_Pin GPIO_PIN_5
#define ENC_RR_GPIO_Port GPIOA
#define ENC_RR_EXTI_IRQn EXTI9_5_IRQn
#define TB6612_BIN1_Pin GPIO_PIN_10
#define TB6612_BIN1_GPIO_Port GPIOB
#define TB6612_BIN2_Pin GPIO_PIN_11
#define TB6612_BIN2_GPIO_Port GPIOB
#define W25Q64_CS_Pin GPIO_PIN_12
#define W25Q64_CS_GPIO_Port GPIOB
#define HC_ECHO_Pin GPIO_PIN_15
#define HC_ECHO_GPIO_Port GPIOA
#define HC_ECHO_EXTI_IRQn EXTI15_10_IRQn
#define ENC_LR_Pin GPIO_PIN_3
#define ENC_LR_GPIO_Port GPIOB
#define ENC_LR_EXTI_IRQn EXTI3_IRQn
#define ENC_RF_Pin GPIO_PIN_4
#define ENC_RF_GPIO_Port GPIOB
#define ENC_RF_EXTI_IRQn EXTI4_IRQn
#define HC_TRIG_Pin GPIO_PIN_5
#define HC_TRIG_GPIO_Port GPIOB
#define TB6612_AIN1_Pin GPIO_PIN_8
#define TB6612_AIN1_GPIO_Port GPIOB
#define TB6612_AIN2_Pin GPIO_PIN_9
#define TB6612_AIN2_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
