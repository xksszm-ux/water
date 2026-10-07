#ifndef ROBOT_BLE_PROTOCOL_SELF_TEST_H
#define ROBOT_BLE_PROTOCOL_SELF_TEST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool RobotBleProtocolSelfTest_Run(uint32_t *failure_mask);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_BLE_PROTOCOL_SELF_TEST_H */
