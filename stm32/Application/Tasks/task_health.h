#ifndef TASK_HEALTH_H
#define TASK_HEALTH_H

#include <stdint.h>

#define TASK_HEALTH_COUNT 7U

typedef struct {
  uint32_t started_at_ms;
  uint32_t last_progress_ms[TASK_HEALTH_COUNT];
  uint32_t last_heartbeat[TASK_HEALTH_COUNT];
  uint8_t fault_mask;
} TaskHealth_t;

void TaskHealth_Init(TaskHealth_t *health, uint32_t now_ms);
uint8_t TaskHealth_Poll(TaskHealth_t *health,
                        const uint32_t heartbeat[TASK_HEALTH_COUNT],
                        uint32_t now_ms);

#endif
