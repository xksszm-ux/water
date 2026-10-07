#include "task_health.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int TaskHealthChecks_Run(void)
{
  TaskHealth_t health;
  uint32_t heartbeat[TASK_HEALTH_COUNT] = {1U, 1U, 1U, 1U, 1U, 1U, 1U};
  TaskHealth_Init(&health, 0U);
  CHECK(TaskHealth_Poll(&health, heartbeat, 2999U) == 0U);
  CHECK(TaskHealth_Poll(&health, heartbeat, 3099U) == 0U);
  CHECK(TaskHealth_Poll(&health, heartbeat, 3100U) == (1U << 1));
  ++heartbeat[1];
  CHECK(TaskHealth_Poll(&health, heartbeat, 3101U) == (1U << 1));
  CHECK((TaskHealth_Poll(&health, heartbeat, 5000U) & (1U << 2)) != 0U);
  CHECK((TaskHealth_Poll(&health, heartbeat, 100000U) & (1U << 6)) == 0U);

  /* A slow startup gets the full grace, including a never-started task. */
  TaskHealth_Init(&health, 100U);
  heartbeat[0] = 0U;
  CHECK(TaskHealth_Poll(&health, heartbeat, 3100U) == 0U);
  CHECK((TaskHealth_Poll(&health, heartbeat, 3101U) & (1U << 0)) != 0U);
  heartbeat[0] = 1U;
  CHECK((TaskHealth_Poll(&health, heartbeat, 3102U) & (1U << 0)) != 0U);

  /* Both tick rollover and heartbeat-counter rollover are normal progress. */
  TaskHealth_Init(&health, UINT32_MAX - 3100U);
  heartbeat[1] = UINT32_MAX;
  CHECK(TaskHealth_Poll(&health, heartbeat, UINT32_MAX - 100U) == 0U);
  heartbeat[1] = 0U;
  CHECK(TaskHealth_Poll(&health, heartbeat, 0U) == 0U);
  CHECK((TaskHealth_Poll(&health, heartbeat, 101U) & (1U << 1)) != 0U);
  return 0;
}
