#include "robot_link_safety.h"
void RobotLinkSafety_Start(RobotLinkSafety_t *s, uint16_t sequence)
{
    *s = (RobotLinkSafety_t){ .pending_sequence = sequence };
}
bool RobotLinkSafety_Acknowledge(RobotLinkSafety_t *s, const RobotProtocolAck_t *ack, uint32_t now)
{
    if (!s->acknowledged && ack != NULL && ack->sequence == s->pending_sequence &&
        ack->request_type == ROBOT_PROTOCOL_TYPE_STOP && ack->result == ROBOT_PROTOCOL_ACK_OK) {
        s->acknowledged = true;
        s->acknowledged_at_ms = now;
        return true;
    }
    return false;
}
bool RobotLinkSafety_Update(RobotLinkSafety_t *s, const RobotProtocolStatus_t *status,
                            uint32_t received_at, uint32_t now)
{
    const bool was_online = s->online;
    /* Strictly later: a pre-ACK status in the same tick cannot prove STOP executed. */
    s->online = s->acknowledged && status != NULL &&
        (uint32_t)(now - received_at) <= ROBOT_UART_STATUS_FRESH_MS &&
        (was_online || (int32_t)(received_at - s->acknowledged_at_ms) > 0) &&
        status->applied_left_pwm == 0 && status->applied_right_pwm == 0;
    return (was_online && !s->online) ||
        (s->acknowledged && !s->online &&
         (uint32_t)(now - s->acknowledged_at_ms) > ROBOT_UART_RESPONSE_TIMEOUT_MS);
}
