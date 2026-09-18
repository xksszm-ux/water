#include "app_tasks.h"
#include "i2c.h"
#include "oled.h"
#include "robot_state.h"
#include "task_entries.h"
#include "cmsis_os2.h"

static void LineReset(char line[22])
{
  for (uint32_t index = 0U; index < 21U; ++index) line[index] = '\0';
}

static void LineAppend(char line[22], uint8_t *position, const char *text)
{
  while ((*text != '\0') && (*position < 21U)) line[(*position)++] = *text++;
  line[*position] = '\0';
}

static void LineAppendFixed(char line[22], uint8_t *position, uint32_t value,
                            uint8_t digits)
{
  uint32_t divisor = 1U;
  for (uint8_t index = 1U; index < digits; ++index) divisor *= 10U;
  while ((digits-- > 0U) && (*position < 21U)) {
    line[(*position)++] = (char)('0' + ((value / divisor) % 10U));
    if (divisor > 1U) divisor /= 10U;
  }
  line[*position] = '\0';
}

static void DrawStatus(const RobotStatus_t *status)
{
  char line[22];
  uint8_t position;
  uint32_t magnitude;
  const char hex[] = "0123456789ABCDEF";

  OLED_Clear();
  OLED_SetCursor(0U, 0U);
  OLED_WriteString("ROBOT STATUS");

  LineReset(line); position = 0U;
  LineAppend(line, &position, "BAT:");
  if (!status->battery_valid) {
    LineAppend(line, &position, "--.--V");
  } else {
    LineAppendFixed(line, &position, status->battery_mv / 1000U, 1U);
    LineAppend(line, &position, ".");
    LineAppendFixed(line, &position, (status->battery_mv % 1000U) / 10U, 2U);
    LineAppend(line, &position, "V");
  }
  OLED_SetCursor(0U, 1U); OLED_WriteString(line);

  LineReset(line); position = 0U;
  LineAppend(line, &position, "L PWM:");
  LineAppend(line, &position, status->left_output_permille < 0 ? "-" : "+");
  magnitude = status->left_output_permille < 0 ?
              (uint32_t)(-status->left_output_permille) :
              (uint32_t)status->left_output_permille;
  LineAppendFixed(line, &position, magnitude / 10U, 3U);
  LineAppend(line, &position, "%");
  OLED_SetCursor(0U, 2U); OLED_WriteString(line);

  LineReset(line); position = 0U;
  LineAppend(line, &position, "R PWM:");
  LineAppend(line, &position, status->right_output_permille < 0 ? "-" : "+");
  magnitude = status->right_output_permille < 0 ?
              (uint32_t)(-status->right_output_permille) :
              (uint32_t)status->right_output_permille;
  LineAppendFixed(line, &position, magnitude / 10U, 3U);
  LineAppend(line, &position, "%");
  OLED_SetCursor(0U, 3U); OLED_WriteString(line);

  LineReset(line); position = 0U;
  LineAppend(line, &position, "DIST:");
  if ((status->sensor_valid_mask & SENSOR_VALID_DISTANCE) != 0U) {
    LineAppendFixed(line, &position, status->distance_mm / 10U, 3U);
  } else {
    LineAppend(line, &position, "---");
  }
  LineAppend(line, &position, "CM");
  OLED_SetCursor(0U, 4U); OLED_WriteString(line);

  OLED_SetCursor(0U, 5U);
  OLED_WriteString(status->mode == ROBOT_MODE_AUTO ? "MODE:AUTO" : "MODE:BLE");

  LineReset(line); position = 0U;
  LineAppend(line, &position, "ERR:");
  line[position++] = hex[(status->error_status >> 4U) & 0x0FU];
  line[position++] = hex[status->error_status & 0x0FU];
  line[position] = '\0';
  OLED_SetCursor(0U, 6U); OLED_WriteString(line);
}

void DisplayTask_Entry(void *argument)
{
  (void)argument;
  RobotStatus_t snapshot;
  bool display_ready = false;
  bool refresh_ok = false;
  uint32_t last_init_attempt = osKernelGetTickCount() - 1000U;
  for (;;) {
    AppTasks_Heartbeat(APP_TASK_DISPLAY);
    const uint32_t now = osKernelGetTickCount();
    if (!display_ready && ((uint32_t)(now - last_init_attempt) >= 1000U)) {
      last_init_attempt = now;
      if (osMutexAcquire(g_i2c1_mutex, 100U) == osOK) {
        display_ready = OLED_Init(&hi2c1) == HAL_OK;
        (void)osMutexRelease(g_i2c1_mutex);
      }
    }

    if (display_ready) {
      refresh_ok = true;
      RobotState_InvalidateSensorIfStale(now);
      RobotState_GetSnapshot(&snapshot);
      DrawStatus(&snapshot);
      for (uint8_t page = 0U; page < OLED_PAGES; ++page) {
        if (osMutexAcquire(g_i2c1_mutex, 50U) != osOK) {
          refresh_ok = false;
          continue;
        }
        const HAL_StatusTypeDef result = OLED_UpdatePage(page);
        (void)osMutexRelease(g_i2c1_mutex);
        if (result != HAL_OK) {
          display_ready = false;
          refresh_ok = false;
          break;
        }
        osThreadYield();
      }
    }
    RobotState_SetError(ROBOT_ERROR_DISPLAY, !display_ready || !refresh_ok,
                        osKernelGetTickCount());
    (void)osDelay(500U);
  }
}
