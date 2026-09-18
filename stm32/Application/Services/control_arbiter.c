#include "control_arbiter.h"

#include <stdbool.h>
#include <stddef.h>

#include "FreeRTOS.h"
#include "app_tasks.h"
#include "motor.h"
#include "task.h"

#define CONTROL_SOURCE_COUNT          3U
#define CONTROL_SESSION_GAP_MS        500U
#define CONTROL_OWNER_LEASE_MS        300U
#define CONTROL_STOP_GUARD_MS         20U
#define CONTROL_CAN_FORWARD_MAX       127U
#define CONTROL_UART_FORWARD_MAX      32767U

typedef struct {
  uint16_t last_sequence;
  uint32_t last_seen_ms;
  bool initialized;
} ControlSequenceState_t;

typedef struct {
  ControlSequenceState_t sequence[CONTROL_SOURCE_COUNT];
  ControlSource_t owner;
  uint32_t owner_last_command_ms;
  uint32_t stop_guard_started_ms;
  uint32_t generation;
  bool stop_guard_active;
} ControlArbiterState_t;

typedef enum {
  SEQUENCE_FORWARD = 0U,
  SEQUENCE_REPLAY
} SequenceResult_t;

static ControlArbiterState_t arbiter;

static bool IsSourceValid(ControlSource_t source)
{
  return (source == CONTROL_SOURCE_CAN) ||
         (source == CONTROL_SOURCE_UART) ||
         (source == CONTROL_SOURCE_TEST);
}

static bool IsModeValid(RobotMode_t mode)
{
  return (mode == ROBOT_MODE_BLE) || (mode == ROBOT_MODE_AUTO);
}

static bool IsOutputInRange(const MotorCommand_t *command)
{
  return (command->left_output_permille >= -MOTOR_OUTPUT_MAX_PERMILLE) &&
         (command->left_output_permille <= MOTOR_OUTPUT_MAX_PERMILLE) &&
         (command->right_output_permille >= -MOTOR_OUTPUT_MAX_PERMILLE) &&
         (command->right_output_permille <= MOTOR_OUTPUT_MAX_PERMILLE);
}

static bool IsMotionCommand(const MotorCommand_t *command)
{
  return command->enable &&
         ((command->left_output_permille != 0) ||
          (command->right_output_permille != 0));
}

static bool IsOwnerLeaseActiveLocked(uint32_t now_ms)
{
  return (arbiter.owner != CONTROL_SOURCE_NONE) &&
         ((uint32_t)(now_ms - arbiter.owner_last_command_ms) <=
          CONTROL_OWNER_LEASE_MS);
}

static bool IsStopGuardActiveLocked(uint32_t now_ms)
{
  if (!arbiter.stop_guard_active) return false;
  if ((uint32_t)(now_ms - arbiter.stop_guard_started_ms) <
      CONTROL_STOP_GUARD_MS) {
    return true;
  }
  arbiter.stop_guard_active = false;
  return false;
}

static void StartStopGuardLocked(uint32_t now_ms)
{
  arbiter.stop_guard_started_ms = now_ms;
  arbiter.stop_guard_active = true;
}

static SequenceResult_t CheckSequenceLocked(ControlSource_t source,
                                            uint16_t sequence,
                                            uint32_t now_ms)
{
  ControlSequenceState_t *state = &arbiter.sequence[(uint8_t)source];
  const bool can_source = source == CONTROL_SOURCE_CAN;
  const uint16_t normalized = can_source ? (uint8_t)sequence : sequence;
  const bool new_session = !state->initialized ||
      ((uint32_t)(now_ms - state->last_seen_ms) > CONTROL_SESSION_GAP_MS);

  /* Replays also count as traffic, so repeated old frames cannot reopen a
     session and become executable merely by keeping the link busy. */
  state->last_seen_ms = now_ms;
  if (new_session) {
    state->initialized = true;
    state->last_sequence = normalized;
    return SEQUENCE_FORWARD;
  }

  if (can_source) {
    const uint8_t delta = (uint8_t)((uint8_t)normalized -
                                    (uint8_t)state->last_sequence);
    if ((delta >= 1U) && (delta <= CONTROL_CAN_FORWARD_MAX)) {
      state->last_sequence = normalized;
      return SEQUENCE_FORWARD;
    }
  } else {
    const uint16_t delta = (uint16_t)(normalized - state->last_sequence);
    if ((delta >= 1U) && (delta <= CONTROL_UART_FORWARD_MAX)) {
      state->last_sequence = normalized;
      return SEQUENCE_FORWARD;
    }
  }
  return SEQUENCE_REPLAY;
}

static void HandleSubmitFailure(ControlSource_t source,
                                uint32_t generation,
                                uint32_t now_ms)
{
  bool stop_required = false;

  taskENTER_CRITICAL();
  if ((arbiter.generation == generation) && (arbiter.owner == source)) {
    arbiter.owner = CONTROL_SOURCE_NONE;
    StartStopGuardLocked(now_ms);
    ++arbiter.generation;
    stop_required = true;
  }
  taskEXIT_CRITICAL();

  if (stop_required) {
    AppTasks_RequestMotorStop(APP_MOTOR_STOP_SOURCE_FAULT);
  }
}

static ControlResult_t Submit(ControlSource_t source,
                              uint16_t sequence,
                              const MotorCommand_t *command,
                              uint32_t now_ms)
{
  RobotStatus_t status;
  SequenceResult_t sequence_result;
  uint32_t accepted_generation = 0U;
  bool request_user_stop = false;
  bool request_handoff_stop = false;
  bool submit_motion = false;
  bool motor_power_allowed = true;
  ControlResult_t result;

  if ((command == NULL) || !IsSourceValid(source)) {
    return CONTROL_RESULT_INTERNAL_ERROR;
  }
  if (!IsOutputInRange(command)) return CONTROL_RESULT_RANGE_REJECTED;
  if (!IsModeValid(command->mode)) return CONTROL_RESULT_MODE_REJECTED;

  const bool requests_motion = IsMotionCommand(command);
  if (requests_motion &&
      (((source == CONTROL_SOURCE_CAN) &&
        (command->mode != ROBOT_MODE_AUTO)) ||
       ((source == CONTROL_SOURCE_UART) &&
        (command->mode != ROBOT_MODE_BLE)))) {
    return CONTROL_RESULT_MODE_REJECTED;
  }

  /* No arbiter caller runs from an ISR. Suspending scheduling makes the
     ownership decision and its queue/STOP side effect one transaction. */
  vTaskSuspendAll();
  if (requests_motion) {
    RobotState_GetSnapshot(&status);
    if (status.mode != command->mode) {
      result = CONTROL_RESULT_MODE_REJECTED;
      goto resume_scheduler;
    }
    motor_power_allowed = RobotState_IsMotorPowerAllowed(now_ms);
  }

  taskENTER_CRITICAL();
  sequence_result = CheckSequenceLocked(source, sequence, now_ms);

  if (!requests_motion) {
    /* STOP is idempotent and executes even for a duplicate/old sequence. */
    StartStopGuardLocked(now_ms);
    ++arbiter.generation;
    request_user_stop = true;
    result = CONTROL_RESULT_STOP_ACCEPTED;
  } else if (sequence_result == SEQUENCE_REPLAY) {
    result = CONTROL_RESULT_REPLAY;
  } else if (!motor_power_allowed) {
    /* Consume this forward sequence, but never acquire ownership or enqueue
       it. Recovery therefore requires a genuinely newer command. */
    result = CONTROL_RESULT_SAFETY_BLOCKED;
  } else if ((arbiter.owner != CONTROL_SOURCE_NONE) &&
             (arbiter.owner != source) &&
             !IsOwnerLeaseActiveLocked(now_ms)) {
    /* Handle an expired foreign owner before the generic stop guard. This
       guarantees that the replacement source's first forward sequence is
       the one that clears the old ownership, even if a STOP just arrived. */
    arbiter.owner = CONTROL_SOURCE_NONE;
    StartStopGuardLocked(now_ms);
    ++arbiter.generation;
    request_handoff_stop = true;
    result = CONTROL_RESULT_HANDOFF_STOP;
  } else if (IsStopGuardActiveLocked(now_ms)) {
    /* The forward sequence is deliberately consumed. The sender must issue
       its next sequence after the physical stop has had time to take effect. */
    result = CONTROL_RESULT_HANDOFF_STOP;
  } else if (arbiter.owner == CONTROL_SOURCE_NONE) {
    arbiter.owner = source;
    arbiter.owner_last_command_ms = now_ms;
    accepted_generation = ++arbiter.generation;
    submit_motion = true;
    result = CONTROL_RESULT_ACCEPTED;
  } else if (arbiter.owner == source) {
    /* The same source may reacquire immediately after its own lease expires. */
    arbiter.owner_last_command_ms = now_ms;
    accepted_generation = ++arbiter.generation;
    submit_motion = true;
    result = CONTROL_RESULT_ACCEPTED;
  } else {
    /* The only remaining case is a foreign owner with an active lease. A
       forward non-owner sequence is consumed but cannot refresh that lease. */
    result = CONTROL_RESULT_OWNER_BUSY;
  }
  taskEXIT_CRITICAL();

  if (request_user_stop) {
    AppTasks_RequestMotorStop(APP_MOTOR_STOP_USER);
  } else if (request_handoff_stop) {
    AppTasks_RequestMotorStop(APP_MOTOR_STOP_HANDOFF);
  } else if (submit_motion &&
             !AppTasks_SubmitMotorCommandInternal(command)) {
    HandleSubmitFailure(source, accepted_generation, now_ms);
    result = CONTROL_RESULT_INTERNAL_ERROR;
  }
resume_scheduler:
  (void)xTaskResumeAll();
  return result;
}

void ControlArbiter_Init(void)
{
  taskENTER_CRITICAL();
  for (uint32_t index = 0U; index < CONTROL_SOURCE_COUNT; ++index) {
    arbiter.sequence[index].last_sequence = 0U;
    arbiter.sequence[index].last_seen_ms = 0U;
    arbiter.sequence[index].initialized = false;
  }
  arbiter.owner = CONTROL_SOURCE_NONE;
  arbiter.owner_last_command_ms = 0U;
  arbiter.stop_guard_started_ms = 0U;
  arbiter.stop_guard_active = false;
  arbiter.generation = 0U;
  taskEXIT_CRITICAL();
}

ControlResult_t ControlArbiter_SubmitCan(
    uint8_t sequence, const MotorCommand_t *command, uint32_t now_ms)
{
  return Submit(CONTROL_SOURCE_CAN, sequence, command, now_ms);
}

ControlResult_t ControlArbiter_SubmitUart(
    uint16_t sequence, const MotorCommand_t *command, uint32_t now_ms)
{
  return Submit(CONTROL_SOURCE_UART, sequence, command, now_ms);
}

ControlResult_t ControlArbiter_SubmitTest(
    uint16_t sequence, const MotorCommand_t *command, uint32_t now_ms)
{
  return Submit(CONTROL_SOURCE_TEST, sequence, command, now_ms);
}

ControlResult_t ControlArbiter_SetModeUart(
    uint16_t sequence, RobotMode_t mode, uint32_t now_ms)
{
  SequenceResult_t sequence_result;

  if (!IsModeValid(mode)) return CONTROL_RESULT_MODE_REJECTED;

  vTaskSuspendAll();
  taskENTER_CRITICAL();
  sequence_result = CheckSequenceLocked(CONTROL_SOURCE_UART, sequence, now_ms);
  if (sequence_result == SEQUENCE_FORWARD) {
    arbiter.owner = CONTROL_SOURCE_NONE;
    StartStopGuardLocked(now_ms);
    ++arbiter.generation;
  }
  taskEXIT_CRITICAL();

  if (sequence_result == SEQUENCE_REPLAY) {
    (void)xTaskResumeAll();
    return CONTROL_RESULT_REPLAY;
  }

  AppTasks_RequestMotorStop(APP_MOTOR_STOP_USER);
  RobotState_SetMode(mode, now_ms);
  (void)xTaskResumeAll();
  return CONTROL_RESULT_ACCEPTED;
}

void ControlArbiter_ReportSourceFault(ControlSource_t source,
                                      uint32_t now_ms)
{
  bool stop_required = false;

  if (!IsSourceValid(source)) return;
  vTaskSuspendAll();
  taskENTER_CRITICAL();
  if (arbiter.owner == source) {
    arbiter.owner = CONTROL_SOURCE_NONE;
    StartStopGuardLocked(now_ms);
    ++arbiter.generation;
    stop_required = true;
  }
  taskEXIT_CRITICAL();

  if (stop_required) {
    AppTasks_RequestMotorStop(APP_MOTOR_STOP_SOURCE_FAULT);
  }
  (void)xTaskResumeAll();
}

ControlSource_t ControlArbiter_GetOwner(uint32_t now_ms)
{
  ControlSource_t owner;

  taskENTER_CRITICAL();
  owner = IsOwnerLeaseActiveLocked(now_ms) ?
      arbiter.owner : CONTROL_SOURCE_NONE;
  taskEXIT_CRITICAL();
  return owner;
}
