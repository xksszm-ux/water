#ifndef ROBOT_UART_LINK_H
#define ROBOT_UART_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "robot_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOT_UART_PORT_NUMBER          2
#define ROBOT_UART_TX_GPIO              17
#define ROBOT_UART_RX_GPIO              16
#define ROBOT_UART_BAUD_RATE            115200

typedef struct
{
    uint32_t received_frames;
    uint32_t crc_errors;
    uint32_t parser_timeouts;
    uint32_t semantic_rejects;
    uint32_t receive_overflows;
    uint32_t uart_errors;
    uint32_t transmitted_frames;
    uint32_t transmit_errors;
    uint32_t stop_retries;
    uint32_t link_loss_count;
} RobotUartLinkDiagnostics_t;

/*
 * Starts the safe V1 link client on UART2. This milestone intentionally has
 * no API for non-zero motor output: it only sends STOP, HEARTBEAT, and
 * STATUS_REQUEST frames until BLE control is integrated in a later step.
 */
esp_err_t RobotUartLink_Start(void);

bool RobotUartLink_IsOnline(void);

/*
 * Requests a new fail-safe STOP handshake from the link task. The call is
 * asynchronous and never writes UART from the caller's context.
 */
esp_err_t RobotUartLink_RequestStop(void);

/* Returns false when no fresh (<=500 ms) STATUS frame is available. */
bool RobotUartLink_GetStatus(RobotProtocolStatus_t *status,
                             uint32_t *age_ms);

void RobotUartLink_GetDiagnostics(RobotUartLinkDiagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif

#endif /* ROBOT_UART_LINK_H */
