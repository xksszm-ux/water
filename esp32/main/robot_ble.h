#ifndef ROBOT_BLE_H
#define ROBOT_BLE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOT_BLE_DEVICE_NAME "SmartRobot-V1"

typedef struct
{
    uint32_t connections;
    uint32_t disconnections;
    uint32_t stop_commands;
    uint32_t rejected_commands;
    uint32_t notifications;
    uint32_t notification_errors;
    uint32_t host_resets;
    uint32_t encryption_failures;
    uint32_t pairing_resets;
    uint32_t pairing_reset_errors;
    uint32_t advertising_retries;
    uint32_t advertising_errors;
    uint32_t lifecycle_errors;
} RobotBleDiagnostics_t;

/*
 * Starts the Step 10A BLE peripheral. This milestone intentionally accepts
 * STOP only. Non-zero motion remains disabled until the UART control state
 * machine, authenticated BLE session, and power hardware have been accepted.
 */
esp_err_t RobotBle_Start(void);

/* Gracefully stops the Host task, the status task, and the NimBLE stack. */
esp_err_t RobotBle_Stop(void);

/* True only after Host sync and the first advertising start both succeed. */
bool RobotBle_IsReady(void);

bool RobotBle_IsConnected(void);

void RobotBle_GetDiagnostics(RobotBleDiagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_BLE_H */
