#ifndef ROBOT_BLE_PROTOCOL_H
#define ROBOT_BLE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOT_BLE_PROTOCOL_VERSION             0x01u
#define ROBOT_BLE_COMMAND_SIZE                 10u
#define ROBOT_BLE_STATUS_SIZE                  20u
#define ROBOT_BLE_PWM_LIMIT                    1000

#define ROBOT_BLE_LINK_UART_ONLINE             (1u << 0)
#define ROBOT_BLE_LINK_CONNECTED               (1u << 1)
#define ROBOT_BLE_LINK_STATUS_FRESH            (1u << 2)
#define ROBOT_BLE_LINK_ENCRYPTED               (1u << 3)
#define ROBOT_BLE_LINK_ALLOWED_MASK            \
    (ROBOT_BLE_LINK_UART_ONLINE | ROBOT_BLE_LINK_CONNECTED | \
     ROBOT_BLE_LINK_STATUS_FRESH | ROBOT_BLE_LINK_ENCRYPTED)

typedef enum
{
    ROBOT_BLE_OPCODE_STOP = 0x00u,
    ROBOT_BLE_OPCODE_DRIVE = 0x01u
} RobotBleOpcode_t;

typedef enum
{
    ROBOT_BLE_DECODE_OK = 0,
    ROBOT_BLE_DECODE_BAD_ARGUMENT,
    ROBOT_BLE_DECODE_BAD_LENGTH,
    ROBOT_BLE_DECODE_BAD_VERSION,
    ROBOT_BLE_DECODE_BAD_CRC,
    ROBOT_BLE_DECODE_BAD_OPCODE,
    ROBOT_BLE_DECODE_BAD_RANGE,
    ROBOT_BLE_DECODE_BAD_SEMANTIC
} RobotBleDecodeResult_t;

typedef struct
{
    uint8_t opcode;
    uint16_t sequence;
    int16_t left_pwm;
    int16_t right_pwm;
} RobotBleCommand_t;

typedef struct
{
    uint8_t link_flags;
    uint8_t valid_flags;
    uint16_t battery_mv;
    int16_t left_pwm;
    int16_t right_pwm;
    uint16_t distance_mm;
    uint8_t mode;
    uint8_t error_status;
    uint8_t owner;
} RobotBleStatusFields_t;

/*
 * Fixed 10-byte command, little-endian:
 * version | opcode | seq_u16 | left_i16 | right_i16 | crc16_ccitt_false.
 */
RobotBleDecodeResult_t RobotBleProtocol_DecodeCommand(
    const uint8_t *data,
    size_t length,
    RobotBleCommand_t *command);

/* Fixed 20-byte status notification; returns zero on rejection. */
size_t RobotBleProtocol_EncodeStatus(uint16_t sequence,
                                     const RobotBleStatusFields_t *fields,
                                     uint8_t *output,
                                     size_t output_capacity);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_BLE_PROTOCOL_H */
