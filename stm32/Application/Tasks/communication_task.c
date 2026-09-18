#include "task_entries.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app_tasks.h"
#include "control_arbiter.h"
#include "motor_step4_test.h"
#include "protocol.h"
#include "robot_state.h"
#include "uart_driver.h"

#define COMM_TASK_WAIT_MS             10U
#define UART_RECOVERY_PERIOD_MS       1000U
#define UART_LINK_TIMEOUT_MS          1500U
#define UART_TX_TIMEOUT_MS            50U
#define UART_STATUS_PERIOD_MS         100U
#define UART_RX_BYTE_BUDGET           128U
#define UART_PARSE_OPERATION_BUDGET   16U
#define UART_FRAME_BUDGET             8U
#define UART_RESPONSE_QUEUE_LENGTH    8U
#define UART_DRIVER_EVENTS                                                \
  (UART_DRIVER_EVENT_RX | UART_DRIVER_EVENT_TX_OK |                      \
   UART_DRIVER_EVENT_ERROR | UART_DRIVER_EVENT_OVERFLOW)

typedef enum {
  UART_TX_PURPOSE_NONE = 0U,
  UART_TX_PURPOSE_STATUS,
  UART_TX_PURPOSE_RESPONSE
} UartTxPurpose_t;

volatile uint32_t g_uart_protocol_reject_count;
volatile uint32_t g_uart_crc_error_count;
volatile uint32_t g_uart_parser_timeout_count;
volatile uint32_t g_uart_response_drop_count;
volatile uint32_t g_uart_tx_timeout_count;

static ProtocolParser_t parser;
static ProtocolFrame_t received_frame;
static RobotStatus_t status_snapshot;
static uint8_t response_queue[UART_RESPONSE_QUEUE_LENGTH]
                             [PROTOCOL_MAX_FRAME_SIZE];
static uint8_t response_length[UART_RESPONSE_QUEUE_LENGTH];
static uint8_t response_head;
static uint8_t response_tail;
static uint8_t response_count;
static uint8_t encode_buffer[PROTOCOL_MAX_FRAME_SIZE];
static uint8_t status_buffer[PROTOCOL_MAX_FRAME_SIZE];

static void ResponseQueueReset(void)
{
  response_head = 0U;
  response_tail = 0U;
  response_count = 0U;
}

static bool ResponseQueuePush(const uint8_t *data, uint8_t length)
{
  if ((data == NULL) || (length == 0U) ||
      (length > PROTOCOL_MAX_FRAME_SIZE) ||
      (response_count >= UART_RESPONSE_QUEUE_LENGTH)) {
    return false;
  }
  memcpy(response_queue[response_head], data, length);
  response_length[response_head] = length;
  response_head = (uint8_t)((response_head + 1U) %
                            UART_RESPONSE_QUEUE_LENGTH);
  ++response_count;
  return true;
}

static const uint8_t *ResponseQueueFront(uint8_t *length)
{
  if ((length == NULL) || (response_count == 0U)) return NULL;
  *length = response_length[response_tail];
  return response_queue[response_tail];
}

static void ResponseQueuePop(void)
{
  if (response_count == 0U) return;
  response_tail = (uint8_t)((response_tail + 1U) %
                            UART_RESPONSE_QUEUE_LENGTH);
  --response_count;
}

static ProtocolAckResult_t MapControlResult(ControlResult_t result)
{
  switch (result) {
    case CONTROL_RESULT_ACCEPTED:
      return PROTOCOL_ACK_OK;
    case CONTROL_RESULT_STOP_ACCEPTED:
      return PROTOCOL_ACK_OK;
    case CONTROL_RESULT_REPLAY:
      return PROTOCOL_ACK_DUPLICATE;
    case CONTROL_RESULT_OWNER_BUSY:
      return PROTOCOL_ACK_OWNER_BUSY;
    case CONTROL_RESULT_HANDOFF_STOP:
      return PROTOCOL_ACK_HANDOFF_STOP;
    case CONTROL_RESULT_MODE_REJECTED:
      return PROTOCOL_ACK_MODE_DENIED;
    case CONTROL_RESULT_RANGE_REJECTED:
      return PROTOCOL_ACK_RANGE_ERROR;
    case CONTROL_RESULT_SAFETY_BLOCKED:
      return PROTOCOL_ACK_SAFETY_BLOCKED;
    case CONTROL_RESULT_INTERNAL_ERROR:
    default:
      return PROTOCOL_ACK_INTERNAL_ERROR;
  }
}

static void QueueAck(const ProtocolFrame_t *frame,
                     ProtocolAckResult_t result,
                     uint32_t now_ms)
{
  if ((frame == NULL) ||
      ((frame->flags & PROTOCOL_FLAG_ACK_REQUEST) == 0U)) return;

  RobotState_GetSnapshot(&status_snapshot);
  const size_t length = Protocol_EncodeAck(
      frame->sequence, frame->type, (uint8_t)result,
      (uint8_t)status_snapshot.mode,
      (uint8_t)ControlArbiter_GetOwner(now_ms), encode_buffer,
      sizeof(encode_buffer));
  if ((length == 0U) || !ResponseQueuePush(encode_buffer, (uint8_t)length)) {
    ++g_uart_response_drop_count;
  }
}

static ProtocolAckResult_t HandleMotorFrame(const ProtocolFrame_t *frame,
                                             uint32_t now_ms)
{
  ProtocolMotorCommand_t decoded;
  if (frame->payload_length != 5U) return PROTOCOL_ACK_BAD_LENGTH;
  if (((frame->payload[4] &
        (uint8_t)(~PROTOCOL_MOTOR_FLAG_ALLOWED_MASK)) != 0U) ||
      ((frame->payload[4] & PROTOCOL_MOTOR_FLAG_ENABLE) == 0U)) {
    return PROTOCOL_ACK_BAD_FLAGS;
  }
  if (!Protocol_DecodeMotor(frame, &decoded)) {
    return PROTOCOL_ACK_RANGE_ERROR;
  }

  const MotorCommand_t command = {
    .left_output_permille = decoded.left_pwm,
    .right_output_permille = decoded.right_pwm,
    .mode = ROBOT_MODE_BLE,
    .enable = true,
    .issued_at_ms = now_ms
  };
  const ControlResult_t result = ControlArbiter_SubmitUart(
      frame->sequence, &command, now_ms);
  return MapControlResult(result);
}

static ProtocolAckResult_t HandleStopFrame(const ProtocolFrame_t *frame,
                                            uint32_t now_ms)
{
  if (frame->payload_length != 0U) return PROTOCOL_ACK_BAD_LENGTH;
  RobotState_GetSnapshot(&status_snapshot);
  const MotorCommand_t stop = {
    .left_output_permille = 0,
    .right_output_permille = 0,
    .mode = status_snapshot.mode,
    .enable = false,
    .issued_at_ms = now_ms
  };
  return MapControlResult(ControlArbiter_SubmitUart(
      frame->sequence, &stop, now_ms));
}

static ProtocolAckResult_t HandleModeFrame(const ProtocolFrame_t *frame,
                                            uint32_t now_ms)
{
  if (frame->payload_length != 1U) return PROTOCOL_ACK_BAD_LENGTH;
  if (frame->payload[0] > (uint8_t)ROBOT_MODE_AUTO) {
    return PROTOCOL_ACK_RANGE_ERROR;
  }
  return MapControlResult(ControlArbiter_SetModeUart(
      frame->sequence, (RobotMode_t)frame->payload[0], now_ms));
}

static bool AckResultKeepsLinkAlive(ProtocolAckResult_t result)
{
  return (result != PROTOCOL_ACK_BAD_LENGTH) &&
         (result != PROTOCOL_ACK_BAD_FLAGS) &&
         (result != PROTOCOL_ACK_RANGE_ERROR) &&
         (result != PROTOCOL_ACK_VERSION_ERROR) &&
         (result != PROTOCOL_ACK_UNSUPPORTED);
}

static void HandleFrame(const ProtocolFrame_t *frame, uint32_t now_ms,
                        bool *status_requested, bool *link_online,
                        uint32_t *last_valid_frame_ms)
{
  ProtocolAckResult_t result = PROTOCOL_ACK_OK;

  if (frame->version != PROTOCOL_VERSION) {
    ++g_uart_protocol_reject_count;
    QueueAck(frame, PROTOCOL_ACK_VERSION_ERROR, now_ms);
    return;
  }
  if ((frame->flags & (uint8_t)(~PROTOCOL_FLAG_ALLOWED_MASK)) != 0U) {
    ++g_uart_protocol_reject_count;
    QueueAck(frame, PROTOCOL_ACK_BAD_FLAGS, now_ms);
    return;
  }

  switch (frame->type) {
    case PROTOCOL_TYPE_MOTOR_CONTROL:
      result = HandleMotorFrame(frame, now_ms);
      break;
    case PROTOCOL_TYPE_STOP:
      result = HandleStopFrame(frame, now_ms);
      break;
    case PROTOCOL_TYPE_SET_MODE:
      result = HandleModeFrame(frame, now_ms);
      break;
    case PROTOCOL_TYPE_STATUS_REQUEST:
      if (frame->payload_length != 0U) {
        result = PROTOCOL_ACK_BAD_LENGTH;
      } else {
        *status_requested = true;
      }
      break;
    case PROTOCOL_TYPE_HEARTBEAT:
      if (frame->payload_length != 0U) result = PROTOCOL_ACK_BAD_LENGTH;
      break;
    default:
      result = PROTOCOL_ACK_UNSUPPORTED;
      break;
  }

  if (AckResultKeepsLinkAlive(result)) {
    *link_online = true;
    *last_valid_frame_ms = now_ms;
    RobotState_SetError(ROBOT_ERROR_UART, false, now_ms);
  }
  if (result != PROTOCOL_ACK_OK) ++g_uart_protocol_reject_count;
  QueueAck(frame, result, now_ms);
}

static void HandleParserResult(ProtocolParseResult_t result,
                               uint32_t now_ms,
                               bool *status_requested,
                               bool *link_online,
                               uint32_t *last_valid_frame_ms,
                               uint32_t *frames_processed)
{
  if (result == PROTOCOL_PARSE_FRAME) {
    ++*frames_processed;
    HandleFrame(&received_frame, now_ms, status_requested, link_online,
                last_valid_frame_ms);
  } else if (result == PROTOCOL_PARSE_DROPPED_BAD_CRC) {
    ++g_uart_crc_error_count;
    ++g_uart_protocol_reject_count;
  } else if (result == PROTOCOL_PARSE_DROPPED_TIMEOUT) {
    ++g_uart_parser_timeout_count;
    ++g_uart_protocol_reject_count;
  } else if ((result == PROTOCOL_PARSE_DROPPED_BAD_LENGTH) ||
             (result == PROTOCOL_PARSE_BAD_ARGUMENT)) {
    ++g_uart_protocol_reject_count;
  }
}

static void ProcessReceivedBytes(uint32_t now_ms, bool *status_requested,
                                 bool *link_online,
                                 uint32_t *last_valid_frame_ms)
{
  uint32_t bytes_processed = 0U;
  uint32_t parse_operations = 0U;
  uint32_t frames_processed = 0U;
  uint8_t byte;

  while ((bytes_processed < UART_RX_BYTE_BUDGET) &&
         (frames_processed < UART_FRAME_BUDGET) &&
         (parse_operations < UART_PARSE_OPERATION_BUDGET) &&
         UartDriver_ReadByte(&byte)) {
    ++bytes_processed;
    if (!ProtocolParser_PushByte(&parser, byte, now_ms)) {
      ProtocolParser_Reset(&parser);
      (void)ProtocolParser_PushByte(&parser, byte, now_ms);
      RobotState_SetError(ROBOT_ERROR_UART, true, now_ms);
      ControlArbiter_ReportSourceFault(CONTROL_SOURCE_UART, now_ms);
      ++g_uart_protocol_reject_count;
    }

    for (;;) {
      const ProtocolParseResult_t result = ProtocolParser_Next(
          &parser, now_ms, &received_frame);
      if (result == PROTOCOL_PARSE_NONE) break;
      ++parse_operations;
      HandleParserResult(result, now_ms, status_requested, link_online,
                         last_valid_frame_ms, &frames_processed);
      if ((parse_operations >= UART_PARSE_OPERATION_BUDGET) ||
          (frames_processed >= UART_FRAME_BUDGET)) return;
    }
  }

  /* This call also expires a partial frame when no new byte has arrived. */
  while ((parse_operations < UART_PARSE_OPERATION_BUDGET) &&
         (frames_processed < UART_FRAME_BUDGET)) {
    const ProtocolParseResult_t result = ProtocolParser_Next(
        &parser, now_ms, &received_frame);
    if (result == PROTOCOL_PARSE_NONE) break;
    ++parse_operations;
    HandleParserResult(result, now_ms, status_requested, link_online,
                       last_valid_frame_ms, &frames_processed);
  }
}

static uint8_t BuildStatusFrame(uint16_t sequence)
{
  ProtocolStatus_t status;
  RobotState_GetSnapshot(&status_snapshot);
  status.battery_mv = status_snapshot.battery_valid ?
      status_snapshot.battery_mv : PROTOCOL_INVALID_U16;
  status.applied_left_pwm = status_snapshot.left_output_permille;
  status.applied_right_pwm = status_snapshot.right_output_permille;
  status.distance_mm =
      ((status_snapshot.sensor_valid_mask & SENSOR_VALID_DISTANCE) != 0U) ?
      status_snapshot.distance_mm : PROTOCOL_INVALID_U16;
  status.mode = (uint8_t)status_snapshot.mode;
  status.error_status = status_snapshot.error_status;
  status.valid_flags = 0U;
  if (status_snapshot.battery_valid) {
    status.valid_flags |= PROTOCOL_STATUS_VALID_BATTERY;
  }
  if ((status_snapshot.sensor_valid_mask & SENSOR_VALID_DISTANCE) != 0U) {
    status.valid_flags |= PROTOCOL_STATUS_VALID_DISTANCE;
  }
  status.owner = (uint8_t)ControlArbiter_GetOwner(osKernelGetTickCount());
  return (uint8_t)Protocol_EncodeStatus(sequence, &status, status_buffer,
                                        sizeof(status_buffer));
}

void CommunicationTask_Entry(void *argument)
{
  (void)argument;
  uint32_t now = osKernelGetTickCount();
  uint32_t last_recovery_ms = now - UART_RECOVERY_PERIOD_MS;
  uint32_t last_valid_frame_ms = 0U;
  uint32_t transmit_started_ms = 0U;
  uint32_t next_status_ms = now;
  uint16_t status_sequence = 0U;
  UartTxPurpose_t tx_purpose = UART_TX_PURPOSE_NONE;
  bool driver_initialized = false;
  bool uart_ready = false;
  bool link_online = false;
  bool status_requested = false;

  ProtocolParser_Init(&parser);
  ResponseQueueReset();
  RobotState_SetError(ROBOT_ERROR_UART, true, now);

  for (;;) {
    AppTasks_Heartbeat(APP_TASK_COMMUNICATION);
    MotorStep4Test_Process();
    now = osKernelGetTickCount();

    if (!uart_ready &&
        ((uint32_t)(now - last_recovery_ms) >= UART_RECOVERY_PERIOD_MS)) {
      last_recovery_ms = now;
      (void)osThreadFlagsClear(UART_DRIVER_EVENTS);
      const HAL_StatusTypeDef result = driver_initialized ?
          UartDriver_Recover(osThreadGetId()) :
          UartDriver_Init(osThreadGetId());
      driver_initialized = true;
      uart_ready = result == HAL_OK;
      tx_purpose = UART_TX_PURPOSE_NONE;
      ProtocolParser_Reset(&parser);
      ResponseQueueReset();
      if (uart_ready) next_status_ms = now;
      RobotState_SetError(ROBOT_ERROR_UART, true, now);
    }

    (void)osThreadFlagsWait(UART_DRIVER_EVENTS, osFlagsWaitAny,
                            COMM_TASK_WAIT_MS);
    now = osKernelGetTickCount();
    const uint32_t events = UartDriver_ConsumeEvents();

    if ((events & UART_DRIVER_EVENT_TX_OK) != 0U) {
      if (tx_purpose == UART_TX_PURPOSE_RESPONSE) ResponseQueuePop();
      tx_purpose = UART_TX_PURPOSE_NONE;
    }

    if ((events & UART_DRIVER_EVENT_OVERFLOW) != 0U) {
      ProtocolParser_Reset(&parser);
      RobotState_SetError(ROBOT_ERROR_UART, true, now);
      ControlArbiter_ReportSourceFault(CONTROL_SOURCE_UART, now);
    }

    if ((events & UART_DRIVER_EVENT_ERROR) != 0U) {
      ProtocolParser_Reset(&parser);
      ResponseQueueReset();
      tx_purpose = UART_TX_PURPOSE_NONE;
      uart_ready = false;
      link_online = false;
      RobotState_SetError(ROBOT_ERROR_UART, true, now);
      ControlArbiter_ReportSourceFault(CONTROL_SOURCE_UART, now);
    }

    if (uart_ready) {
      ProcessReceivedBytes(now, &status_requested, &link_online,
                           &last_valid_frame_ms);
      if (UartDriver_Service() != HAL_OK) {
        uart_ready = false;
        link_online = false;
        RobotState_SetError(ROBOT_ERROR_UART, true, now);
        ControlArbiter_ReportSourceFault(CONTROL_SOURCE_UART, now);
      }
    }

    if (link_online &&
        ((uint32_t)(now - last_valid_frame_ms) > UART_LINK_TIMEOUT_MS)) {
      link_online = false;
      RobotState_SetError(ROBOT_ERROR_UART, true, now);
      ControlArbiter_ReportSourceFault(CONTROL_SOURCE_UART, now);
    }

    if (uart_ready && UartDriver_IsTxPending() &&
        ((uint32_t)(now - transmit_started_ms) > UART_TX_TIMEOUT_MS)) {
      ++g_uart_tx_timeout_count;
      uart_ready = false;
      link_online = false;
      tx_purpose = UART_TX_PURPOSE_NONE;
      RobotState_SetError(ROBOT_ERROR_UART, true, now);
      ControlArbiter_ReportSourceFault(CONTROL_SOURCE_UART, now);
    }

    if (uart_ready && !UartDriver_IsTxPending() &&
        (tx_purpose == UART_TX_PURPOSE_NONE)) {
      const bool periodic_status_due =
          (int32_t)(now - next_status_ms) >= 0;
      uint8_t response_size = 0U;
      const uint8_t *response = ResponseQueueFront(&response_size);
      if (periodic_status_due ||
          ((response == NULL) && status_requested)) {
        const uint8_t length = BuildStatusFrame(status_sequence++);
        if ((length > 0U) &&
            (UartDriver_Send(status_buffer, length) == HAL_OK)) {
          tx_purpose = UART_TX_PURPOSE_STATUS;
          transmit_started_ms = now;
          status_requested = false;
          next_status_ms = now + UART_STATUS_PERIOD_MS;
        }
      } else {
        if ((response != NULL) &&
            (UartDriver_Send(response, response_size) == HAL_OK)) {
          tx_purpose = UART_TX_PURPOSE_RESPONSE;
          transmit_started_ms = now;
        }
      }
    }
  }
}
