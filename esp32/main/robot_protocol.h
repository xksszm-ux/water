#ifndef ROBOT_PROTOCOL_H
#define ROBOT_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOT_PROTOCOL_SOF_0                    0xA5u
#define ROBOT_PROTOCOL_SOF_1                    0x5Au
#define ROBOT_PROTOCOL_VERSION                  0x01u
#define ROBOT_PROTOCOL_MAX_PAYLOAD_SIZE         32u
#define ROBOT_PROTOCOL_HEADER_SIZE              8u
#define ROBOT_PROTOCOL_CRC_SIZE                 2u
#define ROBOT_PROTOCOL_MIN_FRAME_SIZE           10u
#define ROBOT_PROTOCOL_MAX_FRAME_SIZE           \
    (ROBOT_PROTOCOL_MIN_FRAME_SIZE + ROBOT_PROTOCOL_MAX_PAYLOAD_SIZE)
#define ROBOT_PROTOCOL_PARSER_BUFFER_SIZE       96u
#define ROBOT_PROTOCOL_INCOMPLETE_TIMEOUT_MS    50u

#define ROBOT_PROTOCOL_FLAG_ACK_REQUEST         (1u << 0)
#define ROBOT_PROTOCOL_FLAG_ALLOWED_MASK        ROBOT_PROTOCOL_FLAG_ACK_REQUEST

#define ROBOT_PROTOCOL_MOTOR_FLAG_ENABLE        (1u << 0)
#define ROBOT_PROTOCOL_MOTOR_FLAG_ALLOWED_MASK  ROBOT_PROTOCOL_MOTOR_FLAG_ENABLE
#define ROBOT_PROTOCOL_MOTOR_PWM_LIMIT          1000

#define ROBOT_PROTOCOL_STATUS_VALID_BATTERY     (1u << 0)
#define ROBOT_PROTOCOL_STATUS_VALID_DISTANCE    (1u << 1)
#define ROBOT_PROTOCOL_STATUS_VALID_MASK        \
    (ROBOT_PROTOCOL_STATUS_VALID_BATTERY | ROBOT_PROTOCOL_STATUS_VALID_DISTANCE)

#define ROBOT_PROTOCOL_INVALID_U16              0xFFFFu

#define ROBOT_PROTOCOL_MODE_BLE                 0u
#define ROBOT_PROTOCOL_MODE_AUTO                1u

#define ROBOT_PROTOCOL_OWNER_CAN                0u
#define ROBOT_PROTOCOL_OWNER_UART               1u
#define ROBOT_PROTOCOL_OWNER_TEST               2u
#define ROBOT_PROTOCOL_OWNER_NONE               0xFFu

typedef enum
{
    ROBOT_PROTOCOL_TYPE_MOTOR_CONTROL = 0x01u,
    ROBOT_PROTOCOL_TYPE_STOP = 0x02u,
    ROBOT_PROTOCOL_TYPE_SET_MODE = 0x03u,
    ROBOT_PROTOCOL_TYPE_STATUS_REQUEST = 0x04u,
    ROBOT_PROTOCOL_TYPE_HEARTBEAT = 0x05u,
    ROBOT_PROTOCOL_TYPE_ACK = 0x80u,
    ROBOT_PROTOCOL_TYPE_STATUS = 0x81u
} RobotProtocolMessageType_t;

typedef enum
{
    ROBOT_PROTOCOL_ACK_OK = 0u,
    ROBOT_PROTOCOL_ACK_DUPLICATE = 1u,
    ROBOT_PROTOCOL_ACK_BAD_LENGTH = 2u,
    ROBOT_PROTOCOL_ACK_BAD_FLAGS = 3u,
    ROBOT_PROTOCOL_ACK_RANGE_ERROR = 4u,
    ROBOT_PROTOCOL_ACK_VERSION_ERROR = 5u,
    ROBOT_PROTOCOL_ACK_UNSUPPORTED = 6u,
    ROBOT_PROTOCOL_ACK_MODE_DENIED = 7u,
    ROBOT_PROTOCOL_ACK_OWNER_BUSY = 8u,
    ROBOT_PROTOCOL_ACK_SAFETY_BLOCKED = 9u,
    ROBOT_PROTOCOL_ACK_QUEUE_FULL = 10u,
    ROBOT_PROTOCOL_ACK_HANDOFF_STOP = 11u,
    ROBOT_PROTOCOL_ACK_INTERNAL_ERROR = 12u
} RobotProtocolAckResult_t;

typedef enum
{
    ROBOT_PROTOCOL_PARSE_NONE = 0,
    ROBOT_PROTOCOL_PARSE_FRAME,
    ROBOT_PROTOCOL_PARSE_DROPPED_BAD_LENGTH,
    ROBOT_PROTOCOL_PARSE_DROPPED_BAD_CRC,
    ROBOT_PROTOCOL_PARSE_DROPPED_TIMEOUT,
    ROBOT_PROTOCOL_PARSE_BAD_ARGUMENT
} RobotProtocolParseResult_t;

typedef struct
{
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint16_t sequence;
    uint8_t payload_length;
    uint8_t payload[ROBOT_PROTOCOL_MAX_PAYLOAD_SIZE];
} RobotProtocolFrame_t;

typedef struct
{
    uint8_t bytes[ROBOT_PROTOCOL_PARSER_BUFFER_SIZE];
    uint16_t count;
    uint32_t last_byte_ms;
} RobotProtocolParser_t;

typedef struct
{
    uint16_t sequence;
    uint8_t request_type;
    uint8_t result;
    uint8_t current_mode;
    uint8_t owner;
} RobotProtocolAck_t;

typedef struct
{
    uint16_t sequence;
    uint16_t battery_mv;
    int16_t applied_left_pwm;
    int16_t applied_right_pwm;
    uint16_t distance_mm;
    uint8_t mode;
    uint8_t error_status;
    uint8_t valid_flags;
    uint8_t owner;
} RobotProtocolStatus_t;

void RobotProtocolParser_Init(RobotProtocolParser_t *parser);
void RobotProtocolParser_Reset(RobotProtocolParser_t *parser);

bool RobotProtocolParser_PushByte(RobotProtocolParser_t *parser,
                                  uint8_t byte,
                                  uint32_t now_ms);

/*
 * Extracts at most one frame per call. Noise is removed internally. When a
 * bad length, bad CRC, or 50 ms incomplete-frame timeout is found, only the
 * first SOF byte is removed so the next call can resynchronize safely.
 */
RobotProtocolParseResult_t RobotProtocolParser_Next(
    RobotProtocolParser_t *parser,
    uint32_t now_ms,
    RobotProtocolFrame_t *frame);

uint16_t RobotProtocol_Crc16CcittFalse(const uint8_t *data, size_t length);

/* Generic V1 frame encoder. Returns the frame length, or zero on rejection. */
size_t RobotProtocol_EncodeFrame(const RobotProtocolFrame_t *frame,
                                 uint8_t *output,
                                 size_t output_capacity);

size_t RobotProtocol_EncodeMotor(uint16_t sequence,
                                 int16_t left_pwm,
                                 int16_t right_pwm,
                                 bool request_ack,
                                 uint8_t *output,
                                 size_t output_capacity);

size_t RobotProtocol_EncodeStop(uint16_t sequence,
                                bool request_ack,
                                uint8_t *output,
                                size_t output_capacity);

size_t RobotProtocol_EncodeSetMode(uint16_t sequence,
                                   uint8_t mode,
                                   bool request_ack,
                                   uint8_t *output,
                                   size_t output_capacity);

size_t RobotProtocol_EncodeStatusRequest(uint16_t sequence,
                                         bool request_ack,
                                         uint8_t *output,
                                         size_t output_capacity);

size_t RobotProtocol_EncodeHeartbeat(uint16_t sequence,
                                     bool request_ack,
                                     uint8_t *output,
                                     size_t output_capacity);

/* Strict V1 decoders for STM32 response frames. */
bool RobotProtocol_DecodeAck(const RobotProtocolFrame_t *frame,
                             RobotProtocolAck_t *ack);

bool RobotProtocol_DecodeStatus(const RobotProtocolFrame_t *frame,
                                RobotProtocolStatus_t *status);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_PROTOCOL_H */
