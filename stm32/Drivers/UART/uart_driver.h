#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "stm32f1xx_hal.h"

#define UART_DRIVER_EVENT_RX        (1UL << 0)
#define UART_DRIVER_EVENT_TX_OK     (1UL << 1)
#define UART_DRIVER_EVENT_ERROR     (1UL << 2)
#define UART_DRIVER_EVENT_OVERFLOW  (1UL << 3)

#define UART_DRIVER_DMA_RX_SIZE     64U
#define UART_DRIVER_RX_RING_SIZE    256U
#define UART_DRIVER_TX_MAX_SIZE     42U

typedef struct {
  uint32_t received_bytes;
  uint32_t dropped_bytes;
  uint32_t transmitted_frames;
  uint32_t transmitted_bytes;
  uint32_t hardware_errors;
  uint32_t overflow_events;
  uint32_t recovery_attempts;
  uint32_t successful_recoveries;
  uint32_t last_hal_error;
} UartDriverDiagnostics_t;

HAL_StatusTypeDef UartDriver_Init(osThreadId_t notification_task);
HAL_StatusTypeDef UartDriver_Recover(osThreadId_t notification_task);
HAL_StatusTypeDef UartDriver_Service(void);
uint32_t UartDriver_ConsumeEvents(void);
bool UartDriver_ReadByte(uint8_t *byte);
HAL_StatusTypeDef UartDriver_Send(const uint8_t *data, uint16_t length);
bool UartDriver_IsReady(void);
bool UartDriver_IsTxPending(void);
void UartDriver_GetDiagnostics(UartDriverDiagnostics_t *diagnostics);

#endif
