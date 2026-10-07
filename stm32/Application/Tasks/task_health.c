#include "task_health.h"

#define TASK_HEALTH_STARTUP_GRACE_MS 3000U

/* Sensor, Motor, Battery, CAN, Communication, Display, Storage.
 * Storage is observed through its heartbeat but excluded from time-based
 * faults: flash scanning and erase can legitimately block for many seconds. */
static const uint32_t task_deadline_ms[TASK_HEALTH_COUNT] = {
  1000U, 100U, 2000U, 1000U, 1000U, 5000U, 0U
};

void TaskHealth_Init(TaskHealth_t *health, uint32_t now_ms)
{
  health->started_at_ms = now_ms;
  health->fault_mask = 0U;
  for (uint32_t i = 0U; i < TASK_HEALTH_COUNT; ++i) {
    health->last_heartbeat[i] = 0U;
    health->last_progress_ms[i] = now_ms;
  }
}

uint8_t TaskHealth_Poll(TaskHealth_t *health,
                        const uint32_t heartbeat[TASK_HEALTH_COUNT],
                        uint32_t now_ms)
{
  for (uint32_t i = 0U; i < TASK_HEALTH_COUNT; ++i) {
    if (heartbeat[i] != health->last_heartbeat[i]) {
      health->last_heartbeat[i] = heartbeat[i];
      health->last_progress_ms[i] = now_ms;
    } else if (task_deadline_ms[i] != 0U &&
               (uint32_t)(now_ms - health->started_at_ms) >
                   TASK_HEALTH_STARTUP_GRACE_MS &&
               (uint32_t)(now_ms - health->last_progress_ms[i]) >
                   task_deadline_ms[i]) {
      health->fault_mask |= (uint8_t)(1U << i);
    }
  }
  return health->fault_mask;
}
