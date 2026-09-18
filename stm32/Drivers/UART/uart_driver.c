#include "uart_driver.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "usart.h"

#define UART_DRIVER_RX_RING_MASK (UART_DRIVER_RX_RING_SIZE - 1U)

#if ((UART_DRIVER_RX_RING_SIZE & UART_DRIVER_RX_RING_MASK) != 0U)
#error "UART_DRIVER_RX_RING_SIZE must be a power of two"
#endif

static UART_HandleTypeDef * volatile uart_handle;
static volatile osThreadId_t notification_task;

static volatile uint8_t dma_rx_buffer[UART_DRIVER_DMA_RX_SIZE];
static volatile uint8_t rx_ring[UART_DRIVER_RX_RING_SIZE];
static volatile uint8_t tx_buffer[UART_DRIVER_TX_MAX_SIZE];

/* dma_old_position is only changed by USART/DMA callbacks. */
static volatile uint16_t dma_old_position;
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile uint16_t transmit_length;
static volatile uint32_t pending_events;
static volatile bool driver_ready;
static volatile bool transmit_pending;
static volatile bool error_latched;
static volatile UartDriverDiagnostics_t driver_diagnostics;

static void DisableUartIrqs(void)
{
  HAL_NVIC_DisableIRQ(USART1_IRQn);
  HAL_NVIC_DisableIRQ(DMA1_Channel5_IRQn);
}

static void ClearUartPendingIrqs(void)
{
  HAL_NVIC_ClearPendingIRQ(USART1_IRQn);
  HAL_NVIC_ClearPendingIRQ(DMA1_Channel5_IRQn);
}

static void EnableUartIrqs(void)
{
  HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
}

static void SignalTask(uint32_t events)
{
  const osThreadId_t task = notification_task;
  if ((task != NULL) && (events != 0U)) {
    (void)osThreadFlagsSet(task, events);
  }
}

static bool PeripheralConfigurationIsValid(void)
{
  return (uart_handle == &huart1) &&
         (uart_handle->Instance == USART1) &&
         (uart_handle->hdmarx != NULL) &&
         (uart_handle->hdmarx->Instance == DMA1_Channel5) &&
         (uart_handle->hdmarx->Init.Mode == DMA_CIRCULAR);
}

static void ResetDataPath(void)
{
  dma_old_position = 0U;
  rx_head = 0U;
  rx_tail = 0U;
  transmit_length = 0U;
  pending_events = 0U;
  transmit_pending = false;
  error_latched = false;
  memset((void *)dma_rx_buffer, 0, sizeof(dma_rx_buffer));
  memset((void *)rx_ring, 0, sizeof(rx_ring));
  memset((void *)tx_buffer, 0, sizeof(tx_buffer));
}

static HAL_StatusTypeDef StartReception(void)
{
  if (!PeripheralConfigurationIsValid()) return HAL_ERROR;

  /* Reading SR then DR clears a stale IDLE/error condition before DMA starts. */
  __HAL_UART_CLEAR_OREFLAG(uart_handle);
  const HAL_StatusTypeDef result =
      HAL_UARTEx_ReceiveToIdle_DMA(uart_handle, (uint8_t *)dma_rx_buffer,
                                   UART_DRIVER_DMA_RX_SIZE);
  if (result != HAL_OK) return result;

  /* IDLE and TC are sufficient; HT would only duplicate wakeups. */
  __HAL_DMA_DISABLE_IT(uart_handle->hdmarx, DMA_IT_HT);
  driver_ready = true;
  return HAL_OK;
}

static void LatchTaskContextError(void)
{
  bool signal = false;
  taskENTER_CRITICAL();
  if (!error_latched) {
    ++driver_diagnostics.hardware_errors;
    driver_diagnostics.last_hal_error = HAL_UART_GetError(uart_handle);
    error_latched = true;
    driver_ready = false;
    pending_events |= UART_DRIVER_EVENT_ERROR;
    signal = true;
  }
  taskEXIT_CRITICAL();
  if (signal) SignalTask(UART_DRIVER_EVENT_ERROR);
}

HAL_StatusTypeDef UartDriver_Init(osThreadId_t task)
{
  if (task == NULL) return HAL_ERROR;

  uart_handle = &huart1;
  notification_task = task;
  driver_ready = false;
  memset((void *)&driver_diagnostics, 0, sizeof(driver_diagnostics));

  DisableUartIrqs();
  ClearUartPendingIrqs();
  if (HAL_UART_Abort(uart_handle) != HAL_OK) {
    ++driver_diagnostics.hardware_errors;
    driver_diagnostics.last_hal_error = HAL_UART_GetError(uart_handle);
    error_latched = true;
    pending_events |= UART_DRIVER_EVENT_ERROR;
    return HAL_ERROR;
  }
  ResetDataPath();

  const HAL_StatusTypeDef result = StartReception();
  ClearUartPendingIrqs();
  if (result == HAL_OK) {
    EnableUartIrqs();
  } else {
    ++driver_diagnostics.hardware_errors;
    driver_diagnostics.last_hal_error = HAL_UART_GetError(uart_handle);
    error_latched = true;
    pending_events |= UART_DRIVER_EVENT_ERROR;
  }
  return result;
}

HAL_StatusTypeDef UartDriver_Recover(osThreadId_t task)
{
  if (task == NULL) return HAL_ERROR;

  notification_task = task;
  driver_ready = false;
  ++driver_diagnostics.recovery_attempts;
  DisableUartIrqs();
  ClearUartPendingIrqs();

  if (HAL_UART_Abort(&huart1) != HAL_OK) {
    ++driver_diagnostics.hardware_errors;
    driver_diagnostics.last_hal_error = HAL_UART_GetError(&huart1);
    error_latched = true;
    pending_events |= UART_DRIVER_EVENT_ERROR;
    return HAL_ERROR;
  }
  if (HAL_UART_DeInit(&huart1) != HAL_OK) {
    ++driver_diagnostics.hardware_errors;
    driver_diagnostics.last_hal_error = HAL_UART_GetError(&huart1);
    error_latched = true;
    pending_events |= UART_DRIVER_EVENT_ERROR;
    return HAL_ERROR;
  }

  /* CubeMX owns the low-level USART1 and DMA1_Channel5 configuration. */
  MX_USART1_UART_Init();
  uart_handle = &huart1;
  DisableUartIrqs();
  ClearUartPendingIrqs();
  ResetDataPath();

  const HAL_StatusTypeDef result = StartReception();
  ClearUartPendingIrqs();
  if (result == HAL_OK) {
    ++driver_diagnostics.successful_recoveries;
    EnableUartIrqs();
  } else {
    ++driver_diagnostics.hardware_errors;
    driver_diagnostics.last_hal_error = HAL_UART_GetError(uart_handle);
    error_latched = true;
    pending_events |= UART_DRIVER_EVENT_ERROR;
  }
  return result;
}

HAL_StatusTypeDef UartDriver_Service(void)
{
  if ((uart_handle == NULL) || !driver_ready || error_latched ||
      !PeripheralConfigurationIsValid()) {
    return HAL_ERROR;
  }

  if ((uart_handle->RxState != HAL_UART_STATE_BUSY_RX) ||
      (uart_handle->hdmarx->State != HAL_DMA_STATE_BUSY) ||
      ((uart_handle->Instance->CR3 & USART_CR3_DMAR) == 0U) ||
      (transmit_pending &&
       (uart_handle->gState != HAL_UART_STATE_BUSY_TX)) ||
      (!transmit_pending &&
       (uart_handle->gState != HAL_UART_STATE_READY)) ||
      (HAL_UART_GetError(uart_handle) != HAL_UART_ERROR_NONE)) {
    LatchTaskContextError();
    return HAL_ERROR;
  }
  return HAL_OK;
}

uint32_t UartDriver_ConsumeEvents(void)
{
  uint32_t events;
  taskENTER_CRITICAL();
  events = pending_events;
  pending_events = 0U;
  taskEXIT_CRITICAL();
  return events;
}

bool UartDriver_ReadByte(uint8_t *byte)
{
  bool received = false;
  if (byte == NULL) return false;

  taskENTER_CRITICAL();
  if (rx_tail != rx_head) {
    *byte = rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) & UART_DRIVER_RX_RING_MASK);
    received = true;
  }
  taskEXIT_CRITICAL();
  return received;
}

HAL_StatusTypeDef UartDriver_Send(const uint8_t *data, uint16_t length)
{
  HAL_StatusTypeDef result;
  if ((data == NULL) || (length == 0U) ||
      (length > UART_DRIVER_TX_MAX_SIZE)) {
    return HAL_ERROR;
  }

  taskENTER_CRITICAL();
  if (!driver_ready || error_latched) {
    result = HAL_ERROR;
  } else if (transmit_pending) {
    result = HAL_BUSY;
  } else {
    memcpy((void *)tx_buffer, data, length);
    result = HAL_UART_Transmit_IT(uart_handle, (uint8_t *)tx_buffer, length);
    if (result == HAL_OK) {
      transmit_length = length;
      transmit_pending = true;
    }
  }
  taskEXIT_CRITICAL();
  return result;
}

bool UartDriver_IsReady(void)
{
  return driver_ready && !error_latched;
}

bool UartDriver_IsTxPending(void)
{
  return transmit_pending;
}

void UartDriver_GetDiagnostics(UartDriverDiagnostics_t *diagnostics)
{
  if (diagnostics == NULL) return;
  taskENTER_CRITICAL();
  diagnostics->received_bytes = driver_diagnostics.received_bytes;
  diagnostics->dropped_bytes = driver_diagnostics.dropped_bytes;
  diagnostics->transmitted_frames = driver_diagnostics.transmitted_frames;
  diagnostics->transmitted_bytes = driver_diagnostics.transmitted_bytes;
  diagnostics->hardware_errors = driver_diagnostics.hardware_errors;
  diagnostics->overflow_events = driver_diagnostics.overflow_events;
  diagnostics->recovery_attempts = driver_diagnostics.recovery_attempts;
  diagnostics->successful_recoveries =
      driver_diagnostics.successful_recoveries;
  diagnostics->last_hal_error = driver_diagnostics.last_hal_error;
  taskEXIT_CRITICAL();
}

static bool PushRxByteFromIsr(uint8_t byte)
{
  bool dropped = false;
  uint16_t next = (uint16_t)((rx_head + 1U) & UART_DRIVER_RX_RING_MASK);
  if (next == rx_tail) {
    rx_tail = (uint16_t)((rx_tail + 1U) & UART_DRIVER_RX_RING_MASK);
    ++driver_diagnostics.dropped_bytes;
    dropped = true;
  }
  rx_ring[rx_head] = byte;
  __DMB();
  rx_head = next;
  ++driver_diagnostics.received_bytes;
  return dropped;
}

static bool CopyDmaRangeFromIsr(uint16_t begin, uint16_t end)
{
  bool overflow = false;
  for (uint16_t index = begin; index < end; ++index) {
    overflow = PushRxByteFromIsr(dma_rx_buffer[index]) || overflow;
  }
  return overflow;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size)
{
  bool received = false;
  bool overflow = false;
  if ((uart != uart_handle) || !driver_ready ||
      (size == 0U) || (size > UART_DRIVER_DMA_RX_SIZE)) {
    return;
  }

  const HAL_UART_RxEventTypeTypeDef event = HAL_UARTEx_GetRxEventType(uart);
  uint16_t position = size;

  if ((event == HAL_UART_RXEVENT_TC) &&
      (size == UART_DRIVER_DMA_RX_SIZE)) {
    /* TC means the DMA writer just wrapped to index zero. Size is a position. */
    overflow = CopyDmaRangeFromIsr(dma_old_position,
                                   UART_DRIVER_DMA_RX_SIZE);
    received = (dma_old_position < UART_DRIVER_DMA_RX_SIZE);
    dma_old_position = 0U;
  } else {
    if (position == UART_DRIVER_DMA_RX_SIZE) position = 0U;
    if (position > dma_old_position) {
      overflow = CopyDmaRangeFromIsr(dma_old_position, position);
      received = true;
    } else if (position < dma_old_position) {
      overflow = CopyDmaRangeFromIsr(dma_old_position,
                                     UART_DRIVER_DMA_RX_SIZE);
      overflow = CopyDmaRangeFromIsr(0U, position) || overflow;
      received = true;
    }
    /* Repeated IDLE/HT at the same position contains no new bytes. */
    dma_old_position = position;
  }

  uint32_t events = 0U;
  if (received) events |= UART_DRIVER_EVENT_RX;
  if (overflow) {
    ++driver_diagnostics.overflow_events;
    events |= UART_DRIVER_EVENT_OVERFLOW;
  }
  if (events != 0U) {
    pending_events |= events;
    SignalTask(events);
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
  if ((uart != uart_handle) || !transmit_pending) return;
  transmit_pending = false;
  ++driver_diagnostics.transmitted_frames;
  driver_diagnostics.transmitted_bytes += transmit_length;
  transmit_length = 0U;
  pending_events |= UART_DRIVER_EVENT_TX_OK;
  SignalTask(UART_DRIVER_EVENT_TX_OK);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  if (uart != uart_handle) return;
  ++driver_diagnostics.hardware_errors;
  driver_diagnostics.last_hal_error = HAL_UART_GetError(uart);
  error_latched = true;
  driver_ready = false;
  pending_events |= UART_DRIVER_EVENT_ERROR;
  SignalTask(UART_DRIVER_EVENT_ERROR);
}
