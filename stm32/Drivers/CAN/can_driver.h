#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "stm32f1xx_hal.h"

#define CAN_DRIVER_EVENT_RX       (1UL << 0)
#define CAN_DRIVER_EVENT_TX_OK    (1UL << 1)
#define CAN_DRIVER_EVENT_ERROR    (1UL << 2)

#ifndef CAN_DRIVER_INTERNAL_LOOPBACK_TEST
#define CAN_DRIVER_INTERNAL_LOOPBACK_TEST 0U
#endif

typedef struct {
  uint16_t standard_id;
  uint8_t dlc;
  uint8_t data[8];
} CanDriverFrame_t;

typedef struct {
  uint32_t received_frames;
  uint32_t dropped_frames;
  uint32_t transmitted_frames;
  uint32_t transmit_timeouts;
  uint32_t hardware_errors;
  uint32_t last_hal_error;
} CanDriverDiagnostics_t;

HAL_StatusTypeDef CanDriver_Init(CAN_HandleTypeDef *can,
                                 osThreadId_t notification_task);
HAL_StatusTypeDef CanDriver_Recover(void);
HAL_StatusTypeDef CanDriver_Send(uint16_t standard_id,
                                 const uint8_t *data, uint8_t dlc);
HAL_StatusTypeDef CanDriver_Service(uint32_t now_ms,
                                    uint32_t transmit_timeout_ms);
bool CanDriver_Receive(CanDriverFrame_t *frame);
bool CanDriver_IsReady(void);
bool CanDriver_IsTransmitPending(void);
void CanDriver_ClearErrors(void);
void CanDriver_GetDiagnostics(CanDriverDiagnostics_t *diagnostics);

#endif
