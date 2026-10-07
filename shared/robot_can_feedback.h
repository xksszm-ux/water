#pragma once
#include "robot_diagnostics.h"
#define ROBOT_CAN_FEEDBACK_TYPE 0x83U
#define ROBOT_CAN_FEEDBACK_SIZE 16U
#define ROBOT_CAN_FEEDBACK_REVISION 1U
#define ROBOT_CAN_FEEDBACK_OBSERVED 1U
#define ROBOT_CAN_FEEDBACK_SEQUENCE_VALID 2U
/* ESP32/BLE only: a fresh report is available (including no CAN event yet). */
#define ROBOT_CAN_FEEDBACK_AVAILABLE 4U
#define ROBOT_CAN_FEEDBACK_FRESH_MS 3000U
/* Results 0..8 match ControlResult_t; 9 is an invalid CAN control packet. */
#define ROBOT_CAN_FEEDBACK_PROTOCOL_INVALID 9U
typedef struct {
    uint8_t flags, sequence, result;
    uint32_t rejected, replays, age_ms;
} RobotCanFeedback_t;
static inline bool RobotCanFeedback_IsValid(const RobotCanFeedback_t *d)
{
    return d && (d->flags & ~7U) == 0U &&
        (!(d->flags & ROBOT_CAN_FEEDBACK_SEQUENCE_VALID) || (d->flags & ROBOT_CAN_FEEDBACK_OBSERVED)) &&
        d->result <= ROBOT_CAN_FEEDBACK_PROTOCOL_INVALID &&
        ((d->flags & ROBOT_CAN_FEEDBACK_SEQUENCE_VALID) || d->sequence == 0U) &&
        ((d->flags & ROBOT_CAN_FEEDBACK_OBSERVED) ||
         (d->result == 0U && d->rejected == 0U && d->replays == 0U && d->age_ms == UINT32_MAX));
}
static inline bool RobotCanFeedback_Encode(const RobotCanFeedback_t *d, uint8_t *p, size_t n)
{
    if (!p || n < ROBOT_CAN_FEEDBACK_SIZE || !RobotCanFeedback_IsValid(d)) return false;
    p[0]=ROBOT_CAN_FEEDBACK_REVISION; p[1]=d->flags; p[2]=d->sequence; p[3]=d->result;
    RobotDiagnostics_Write(p+4,d->rejected,4); RobotDiagnostics_Write(p+8,d->replays,4);
    RobotDiagnostics_Write(p+12,d->age_ms,4);
    return true;
}
static inline bool RobotCanFeedback_Decode(const uint8_t *p, size_t n, RobotCanFeedback_t *d)
{
    if (!p || !d || n != ROBOT_CAN_FEEDBACK_SIZE || p[0] != ROBOT_CAN_FEEDBACK_REVISION) return false;
    RobotCanFeedback_t value={.flags=p[1],.sequence=p[2],.result=p[3],
        .rejected=RobotDiagnostics_Read(p+4,4),.replays=RobotDiagnostics_Read(p+8,4),
        .age_ms=RobotDiagnostics_Read(p+12,4)};
    if (!RobotCanFeedback_IsValid(&value)) return false;
    *d=value; return true;
}
/* Report transport freshness is independent of the age of the last CAN event.
   Offline/stale reports are unavailable; event age adds transit time, saturating. */
static inline bool RobotCanFeedback_Current(const RobotCanFeedback_t *saved,
    bool have, bool online, uint32_t received_at, uint32_t now, RobotCanFeedback_t *out)
{
    const uint32_t transit=now-received_at;
    if (!saved || !out || !have || !online || transit > ROBOT_CAN_FEEDBACK_FRESH_MS) return false;
    *out=*saved;
    out->flags |= ROBOT_CAN_FEEDBACK_AVAILABLE;
    if (out->flags & ROBOT_CAN_FEEDBACK_OBSERVED)
        out->age_ms = UINT32_MAX-out->age_ms < transit ? UINT32_MAX : out->age_ms+transit;
    return true;
}
