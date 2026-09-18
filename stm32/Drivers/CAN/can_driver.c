#include "can_driver.h"

#include <string.h>

#include "can_protocol.h"

#define CAN_RX_RING_SIZE 4U
#define CAN_NOTIFICATION_MASK                                                \
  (CAN_IT_TX_MAILBOX_EMPTY | CAN_IT_RX_FIFO0_MSG_PENDING |                 \
   CAN_IT_RX_FIFO0_OVERRUN | CAN_IT_BUSOFF |                               \
   CAN_IT_ERROR)

#define CAN_FATAL_HAL_ERRORS                                                 \
  (HAL_CAN_ERROR_TIMEOUT | HAL_CAN_ERROR_NOT_INITIALIZED |                 \
   HAL_CAN_ERROR_NOT_READY | HAL_CAN_ERROR_NOT_STARTED |                   \
   HAL_CAN_ERROR_PARAM | HAL_CAN_ERROR_INTERNAL)

static CAN_HandleTypeDef *can_handle;
static osThreadId_t volatile notification_task;
static CanDriverFrame_t rx_ring[CAN_RX_RING_SIZE];
static CanDriverFrame_t latest_control_frame;
static CanDriverFrame_t latest_stop_frame;
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
static volatile bool control_frame_pending;
static volatile bool stop_frame_pending;
static volatile bool driver_ready;
static volatile bool transmit_pending;
static volatile bool control_fault_latched;
static volatile uint32_t transmit_mailbox;
static volatile uint32_t transmit_started_ms;
static volatile CanDriverDiagnostics_t driver_diagnostics;

static void DisableCanIrqs(void)
{
  HAL_NVIC_DisableIRQ(USB_HP_CAN1_TX_IRQn);
  HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
  HAL_NVIC_DisableIRQ(CAN1_SCE_IRQn);
}

static void ClearCanPendingIrqs(void)
{
  HAL_NVIC_ClearPendingIRQ(USB_HP_CAN1_TX_IRQn);
  HAL_NVIC_ClearPendingIRQ(USB_LP_CAN1_RX0_IRQn);
  HAL_NVIC_ClearPendingIRQ(CAN1_SCE_IRQn);
}

static void EnableCanIrqs(void)
{
  HAL_NVIC_EnableIRQ(USB_HP_CAN1_TX_IRQn);
  HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
  HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);
}

static void SignalTask(uint32_t event)
{
  const osThreadId_t task = notification_task;
  if (task != NULL) (void)osThreadFlagsSet(task, event);
}

static HAL_StatusTypeDef ConfigureFilter(void)
{
  CAN_FilterTypeDef filter = {0};
  filter.FilterBank = 0U;
  filter.FilterMode = CAN_FILTERMODE_IDLIST;
  filter.FilterScale = CAN_FILTERSCALE_16BIT;
  filter.FilterIdHigh = (uint32_t)(CAN_ID_MOTOR_COMMAND << 5U);
  filter.FilterIdLow = (uint32_t)(CAN_ID_PID_CONFIG << 5U);
  filter.FilterMaskIdHigh = (uint32_t)(CAN_ID_MOTOR_COMMAND << 5U);
  filter.FilterMaskIdLow = (uint32_t)(CAN_ID_PID_CONFIG << 5U);
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  filter.FilterActivation = CAN_FILTER_ENABLE;
  filter.SlaveStartFilterBank = 14U;
  return HAL_CAN_ConfigFilter(can_handle, &filter);
}

static HAL_StatusTypeDef StartPeripheral(void)
{
  if (ConfigureFilter() != HAL_OK) return HAL_ERROR;
  if (HAL_CAN_Start(can_handle) != HAL_OK) return HAL_ERROR;
  if (HAL_CAN_ActivateNotification(can_handle, CAN_NOTIFICATION_MASK) !=
      HAL_OK) {
    (void)HAL_CAN_Stop(can_handle);
    return HAL_ERROR;
  }
  driver_ready = true;
  return HAL_OK;
}

HAL_StatusTypeDef CanDriver_Init(CAN_HandleTypeDef *can,
                                 osThreadId_t task)
{
  if ((can == NULL) || (task == NULL)) return HAL_ERROR;
  can_handle = can;
  notification_task = task;
  rx_head = 0U;
  rx_tail = 0U;
  control_frame_pending = false;
  stop_frame_pending = false;
  driver_ready = false;
  transmit_pending = false;
  control_fault_latched = false;
  memset((void *)&driver_diagnostics, 0, sizeof(driver_diagnostics));
  DisableCanIrqs();
  ClearCanPendingIrqs();

#if CAN_DRIVER_INTERNAL_LOOPBACK_TEST
  (void)HAL_CAN_DeInit(can_handle);
  can_handle->Init.Mode = CAN_MODE_LOOPBACK;
  if (HAL_CAN_Init(can_handle) != HAL_OK) return HAL_ERROR;
  DisableCanIrqs();
#else
  if (can_handle->State == HAL_CAN_STATE_LISTENING) {
    if (HAL_CAN_Stop(can_handle) != HAL_OK) return HAL_ERROR;
  } else if (can_handle->State != HAL_CAN_STATE_READY) {
    if ((HAL_CAN_DeInit(can_handle) != HAL_OK) ||
        (HAL_CAN_Init(can_handle) != HAL_OK)) return HAL_ERROR;
    DisableCanIrqs();
  }
#endif
  const HAL_StatusTypeDef result = StartPeripheral();
  ClearCanPendingIrqs();
  if (result == HAL_OK) EnableCanIrqs();
  return result;
}

HAL_StatusTypeDef CanDriver_Recover(void)
{
  if (can_handle == NULL) return HAL_ERROR;
  driver_ready = false;
  DisableCanIrqs();
  ClearCanPendingIrqs();
  transmit_pending = false;
  control_fault_latched = false;
  (void)HAL_CAN_DeactivateNotification(can_handle, CAN_NOTIFICATION_MASK);
  (void)HAL_CAN_AbortTxRequest(can_handle, CAN_TX_MAILBOX0 |
                                           CAN_TX_MAILBOX1 |
                                           CAN_TX_MAILBOX2);
  if ((HAL_CAN_DeInit(can_handle) != HAL_OK) ||
      (HAL_CAN_Init(can_handle) != HAL_OK)) return HAL_ERROR;
  DisableCanIrqs();
  rx_head = 0U;
  rx_tail = 0U;
  control_frame_pending = false;
  stop_frame_pending = false;
  const HAL_StatusTypeDef result = StartPeripheral();
  ClearCanPendingIrqs();
  if (result == HAL_OK) EnableCanIrqs();
  return result;
}

HAL_StatusTypeDef CanDriver_Send(uint16_t standard_id,
                                 const uint8_t *data, uint8_t dlc)
{
  CAN_TxHeaderTypeDef header = {0};
  uint32_t mailbox;
  HAL_StatusTypeDef result;
  if (!driver_ready || (data == NULL) || (standard_id > 0x7FFU) ||
      (dlc > 8U)) return HAL_ERROR;
  if (transmit_pending || (HAL_CAN_GetTxMailboxesFreeLevel(can_handle) == 0U)) {
    return HAL_BUSY;
  }

  header.StdId = standard_id;
  header.IDE = CAN_ID_STD;
  header.RTR = CAN_RTR_DATA;
  header.DLC = dlc;
  header.TransmitGlobalTime = DISABLE;
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  result = HAL_CAN_AddTxMessage(can_handle, &header, (uint8_t *)data,
                                &mailbox);
  if (result == HAL_OK) {
    transmit_mailbox = mailbox;
    transmit_started_ms = osKernelGetTickCount();
    transmit_pending = true;
  }
  if (primask == 0U) __enable_irq();
  if (result != HAL_OK) return HAL_ERROR;
  return HAL_OK;
}

HAL_StatusTypeDef CanDriver_Service(uint32_t now_ms,
                                    uint32_t transmit_timeout_ms)
{
  bool abort_transmit = false;
  uint32_t mailbox_to_abort = 0U;
  if (!driver_ready || (can_handle == NULL)) return HAL_ERROR;
  if (control_fault_latched) return HAL_ERROR;
  if (transmit_pending &&
      ((uint32_t)(now_ms - transmit_started_ms) > transmit_timeout_ms)) {
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (transmit_pending &&
        ((uint32_t)(now_ms - transmit_started_ms) > transmit_timeout_ms) &&
        (HAL_CAN_IsTxMessagePending(can_handle, transmit_mailbox) != 0U)) {
      mailbox_to_abort = transmit_mailbox;
      transmit_pending = false;
      ++driver_diagnostics.transmit_timeouts;
      abort_transmit = true;
    }
    if (primask == 0U) __enable_irq();
    if (abort_transmit) {
      (void)HAL_CAN_AbortTxRequest(can_handle, mailbox_to_abort);
      return HAL_TIMEOUT;
    }
  }

  const uint32_t error = HAL_CAN_GetError(can_handle);
  if ((can_handle->State == HAL_CAN_STATE_ERROR) ||
      ((error & (CAN_FATAL_HAL_ERRORS | HAL_CAN_ERROR_BOF |
                 HAL_CAN_ERROR_RX_FOV0)) != 0U) ||
      ((can_handle->Instance->ESR & CAN_ESR_BOFF) != 0U)) {
    return HAL_ERROR;
  }
  if (error != HAL_CAN_ERROR_NONE) return HAL_BUSY;
  return HAL_OK;
}

bool CanDriver_Receive(CanDriverFrame_t *frame)
{
  bool received = false;
  if (frame == NULL) return false;
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (stop_frame_pending) {
    *frame = latest_stop_frame;
    stop_frame_pending = false;
    received = true;
  } else if (control_frame_pending) {
    *frame = latest_control_frame;
    control_frame_pending = false;
    received = true;
  } else if (rx_tail != rx_head) {
    *frame = rx_ring[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1U) % CAN_RX_RING_SIZE);
    received = true;
  }
  if (primask == 0U) __enable_irq();
  return received;
}

bool CanDriver_IsReady(void)
{
  return driver_ready;
}

bool CanDriver_IsTransmitPending(void)
{
  return transmit_pending;
}

void CanDriver_ClearErrors(void)
{
  if (can_handle != NULL) (void)HAL_CAN_ResetError(can_handle);
}

void CanDriver_GetDiagnostics(CanDriverDiagnostics_t *diagnostics)
{
  if (diagnostics == NULL) return;
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  diagnostics->received_frames = driver_diagnostics.received_frames;
  diagnostics->dropped_frames = driver_diagnostics.dropped_frames;
  diagnostics->transmitted_frames = driver_diagnostics.transmitted_frames;
  diagnostics->transmit_timeouts = driver_diagnostics.transmit_timeouts;
  diagnostics->hardware_errors = driver_diagnostics.hardware_errors;
  diagnostics->last_hal_error = driver_diagnostics.last_hal_error;
  if (primask == 0U) __enable_irq();
}

static void CompleteTransmit(CAN_HandleTypeDef *can, uint32_t mailbox)
{
  if ((can != can_handle) || !transmit_pending ||
      (transmit_mailbox != mailbox)) return;
  transmit_pending = false;
  ++driver_diagnostics.transmitted_frames;
  SignalTask(CAN_DRIVER_EVENT_TX_OK);
}

void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *can)
{
  CompleteTransmit(can, CAN_TX_MAILBOX0);
}

void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *can)
{
  CompleteTransmit(can, CAN_TX_MAILBOX1);
}

void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *can)
{
  CompleteTransmit(can, CAN_TX_MAILBOX2);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *can)
{
  bool received = false;
  if (can != can_handle) return;
  while (HAL_CAN_GetRxFifoFillLevel(can, CAN_RX_FIFO0) > 0U) {
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];
    if (HAL_CAN_GetRxMessage(can, CAN_RX_FIFO0, &header, data) != HAL_OK) {
      control_fault_latched = true;
      ++driver_diagnostics.hardware_errors;
      driver_diagnostics.last_hal_error = HAL_CAN_GetError(can);
      SignalTask(CAN_DRIVER_EVENT_ERROR);
      return;
    }
    if ((header.IDE != CAN_ID_STD) || (header.RTR != CAN_RTR_DATA) ||
        (header.DLC > 8U)) continue;
    if (header.StdId == CAN_ID_MOTOR_COMMAND) {
      CanMotorCommand_t decoded;
      const bool valid_stop =
          (CanProtocol_DecodeControl(data, (uint8_t)header.DLC, &decoded) ==
           CAN_PROTOCOL_OK) &&
          (!decoded.enable ||
           ((decoded.left_output_permille == 0) &&
            (decoded.right_output_permille == 0)));
      CanDriverFrame_t *destination;
      volatile bool *pending;
      if (valid_stop) {
        destination = &latest_stop_frame;
        pending = &stop_frame_pending;
      } else {
        destination = &latest_control_frame;
        pending = &control_frame_pending;
      }
      if (*pending) ++driver_diagnostics.dropped_frames;
      destination->standard_id = (uint16_t)header.StdId;
      destination->dlc = (uint8_t)header.DLC;
      memcpy(destination->data, data, sizeof(data));
      __DMB();
      *pending = true;
    } else {
      const uint8_t next = (uint8_t)((rx_head + 1U) % CAN_RX_RING_SIZE);
      if (next == rx_tail) {
        rx_tail = (uint8_t)((rx_tail + 1U) % CAN_RX_RING_SIZE);
        ++driver_diagnostics.dropped_frames;
      }
      rx_ring[rx_head].standard_id = (uint16_t)header.StdId;
      rx_ring[rx_head].dlc = (uint8_t)header.DLC;
      memcpy(rx_ring[rx_head].data, data, sizeof(data));
      __DMB();
      rx_head = next;
    }
    ++driver_diagnostics.received_frames;
    received = true;
  }
  if (received) SignalTask(CAN_DRIVER_EVENT_RX);
}

void HAL_CAN_RxFifo0FullCallback(CAN_HandleTypeDef *can)
{
  if (can != can_handle) return;
  control_fault_latched = true;
  ++driver_diagnostics.hardware_errors;
  driver_diagnostics.last_hal_error = HAL_CAN_GetError(can);
  SignalTask(CAN_DRIVER_EVENT_ERROR);
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *can)
{
  if (can != can_handle) return;
  const uint32_t error = HAL_CAN_GetError(can);
  if (((can->Instance->ESR & CAN_ESR_BOFF) != 0U) ||
      ((error & (CAN_FATAL_HAL_ERRORS | HAL_CAN_ERROR_BOF |
                 HAL_CAN_ERROR_RX_FOV0)) != 0U)) {
    control_fault_latched = true;
  }
  ++driver_diagnostics.hardware_errors;
  driver_diagnostics.last_hal_error = error;
  SignalTask(CAN_DRIVER_EVENT_ERROR);
}
