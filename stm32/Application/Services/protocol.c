#include "protocol.h"

#include <string.h>

#define PROTOCOL_CRC_POLYNOMIAL                 0x1021u
#define PROTOCOL_CRC_INITIAL_VALUE              0xFFFFu
#define PROTOCOL_CRC_INPUT_OFFSET               2u
#define PROTOCOL_FIXED_CRC_INPUT_SIZE           6u
#define PROTOCOL_STATUS_PAYLOAD_SIZE            12u
#define PROTOCOL_ACK_PAYLOAD_SIZE               4u

static void ProtocolParser_RemovePrefix(ProtocolParser_t *parser, uint16_t length)
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

static bool ProtocolParser_IsIncompleteTimedOut(const ProtocolParser_t *parser,
                                                uint32_t now_ms)
{
    return ((uint32_t)(now_ms - parser->last_byte_ms) >=
            PROTOCOL_INCOMPLETE_TIMEOUT_MS);
}

static uint16_t Protocol_ReadU16Le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8u));
}

static int16_t Protocol_ReadI16Le(const uint8_t *data)
{
    uint16_t raw = Protocol_ReadU16Le(data);
    int32_t value = (int32_t)raw;

    if ((raw & 0x8000u) != 0u)
    {
        value -= 65536L;
    }

    return (int16_t)value;
}

static void Protocol_WriteU16Le(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xFFu);
    output[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void Protocol_WriteI16Le(uint8_t *output, int16_t value)
{
    Protocol_WriteU16Le(output, (uint16_t)value);
}

void ProtocolParser_Init(ProtocolParser_t *parser)
{
    ProtocolParser_Reset(parser);
}

void ProtocolParser_Reset(ProtocolParser_t *parser)
{
    if (parser == NULL)
    {
        return;
    }

    parser->count = 0u;
    parser->last_byte_ms = 0u;
}

bool ProtocolParser_PushByte(ProtocolParser_t *parser,
                             uint8_t byte,
                             uint32_t now_ms)
{
    if ((parser == NULL) || (parser->count >= PROTOCOL_PARSER_BUFFER_SIZE))
    {
        return false;
    }

    parser->bytes[parser->count] = byte;
    parser->count++;
    parser->last_byte_ms = now_ms;
    return true;
}

ProtocolParseResult_t ProtocolParser_Next(ProtocolParser_t *parser,
                                          uint32_t now_ms,
                                          ProtocolFrame_t *frame)
{
    uint8_t payload_length;
    uint16_t received_crc;
    uint16_t calculated_crc;
    uint16_t frame_length;

    if ((parser == NULL) || (frame == NULL))
    {
        return PROTOCOL_PARSE_BAD_ARGUMENT;
    }

    for (;;)
    {
        if (parser->count == 0u)
        {
            return PROTOCOL_PARSE_NONE;
        }

        if (parser->bytes[0] != PROTOCOL_SOF_0)
        {
            ProtocolParser_RemovePrefix(parser, 1u);
            continue;
        }

        if (parser->count < 2u)
        {
            if (ProtocolParser_IsIncompleteTimedOut(parser, now_ms))
            {
                ProtocolParser_RemovePrefix(parser, 1u);
                return PROTOCOL_PARSE_DROPPED_TIMEOUT;
            }

            return PROTOCOL_PARSE_NONE;
        }

        if (parser->bytes[1] != PROTOCOL_SOF_1)
        {
            ProtocolParser_RemovePrefix(parser, 1u);
            continue;
        }

        if (parser->count < PROTOCOL_HEADER_SIZE)
        {
            if (ProtocolParser_IsIncompleteTimedOut(parser, now_ms))
            {
                ProtocolParser_RemovePrefix(parser, 1u);
                return PROTOCOL_PARSE_DROPPED_TIMEOUT;
            }

            return PROTOCOL_PARSE_NONE;
        }

        payload_length = parser->bytes[7];
        if (payload_length > PROTOCOL_MAX_PAYLOAD_SIZE)
        {
            ProtocolParser_RemovePrefix(parser, 1u);
            return PROTOCOL_PARSE_DROPPED_BAD_LENGTH;
        }

        frame_length = (uint16_t)(PROTOCOL_MIN_FRAME_SIZE + payload_length);
        if (parser->count < frame_length)
        {
            if (ProtocolParser_IsIncompleteTimedOut(parser, now_ms))
            {
                ProtocolParser_RemovePrefix(parser, 1u);
                return PROTOCOL_PARSE_DROPPED_TIMEOUT;
            }

            return PROTOCOL_PARSE_NONE;
        }

        received_crc = Protocol_ReadU16Le(
            &parser->bytes[PROTOCOL_HEADER_SIZE + payload_length]);
        calculated_crc = Protocol_Crc16CcittFalse(
            &parser->bytes[PROTOCOL_CRC_INPUT_OFFSET],
            (size_t)(PROTOCOL_FIXED_CRC_INPUT_SIZE + payload_length));

        if (received_crc != calculated_crc)
        {
            ProtocolParser_RemovePrefix(parser, 1u);
            return PROTOCOL_PARSE_DROPPED_BAD_CRC;
        }

        frame->version = parser->bytes[2];
        frame->type = parser->bytes[3];
        frame->flags = parser->bytes[4];
        frame->sequence = Protocol_ReadU16Le(&parser->bytes[5]);
        frame->payload_length = payload_length;
        if (payload_length > 0u)
        {
            (void)memcpy(frame->payload,
                         &parser->bytes[PROTOCOL_HEADER_SIZE],
                         payload_length);
        }

        ProtocolParser_RemovePrefix(parser, frame_length);
        return PROTOCOL_PARSE_FRAME;
    }
}

uint16_t Protocol_Crc16CcittFalse(const uint8_t *data, size_t length)
{
    uint16_t crc = PROTOCOL_CRC_INITIAL_VALUE;
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
                crc = (uint16_t)((crc << 1u) ^ PROTOCOL_CRC_POLYNOMIAL);
            }
            else
            {
                crc = (uint16_t)(crc << 1u);
            }
        }
    }

    return crc;
}

size_t Protocol_Encode(const ProtocolFrame_t *frame,
                       uint8_t *output,
                       size_t output_capacity)
{
    uint16_t crc;
    size_t frame_length;

    if ((frame == NULL) || (output == NULL) ||
        (frame->version != PROTOCOL_VERSION) ||
        (frame->payload_length > PROTOCOL_MAX_PAYLOAD_SIZE) ||
        ((frame->flags & (uint8_t)(~PROTOCOL_FLAG_ALLOWED_MASK)) != 0u))
    {
        return 0u;
    }

    frame_length = (size_t)(PROTOCOL_MIN_FRAME_SIZE + frame->payload_length);
    if (output_capacity < frame_length)
    {
        return 0u;
    }

    output[0] = PROTOCOL_SOF_0;
    output[1] = PROTOCOL_SOF_1;
    output[2] = frame->version;
    output[3] = frame->type;
    output[4] = frame->flags;
    Protocol_WriteU16Le(&output[5], frame->sequence);
    output[7] = frame->payload_length;

    if (frame->payload_length > 0u)
    {
        (void)memcpy(&output[PROTOCOL_HEADER_SIZE],
                     frame->payload,
                     frame->payload_length);
    }

    crc = Protocol_Crc16CcittFalse(
        &output[PROTOCOL_CRC_INPUT_OFFSET],
        (size_t)(PROTOCOL_FIXED_CRC_INPUT_SIZE + frame->payload_length));
    Protocol_WriteU16Le(&output[PROTOCOL_HEADER_SIZE + frame->payload_length], crc);

    return frame_length;
}

bool Protocol_DecodeMotor(const ProtocolFrame_t *frame,
                          ProtocolMotorCommand_t *command)
{
    int16_t left_pwm;
    int16_t right_pwm;
    uint8_t control_flags;

    if ((frame == NULL) || (command == NULL) ||
        (frame->version != PROTOCOL_VERSION) ||
        (frame->type != PROTOCOL_TYPE_MOTOR_CONTROL) ||
        ((frame->flags & (uint8_t)(~PROTOCOL_FLAG_ALLOWED_MASK)) != 0u) ||
        (frame->payload_length != 5u))
    {
        return false;
    }

    left_pwm = Protocol_ReadI16Le(&frame->payload[0]);
    right_pwm = Protocol_ReadI16Le(&frame->payload[2]);
    control_flags = frame->payload[4];

    if (((control_flags & (uint8_t)(~PROTOCOL_MOTOR_FLAG_ALLOWED_MASK)) != 0u) ||
        ((control_flags & PROTOCOL_MOTOR_FLAG_ENABLE) == 0u) ||
        (left_pwm < -PROTOCOL_MOTOR_PWM_LIMIT) ||
        (left_pwm > PROTOCOL_MOTOR_PWM_LIMIT) ||
        (right_pwm < -PROTOCOL_MOTOR_PWM_LIMIT) ||
        (right_pwm > PROTOCOL_MOTOR_PWM_LIMIT))
    {
        return false;
    }

    command->left_pwm = left_pwm;
    command->right_pwm = right_pwm;
    command->control_flags = control_flags;
    return true;
}

size_t Protocol_EncodeAck(uint16_t sequence,
                          uint8_t request_type,
                          uint8_t result,
                          uint8_t current_mode,
                          uint8_t owner,
                          uint8_t *output,
                          size_t output_capacity)
{
    ProtocolFrame_t frame;

    frame.version = PROTOCOL_VERSION;
    frame.type = PROTOCOL_TYPE_ACK;
    frame.flags = 0u;
    frame.sequence = sequence;
    frame.payload_length = PROTOCOL_ACK_PAYLOAD_SIZE;
    frame.payload[0] = request_type;
    frame.payload[1] = result;
    frame.payload[2] = current_mode;
    frame.payload[3] = owner;

    return Protocol_Encode(&frame, output, output_capacity);
}

size_t Protocol_EncodeStatus(uint16_t sequence,
                             const ProtocolStatus_t *status,
                             uint8_t *output,
                             size_t output_capacity)
{
    ProtocolFrame_t frame;

    if (status == NULL)
    {
        return 0u;
    }

    frame.version = PROTOCOL_VERSION;
    frame.type = PROTOCOL_TYPE_STATUS;
    frame.flags = 0u;
    frame.sequence = sequence;
    frame.payload_length = PROTOCOL_STATUS_PAYLOAD_SIZE;
    Protocol_WriteU16Le(&frame.payload[0], status->battery_mv);
    Protocol_WriteI16Le(&frame.payload[2], status->applied_left_pwm);
    Protocol_WriteI16Le(&frame.payload[4], status->applied_right_pwm);
    Protocol_WriteU16Le(&frame.payload[6], status->distance_mm);
    frame.payload[8] = status->mode;
    frame.payload[9] = status->error_status;
    frame.payload[10] = status->valid_flags;
    frame.payload[11] = status->owner;

    return Protocol_Encode(&frame, output, output_capacity);
}
