#include "robot_protocol.h"

#include <string.h>

#define ROBOT_PROTOCOL_CRC_POLYNOMIAL          0x1021u
#define ROBOT_PROTOCOL_CRC_INITIAL_VALUE       0xFFFFu
#define ROBOT_PROTOCOL_CRC_INPUT_OFFSET        2u
#define ROBOT_PROTOCOL_FIXED_CRC_INPUT_SIZE    6u
#define ROBOT_PROTOCOL_ACK_PAYLOAD_SIZE        4u
#define ROBOT_PROTOCOL_STATUS_PAYLOAD_SIZE     12u

static void RobotProtocolParser_RemovePrefix(RobotProtocolParser_t *parser,
                                             uint16_t length)
{
    if (length >= parser->count)
    {
        parser->count = 0u;
        return;
    }

    (void)memmove(parser->bytes,
                  &parser->bytes[length],
                  (size_t)(parser->count - length));
    parser->count = (uint16_t)(parser->count - length);
}

static bool RobotProtocolParser_IsIncompleteTimedOut(
    const RobotProtocolParser_t *parser,
    uint32_t now_ms)
{
    return ((uint32_t)(now_ms - parser->last_byte_ms) >=
            ROBOT_PROTOCOL_INCOMPLETE_TIMEOUT_MS);
}

static uint16_t RobotProtocol_ReadU16Le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8u));
}

static int16_t RobotProtocol_ReadI16Le(const uint8_t *data)
{
    const uint16_t raw = RobotProtocol_ReadU16Le(data);
    int32_t value = (int32_t)raw;

    if ((raw & 0x8000u) != 0u)
    {
        value -= 65536L;
    }

    return (int16_t)value;
}

static void RobotProtocol_WriteU16Le(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xFFu);
    output[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void RobotProtocol_WriteI16Le(uint8_t *output, int16_t value)
{
    RobotProtocol_WriteU16Le(output, (uint16_t)value);
}

static uint8_t RobotProtocol_AckFlag(bool request_ack)
{
    return request_ack ? ROBOT_PROTOCOL_FLAG_ACK_REQUEST : 0u;
}

static bool RobotProtocol_IsKnownOwner(uint8_t owner)
{
    return (owner == ROBOT_PROTOCOL_OWNER_CAN) ||
           (owner == ROBOT_PROTOCOL_OWNER_UART) ||
           (owner == ROBOT_PROTOCOL_OWNER_TEST) ||
           (owner == ROBOT_PROTOCOL_OWNER_NONE);
}

static size_t RobotProtocol_EncodeEmpty(uint8_t type,
                                       uint16_t sequence,
                                       bool request_ack,
                                       uint8_t *output,
                                       size_t output_capacity)
{
    RobotProtocolFrame_t frame = {0};

    frame.version = ROBOT_PROTOCOL_VERSION;
    frame.type = type;
    frame.flags = RobotProtocol_AckFlag(request_ack);
    frame.sequence = sequence;
    return RobotProtocol_EncodeFrame(&frame, output, output_capacity);
}

void RobotProtocolParser_Init(RobotProtocolParser_t *parser)
{
    RobotProtocolParser_Reset(parser);
}

void RobotProtocolParser_Reset(RobotProtocolParser_t *parser)
{
    if (parser == NULL)
    {
        return;
    }

    parser->count = 0u;
    parser->last_byte_ms = 0u;
}

bool RobotProtocolParser_PushByte(RobotProtocolParser_t *parser,
                                  uint8_t byte,
                                  uint32_t now_ms)
{
    if ((parser == NULL) ||
        (parser->count >= ROBOT_PROTOCOL_PARSER_BUFFER_SIZE))
    {
        return false;
    }

    parser->bytes[parser->count] = byte;
    parser->count++;
    parser->last_byte_ms = now_ms;
    return true;
}

RobotProtocolParseResult_t RobotProtocolParser_Next(
    RobotProtocolParser_t *parser,
    uint32_t now_ms,
    RobotProtocolFrame_t *frame)
{
    uint8_t payload_length;
    uint16_t received_crc;
    uint16_t calculated_crc;
    uint16_t frame_length;

    if ((parser == NULL) || (frame == NULL))
    {
        return ROBOT_PROTOCOL_PARSE_BAD_ARGUMENT;
    }

    for (;;)
    {
        if (parser->count == 0u)
        {
            return ROBOT_PROTOCOL_PARSE_NONE;
        }

        if (parser->bytes[0] != ROBOT_PROTOCOL_SOF_0)
        {
            RobotProtocolParser_RemovePrefix(parser, 1u);
            continue;
        }

        if (parser->count < 2u)
        {
            if (RobotProtocolParser_IsIncompleteTimedOut(parser, now_ms))
            {
                RobotProtocolParser_RemovePrefix(parser, 1u);
                return ROBOT_PROTOCOL_PARSE_DROPPED_TIMEOUT;
            }

            return ROBOT_PROTOCOL_PARSE_NONE;
        }

        if (parser->bytes[1] != ROBOT_PROTOCOL_SOF_1)
        {
            RobotProtocolParser_RemovePrefix(parser, 1u);
            continue;
        }

        if (parser->count < ROBOT_PROTOCOL_HEADER_SIZE)
        {
            if (RobotProtocolParser_IsIncompleteTimedOut(parser, now_ms))
            {
                RobotProtocolParser_RemovePrefix(parser, 1u);
                return ROBOT_PROTOCOL_PARSE_DROPPED_TIMEOUT;
            }

            return ROBOT_PROTOCOL_PARSE_NONE;
        }

        payload_length = parser->bytes[7];
        if (payload_length > ROBOT_PROTOCOL_MAX_PAYLOAD_SIZE)
        {
            RobotProtocolParser_RemovePrefix(parser, 1u);
            return ROBOT_PROTOCOL_PARSE_DROPPED_BAD_LENGTH;
        }

        frame_length = (uint16_t)(ROBOT_PROTOCOL_MIN_FRAME_SIZE +
                                  payload_length);
        if (parser->count < frame_length)
        {
            if (RobotProtocolParser_IsIncompleteTimedOut(parser, now_ms))
            {
                RobotProtocolParser_RemovePrefix(parser, 1u);
                return ROBOT_PROTOCOL_PARSE_DROPPED_TIMEOUT;
            }

            return ROBOT_PROTOCOL_PARSE_NONE;
        }

        received_crc = RobotProtocol_ReadU16Le(
            &parser->bytes[ROBOT_PROTOCOL_HEADER_SIZE + payload_length]);
        calculated_crc = RobotProtocol_Crc16CcittFalse(
            &parser->bytes[ROBOT_PROTOCOL_CRC_INPUT_OFFSET],
            (size_t)(ROBOT_PROTOCOL_FIXED_CRC_INPUT_SIZE + payload_length));

        if (received_crc != calculated_crc)
        {
            RobotProtocolParser_RemovePrefix(parser, 1u);
            return ROBOT_PROTOCOL_PARSE_DROPPED_BAD_CRC;
        }

        frame->version = parser->bytes[2];
        frame->type = parser->bytes[3];
        frame->flags = parser->bytes[4];
        frame->sequence = RobotProtocol_ReadU16Le(&parser->bytes[5]);
        frame->payload_length = payload_length;
        if (payload_length > 0u)
        {
            (void)memcpy(frame->payload,
                         &parser->bytes[ROBOT_PROTOCOL_HEADER_SIZE],
                         payload_length);
        }

        RobotProtocolParser_RemovePrefix(parser, frame_length);
        return ROBOT_PROTOCOL_PARSE_FRAME;
    }
}

uint16_t RobotProtocol_Crc16CcittFalse(const uint8_t *data, size_t length)
{
    uint16_t crc = ROBOT_PROTOCOL_CRC_INITIAL_VALUE;
    size_t index;
    uint8_t bit;

    if ((data == NULL) && (length != 0u))
    {
        return 0u;
    }

    for (index = 0u; index < length; index++)
    {
        crc ^= (uint16_t)((uint16_t)data[index] << 8u);

        for (bit = 0u; bit < 8u; bit++)
        {
            if ((crc & 0x8000u) != 0u)
            {
                crc = (uint16_t)((crc << 1u) ^
                                 ROBOT_PROTOCOL_CRC_POLYNOMIAL);
            }
            else
            {
                crc = (uint16_t)(crc << 1u);
            }
        }
    }

    return crc;
}

size_t RobotProtocol_EncodeFrame(const RobotProtocolFrame_t *frame,
                                 uint8_t *output,
                                 size_t output_capacity)
{
    uint16_t crc;
    size_t frame_length;

    if ((frame == NULL) || (output == NULL) ||
        (frame->version != ROBOT_PROTOCOL_VERSION) ||
        (frame->payload_length > ROBOT_PROTOCOL_MAX_PAYLOAD_SIZE) ||
        ((frame->flags &
          (uint8_t)(~ROBOT_PROTOCOL_FLAG_ALLOWED_MASK)) != 0u))
    {
        return 0u;
    }

    frame_length = (size_t)(ROBOT_PROTOCOL_MIN_FRAME_SIZE +
                            frame->payload_length);
    if (output_capacity < frame_length)
    {
        return 0u;
    }

    output[0] = ROBOT_PROTOCOL_SOF_0;
    output[1] = ROBOT_PROTOCOL_SOF_1;
    output[2] = frame->version;
    output[3] = frame->type;
    output[4] = frame->flags;
    RobotProtocol_WriteU16Le(&output[5], frame->sequence);
    output[7] = frame->payload_length;

    if (frame->payload_length > 0u)
    {
        (void)memcpy(&output[ROBOT_PROTOCOL_HEADER_SIZE],
                     frame->payload,
                     frame->payload_length);
    }

    crc = RobotProtocol_Crc16CcittFalse(
        &output[ROBOT_PROTOCOL_CRC_INPUT_OFFSET],
        (size_t)(ROBOT_PROTOCOL_FIXED_CRC_INPUT_SIZE +
                 frame->payload_length));
    RobotProtocol_WriteU16Le(
        &output[ROBOT_PROTOCOL_HEADER_SIZE + frame->payload_length], crc);

    return frame_length;
}

size_t RobotProtocol_EncodeMotor(uint16_t sequence,
                                 int16_t left_pwm,
                                 int16_t right_pwm,
                                 bool request_ack,
                                 uint8_t *output,
                                 size_t output_capacity)
{
    RobotProtocolFrame_t frame = {0};

    if ((left_pwm < -ROBOT_PROTOCOL_MOTOR_PWM_LIMIT) ||
        (left_pwm > ROBOT_PROTOCOL_MOTOR_PWM_LIMIT) ||
        (right_pwm < -ROBOT_PROTOCOL_MOTOR_PWM_LIMIT) ||
        (right_pwm > ROBOT_PROTOCOL_MOTOR_PWM_LIMIT))
    {
        return 0u;
    }

    frame.version = ROBOT_PROTOCOL_VERSION;
    frame.type = ROBOT_PROTOCOL_TYPE_MOTOR_CONTROL;
    frame.flags = RobotProtocol_AckFlag(request_ack);
    frame.sequence = sequence;
    frame.payload_length = 5u;
    RobotProtocol_WriteI16Le(&frame.payload[0], left_pwm);
    RobotProtocol_WriteI16Le(&frame.payload[2], right_pwm);
    frame.payload[4] = ROBOT_PROTOCOL_MOTOR_FLAG_ENABLE;

    return RobotProtocol_EncodeFrame(&frame, output, output_capacity);
}

size_t RobotProtocol_EncodeStop(uint16_t sequence,
                                bool request_ack,
                                uint8_t *output,
                                size_t output_capacity)
{
    return RobotProtocol_EncodeEmpty(ROBOT_PROTOCOL_TYPE_STOP,
                                     sequence,
                                     request_ack,
                                     output,
                                     output_capacity);
}

size_t RobotProtocol_EncodeSetMode(uint16_t sequence,
                                   uint8_t mode,
                                   bool request_ack,
                                   uint8_t *output,
                                   size_t output_capacity)
{
    RobotProtocolFrame_t frame = {0};

    if (mode > ROBOT_PROTOCOL_MODE_AUTO)
    {
        return 0u;
    }

    frame.version = ROBOT_PROTOCOL_VERSION;
    frame.type = ROBOT_PROTOCOL_TYPE_SET_MODE;
    frame.flags = RobotProtocol_AckFlag(request_ack);
    frame.sequence = sequence;
    frame.payload_length = 1u;
    frame.payload[0] = mode;
    return RobotProtocol_EncodeFrame(&frame, output, output_capacity);
}

size_t RobotProtocol_EncodeStatusRequest(uint16_t sequence,
                                         bool request_ack,
                                         uint8_t *output,
                                         size_t output_capacity)
{
    return RobotProtocol_EncodeEmpty(ROBOT_PROTOCOL_TYPE_STATUS_REQUEST,
                                     sequence,
                                     request_ack,
                                     output,
                                     output_capacity);
}

size_t RobotProtocol_EncodeHeartbeat(uint16_t sequence,
                                     bool request_ack,
                                     uint8_t *output,
                                     size_t output_capacity)
{
    return RobotProtocol_EncodeEmpty(ROBOT_PROTOCOL_TYPE_HEARTBEAT,
                                     sequence,
                                     request_ack,
                                     output,
                                     output_capacity);
}

bool RobotProtocol_DecodeAck(const RobotProtocolFrame_t *frame,
                             RobotProtocolAck_t *ack)
{
    RobotProtocolAck_t decoded;

    if ((frame == NULL) || (ack == NULL) ||
        (frame->version != ROBOT_PROTOCOL_VERSION) ||
        (frame->type != ROBOT_PROTOCOL_TYPE_ACK) ||
        (frame->flags != 0u) ||
        (frame->payload_length != ROBOT_PROTOCOL_ACK_PAYLOAD_SIZE))
    {
        return false;
    }

    decoded.sequence = frame->sequence;
    decoded.request_type = frame->payload[0];
    decoded.result = frame->payload[1];
    decoded.current_mode = frame->payload[2];
    decoded.owner = frame->payload[3];

    if ((decoded.result > ROBOT_PROTOCOL_ACK_INTERNAL_ERROR) ||
        (decoded.current_mode > ROBOT_PROTOCOL_MODE_AUTO) ||
        !RobotProtocol_IsKnownOwner(decoded.owner))
    {
        return false;
    }

    *ack = decoded;
    return true;
}

bool RobotProtocol_DecodeStatus(const RobotProtocolFrame_t *frame,
                                RobotProtocolStatus_t *status)
{
    RobotProtocolStatus_t decoded;
    bool battery_valid;
    bool distance_valid;

    if ((frame == NULL) || (status == NULL) ||
        (frame->version != ROBOT_PROTOCOL_VERSION) ||
        (frame->type != ROBOT_PROTOCOL_TYPE_STATUS) ||
        (frame->flags != 0u) ||
        (frame->payload_length != ROBOT_PROTOCOL_STATUS_PAYLOAD_SIZE))
    {
        return false;
    }

    decoded.sequence = frame->sequence;
    decoded.battery_mv = RobotProtocol_ReadU16Le(&frame->payload[0]);
    decoded.applied_left_pwm = RobotProtocol_ReadI16Le(&frame->payload[2]);
    decoded.applied_right_pwm = RobotProtocol_ReadI16Le(&frame->payload[4]);
    decoded.distance_mm = RobotProtocol_ReadU16Le(&frame->payload[6]);
    decoded.mode = frame->payload[8];
    decoded.error_status = frame->payload[9];
    decoded.valid_flags = frame->payload[10];
    decoded.owner = frame->payload[11];
    battery_valid = (decoded.valid_flags &
                     ROBOT_PROTOCOL_STATUS_VALID_BATTERY) != 0u;
    distance_valid = (decoded.valid_flags &
                      ROBOT_PROTOCOL_STATUS_VALID_DISTANCE) != 0u;

    if ((decoded.mode > ROBOT_PROTOCOL_MODE_AUTO) ||
        ((decoded.valid_flags &
          (uint8_t)(~ROBOT_PROTOCOL_STATUS_VALID_MASK)) != 0u) ||
        (decoded.applied_left_pwm < -ROBOT_PROTOCOL_MOTOR_PWM_LIMIT) ||
        (decoded.applied_left_pwm > ROBOT_PROTOCOL_MOTOR_PWM_LIMIT) ||
        (decoded.applied_right_pwm < -ROBOT_PROTOCOL_MOTOR_PWM_LIMIT) ||
        (decoded.applied_right_pwm > ROBOT_PROTOCOL_MOTOR_PWM_LIMIT) ||
        (battery_valid !=
         (decoded.battery_mv != ROBOT_PROTOCOL_INVALID_U16)) ||
        (distance_valid !=
         (decoded.distance_mm != ROBOT_PROTOCOL_INVALID_U16)) ||
        !RobotProtocol_IsKnownOwner(decoded.owner))
    {
        return false;
    }

    *status = decoded;
    return true;
}
