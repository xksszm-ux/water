#include "robot_ble_protocol.h"

#include "robot_protocol.h"

#define ROBOT_BLE_COMMAND_CRC_OFFSET  8u
#define ROBOT_BLE_STATUS_CRC_OFFSET   18u
#define ROBOT_BLE_STATUS_TYPE         0x81u

static uint16_t ReadU16Le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8u));
}

static int16_t ReadI16Le(const uint8_t *data)
{
    const uint16_t raw = ReadU16Le(data);
    int32_t value = (int32_t)raw;

    if ((raw & 0x8000u) != 0u)
    {
        value -= 65536L;
    }
    return (int16_t)value;
}

static void WriteU16Le(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xFFu);
    output[1] = (uint8_t)(value >> 8u);
}

static void WriteI16Le(uint8_t *output, int16_t value)
{
    WriteU16Le(output, (uint16_t)value);
}

RobotBleDecodeResult_t RobotBleProtocol_DecodeCommand(
    const uint8_t *data,
    size_t length,
    RobotBleCommand_t *command)
{
    RobotBleCommand_t decoded;
    uint16_t expected_crc;
    uint16_t received_crc;

    if ((data == NULL) || (command == NULL))
    {
        return ROBOT_BLE_DECODE_BAD_ARGUMENT;
    }
    if (length != ROBOT_BLE_COMMAND_SIZE)
    {
        return ROBOT_BLE_DECODE_BAD_LENGTH;
    }
    if (data[0] != ROBOT_BLE_PROTOCOL_VERSION)
    {
        return ROBOT_BLE_DECODE_BAD_VERSION;
    }

    expected_crc = RobotProtocol_Crc16CcittFalse(
        data, ROBOT_BLE_COMMAND_CRC_OFFSET);
    received_crc = ReadU16Le(&data[ROBOT_BLE_COMMAND_CRC_OFFSET]);
    if (received_crc != expected_crc)
    {
        return ROBOT_BLE_DECODE_BAD_CRC;
    }

    decoded.opcode = data[1];
    decoded.sequence = ReadU16Le(&data[2]);
    decoded.left_pwm = ReadI16Le(&data[4]);
    decoded.right_pwm = ReadI16Le(&data[6]);

    if ((decoded.opcode != ROBOT_BLE_OPCODE_STOP) &&
        (decoded.opcode != ROBOT_BLE_OPCODE_DRIVE))
    {
        return ROBOT_BLE_DECODE_BAD_OPCODE;
    }
    if ((decoded.left_pwm < -ROBOT_BLE_PWM_LIMIT) ||
        (decoded.left_pwm > ROBOT_BLE_PWM_LIMIT) ||
        (decoded.right_pwm < -ROBOT_BLE_PWM_LIMIT) ||
        (decoded.right_pwm > ROBOT_BLE_PWM_LIMIT))
    {
        return ROBOT_BLE_DECODE_BAD_RANGE;
    }
    if ((decoded.opcode == ROBOT_BLE_OPCODE_STOP) &&
        ((decoded.left_pwm != 0) || (decoded.right_pwm != 0)))
    {
        return ROBOT_BLE_DECODE_BAD_SEMANTIC;
    }

    *command = decoded;
    return ROBOT_BLE_DECODE_OK;
}

size_t RobotBleProtocol_EncodeStatus(uint16_t sequence,
                                     const RobotBleStatusFields_t *fields,
                                     uint8_t *output,
                                     size_t output_capacity)
{
    uint16_t crc;

    if ((fields == NULL) || (output == NULL) ||
        (output_capacity < ROBOT_BLE_STATUS_SIZE) ||
        ((fields->link_flags &
          (uint8_t)(~ROBOT_BLE_LINK_ALLOWED_MASK)) != 0u) ||
        (fields->left_pwm < -ROBOT_BLE_PWM_LIMIT) ||
        (fields->left_pwm > ROBOT_BLE_PWM_LIMIT) ||
        (fields->right_pwm < -ROBOT_BLE_PWM_LIMIT) ||
        (fields->right_pwm > ROBOT_BLE_PWM_LIMIT))
    {
        return 0u;
    }

    output[0] = ROBOT_BLE_PROTOCOL_VERSION;
    output[1] = ROBOT_BLE_STATUS_TYPE;
    WriteU16Le(&output[2], sequence);
    output[4] = fields->link_flags;
    output[5] = fields->valid_flags;
    WriteU16Le(&output[6], fields->battery_mv);
    WriteI16Le(&output[8], fields->left_pwm);
    WriteI16Le(&output[10], fields->right_pwm);
    WriteU16Le(&output[12], fields->distance_mm);
    output[14] = fields->mode;
    output[15] = fields->error_status;
    output[16] = fields->owner;
    output[17] = 0u;
    crc = RobotProtocol_Crc16CcittFalse(output, ROBOT_BLE_STATUS_CRC_OFFSET);
    WriteU16Le(&output[ROBOT_BLE_STATUS_CRC_OFFSET], crc);
    return ROBOT_BLE_STATUS_SIZE;
}
