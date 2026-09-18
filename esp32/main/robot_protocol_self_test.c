#include "robot_protocol_self_test.h"

#include <stddef.h>
#include <string.h>

#include "robot_protocol.h"

static bool SelfTest_PushBytes(RobotProtocolParser_t *parser,
                               const uint8_t *bytes,
                               size_t length,
                               uint32_t now_ms)
{
    size_t index;

    if ((parser == NULL) || ((bytes == NULL) && (length != 0u)))
    {
        return false;
    }

    for (index = 0u; index < length; index++)
    {
        if (!RobotProtocolParser_PushByte(parser, bytes[index], now_ms))
        {
            return false;
        }
    }

    return true;
}

static bool SelfTest_DecodeWire(const uint8_t *wire,
                                size_t length,
                                RobotProtocolFrame_t *frame)
{
    RobotProtocolParser_t parser;

    RobotProtocolParser_Init(&parser);
    return SelfTest_PushBytes(&parser, wire, length, 1u) &&
           (RobotProtocolParser_Next(&parser, 1u, frame) ==
            ROBOT_PROTOCOL_PARSE_FRAME) &&
           (parser.count == 0u);
}

static bool SelfTest_Crc(void)
{
    static const uint8_t standard_vector[] = "123456789";

    return (RobotProtocol_Crc16CcittFalse(
                standard_vector, sizeof(standard_vector) - 1u) == 0x29B1u) &&
           (RobotProtocol_Crc16CcittFalse(NULL, 0u) == 0xFFFFu) &&
           (RobotProtocol_Crc16CcittFalse(NULL, 1u) == 0u);
}

static bool SelfTest_KnownFrame(void)
{
    static const uint8_t expected_stop[] = {
        0xA5u, 0x5Au, 0x01u, 0x02u, 0x01u,
        0x01u, 0x00u, 0x00u, 0xB7u, 0x4Eu
    };
    static const uint8_t expected_heartbeat[] = {
        0xA5u, 0x5Au, 0x01u, 0x05u, 0x00u,
        0x00u, 0x00u, 0x00u, 0xE7u, 0x68u
    };
    static const uint8_t expected_motor[] = {
        0xA5u, 0x5Au, 0x01u, 0x01u, 0x01u,
        0x34u, 0x12u, 0x05u, 0xF4u, 0x01u,
        0x0Cu, 0xFEu, 0x01u, 0x62u, 0x40u
    };
    static const uint8_t expected_stop_ack[] = {
        0xA5u, 0x5Au, 0x01u, 0x80u, 0x00u,
        0x01u, 0x00u, 0x04u, 0x02u, 0x00u,
        0x00u, 0x01u, 0xE0u, 0xFDu
    };
    uint8_t output[ROBOT_PROTOCOL_MAX_FRAME_SIZE];
    RobotProtocolFrame_t frame;
    RobotProtocolAck_t ack;
    size_t length;

    length = RobotProtocol_EncodeStop(1u, true, output, sizeof(output));
    if ((length != sizeof(expected_stop)) ||
        (memcmp(output, expected_stop, sizeof(expected_stop)) != 0))
    {
        return false;
    }

    length = RobotProtocol_EncodeHeartbeat(0u, false, output, sizeof(output));
    if ((length != sizeof(expected_heartbeat)) ||
        (memcmp(output, expected_heartbeat, sizeof(expected_heartbeat)) != 0))
    {
        return false;
    }

    length = RobotProtocol_EncodeMotor(
        0x1234u, 500, -500, true, output, sizeof(output));
    if ((length != sizeof(expected_motor)) ||
        (memcmp(output, expected_motor, sizeof(expected_motor)) != 0))
    {
        return false;
    }

    return SelfTest_DecodeWire(expected_stop_ack,
                               sizeof(expected_stop_ack),
                               &frame) &&
           RobotProtocol_DecodeAck(&frame, &ack) &&
           (ack.sequence == 1u) &&
           (ack.request_type == ROBOT_PROTOCOL_TYPE_STOP) &&
           (ack.result == ROBOT_PROTOCOL_ACK_OK) &&
           (ack.current_mode == ROBOT_PROTOCOL_MODE_BLE) &&
           (ack.owner == ROBOT_PROTOCOL_OWNER_UART);
}

static bool SelfTest_Encoders(void)
{
    uint8_t output[ROBOT_PROTOCOL_MAX_FRAME_SIZE];
    RobotProtocolFrame_t frame;
    size_t length;
    uint8_t index;

    length = RobotProtocol_EncodeMotor(
        0xBEEFu, -1000, 1000, true, output, sizeof(output));
    if ((length != 15u) || !SelfTest_DecodeWire(output, length, &frame) ||
        (frame.type != ROBOT_PROTOCOL_TYPE_MOTOR_CONTROL) ||
        (frame.flags != ROBOT_PROTOCOL_FLAG_ACK_REQUEST) ||
        (frame.sequence != 0xBEEFu) || (frame.payload_length != 5u) ||
        (frame.payload[0] != 0x18u) || (frame.payload[1] != 0xFCu) ||
        (frame.payload[2] != 0xE8u) || (frame.payload[3] != 0x03u) ||
        (frame.payload[4] != ROBOT_PROTOCOL_MOTOR_FLAG_ENABLE))
    {
        return false;
    }

    length = RobotProtocol_EncodeSetMode(
        2u, ROBOT_PROTOCOL_MODE_AUTO, false, output, sizeof(output));
    if ((length != 11u) || !SelfTest_DecodeWire(output, length, &frame) ||
        (frame.type != ROBOT_PROTOCOL_TYPE_SET_MODE) ||
        (frame.flags != 0u) || (frame.payload_length != 1u) ||
        (frame.payload[0] != ROBOT_PROTOCOL_MODE_AUTO))
    {
        return false;
    }

    length = RobotProtocol_EncodeStatusRequest(
        3u, true, output, sizeof(output));
    if ((length != ROBOT_PROTOCOL_MIN_FRAME_SIZE) ||
        !SelfTest_DecodeWire(output, length, &frame) ||
        (frame.type != ROBOT_PROTOCOL_TYPE_STATUS_REQUEST) ||
        (frame.payload_length != 0u))
    {
        return false;
    }

    length = RobotProtocol_EncodeHeartbeat(
        4u, false, output, sizeof(output));
    if ((length != ROBOT_PROTOCOL_MIN_FRAME_SIZE) ||
        !SelfTest_DecodeWire(output, length, &frame) ||
        (frame.type != ROBOT_PROTOCOL_TYPE_HEARTBEAT) ||
        (frame.flags != 0u) || (frame.payload_length != 0u))
    {
        return false;
    }

    (void)memset(&frame, 0, sizeof(frame));
    frame.version = ROBOT_PROTOCOL_VERSION;
    frame.type = 0x7Fu;
    frame.sequence = 0x55AAu;
    frame.payload_length = ROBOT_PROTOCOL_MAX_PAYLOAD_SIZE;
    for (index = 0u; index < ROBOT_PROTOCOL_MAX_PAYLOAD_SIZE; index++)
    {
        frame.payload[index] = index;
    }
    length = RobotProtocol_EncodeFrame(&frame, output, sizeof(output));
    if ((length != ROBOT_PROTOCOL_MAX_FRAME_SIZE) ||
        !SelfTest_DecodeWire(output, length, &frame) ||
        (frame.type != 0x7Fu) || (frame.sequence != 0x55AAu) ||
        (frame.payload_length != ROBOT_PROTOCOL_MAX_PAYLOAD_SIZE))
    {
        return false;
    }
    for (index = 0u; index < ROBOT_PROTOCOL_MAX_PAYLOAD_SIZE; index++)
    {
        if (frame.payload[index] != index)
        {
            return false;
        }
    }
    return true;
}

static bool SelfTest_ParserFragmentAndNoise(void)
{
    uint8_t wire[ROBOT_PROTOCOL_MAX_FRAME_SIZE];
    RobotProtocolParser_t parser;
    RobotProtocolFrame_t frame;
    size_t length;

    length = RobotProtocol_EncodeHeartbeat(
        0x2244u, false, wire, sizeof(wire));
    if (length == 0u)
    {
        return false;
    }

    RobotProtocolParser_Init(&parser);
    if (!RobotProtocolParser_PushByte(&parser, 0x00u, 1u) ||
        !RobotProtocolParser_PushByte(&parser, ROBOT_PROTOCOL_SOF_0, 2u) ||
        !RobotProtocolParser_PushByte(&parser, 0x11u, 3u) ||
        !SelfTest_PushBytes(&parser, wire, 4u, 4u) ||
        (RobotProtocolParser_Next(&parser, 4u, &frame) !=
         ROBOT_PROTOCOL_PARSE_NONE) ||
        !SelfTest_PushBytes(&parser, &wire[4], length - 4u, 5u))
    {
        return false;
    }

    if ((RobotProtocolParser_Next(&parser, 5u, &frame) !=
         ROBOT_PROTOCOL_PARSE_FRAME) ||
        (frame.type != ROBOT_PROTOCOL_TYPE_HEARTBEAT) ||
        (frame.sequence != 0x2244u) || (parser.count != 0u))
    {
        return false;
    }

    /* Two frames may arrive in one UART event; both must remain extractable. */
    RobotProtocolParser_Reset(&parser);
    if (!SelfTest_PushBytes(&parser, wire, length, 6u) ||
        !SelfTest_PushBytes(&parser, wire, length, 6u) ||
        (RobotProtocolParser_Next(&parser, 6u, &frame) !=
         ROBOT_PROTOCOL_PARSE_FRAME) ||
        (RobotProtocolParser_Next(&parser, 6u, &frame) !=
         ROBOT_PROTOCOL_PARSE_FRAME) ||
        (parser.count != 0u))
    {
        return false;
    }

    /* A5 A5 5A overlap must discard only the first candidate SOF. */
    RobotProtocolParser_Reset(&parser);
    return RobotProtocolParser_PushByte(
               &parser, ROBOT_PROTOCOL_SOF_0, 7u) &&
           SelfTest_PushBytes(&parser, wire, length, 7u) &&
           (RobotProtocolParser_Next(&parser, 7u, &frame) ==
            ROBOT_PROTOCOL_PARSE_FRAME) &&
           (frame.sequence == 0x2244u) && (parser.count == 0u);
}

static bool SelfTest_ParserRecovery(void)
{
    static const uint8_t bad_length_header[] = {
        0xA5u, 0x5Au, 0x01u, 0x05u,
        0x00u, 0x01u, 0x00u, 0x21u
    };
    uint8_t valid[ROBOT_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t damaged[ROBOT_PROTOCOL_MAX_FRAME_SIZE];
    RobotProtocolParser_t parser;
    RobotProtocolFrame_t frame;
    size_t length;

    length = RobotProtocol_EncodeStop(
        0x77AAu, false, valid, sizeof(valid));
    if (length == 0u)
    {
        return false;
    }

    (void)memcpy(damaged, valid, length);
    damaged[length - 1u] ^= 0x80u;

    RobotProtocolParser_Init(&parser);
    if (!SelfTest_PushBytes(&parser, damaged, length, 10u) ||
        !SelfTest_PushBytes(&parser, valid, length, 11u) ||
        (RobotProtocolParser_Next(&parser, 11u, &frame) !=
         ROBOT_PROTOCOL_PARSE_DROPPED_BAD_CRC))
    {
        return false;
    }

    if ((RobotProtocolParser_Next(&parser, 11u, &frame) !=
         ROBOT_PROTOCOL_PARSE_FRAME) ||
        (frame.type != ROBOT_PROTOCOL_TYPE_STOP) ||
        (frame.sequence != 0x77AAu) || (parser.count != 0u))
    {
        return false;
    }

    RobotProtocolParser_Reset(&parser);
    if (!SelfTest_PushBytes(&parser,
                            bad_length_header,
                            sizeof(bad_length_header),
                            20u) ||
        !SelfTest_PushBytes(&parser, valid, length, 21u) ||
        (RobotProtocolParser_Next(&parser, 21u, &frame) !=
         ROBOT_PROTOCOL_PARSE_DROPPED_BAD_LENGTH))
    {
        return false;
    }

    return (RobotProtocolParser_Next(&parser, 21u, &frame) ==
            ROBOT_PROTOCOL_PARSE_FRAME) &&
           (frame.type == ROBOT_PROTOCOL_TYPE_STOP) &&
           (frame.sequence == 0x77AAu) && (parser.count == 0u);
}

static bool SelfTest_Timeout(void)
{
    static const uint8_t partial_header[] = {
        ROBOT_PROTOCOL_SOF_0, ROBOT_PROTOCOL_SOF_1, ROBOT_PROTOCOL_VERSION
    };
    uint8_t wire[ROBOT_PROTOCOL_MAX_FRAME_SIZE];
    RobotProtocolParser_t parser;
    RobotProtocolFrame_t frame;
    size_t length;

    RobotProtocolParser_Init(&parser);
    if (!RobotProtocolParser_PushByte(&parser, ROBOT_PROTOCOL_SOF_0, 100u) ||
        (RobotProtocolParser_Next(&parser, 149u, &frame) !=
         ROBOT_PROTOCOL_PARSE_NONE))
    {
        return false;
    }

    if ((RobotProtocolParser_Next(&parser, 150u, &frame) !=
         ROBOT_PROTOCOL_PARSE_DROPPED_TIMEOUT) ||
        (parser.count != 0u))
    {
        return false;
    }

    RobotProtocolParser_Reset(&parser);
    if (!SelfTest_PushBytes(&parser,
                            partial_header,
                            sizeof(partial_header),
                            200u) ||
        (RobotProtocolParser_Next(&parser, 249u, &frame) !=
         ROBOT_PROTOCOL_PARSE_NONE) ||
        (RobotProtocolParser_Next(&parser, 250u, &frame) !=
         ROBOT_PROTOCOL_PARSE_DROPPED_TIMEOUT))
    {
        return false;
    }

    length = RobotProtocol_EncodeMotor(
        1u, 100, -100, false, wire, sizeof(wire));
    if (length != 15u)
    {
        return false;
    }

    /* Timeout while waiting for the rest of the payload. */
    RobotProtocolParser_Reset(&parser);
    if (!SelfTest_PushBytes(&parser,
                            wire,
                            ROBOT_PROTOCOL_HEADER_SIZE + 1u,
                            300u) ||
        (RobotProtocolParser_Next(&parser, 350u, &frame) !=
         ROBOT_PROTOCOL_PARSE_DROPPED_TIMEOUT))
    {
        return false;
    }

    /* Timeout after a complete payload but before the second CRC byte. */
    RobotProtocolParser_Reset(&parser);
    if (!SelfTest_PushBytes(&parser, wire, length - 1u, 400u) ||
        (RobotProtocolParser_Next(&parser, 450u, &frame) !=
         ROBOT_PROTOCOL_PARSE_DROPPED_TIMEOUT))
    {
        return false;
    }

    /* The uint32_t subtraction must remain correct across tick wraparound. */
    RobotProtocolParser_Reset(&parser);
    if (!RobotProtocolParser_PushByte(
            &parser, ROBOT_PROTOCOL_SOF_0, UINT32_MAX - 20u) ||
        (RobotProtocolParser_Next(&parser, 28u, &frame) !=
         ROBOT_PROTOCOL_PARSE_NONE))
    {
        return false;
    }
    return (RobotProtocolParser_Next(&parser, 29u, &frame) ==
            ROBOT_PROTOCOL_PARSE_DROPPED_TIMEOUT) &&
           (parser.count == 0u);
}

static bool SelfTest_Ack(void)
{
    static const uint8_t known_ack[] = {
        0xA5u, 0x5Au, 0x01u, 0x80u, 0x00u, 0x57u, 0x13u, 0x04u,
        0x77u, 0x06u, 0x00u, 0xFFu, 0x77u, 0x20u
    };
    RobotProtocolFrame_t frame;
    RobotProtocolAck_t ack;

    if (!SelfTest_DecodeWire(known_ack, sizeof(known_ack), &frame) ||
        !RobotProtocol_DecodeAck(&frame, &ack) ||
        (ack.sequence != 0x1357u) || (ack.request_type != 0x77u) ||
        (ack.result != ROBOT_PROTOCOL_ACK_UNSUPPORTED) ||
        (ack.current_mode != ROBOT_PROTOCOL_MODE_BLE) ||
        (ack.owner != ROBOT_PROTOCOL_OWNER_NONE))
    {
        return false;
    }

    frame.payload[1] = (uint8_t)(ROBOT_PROTOCOL_ACK_INTERNAL_ERROR + 1u);
    if (RobotProtocol_DecodeAck(&frame, &ack))
    {
        return false;
    }
    frame.payload[1] = ROBOT_PROTOCOL_ACK_OK;
    frame.payload[2] = (uint8_t)(ROBOT_PROTOCOL_MODE_AUTO + 1u);
    if (RobotProtocol_DecodeAck(&frame, &ack))
    {
        return false;
    }
    frame.payload[2] = ROBOT_PROTOCOL_MODE_AUTO;
    frame.payload[3] = 3u;
    if (RobotProtocol_DecodeAck(&frame, &ack))
    {
        return false;
    }
    frame.payload[3] = ROBOT_PROTOCOL_OWNER_UART;
    frame.payload_length = 3u;
    if (RobotProtocol_DecodeAck(&frame, &ack))
    {
        return false;
    }
    frame.payload_length = 4u;
    frame.type = ROBOT_PROTOCOL_TYPE_STATUS;
    if (RobotProtocol_DecodeAck(&frame, &ack))
    {
        return false;
    }
    frame.type = ROBOT_PROTOCOL_TYPE_ACK;
    frame.version = (uint8_t)(ROBOT_PROTOCOL_VERSION + 1u);
    if (RobotProtocol_DecodeAck(&frame, &ack))
    {
        return false;
    }
    frame.version = ROBOT_PROTOCOL_VERSION;
    frame.flags = ROBOT_PROTOCOL_FLAG_ACK_REQUEST;
    return !RobotProtocol_DecodeAck(&frame, &ack);
}

static void SelfTest_InitStatusFrame(RobotProtocolFrame_t *frame,
                                     uint16_t battery_mv,
                                     int16_t left_pwm,
                                     int16_t right_pwm,
                                     uint16_t distance_mm,
                                     uint8_t valid_flags)
{
    uint16_t left_bits = (uint16_t)left_pwm;
    uint16_t right_bits = (uint16_t)right_pwm;

    (void)memset(frame, 0, sizeof(*frame));
    frame->version = ROBOT_PROTOCOL_VERSION;
    frame->type = ROBOT_PROTOCOL_TYPE_STATUS;
    frame->sequence = 0x2468u;
    frame->payload_length = 12u;
    frame->payload[0] = (uint8_t)(battery_mv & 0xFFu);
    frame->payload[1] = (uint8_t)(battery_mv >> 8u);
    frame->payload[2] = (uint8_t)(left_bits & 0xFFu);
    frame->payload[3] = (uint8_t)(left_bits >> 8u);
    frame->payload[4] = (uint8_t)(right_bits & 0xFFu);
    frame->payload[5] = (uint8_t)(right_bits >> 8u);
    frame->payload[6] = (uint8_t)(distance_mm & 0xFFu);
    frame->payload[7] = (uint8_t)(distance_mm >> 8u);
    frame->payload[8] = ROBOT_PROTOCOL_MODE_BLE;
    frame->payload[9] = 0u;
    frame->payload[10] = valid_flags;
    frame->payload[11] = ROBOT_PROTOCOL_OWNER_NONE;
}

static bool SelfTest_Status(void)
{
    static const uint8_t known_status[] = {
        0xA5u, 0x5Au, 0x01u, 0x81u, 0x00u, 0x68u, 0x24u, 0x0Cu,
        0x64u, 0x10u, 0xBFu, 0xFEu, 0x8Eu, 0x02u, 0xF4u, 0x01u,
        0x01u, 0x82u, 0x03u, 0x01u, 0x7Fu, 0x60u
    };
    /* Exact STM32 startup STATUS: invalid battery/distance, zero PWM. */
    static const uint8_t startup_status[] = {
        0xA5u, 0x5Au, 0x01u, 0x81u, 0x00u, 0x00u, 0x00u, 0x0Cu,
        0xFFu, 0xFFu, 0x00u, 0x00u, 0x00u, 0x00u, 0xFFu, 0xFFu,
        0x00u, 0xE0u, 0x00u, 0xFFu, 0xAFu, 0x6Fu
    };
    RobotProtocolFrame_t frame;
    RobotProtocolStatus_t status;

    if (!SelfTest_DecodeWire(known_status, sizeof(known_status), &frame) ||
        !RobotProtocol_DecodeStatus(&frame, &status) ||
        (status.sequence != 0x2468u) || (status.battery_mv != 4196u) ||
        (status.applied_left_pwm != -321) ||
        (status.applied_right_pwm != 654) ||
        (status.distance_mm != 500u) ||
        (status.mode != ROBOT_PROTOCOL_MODE_AUTO) ||
        (status.error_status != 0x82u) ||
        (status.valid_flags != ROBOT_PROTOCOL_STATUS_VALID_MASK) ||
        (status.owner != ROBOT_PROTOCOL_OWNER_UART))
    {
        return false;
    }

    if (!SelfTest_DecodeWire(startup_status, sizeof(startup_status), &frame) ||
        !RobotProtocol_DecodeStatus(&frame, &status) ||
        (status.battery_mv != ROBOT_PROTOCOL_INVALID_U16) ||
        (status.distance_mm != ROBOT_PROTOCOL_INVALID_U16) ||
        (status.applied_left_pwm != 0) ||
        (status.applied_right_pwm != 0) ||
        (status.mode != ROBOT_PROTOCOL_MODE_BLE) ||
        (status.error_status != 0xE0u) || (status.valid_flags != 0u) ||
        (status.owner != ROBOT_PROTOCOL_OWNER_NONE))
    {
        return false;
    }

    /* All four valid-bit combinations must match their 0xFFFF sentinels. */
    SelfTest_InitStatusFrame(&frame,
                             ROBOT_PROTOCOL_INVALID_U16,
                             0,
                             0,
                             ROBOT_PROTOCOL_INVALID_U16,
                             0u);
    if (!RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    SelfTest_InitStatusFrame(&frame,
                             4200u,
                             0,
                             0,
                             ROBOT_PROTOCOL_INVALID_U16,
                             ROBOT_PROTOCOL_STATUS_VALID_BATTERY);
    if (!RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    SelfTest_InitStatusFrame(&frame,
                             ROBOT_PROTOCOL_INVALID_U16,
                             0,
                             0,
                             4000u,
                             ROBOT_PROTOCOL_STATUS_VALID_DISTANCE);
    if (!RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    SelfTest_InitStatusFrame(&frame,
                             4200u,
                             -1000,
                             1000,
                             4000u,
                             ROBOT_PROTOCOL_STATUS_VALID_MASK);
    if (!RobotProtocol_DecodeStatus(&frame, &status) ||
        (status.applied_left_pwm != -1000) ||
        (status.applied_right_pwm != 1000))
    {
        return false;
    }

    SelfTest_InitStatusFrame(&frame,
                             4200u,
                             1000,
                             -1000,
                             4000u,
                             ROBOT_PROTOCOL_STATUS_VALID_MASK);
    if (!RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }

    /* Reject every valid-bit/sentinel contradiction. */
    frame.payload[0] = 0xFFu;
    frame.payload[1] = 0xFFu;
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    frame.payload[10] = ROBOT_PROTOCOL_STATUS_VALID_DISTANCE;
    frame.payload[0] = 0x68u;
    frame.payload[1] = 0x10u;
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    frame.payload[10] = ROBOT_PROTOCOL_STATUS_VALID_MASK;
    frame.payload[6] = 0xFFu;
    frame.payload[7] = 0xFFu;
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    frame.payload[10] = ROBOT_PROTOCOL_STATUS_VALID_BATTERY;
    frame.payload[6] = 0xA0u;
    frame.payload[7] = 0x0Fu;
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }

    /* Reject both channels on both sides of the PWM contract. */
    SelfTest_InitStatusFrame(&frame,
                             4200u,
                             1001,
                             0,
                             4000u,
                             ROBOT_PROTOCOL_STATUS_VALID_MASK);
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    SelfTest_InitStatusFrame(&frame,
                             4200u,
                             -1001,
                             0,
                             4000u,
                             ROBOT_PROTOCOL_STATUS_VALID_MASK);
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    SelfTest_InitStatusFrame(&frame,
                             4200u,
                             0,
                             1001,
                             4000u,
                             ROBOT_PROTOCOL_STATUS_VALID_MASK);
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    SelfTest_InitStatusFrame(&frame,
                             4200u,
                             0,
                             -1001,
                             4000u,
                             ROBOT_PROTOCOL_STATUS_VALID_MASK);
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }

    /* Reject malformed metadata and NULL arguments. */
    SelfTest_InitStatusFrame(&frame,
                             4200u,
                             0,
                             0,
                             4000u,
                             ROBOT_PROTOCOL_STATUS_VALID_MASK);
    if (RobotProtocol_DecodeStatus(NULL, &status) ||
        RobotProtocol_DecodeStatus(&frame, NULL))
    {
        return false;
    }
    frame.version = (uint8_t)(ROBOT_PROTOCOL_VERSION + 1u);
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    frame.version = ROBOT_PROTOCOL_VERSION;
    frame.type = ROBOT_PROTOCOL_TYPE_ACK;
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    frame.type = ROBOT_PROTOCOL_TYPE_STATUS;
    frame.payload_length = 11u;
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    frame.payload_length = 12u;
    frame.payload[10] = 0x04u;
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    frame.payload[10] = ROBOT_PROTOCOL_STATUS_VALID_MASK;
    frame.payload[8] = 2u;
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    frame.payload[8] = ROBOT_PROTOCOL_MODE_BLE;
    frame.payload[11] = 3u;
    if (RobotProtocol_DecodeStatus(&frame, &status))
    {
        return false;
    }
    frame.payload[11] = ROBOT_PROTOCOL_OWNER_NONE;
    frame.flags = ROBOT_PROTOCOL_FLAG_ACK_REQUEST;
    return !RobotProtocol_DecodeStatus(&frame, &status);
}

static bool SelfTest_RangeRejection(void)
{
    uint8_t output[ROBOT_PROTOCOL_MAX_FRAME_SIZE];
    RobotProtocolFrame_t frame = {0};
    RobotProtocolParser_t parser;

    if ((RobotProtocol_EncodeMotor(
             1u, 1001, 0, false, output, sizeof(output)) != 0u) ||
        (RobotProtocol_EncodeMotor(
             1u, 0, -1001, false, output, sizeof(output)) != 0u) ||
        (RobotProtocol_EncodeSetMode(
             1u, 2u, false, output, sizeof(output)) != 0u) ||
        (RobotProtocol_EncodeStop(
             1u, false, output, ROBOT_PROTOCOL_MIN_FRAME_SIZE - 1u) != 0u) ||
        (RobotProtocol_EncodeStop(
             1u, false, NULL, sizeof(output)) != 0u))
    {
        return false;
    }

    frame.version = ROBOT_PROTOCOL_VERSION;
    frame.type = ROBOT_PROTOCOL_TYPE_STOP;
    frame.flags = 0x80u;
    if (RobotProtocol_EncodeFrame(&frame, output, sizeof(output)) != 0u)
    {
        return false;
    }
    frame.flags = 0u;
    frame.version = (uint8_t)(ROBOT_PROTOCOL_VERSION + 1u);
    if (RobotProtocol_EncodeFrame(&frame, output, sizeof(output)) != 0u)
    {
        return false;
    }

    RobotProtocolParser_Init(&parser);
    while (parser.count < ROBOT_PROTOCOL_PARSER_BUFFER_SIZE)
    {
        if (!RobotProtocolParser_PushByte(&parser, 0u, 1u))
        {
            return false;
        }
    }
    return !RobotProtocolParser_PushByte(&parser, 0u, 1u);
}

bool RobotProtocolSelfTest_Run(uint32_t *failure_mask)
{
    uint32_t failures = 0u;

    if (!SelfTest_Crc())
    {
        failures |= ROBOT_PROTOCOL_SELF_TEST_CRC;
    }
    if (!SelfTest_KnownFrame())
    {
        failures |= ROBOT_PROTOCOL_SELF_TEST_KNOWN_FRAME;
    }
    if (!SelfTest_Encoders())
    {
        failures |= ROBOT_PROTOCOL_SELF_TEST_ENCODERS;
    }
    if (!SelfTest_ParserFragmentAndNoise())
    {
        failures |= ROBOT_PROTOCOL_SELF_TEST_PARSER_FRAGMENT;
    }
    if (!SelfTest_ParserRecovery())
    {
        failures |= ROBOT_PROTOCOL_SELF_TEST_PARSER_RECOVERY;
    }
    if (!SelfTest_Timeout())
    {
        failures |= ROBOT_PROTOCOL_SELF_TEST_TIMEOUT;
    }
    if (!SelfTest_Ack())
    {
        failures |= ROBOT_PROTOCOL_SELF_TEST_ACK;
    }
    if (!SelfTest_Status())
    {
        failures |= ROBOT_PROTOCOL_SELF_TEST_STATUS;
    }
    if (!SelfTest_RangeRejection())
    {
        failures |= ROBOT_PROTOCOL_SELF_TEST_RANGE_REJECTION;
    }

    if (failure_mask != NULL)
    {
        *failure_mask = failures;
    }
    return failures == 0u;
}

#if defined(ROBOT_PROTOCOL_SELF_TEST_STANDALONE)
int main(void)
{
    return RobotProtocolSelfTest_Run(NULL) ? 0 : 1;
}
#endif
