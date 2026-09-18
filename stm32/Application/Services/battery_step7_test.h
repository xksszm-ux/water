#ifndef BATTERY_STEP7_TEST_H
#define BATTERY_STEP7_TEST_H

#include <stdbool.h>

extern volatile bool g_battery_step7_self_test_passed;

bool BatteryStep7Test_Run(void);

#endif
