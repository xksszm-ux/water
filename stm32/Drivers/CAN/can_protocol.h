#ifndef CAN_PROTOCOL_H
#define CAN_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "robot_state.h"

#define CAN_PROTOCOL_VERSION       1U
#define CAN_PROTOCOL_FRAME_SIZE    8U
#define CAN_ID_MOTOR_COMMAND       0x100U
#define CAN_ID_ROBOT_STATUS        0x101U
#define CAN_ID_PID_CONFIG          0x102U
#define CAN_ID_CONFIG_RESPONSE     0x103U

typedef enum {
  CAN_PROTOCOL_OK = 0U,
  CAN_PROTOCOL_FORMAT_ERROR = 1U,
  CAN_PROTOCOL_VERSION_ERROR = 2U,
  CAN_PROTOCOL_RANGE_ERROR = 3U,
  CAN_PROTOCOL_UNSUPPORTED = 4U,
  CAN_PROTOCOL_INTERNAL_ERROR = 5U,
  CAN_PROTOCOL_MODE_ERROR = 6U
} CanProtocolResult_t;

typedef struct {
  int16_t left_output_permille;
  int16_t right_output_permille;
  RobotMode_t mode;
  bool enable;
  uint8_t sequence;
} CanMotorCommand_t;

typedef enum {
  CAN_PID_TARGET_LEFT = 0U,
  CAN_PID_TARGET_RIGHT = 1U,
  CAN_PID_TARGET_BOTH = 2U
} CanPidTarget_t;

typedef enum {
  CAN_PID_PARAMETER_KP = 0U,
  CAN_PID_PARAMETER_KI = 1U,
  CAN_PID_PARAMETER_KD = 2U,
  CAN_PID_PARAMETER_OUTPUT_LIMIT = 3U
} CanPidParameter_t;

typedef struct {
  CanPidTarget_t target;
  CanPidParameter_t parameter;
  int32_t value_milli;
  uint8_t sequence;
} CanPidConfig_t;

void CanProtocol_EncodeStatus(const RobotStatus_t *status,
                              uint8_t data[CAN_PROTOCOL_FRAME_SIZE]);
void CanProtocol_EncodeControl(const CanMotorCommand_t *command,
                               uint8_t data[CAN_PROTOCOL_FRAME_SIZE]);
CanProtocolResult_t CanProtocol_DecodeControl(
    const uint8_t *data, uint8_t dlc, CanMotorCommand_t *command);
CanProtocolResult_t CanProtocol_DecodePidConfig(
    const uint8_t *data, uint8_t dlc, CanPidConfig_t *config);
void CanProtocol_EncodeConfigResponse(
    const CanPidConfig_t *config, CanProtocolResult_t result,
    uint8_t data[CAN_PROTOCOL_FRAME_SIZE]);

#endif
