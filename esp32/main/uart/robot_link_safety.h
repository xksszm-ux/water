#pragma once
#include "robot_protocol.h"
#define ROBOT_UART_STATUS_FRESH_MS 500U
#define ROBOT_UART_RESPONSE_TIMEOUT_MS 1200U
/* Pure policy; queue ownership and UART I/O stay in robot_uart_link.c. */
typedef struct {
    uint16_t pending_sequence;
    bool acknowledged;
    bool online;
    uint32_t acknowledged_at_ms;
} RobotLinkSafety_t;
void RobotLinkSafety_Start(RobotLinkSafety_t *state, uint16_t sequence);
bool RobotLinkSafety_Acknowledge(RobotLinkSafety_t *state, const RobotProtocolAck_t *ack, uint32_t now);
/* true requests a new STOP handshake. This policy deliberately remains STOP-only. */
bool RobotLinkSafety_Update(RobotLinkSafety_t *state, const RobotProtocolStatus_t *status,
                            uint32_t received_at, uint32_t now);
