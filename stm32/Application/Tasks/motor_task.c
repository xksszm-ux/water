#include "app_tasks.h"
#include "task_entries.h"
#include "main.h"
#include "motor.h"
#include "power_supply_guard.h"

#define MOTOR_COMMAND_TIMEOUT_MS 300U
#define MOTOR_CONTROL_PERIOD_MS 10U
#define MOTOR_REVERSAL_DEADTIME_MS MOTOR_CONTROL_PERIOD_MS
#define MOTOR_REVERSAL_RESTART_LIMIT_PERMILLE 200

typedef enum {
  MOTOR_REVERSAL_IDLE = 0,
  MOTOR_REVERSAL_WAIT,
  MOTOR_REVERSAL_RESTART
} MotorReversalState_t;

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

void MotorTask_Entry(void *argument)
{
  (void)argument;
  uint32_t next_wake = osKernelGetTickCount();
  MotorCommand_t command = {0};
  bool command_seen = false;
  int16_t applied_left = 0;
  int16_t applied_right = 0;
  uint32_t reversal_started_ms = 0U;
  MotorReversalState_t reversal_state = MOTOR_REVERSAL_IDLE;
  if (MotorDriver_Init() != HAL_OK) Error_Handler();
  MotorDriver_Stop(MOTOR_STOP_STANDBY);
  RobotState_UpdateMotorOutput(0, 0, osKernelGetTickCount());
  for (;;) {
    AppTasks_Heartbeat(APP_TASK_MOTOR);
    MotorCommand_t latest;
    const uint32_t now = osKernelGetTickCount();
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
      command = latest;
      command_seen = true;
      RobotState_SetError(ROBOT_ERROR_COMM_TIMEOUT, false, now);
    }

    RobotState_InvalidateBatteryIfStale(now);
    const bool supply_safe = PowerSupplyGuard_IsSafe();
    if (!supply_safe) RobotState_InvalidateBattery(now);
    const bool motor_power_allowed = supply_safe &&
        RobotState_IsMotorPowerAllowed(now);
    const bool command_expired = command_seen &&
        ((uint32_t)(now - command.issued_at_ms) > MOTOR_COMMAND_TIMEOUT_MS);
    if (!motor_power_allowed) {
      command.left_output_permille = 0;
      command.right_output_permille = 0;
      command.enable = false;
      command_seen = false;
      applied_left = 0;
      applied_right = 0;
      reversal_state = MOTOR_REVERSAL_IDLE;
      MotorDriver_Stop(MOTOR_STOP_STANDBY);
      RobotState_UpdateMotorOutput(0, 0, now);
    } else if (stop_requested) {
      command.left_output_permille = 0;
      command.right_output_permille = 0;
      command.enable = false;
      command_seen = false;
      applied_left = 0;
      applied_right = 0;
      reversal_state = MOTOR_REVERSAL_IDLE;
      MotorDriver_Stop(MOTOR_STOP_STANDBY);
      RobotState_UpdateMotorOutput(0, 0, now);
    } else if (command_expired) {
      command.left_output_permille = 0;
      command.right_output_permille = 0;
      command.enable = false;
      command_seen = false;
      applied_left = 0;
      applied_right = 0;
      reversal_state = MOTOR_REVERSAL_IDLE;
      MotorDriver_Stop(MOTOR_STOP_STANDBY);
      RobotState_UpdateMotorOutput(0, 0, now);
      RobotState_SetError(ROBOT_ERROR_COMM_TIMEOUT, true, now);
    } else if (command_seen) {
      const int16_t requested_left = command.enable ? command.left_output_permille : 0;
      const int16_t requested_right = command.enable ? command.right_output_permille : 0;

      if (!command.enable || ((requested_left == 0) && (requested_right == 0))) {
        MotorDriver_Stop(MOTOR_STOP_STANDBY);
        RobotState_UpdateMotorOutput(0, 0, now);
        applied_left = 0;
        applied_right = 0;
        reversal_state = MOTOR_REVERSAL_IDLE;
      } else if (reversal_state == MOTOR_REVERSAL_WAIT) {
        MotorDriver_Stop(MOTOR_STOP_STANDBY);
        RobotState_UpdateMotorOutput(0, 0, now);
        if ((uint32_t)(now - reversal_started_ms) >= MOTOR_REVERSAL_DEADTIME_MS) {
          applied_left = LimitRestartOutput(requested_left);
          applied_right = LimitRestartOutput(requested_right);
          if (MotorDriver_SetOutput(applied_left, applied_right)) {
            RobotState_UpdateMotorOutput(applied_left, applied_right, now);
            reversal_state = MOTOR_REVERSAL_RESTART;
          } else {
            applied_left = 0;
            applied_right = 0;
            command_seen = false;
            reversal_state = MOTOR_REVERSAL_IDLE;
            RobotState_UpdateMotorOutput(0, 0, now);
          }
        }
      } else if (reversal_state == MOTOR_REVERSAL_RESTART) {
        if (IsDirectionReversed(applied_left, requested_left) ||
            IsDirectionReversed(applied_right, requested_right)) {
          MotorDriver_Stop(MOTOR_STOP_STANDBY);
          RobotState_UpdateMotorOutput(0, 0, now);
          applied_left = 0;
          applied_right = 0;
          reversal_started_ms = now;
          reversal_state = MOTOR_REVERSAL_WAIT;
        } else {
          if (MotorDriver_SetOutput(requested_left, requested_right)) {
            RobotState_UpdateMotorOutput(requested_left, requested_right, now);
            applied_left = requested_left;
            applied_right = requested_right;
            reversal_state = MOTOR_REVERSAL_IDLE;
          } else {
            applied_left = 0;
            applied_right = 0;
            command_seen = false;
            reversal_state = MOTOR_REVERSAL_IDLE;
            RobotState_UpdateMotorOutput(0, 0, now);
          }
        }
      } else if (IsDirectionReversed(applied_left, requested_left) ||
                 IsDirectionReversed(applied_right, requested_right)) {
        MotorDriver_Stop(MOTOR_STOP_STANDBY);
        RobotState_UpdateMotorOutput(0, 0, now);
        applied_left = 0;
        applied_right = 0;
        reversal_started_ms = now;
        reversal_state = MOTOR_REVERSAL_WAIT;
      } else {
        if (MotorDriver_SetOutput(requested_left, requested_right)) {
          RobotState_UpdateMotorOutput(requested_left, requested_right, now);
          applied_left = requested_left;
          applied_right = requested_right;
        } else {
          applied_left = 0;
          applied_right = 0;
          command_seen = false;
          reversal_state = MOTOR_REVERSAL_IDLE;
          RobotState_UpdateMotorOutput(0, 0, now);
        }
      }
    }

    next_wake += MOTOR_CONTROL_PERIOD_MS;
    if (osDelayUntil(next_wake) != osOK) {
      next_wake = osKernelGetTickCount();
    }
  }
}
