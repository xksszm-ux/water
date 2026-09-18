#include "app_tasks.h"
#include "can.h"
#include "can_driver.h"
#include "can_protocol.h"
#include "control_arbiter.h"
#include "robot_state.h"
#include "task_entries.h"

#include <string.h>

#define CAN_TASK_WAIT_MS             10U
#define CAN_STATUS_PERIOD_MS         100U
#define CAN_TX_TIMEOUT_MS            50U
#define CAN_RECOVERY_PERIOD_MS       1000U
#define CAN_RESPONSE_QUEUE_LENGTH    4U
#define CAN_RX_PROCESS_BUDGET        4U
#define CAN_DRIVER_EVENTS                                                \
  (CAN_DRIVER_EVENT_RX | CAN_DRIVER_EVENT_TX_OK | CAN_DRIVER_EVENT_ERROR)

typedef enum {
  CAN_TX_PURPOSE_NONE = 0U,
  CAN_TX_PURPOSE_STATUS,
  CAN_TX_PURPOSE_RESPONSE,
#if CAN_DRIVER_INTERNAL_LOOPBACK_TEST
  CAN_TX_PURPOSE_LOOPBACK_TX_PROBE,
  CAN_TX_PURPOSE_LOOPBACK_CONTROL
#endif
} CanTxPurpose_t;

volatile uint32_t g_can_protocol_reject_count;
volatile uint32_t g_can_pid_unsupported_count;
volatile uint32_t g_can_duplicate_command_count;
volatile bool g_can_loopback_self_test_passed;

static uint8_t response_queue[CAN_RESPONSE_QUEUE_LENGTH]
                             [CAN_PROTOCOL_FRAME_SIZE];
static uint8_t response_head;
static uint8_t response_tail;
static uint8_t response_count;

static bool ResponseQueuePush(const uint8_t data[CAN_PROTOCOL_FRAME_SIZE])
{
  if ((data == NULL) || (response_count >= CAN_RESPONSE_QUEUE_LENGTH)) {
    return false;
  }
  memcpy(response_queue[response_head], data, CAN_PROTOCOL_FRAME_SIZE);
  response_head = (uint8_t)((response_head + 1U) % CAN_RESPONSE_QUEUE_LENGTH);
  ++response_count;
  return true;
}

static const uint8_t *ResponseQueueFront(void)
{
  return response_count > 0U ? response_queue[response_tail] : NULL;
}

static void ResponseQueuePop(void)
{
  if (response_count == 0U) return;
  response_tail = (uint8_t)((response_tail + 1U) %
                            CAN_RESPONSE_QUEUE_LENGTH);
  --response_count;
}

static bool SubmitControlCommand(const CanMotorCommand_t *received,
                                 uint32_t now_ms)
{
  MotorCommand_t command = {0};
  const bool requests_motion = received->enable &&
      ((received->left_output_permille != 0) ||
       (received->right_output_permille != 0));
  command.left_output_permille = requests_motion ?
      received->left_output_permille : 0;
  command.right_output_permille = requests_motion ?
      received->right_output_permille : 0;
  command.mode = received->mode;
  command.enable = requests_motion;
  const ControlResult_t result = ControlArbiter_SubmitCan(
      received->sequence, &command, now_ms);
  if (result == CONTROL_RESULT_REPLAY) ++g_can_duplicate_command_count;
  return (result == CONTROL_RESULT_ACCEPTED) ||
         (result == CONTROL_RESULT_STOP_ACCEPTED) ||
         (result == CONTROL_RESULT_REPLAY) ||
         (result == CONTROL_RESULT_OWNER_BUSY) ||
         (result == CONTROL_RESULT_HANDOFF_STOP);
}

static void ProcessEmergencyStopOnly(uint32_t now_ms)
{
  CanDriverFrame_t frame;
  CanMotorCommand_t command;
  for (uint32_t count = 0U; count < CAN_RX_PROCESS_BUDGET; ++count) {
    if (!CanDriver_Receive(&frame)) break;
    if (frame.standard_id != CAN_ID_MOTOR_COMMAND) continue;
    if (CanProtocol_DecodeControl(frame.data, frame.dlc, &command) !=
        CAN_PROTOCOL_OK) continue;
    if (command.enable &&
        ((command.left_output_permille != 0) ||
         (command.right_output_permille != 0))) continue;
    (void)SubmitControlCommand(&command, now_ms);
  }
}

void CanTask_Entry(void *argument)
{
  (void)argument;
  CanDriverFrame_t frame;
  CanMotorCommand_t received_command;
  CanPidConfig_t pid_config;
  RobotStatus_t snapshot;
  uint8_t tx_data[CAN_PROTOCOL_FRAME_SIZE];
  uint32_t next_status = osKernelGetTickCount();
  uint32_t last_recovery = osKernelGetTickCount() - CAN_RECOVERY_PERIOD_MS;
  CanTxPurpose_t tx_purpose = CAN_TX_PURPOSE_NONE;
  bool driver_initialized = false;
  bool can_ready = false;
#if CAN_DRIVER_INTERNAL_LOOPBACK_TEST
  bool loopback_tx_probe_pending = true;
  bool loopback_control_pending = false;
  bool loopback_tx_probe_completed = false;
  bool loopback_control_tx_completed = false;
  bool loopback_control_submitted = false;
#endif

  response_head = 0U;
  response_tail = 0U;
  response_count = 0U;
  g_can_loopback_self_test_passed = false;
  RobotState_SetError(ROBOT_ERROR_CAN, true, osKernelGetTickCount());
  for (;;) {
    AppTasks_Heartbeat(APP_TASK_CAN);
    uint32_t now = osKernelGetTickCount();

    if (!can_ready &&
        ((uint32_t)(now - last_recovery) >= CAN_RECOVERY_PERIOD_MS)) {
      last_recovery = now;
      (void)osThreadFlagsClear(CAN_DRIVER_EVENTS);
      const HAL_StatusTypeDef result = driver_initialized ?
          CanDriver_Recover() : CanDriver_Init(&hcan, osThreadGetId());
      driver_initialized = true;
      can_ready = result == HAL_OK;
      tx_purpose = CAN_TX_PURPOSE_NONE;
      if (can_ready) next_status = now;
      RobotState_SetError(ROBOT_ERROR_CAN, true, now);
    }

    const uint32_t events = osThreadFlagsWait(
        CAN_DRIVER_EVENTS, osFlagsWaitAny, CAN_TASK_WAIT_MS);
    now = osKernelGetTickCount();
    if ((events & osFlagsError) == 0U) {
      if ((events & CAN_DRIVER_EVENT_TX_OK) != 0U) {
        if (tx_purpose == CAN_TX_PURPOSE_RESPONSE) ResponseQueuePop();
#if CAN_DRIVER_INTERNAL_LOOPBACK_TEST
        if (tx_purpose == CAN_TX_PURPOSE_LOOPBACK_TX_PROBE) {
          loopback_tx_probe_completed = true;
          loopback_control_pending = true;
        } else if (tx_purpose == CAN_TX_PURPOSE_LOOPBACK_CONTROL) {
          loopback_control_tx_completed = true;
        }
#endif
        tx_purpose = CAN_TX_PURPOSE_NONE;
        CanDriver_ClearErrors();
        RobotState_SetError(ROBOT_ERROR_CAN, false, now);
      }
      if ((events & CAN_DRIVER_EVENT_ERROR) != 0U) {
        RobotState_SetError(ROBOT_ERROR_CAN, true, now);
      }
    }

    if (can_ready) {
      const HAL_StatusTypeDef service =
          CanDriver_Service(now, CAN_TX_TIMEOUT_MS);
      if (service != HAL_OK) {
        RobotState_SetError(ROBOT_ERROR_CAN, true, now);
      }
      if ((service == HAL_TIMEOUT) || (service == HAL_ERROR)) {
#if CAN_DRIVER_INTERNAL_LOOPBACK_TEST
        if (tx_purpose == CAN_TX_PURPOSE_LOOPBACK_TX_PROBE) {
          loopback_tx_probe_pending = true;
          loopback_tx_probe_completed = false;
        } else if (tx_purpose == CAN_TX_PURPOSE_LOOPBACK_CONTROL) {
          loopback_control_pending = true;
          loopback_control_tx_completed = false;
        }
#endif
        tx_purpose = CAN_TX_PURPOSE_NONE;
      }
      if (service == HAL_ERROR) {
        /* A failed control path may have queued a STOP before it failed.
           Execute only STOP; never let buffered motion pass the fault. */
        ControlArbiter_ReportSourceFault(CONTROL_SOURCE_CAN, now);
        ProcessEmergencyStopOnly(now);
        can_ready = false;
        last_recovery = now - CAN_RECOVERY_PERIOD_MS;
      }
    }

    for (uint32_t rx_count = 0U;
         can_ready && (rx_count < CAN_RX_PROCESS_BUDGET); ++rx_count) {
      if (!CanDriver_Receive(&frame)) break;
      if (frame.standard_id == CAN_ID_MOTOR_COMMAND) {
        const CanProtocolResult_t result = CanProtocol_DecodeControl(
            frame.data, frame.dlc, &received_command);
        const bool submitted = (result == CAN_PROTOCOL_OK) &&
            SubmitControlCommand(&received_command, now);
#if CAN_DRIVER_INTERNAL_LOOPBACK_TEST
        if (submitted && !received_command.enable &&
            (received_command.sequence == 0xA5U)) {
          loopback_control_submitted = true;
        }
#endif
        if (!submitted) ++g_can_protocol_reject_count;
      } else if (frame.standard_id == CAN_ID_PID_CONFIG) {
        pid_config = (CanPidConfig_t){0};
        CanProtocolResult_t result = CanProtocol_DecodePidConfig(
            frame.data, frame.dlc, &pid_config);
        if (result == CAN_PROTOCOL_OK) {
          /* V1 has no wheel encoders or speed PID: reject explicitly. */
          result = CAN_PROTOCOL_UNSUPPORTED;
          ++g_can_pid_unsupported_count;
        } else {
          ++g_can_protocol_reject_count;
        }
        CanProtocol_EncodeConfigResponse(&pid_config, result, tx_data);
        if (!ResponseQueuePush(tx_data)) ++g_can_protocol_reject_count;
      }
    }

    if (can_ready && !CanDriver_IsTransmitPending() &&
        (tx_purpose == CAN_TX_PURPOSE_NONE)) {
#if CAN_DRIVER_INTERNAL_LOOPBACK_TEST
      if (loopback_tx_probe_pending) {
        /* 0x101 is not in the RX filter, so only the TX IRQ can complete it. */
        RobotState_GetSnapshot(&snapshot);
        CanProtocol_EncodeStatus(&snapshot, tx_data);
        if (CanDriver_Send(CAN_ID_ROBOT_STATUS, tx_data,
                           CAN_PROTOCOL_FRAME_SIZE) == HAL_OK) {
          loopback_tx_probe_pending = false;
          tx_purpose = CAN_TX_PURPOSE_LOOPBACK_TX_PROBE;
        }
      } else if (loopback_control_pending) {
        const CanMotorCommand_t stop = {
          .left_output_permille = 0,
          .right_output_permille = 0,
          .mode = ROBOT_MODE_AUTO,
          .enable = false,
          .sequence = 0xA5U
        };
        CanProtocol_EncodeControl(&stop, tx_data);
        if (CanDriver_Send(CAN_ID_MOTOR_COMMAND, tx_data,
                           CAN_PROTOCOL_FRAME_SIZE) == HAL_OK) {
          loopback_control_pending = false;
          tx_purpose = CAN_TX_PURPOSE_LOOPBACK_CONTROL;
        }
      } else
#endif
      {
        const uint8_t *response = ResponseQueueFront();
        if ((int32_t)(now - next_status) >= 0) {
          RobotState_GetSnapshot(&snapshot);
          CanProtocol_EncodeStatus(&snapshot, tx_data);
          if (CanDriver_Send(CAN_ID_ROBOT_STATUS, tx_data,
                             CAN_PROTOCOL_FRAME_SIZE) == HAL_OK) {
            tx_purpose = CAN_TX_PURPOSE_STATUS;
          } else {
            RobotState_SetError(ROBOT_ERROR_CAN, true, now);
          }
          next_status += CAN_STATUS_PERIOD_MS;
          if ((int32_t)(now - next_status) >= 0) {
            next_status = now + CAN_STATUS_PERIOD_MS;
          }
        } else if (response != NULL) {
          if (CanDriver_Send(CAN_ID_CONFIG_RESPONSE, response,
                             CAN_PROTOCOL_FRAME_SIZE) == HAL_OK) {
            tx_purpose = CAN_TX_PURPOSE_RESPONSE;
          }
        }
      }
    }

#if CAN_DRIVER_INTERNAL_LOOPBACK_TEST
    if (loopback_tx_probe_completed && loopback_control_tx_completed &&
        loopback_control_submitted) {
      CanDriverDiagnostics_t diagnostics;
      CanDriver_GetDiagnostics(&diagnostics);
      g_can_loopback_self_test_passed =
          (diagnostics.transmitted_frames >= 2U) &&
          (diagnostics.transmit_timeouts == 0U);
    }
#endif
  }
}
