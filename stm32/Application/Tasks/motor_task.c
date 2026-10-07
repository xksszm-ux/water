#include "app_tasks.h"
#include "motor_task.h"
#include "task_entries.h"
#include "main.h"
#include "motor.h"
#include "power_supply_guard.h"

#define MOTOR_COMMAND_TIMEOUT_MS 300U
#define MOTOR_CONTROL_PERIOD_MS 10U
#define MOTOR_REVERSAL_DEADTIME_MS MOTOR_CONTROL_PERIOD_MS
#define MOTOR_REVERSAL_RESTART_LIMIT_PERMILLE 200

static bool IsDirectionReversed(int16_t applied, int16_t requested)
{
  return ((applied > 0) && (requested < 0)) ||
         ((applied < 0) && (requested > 0));
}

static int16_t LimitRestartOutput(int16_t output)
{
  if (output > MOTOR_REVERSAL_RESTART_LIMIT_PERMILLE) {
    return MOTOR_REVERSAL_RESTART_LIMIT_PERMILLE;
  }
  if (output < -MOTOR_REVERSAL_RESTART_LIMIT_PERMILLE) {
    return -MOTOR_REVERSAL_RESTART_LIMIT_PERMILLE;
  }
  return output;
}

/* A cycle is separated from scheduling so host checks execute production logic. */
void MotorTask_InitState(MotorTaskState_t *state)
{
  *state = (MotorTaskState_t){0};
  if (MotorDriver_Init() != HAL_OK) Error_Handler();
  MotorDriver_Stop(MOTOR_STOP_STANDBY);
  RobotState_UpdateMotorOutput(0, 0, osKernelGetTickCount());
}

void MotorTask_RunCycle(MotorTaskState_t *state)
{
  AppTasks_Heartbeat(APP_TASK_MOTOR);
  MotorCommand_t latest;
  const uint32_t now = osKernelGetTickCount();
  (void)AppTasks_HealthPoll();
  const uint8_t stop_reasons = AppTasks_ConsumeMotorStopRequest();
  const bool stop_requested = stop_reasons != 0U;
  if (stop_requested) {
    /* Discard motion queued behind STOP; the sender must issue a new frame. */
    while (osMessageQueueGet(g_motor_command_queue, &latest, NULL, 0U) ==
           osOK) {
    }
    if ((stop_reasons & APP_MOTOR_STOP_SOURCE_FAULT) != 0U) {
      RobotState_SetError(ROBOT_ERROR_COMM_TIMEOUT, true, now);
    } else if ((stop_reasons & APP_MOTOR_STOP_USER) != 0U) {
      RobotState_SetError(ROBOT_ERROR_COMM_TIMEOUT, false, now);
    }
  } else if (osMessageQueueGet(g_motor_command_queue, &latest, NULL, 0U) ==
             osOK) {
    state->command = latest;
    state->command_seen = true;
    RobotState_SetError(ROBOT_ERROR_COMM_TIMEOUT, false, now);
  }

  RobotState_InvalidateBatteryIfStale();
  const bool supply_safe = PowerSupplyGuard_IsSafe();
  if (!supply_safe) RobotState_InvalidateBattery(now);
  const bool motor_power_allowed = supply_safe &&
      RobotState_IsMotorPowerAllowed();
  const bool command_expired = state->command_seen &&
      ((uint32_t)(now - state->command.issued_at_ms) > MOTOR_COMMAND_TIMEOUT_MS);
  if (!motor_power_allowed) {
    state->command.left_output_permille = 0;
    state->command.right_output_permille = 0;
    state->command.enable = false;
    state->command_seen = false;
    state->applied_left = 0;
    state->applied_right = 0;
    state->reversal_state = MOTOR_REVERSAL_IDLE;
    MotorDriver_Stop(MOTOR_STOP_STANDBY);
    RobotState_UpdateMotorOutput(0, 0, now);
  } else if (stop_requested) {
    state->command.left_output_permille = 0;
    state->command.right_output_permille = 0;
    state->command.enable = false;
    state->command_seen = false;
    state->applied_left = 0;
    state->applied_right = 0;
    state->reversal_state = MOTOR_REVERSAL_IDLE;
    MotorDriver_Stop(MOTOR_STOP_STANDBY);
    RobotState_UpdateMotorOutput(0, 0, now);
  } else if (command_expired) {
    state->command.left_output_permille = 0;
    state->command.right_output_permille = 0;
    state->command.enable = false;
    state->command_seen = false;
    state->applied_left = 0;
    state->applied_right = 0;
    state->reversal_state = MOTOR_REVERSAL_IDLE;
    MotorDriver_Stop(MOTOR_STOP_STANDBY);
    RobotState_UpdateMotorOutput(0, 0, now);
    RobotState_SetError(ROBOT_ERROR_COMM_TIMEOUT, true, now);
  } else if (state->command_seen) {
    const int16_t requested_left = state->command.enable ? state->command.left_output_permille : 0;
    const int16_t requested_right = state->command.enable ? state->command.right_output_permille : 0;

    if (!state->command.enable || ((requested_left == 0) && (requested_right == 0))) {
      MotorDriver_Stop(MOTOR_STOP_STANDBY);
      RobotState_UpdateMotorOutput(0, 0, now);
      state->applied_left = 0;
      state->applied_right = 0;
      state->reversal_state = MOTOR_REVERSAL_IDLE;
    } else if (state->reversal_state == MOTOR_REVERSAL_WAIT) {
      MotorDriver_Stop(MOTOR_STOP_STANDBY);
      RobotState_UpdateMotorOutput(0, 0, now);
      if ((uint32_t)(now - state->reversal_started_ms) >= MOTOR_REVERSAL_DEADTIME_MS) {
        state->applied_left = LimitRestartOutput(requested_left);
        state->applied_right = LimitRestartOutput(requested_right);
        if (MotorDriver_SetOutput(state->applied_left, state->applied_right)) {
          RobotState_UpdateMotorOutput(state->applied_left, state->applied_right, now);
          state->reversal_state = MOTOR_REVERSAL_RESTART;
        } else {
          state->applied_left = 0;
          state->applied_right = 0;
          state->command_seen = false;
          state->reversal_state = MOTOR_REVERSAL_IDLE;
          RobotState_UpdateMotorOutput(0, 0, now);
        }
      }
    } else if (state->reversal_state == MOTOR_REVERSAL_RESTART) {
      if (IsDirectionReversed(state->applied_left, requested_left) ||
          IsDirectionReversed(state->applied_right, requested_right)) {
        MotorDriver_Stop(MOTOR_STOP_STANDBY);
        RobotState_UpdateMotorOutput(0, 0, now);
        state->applied_left = 0;
        state->applied_right = 0;
        state->reversal_started_ms = now;
        state->reversal_state = MOTOR_REVERSAL_WAIT;
      } else {
        if (MotorDriver_SetOutput(requested_left, requested_right)) {
          RobotState_UpdateMotorOutput(requested_left, requested_right, now);
          state->applied_left = requested_left;
          state->applied_right = requested_right;
          state->reversal_state = MOTOR_REVERSAL_IDLE;
        } else {
          state->applied_left = 0;
          state->applied_right = 0;
          state->command_seen = false;
          state->reversal_state = MOTOR_REVERSAL_IDLE;
          RobotState_UpdateMotorOutput(0, 0, now);
        }
      }
    } else if (IsDirectionReversed(state->applied_left, requested_left) ||
               IsDirectionReversed(state->applied_right, requested_right)) {
      MotorDriver_Stop(MOTOR_STOP_STANDBY);
      RobotState_UpdateMotorOutput(0, 0, now);
      state->applied_left = 0;
      state->applied_right = 0;
      state->reversal_started_ms = now;
      state->reversal_state = MOTOR_REVERSAL_WAIT;
    } else {
      if (MotorDriver_SetOutput(requested_left, requested_right)) {
        RobotState_UpdateMotorOutput(requested_left, requested_right, now);
        state->applied_left = requested_left;
        state->applied_right = requested_right;
      } else {
        state->applied_left = 0;
        state->applied_right = 0;
        state->command_seen = false;
        state->reversal_state = MOTOR_REVERSAL_IDLE;
        RobotState_UpdateMotorOutput(0, 0, now);
      }
    }
  }

}

void MotorTask_Entry(void *argument)
{
  (void)argument;
  uint32_t next_wake = osKernelGetTickCount();
  MotorTaskState_t state;
  MotorTask_InitState(&state);
  for (;;) {
    MotorTask_RunCycle(&state);
    next_wake += MOTOR_CONTROL_PERIOD_MS;
    if (osDelayUntil(next_wake) != osOK) next_wake = osKernelGetTickCount();
  }
}
