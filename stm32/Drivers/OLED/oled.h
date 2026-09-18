#ifndef OLED_H
#define OLED_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f1xx_hal.h"

#define OLED_WIDTH  128U
#define OLED_HEIGHT 64U
#define OLED_PAGES  (OLED_HEIGHT / 8U)

HAL_StatusTypeDef OLED_Init(I2C_HandleTypeDef *i2c);
void OLED_Clear(void);
void OLED_SetCursor(uint8_t x, uint8_t page);
void OLED_WriteString(const char *text);
HAL_StatusTypeDef OLED_UpdatePage(uint8_t page);
bool OLED_IsReady(void);
uint8_t OLED_GetAddress(void);

#endif
