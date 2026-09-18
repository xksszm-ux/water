#ifndef ROBOT_PROTOCOL_SELF_TEST_H
#define ROBOT_PROTOCOL_SELF_TEST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    ROBOT_PROTOCOL_SELF_TEST_CRC = (1u << 0),
    ROBOT_PROTOCOL_SELF_TEST_KNOWN_FRAME = (1u << 1),
    ROBOT_PROTOCOL_SELF_TEST_ENCODERS = (1u << 2),
    ROBOT_PROTOCOL_SELF_TEST_PARSER_FRAGMENT = (1u << 3),
    ROBOT_PROTOCOL_SELF_TEST_PARSER_RECOVERY = (1u << 4),
    ROBOT_PROTOCOL_SELF_TEST_TIMEOUT = (1u << 5),
    ROBOT_PROTOCOL_SELF_TEST_ACK = (1u << 6),
    ROBOT_PROTOCOL_SELF_TEST_STATUS = (1u << 7),
    ROBOT_PROTOCOL_SELF_TEST_RANGE_REJECTION = (1u << 8)
} RobotProtocolSelfTestFailure_t;

/*
 * Pure-C startup test. Returns true only when every check passes. If non-NULL,
 * failure_mask receives a bitwise OR of RobotProtocolSelfTestFailure_t values.
 */
bool RobotProtocolSelfTest_Run(uint32_t *failure_mask);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_PROTOCOL_SELF_TEST_H */
