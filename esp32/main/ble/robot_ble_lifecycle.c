#include "robot_ble_lifecycle.h"

#define ROBOT_BLE_DISCONNECT_RETRY_MS 200U

void RobotBleDisconnectWatch_Arm(RobotBleDisconnectWatch_t *watch,
                                 uint32_t now_ms)
{
    watch->pending = true;
    watch->deadline_ms = now_ms + ROBOT_BLE_DISCONNECT_RETRY_MS;
}

bool RobotBleDisconnectWatch_TakeDue(RobotBleDisconnectWatch_t *watch,
                                     uint32_t now_ms)
{
    if (!watch->pending ||
        (int32_t)(now_ms - watch->deadline_ms) < 0)
    {
        return false;
    }
    watch->pending = false;
    return true;
}

void RobotBleDisconnectWatch_Clear(RobotBleDisconnectWatch_t *watch)
{
    watch->pending = false;
}

bool RobotBlePairing_AllowConnection(bool bonded, bool pairing_window_open,
                                     bool reset_requested)
{
    return !reset_requested && (bonded || pairing_window_open);
}
