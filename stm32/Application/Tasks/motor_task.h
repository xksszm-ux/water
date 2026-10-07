#pragma once
#include "robot_state.h"
/* Private single-owner task state; no caller may run cycles concurrently. */
typedef enum { MOTOR_REVERSAL_IDLE, MOTOR_REVERSAL_WAIT, MOTOR_REVERSAL_RESTART } MotorReversalState_t;
typedef struct {
  MotorCommand_t command;
  bool command_seen;
  int16_t applied_left, applied_right;
  uint32_t reversal_started_ms;
  MotorReversalState_t reversal_state;
} MotorTaskState_t;
void MotorTask_InitState(MotorTaskState_t *state);
void MotorTask_RunCycle(MotorTaskState_t *state);
