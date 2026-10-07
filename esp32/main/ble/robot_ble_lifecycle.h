#ifndef ROBOT_BLE_LIFECYCLE_H
#define ROBOT_BLE_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool pending;
    uint32_t deadline_ms;
} RobotBleDisconnectWatch_t;

/* Called with the BLE state lock held. A successful terminate request still
 * needs a deadline because its disconnect callback may never arrive. */
void RobotBleDisconnectWatch_Arm(RobotBleDisconnectWatch_t *watch,
                                 uint32_t now_ms);
bool RobotBleDisconnectWatch_TakeDue(RobotBleDisconnectWatch_t *watch,
                                     uint32_t now_ms);
void RobotBleDisconnectWatch_Clear(RobotBleDisconnectWatch_t *watch);

bool RobotBlePairing_AllowConnection(bool bonded, bool pairing_window_open,
                                     bool reset_requested);

#endif
