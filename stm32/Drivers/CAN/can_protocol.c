#include "can_protocol.h"

#include <limits.h>
#include <string.h>

#define CAN_CONTROL_ENABLE_FLAG       (1U << 0)
#define CAN_CONTROL_ALLOWED_FLAGS     CAN_CONTROL_ENABLE_FLAG
#define CAN_PID_GAIN_MAX_MILLI        1000000L
#define CAN_PID_OUTPUT_LIMIT_MAX      1000L

static uint16_t ReadU16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static int32_t ReadI32(const uint8_t *data)
{
  const uint32_t value = (uint32_t)data[0] |
                         ((uint32_t)data[1] << 8U) |
                         ((uint32_t)data[2] << 16U) |
                         ((uint32_t)data[3] << 24U);
  return (int32_t)value;
}

static void WriteU16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8U);
}

void CanProtocol_EncodeStatus(const RobotStatus_t *status,
                              uint8_t data[CAN_PROTOCOL_FRAME_SIZE])
{
  if ((status == NULL) || (data == NULL)) return;
  WriteU16(&data[0], status->battery_valid ? status->battery_mv : UINT16_MAX);
  WriteU16(&data[2], (uint16_t)status->left_output_permille);
  WriteU16(&data[4], (uint16_t)status->right_output_permille);
  data[6] = (uint8_t)status->mode;
  data[7] = status->error_status;
}

void CanProtocol_EncodeControl(const CanMotorCommand_t *command,
                               uint8_t data[CAN_PROTOCOL_FRAME_SIZE])
{
  if ((command == NULL) || (data == NULL)) return;
  WriteU16(&data[0], (uint16_t)command->left_output_permille);
  WriteU16(&data[2], (uint16_t)command->right_output_permille);
  data[4] = (uint8_t)command->mode;
  data[5] = command->enable ? CAN_CONTROL_ENABLE_FLAG : 0U;
  data[6] = command->sequence;
  data[7] = CAN_PROTOCOL_VERSION;
}

CanProtocolResult_t CanProtocol_DecodeControl(
    const uint8_t *data, uint8_t dlc, CanMotorCommand_t *command)
{
  if ((data == NULL) || (command == NULL) ||
      (dlc != CAN_PROTOCOL_FRAME_SIZE)) return CAN_PROTOCOL_FORMAT_ERROR;
  if (data[7] != CAN_PROTOCOL_VERSION) return CAN_PROTOCOL_VERSION_ERROR;
  if ((data[5] & (uint8_t)(~CAN_CONTROL_ALLOWED_FLAGS)) != 0U) {
    return CAN_PROTOCOL_FORMAT_ERROR;
  }
  if (data[4] > (uint8_t)ROBOT_MODE_AUTO) return CAN_PROTOCOL_RANGE_ERROR;

  const int16_t left = (int16_t)ReadU16(&data[0]);
  const int16_t right = (int16_t)ReadU16(&data[2]);
  if ((left < -1000) || (left > 1000) ||
      (right < -1000) || (right > 1000)) {
    return CAN_PROTOCOL_RANGE_ERROR;
  }

  command->enable = (data[5] & CAN_CONTROL_ENABLE_FLAG) != 0U;
  command->left_output_permille = command->enable ? left : 0;
  command->right_output_permille = command->enable ? right : 0;
  command->mode = (RobotMode_t)data[4];
  command->sequence = data[6];
  return CAN_PROTOCOL_OK;
}

CanProtocolResult_t CanProtocol_DecodePidConfig(
    const uint8_t *data, uint8_t dlc, CanPidConfig_t *config)
{
  if ((data == NULL) || (config == NULL) ||
      (dlc != CAN_PROTOCOL_FRAME_SIZE)) return CAN_PROTOCOL_FORMAT_ERROR;

  config->target = (CanPidTarget_t)data[0];
  config->parameter = (CanPidParameter_t)data[1];
  config->value_milli = ReadI32(&data[2]);
  config->sequence = data[6];
  if (data[7] != CAN_PROTOCOL_VERSION) return CAN_PROTOCOL_VERSION_ERROR;
  if ((data[0] > (uint8_t)CAN_PID_TARGET_BOTH) ||
      (data[1] > (uint8_t)CAN_PID_PARAMETER_OUTPUT_LIMIT)) {
    return CAN_PROTOCOL_RANGE_ERROR;
  }
  if (config->value_milli < 0) return CAN_PROTOCOL_RANGE_ERROR;
  if ((config->parameter == CAN_PID_PARAMETER_OUTPUT_LIMIT) &&
      (config->value_milli > CAN_PID_OUTPUT_LIMIT_MAX)) {
    return CAN_PROTOCOL_RANGE_ERROR;
  }
  if ((config->parameter != CAN_PID_PARAMETER_OUTPUT_LIMIT) &&
      (config->value_milli > CAN_PID_GAIN_MAX_MILLI)) {
    return CAN_PROTOCOL_RANGE_ERROR;
  }
  return CAN_PROTOCOL_OK;
}

void CanProtocol_EncodeConfigResponse(
    const CanPidConfig_t *config, CanProtocolResult_t result,
    uint8_t data[CAN_PROTOCOL_FRAME_SIZE])
{
  if (data == NULL) return;
  memset(data, 0, CAN_PROTOCOL_FRAME_SIZE);
  data[0] = 2U;
  data[1] = config != NULL ? config->sequence : 0U;
  data[2] = (uint8_t)result;
  data[3] = config != NULL ? (uint8_t)config->target : 0U;
  data[4] = config != NULL ? (uint8_t)config->parameter : 0U;
  data[7] = CAN_PROTOCOL_VERSION;
}
