#ifndef APPLICATION_SERVICES_PROTOCOL_H
#define APPLICATION_SERVICES_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTOCOL_SOF_0                         0xA5u
#define PROTOCOL_SOF_1                         0x5Au
#define PROTOCOL_VERSION                       0x01u
#define PROTOCOL_MAX_PAYLOAD_SIZE              32u
#define PROTOCOL_HEADER_SIZE                   8u
#define PROTOCOL_CRC_SIZE                      2u
#define PROTOCOL_MIN_FRAME_SIZE                10u
#define PROTOCOL_MAX_FRAME_SIZE                (PROTOCOL_MIN_FRAME_SIZE + PROTOCOL_MAX_PAYLOAD_SIZE)
#define PROTOCOL_PARSER_BUFFER_SIZE            96u
#define PROTOCOL_INCOMPLETE_TIMEOUT_MS         50u

#define PROTOCOL_FLAG_ACK_REQUEST              (1u << 0)
#define PROTOCOL_FLAG_ALLOWED_MASK             PROTOCOL_FLAG_ACK_REQUEST

#define PROTOCOL_MOTOR_FLAG_ENABLE             (1u << 0)
#define PROTOCOL_MOTOR_FLAG_ALLOWED_MASK       PROTOCOL_MOTOR_FLAG_ENABLE
#define PROTOCOL_MOTOR_PWM_LIMIT               1000

#define PROTOCOL_STATUS_VALID_BATTERY          (1u << 0)
#define PROTOCOL_STATUS_VALID_DISTANCE         (1u << 1)

#define PROTOCOL_INVALID_U16                   0xFFFFu

typedef enum
{
    PROTOCOL_TYPE_MOTOR_CONTROL = 0x01u,
    PROTOCOL_TYPE_STOP = 0x02u,
    PROTOCOL_TYPE_SET_MODE = 0x03u,
    PROTOCOL_TYPE_STATUS_REQUEST = 0x04u,
    PROTOCOL_TYPE_HEARTBEAT = 0x05u,
    PROTOCOL_TYPE_ACK = 0x80u,
    PROTOCOL_TYPE_STATUS = 0x81u
} ProtocolMessageType_t;

typedef enum
{
    PROTOCOL_ACK_OK = 0u,
    PROTOCOL_ACK_DUPLICATE = 1u,
    PROTOCOL_ACK_BAD_LENGTH = 2u,
    PROTOCOL_ACK_BAD_FLAGS = 3u,
    PROTOCOL_ACK_RANGE_ERROR = 4u,
    PROTOCOL_ACK_VERSION_ERROR = 5u,
    PROTOCOL_ACK_UNSUPPORTED = 6u,
    PROTOCOL_ACK_MODE_DENIED = 7u,
    PROTOCOL_ACK_OWNER_BUSY = 8u,
    PROTOCOL_ACK_SAFETY_BLOCKED = 9u,
    PROTOCOL_ACK_QUEUE_FULL = 10u,
    PROTOCOL_ACK_HANDOFF_STOP = 11u,
    PROTOCOL_ACK_INTERNAL_ERROR = 12u
} ProtocolAckResult_t;

typedef enum
{
    PROTOCOL_PARSE_NONE = 0,
    PROTOCOL_PARSE_FRAME,
    PROTOCOL_PARSE_DROPPED_BAD_LENGTH,
    PROTOCOL_PARSE_DROPPED_BAD_CRC,
    PROTOCOL_PARSE_DROPPED_TIMEOUT,
    PROTOCOL_PARSE_BAD_ARGUMENT
} ProtocolParseResult_t;

typedef struct
{
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint16_t sequence;
    uint8_t payload_length;
    uint8_t payload[PROTOCOL_MAX_PAYLOAD_SIZE];
} ProtocolFrame_t;

typedef struct
{
    uint8_t bytes[PROTOCOL_PARSER_BUFFER_SIZE];
    uint16_t count;
    uint32_t last_byte_ms;
} ProtocolParser_t;

typedef struct
{
    int16_t left_pwm;
    int16_t right_pwm;
    uint8_t control_flags;
} ProtocolMotorCommand_t;

typedef struct
{
    uint16_t battery_mv;
    int16_t applied_left_pwm;
    int16_t applied_right_pwm;
    uint16_t distance_mm;
    uint8_t mode;
    uint8_t error_status;
    uint8_t valid_flags;
    uint8_t owner;
} ProtocolStatus_t;

void ProtocolParser_Init(ProtocolParser_t *parser);
void ProtocolParser_Reset(ProtocolParser_t *parser);

/*
 * Returns false only when the parser buffer is already full. The caller should
 * treat that as a receive overflow and reset the parser before feeding more data.
 */
bool ProtocolParser_PushByte(ProtocolParser_t *parser,
                             uint8_t byte,
                             uint32_t now_ms);

/*
 * Extracts at most one frame per call. Noise is discarded internally. A bad
 * length, CRC, or timed-out candidate discards only its first SOF byte so that
 * a valid frame embedded after it can be found by the next call.
 */
ProtocolParseResult_t ProtocolParser_Next(ProtocolParser_t *parser,
                                          uint32_t now_ms,
                                          ProtocolFrame_t *frame);

uint16_t Protocol_Crc16CcittFalse(const uint8_t *data, size_t length);

/* Returns the encoded byte count, or zero if an argument is invalid. */
size_t Protocol_Encode(const ProtocolFrame_t *frame,
                       uint8_t *output,
                       size_t output_capacity);

/* Strict decoder: MOTOR type, V1, legal header flags, len=5, enable=1, PWM +/-1000. */
bool Protocol_DecodeMotor(const ProtocolFrame_t *frame,
                          ProtocolMotorCommand_t *command);

/* ACK payload: request_type, result, current_mode, owner. */
size_t Protocol_EncodeAck(uint16_t sequence,
                          uint8_t request_type,
                          uint8_t result,
                          uint8_t current_mode,
                          uint8_t owner,
                          uint8_t *output,
                          size_t output_capacity);

/* STATUS payload is exactly 12 bytes in the field order documented by ProtocolStatus_t. */
size_t Protocol_EncodeStatus(uint16_t sequence,
                             const ProtocolStatus_t *status,
                             uint8_t *output,
                             size_t output_capacity);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_SERVICES_PROTOCOL_H */
