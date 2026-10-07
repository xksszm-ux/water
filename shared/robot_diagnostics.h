#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Additive V1 message; STATUS and BLE formats remain unchanged. */
#define ROBOT_DIAGNOSTICS_TYPE 0x82U
#define ROBOT_DIAGNOSTICS_SIZE 32U
#define ROBOT_DIAGNOSTICS_REVISION 2U
#define ROBOT_DIAGNOSTICS_TASK_COUNT 7U
typedef struct {
    uint16_t free_heap, minimum_heap;
    /* Sensor, Motor, Battery, CAN, Communication, Display, Storage; bytes. */
    uint16_t stack_free[ROBOT_DIAGNOSTICS_TASK_COUNT];
    /* Latched software liveness faults; bit order matches stack_free. */
    uint8_t health_fault_mask;
    uint32_t receive_errors, response_drops, sensor_queue_drops;
} RobotDiagnostics_t;
static inline void RobotDiagnostics_Write(uint8_t *p, uint32_t value, unsigned n)
{ for (unsigned i=0; i<n; ++i) p[i]=(uint8_t)(value >> (8U*i)); }
static inline uint32_t RobotDiagnostics_Read(const uint8_t *p, unsigned n)
{ uint32_t v=0; for (unsigned i=0; i<n; ++i) v|=(uint32_t)p[i] << (8U*i); return v; }
static inline bool RobotDiagnostics_Encode(const RobotDiagnostics_t *d, uint8_t *p, size_t capacity)
{
    if (!d || !p || capacity < ROBOT_DIAGNOSTICS_SIZE || (d->health_fault_mask & 0x80U)) return false;
    p[0]=ROBOT_DIAGNOSTICS_REVISION;
    RobotDiagnostics_Write(p+1,d->free_heap,2); RobotDiagnostics_Write(p+3,d->minimum_heap,2);
    for (unsigned i=0; i<ROBOT_DIAGNOSTICS_TASK_COUNT; ++i) RobotDiagnostics_Write(p+5+2*i,d->stack_free[i],2);
    p[19]=d->health_fault_mask;
    RobotDiagnostics_Write(p+20,d->receive_errors,4);
    RobotDiagnostics_Write(p+24,d->response_drops,4);
    RobotDiagnostics_Write(p+28,d->sensor_queue_drops,4);
    return true;
}
static inline bool RobotDiagnostics_Decode(const uint8_t *p, size_t length, RobotDiagnostics_t *d)
{
    if (!p || !d || length != ROBOT_DIAGNOSTICS_SIZE || p[0] != ROBOT_DIAGNOSTICS_REVISION || (p[19] & 0x80U)) return false;
    d->free_heap=(uint16_t)RobotDiagnostics_Read(p+1,2);
    d->minimum_heap=(uint16_t)RobotDiagnostics_Read(p+3,2);
    for (unsigned i=0; i<ROBOT_DIAGNOSTICS_TASK_COUNT; ++i) d->stack_free[i]=(uint16_t)RobotDiagnostics_Read(p+5+2*i,2);
    d->health_fault_mask=p[19];
    d->receive_errors=RobotDiagnostics_Read(p+20,4);
    d->response_drops=RobotDiagnostics_Read(p+24,4);
    d->sensor_queue_drops=RobotDiagnostics_Read(p+28,4);
    return true;
}
