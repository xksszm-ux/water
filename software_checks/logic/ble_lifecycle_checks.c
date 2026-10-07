#include <stdint.h>

#include "robot_ble_lifecycle.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int BleLifecycleChecks_Run(void)
{
    RobotBleDisconnectWatch_t watch = {0};

    /* A successful terminate request without a callback must be retried. */
    RobotBleDisconnectWatch_Arm(&watch, 100U);
    CHECK(!RobotBleDisconnectWatch_TakeDue(&watch, 299U));
    CHECK(RobotBleDisconnectWatch_TakeDue(&watch, 300U));
    CHECK(!RobotBleDisconnectWatch_TakeDue(&watch, 300U));

    /* Each attempt gets its own bounded wait; disconnect cancels it. */
    RobotBleDisconnectWatch_Arm(&watch, 300U);
    CHECK(!RobotBleDisconnectWatch_TakeDue(&watch, 499U));
    RobotBleDisconnectWatch_Clear(&watch);
    CHECK(!RobotBleDisconnectWatch_TakeDue(&watch, 500U));

    RobotBleDisconnectWatch_Arm(&watch, UINT32_MAX - 50U);
    CHECK(!RobotBleDisconnectWatch_TakeDue(&watch, 148U));
    CHECK(RobotBleDisconnectWatch_TakeDue(&watch, 149U));
    CHECK(RobotBlePairing_AllowConnection(true, false, false));
    CHECK(RobotBlePairing_AllowConnection(false, true, false));
    CHECK(!RobotBlePairing_AllowConnection(false, false, false));
    CHECK(!RobotBlePairing_AllowConnection(true, true, true));
    return 0;
}
