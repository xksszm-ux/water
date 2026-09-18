#ifndef CONTROL_ARBITER_H
#define CONTROL_ARBITER_H

#include <stdint.h>

#include "robot_state.h"

typedef enum {
  CONTROL_SOURCE_CAN = 0U,
  CONTROL_SOURCE_UART = 1U,
  CONTROL_SOURCE_TEST = 2U,
  CONTROL_SOURCE_NONE = 0xFFU
} ControlSource_t;

typedef enum {
  CONTROL_RESULT_ACCEPTED = 0U,
  CONTROL_RESULT_STOP_ACCEPTED = 1U,
  CONTROL_RESULT_REPLAY = 2U,
  CONTROL_RESULT_OWNER_BUSY = 3U,
  CONTROL_RESULT_HANDOFF_STOP = 4U,
  CONTROL_RESULT_MODE_REJECTED = 5U,
  CONTROL_RESULT_RANGE_REJECTED = 6U,
  CONTROL_RESULT_SAFETY_BLOCKED = 7U,
  CONTROL_RESULT_INTERNAL_ERROR = 8U
} ControlResult_t;

/* Initialize before either communication task can submit a command. */
void ControlArbiter_Init(void);

ControlResult_t ControlArbiter_SubmitCan(
    uint8_t sequence, const MotorCommand_t *command, uint32_t now_ms);
ControlResult_t ControlArbiter_SubmitUart(
    uint16_t sequence, const MotorCommand_t *command, uint32_t now_ms);
ControlResult_t ControlArbiter_SubmitTest(
    uint16_t sequence, const MotorCommand_t *command, uint32_t now_ms);
ControlResult_t ControlArbiter_SetModeUart(
    uint16_t sequence, RobotMode_t mode, uint32_t now_ms);

void ControlArbiter_ReportSourceFault(ControlSource_t source,
                                      uint32_t now_ms);
ControlSource_t ControlArbiter_GetOwner(uint32_t now_ms);

#endif
