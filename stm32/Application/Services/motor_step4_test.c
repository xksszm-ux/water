#include "motor_step4_test.h"

#include <stdbool.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "control_arbiter.h"

#if MOTOR_STEP4_TEST_ENABLED
static void SubmitTestOutput(int16_t left_permille, int16_t right_permille)
{
  static uint16_t sequence;
  const uint32_t now = osKernelGetTickCount();
  const MotorCommand_t command = {
      .left_output_permille = left_permille,
      .right_output_permille = right_permille,
      .mode = ROBOT_MODE_BLE,
      .enable = (left_permille != 0) || (right_permille != 0),
      .issued_at_ms = now,
  };
  (void)ControlArbiter_SubmitTest(sequence++, &command, now);
}
#endif

void MotorStep4Test_Process(void)
{
#if MOTOR_STEP4_TEST_ENABLED
  static bool started;
  static bool finished;
  static uint32_t started_at_ms;
  const uint32_t now = osKernelGetTickCount();

  if (finished) return;
  if (!started) {
    started = true;
    started_at_ms = now;
  }

  const uint32_t elapsed = now - started_at_ms;
  if (elapsed < 1000U) {
    SubmitTestOutput(100, 0);
  } else if (elapsed < 1500U) {
    SubmitTestOutput(0, 0);
  } else if (elapsed < 2500U) {
    SubmitTestOutput(-100, 0);
  } else if (elapsed < 3000U) {
    SubmitTestOutput(0, 0);
  } else if (elapsed < 4000U) {
    SubmitTestOutput(0, 100);
  } else if (elapsed < 4500U) {
    SubmitTestOutput(0, 0);
  } else if (elapsed < 5500U) {
    SubmitTestOutput(0, -100);
  } else if (elapsed < 6000U) {
    SubmitTestOutput(0, 0);
  } else if (elapsed < 7000U) {
    SubmitTestOutput(100, 100);
  } else {
    SubmitTestOutput(0, 0);
    finished = true;
  }
#endif
}
