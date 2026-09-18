#include "robot_ble_protocol_self_test.h"

#include <string.h>

#include "robot_ble_protocol.h"
#include "robot_protocol.h"

#define TEST_COMMANDS  (1u << 0)
#define TEST_REJECTION (1u << 1)
#define TEST_STATUS    (1u << 2)

static void EncodeTestCommand(uint8_t opcode,
                              uint16_t sequence,
                              int16_t left_pwm,
                              int16_t right_pwm,
                              uint8_t output[ROBOT_BLE_COMMAND_SIZE])
{
    const uint16_t left = (uint16_t)left_pwm;
    const uint16_t right = (uint16_t)right_pwm;
    uint16_t crc;

    output[0] = ROBOT_BLE_PROTOCOL_VERSION;
    output[1] = opcode;
    output[2] = (uint8_t)(sequence & 0xFFu);
    output[3] = (uint8_t)(sequence >> 8u);
    output[4] = (uint8_t)(left & 0xFFu);
    output[5] = (uint8_t)(left >> 8u);
    output[6] = (uint8_t)(right & 0xFFu);
    output[7] = (uint8_t)(right >> 8u);
    crc = RobotProtocol_Crc16CcittFalse(output, 8u);
    output[8] = (uint8_t)(crc & 0xFFu);
    output[9] = (uint8_t)(crc >> 8u);
}

static bool TestCommands(void)
{
    static const uint8_t expected_stop[ROBOT_BLE_COMMAND_SIZE] = {
        0x01u, 0x00u, 0x34u, 0x12u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x19u, 0x1Fu
    };
    uint8_t wire[ROBOT_BLE_COMMAND_SIZE];
    RobotBleCommand_t command;

    EncodeTestCommand(ROBOT_BLE_OPCODE_STOP, 0x1234u, 0, 0, wire);
    if ((memcmp(wire, expected_stop, sizeof(wire)) != 0) ||
        (RobotBleProtocol_DecodeCommand(wire, sizeof(wire), &command) !=
         ROBOT_BLE_DECODE_OK) ||
        (command.opcode != ROBOT_BLE_OPCODE_STOP) ||
        (command.sequence != 0x1234u) || (command.left_pwm != 0) ||
        (command.right_pwm != 0))
    {
        return false;
    }

    EncodeTestCommand(ROBOT_BLE_OPCODE_DRIVE,
                      0xFFFFu,
                      -ROBOT_BLE_PWM_LIMIT,
                      ROBOT_BLE_PWM_LIMIT,
                      wire);
    return (RobotBleProtocol_DecodeCommand(wire, sizeof(wire), &command) ==
            ROBOT_BLE_DECODE_OK) &&
           (command.left_pwm == -ROBOT_BLE_PWM_LIMIT) &&
           (command.right_pwm == ROBOT_BLE_PWM_LIMIT);
}

static bool TestRejection(void)
{
    uint8_t wire[ROBOT_BLE_COMMAND_SIZE];
    RobotBleCommand_t command;

    EncodeTestCommand(ROBOT_BLE_OPCODE_STOP, 1u, 0, 0, wire);
    if ((RobotBleProtocol_DecodeCommand(NULL, sizeof(wire), &command) !=
         ROBOT_BLE_DECODE_BAD_ARGUMENT) ||
        (RobotBleProtocol_DecodeCommand(wire, sizeof(wire), NULL) !=
         ROBOT_BLE_DECODE_BAD_ARGUMENT) ||
        (RobotBleProtocol_DecodeCommand(wire, 0u, &command) !=
         ROBOT_BLE_DECODE_BAD_LENGTH) ||
        (RobotBleProtocol_DecodeCommand(wire, sizeof(wire) - 1u, &command) !=
         ROBOT_BLE_DECODE_BAD_LENGTH) ||
        (RobotBleProtocol_DecodeCommand(wire, sizeof(wire) + 1u, &command) !=
         ROBOT_BLE_DECODE_BAD_LENGTH))
    {
        return false;
    }

    wire[0] = 2u;
    if (RobotBleProtocol_DecodeCommand(wire, sizeof(wire), &command) !=
        ROBOT_BLE_DECODE_BAD_VERSION)
    {
        return false;
    }

    EncodeTestCommand(0x7Fu, 1u, 0, 0, wire);
    if (RobotBleProtocol_DecodeCommand(wire, sizeof(wire), &command) !=
        ROBOT_BLE_DECODE_BAD_OPCODE)
    {
        return false;
    }

    EncodeTestCommand(ROBOT_BLE_OPCODE_DRIVE, 1u, 1001, 0, wire);
    if (RobotBleProtocol_DecodeCommand(wire, sizeof(wire), &command) !=
        ROBOT_BLE_DECODE_BAD_RANGE)
    {
        return false;
    }

    EncodeTestCommand(ROBOT_BLE_OPCODE_STOP, 1u, 1, 0, wire);
    if (RobotBleProtocol_DecodeCommand(wire, sizeof(wire), &command) !=
        ROBOT_BLE_DECODE_BAD_SEMANTIC)
    {
        return false;
    }

    EncodeTestCommand(ROBOT_BLE_OPCODE_STOP, 1u, 0, -1, wire);
    if (RobotBleProtocol_DecodeCommand(wire, sizeof(wire), &command) !=
        ROBOT_BLE_DECODE_BAD_SEMANTIC)
    {
        return false;
    }

    EncodeTestCommand(ROBOT_BLE_OPCODE_STOP, 1u, 0, 0, wire);
    wire[8] ^= 0x01u;
    return RobotBleProtocol_DecodeCommand(wire, sizeof(wire), &command) ==
           ROBOT_BLE_DECODE_BAD_CRC;
}

static bool TestStatus(void)
{
    static const uint8_t expected_status[ROBOT_BLE_STATUS_SIZE] = {
        0x01u, 0x81u, 0x68u, 0x24u, 0x0Fu,
        0x03u, 0x68u, 0x10u, 0x18u, 0xFCu,
        0xE8u, 0x03u, 0xF4u, 0x01u, 0x01u,
        0x82u, 0x01u, 0x00u, 0xE4u, 0xC5u
    };
    RobotBleStatusFields_t fields = {
        .link_flags = ROBOT_BLE_LINK_UART_ONLINE |
                      ROBOT_BLE_LINK_CONNECTED |
                      ROBOT_BLE_LINK_STATUS_FRESH |
                      ROBOT_BLE_LINK_ENCRYPTED,
        .valid_flags = 0x03u,
        .battery_mv = 4200u,
        .left_pwm = -1000,
        .right_pwm = 1000,
        .distance_mm = 500u,
        .mode = 1u,
        .error_status = 0x82u,
        .owner = 1u
    };
    uint8_t output[ROBOT_BLE_STATUS_SIZE];
    uint16_t crc;

    if (RobotBleProtocol_EncodeStatus(
            0x2468u, &fields, output, sizeof(output)) != sizeof(output))
    {
        return false;
    }
    crc = RobotProtocol_Crc16CcittFalse(output, 18u);
    if ((output[0] != ROBOT_BLE_PROTOCOL_VERSION) ||
        (output[1] != 0x81u) || (output[2] != 0x68u) ||
        (output[3] != 0x24u) || (output[6] != 0x68u) ||
        (output[7] != 0x10u) || (output[8] != 0x18u) ||
        (output[9] != 0xFCu) || (output[10] != 0xE8u) ||
        (output[11] != 0x03u) ||
        ((uint16_t)((uint16_t)output[18] |
                    ((uint16_t)output[19] << 8u)) != crc))
    {
        return false;
    }
    if (memcmp(output, expected_status, sizeof(output)) != 0)
    {
        return false;
    }

    fields.left_pwm = 1001;
    if (RobotBleProtocol_EncodeStatus(
            1u, &fields, output, sizeof(output)) != 0u)
    {
        return false;
    }
    fields.left_pwm = -1000;
    return (RobotBleProtocol_EncodeStatus(
                1u, &fields, output, sizeof(output) - 1u) == 0u) &&
           (RobotBleProtocol_EncodeStatus(
                1u, NULL, output, sizeof(output)) == 0u) &&
           (RobotBleProtocol_EncodeStatus(
                1u, &fields, NULL, sizeof(output)) == 0u);
}

bool RobotBleProtocolSelfTest_Run(uint32_t *failure_mask)
{
    uint32_t failures = 0u;

    if (!TestCommands())
    {
        failures |= TEST_COMMANDS;
    }
    if (!TestRejection())
    {
        failures |= TEST_REJECTION;
    }
    if (!TestStatus())
    {
        failures |= TEST_STATUS;
    }

    if (failure_mask != NULL)
    {
        *failure_mask = failures;
    }
    return failures == 0u;
}
