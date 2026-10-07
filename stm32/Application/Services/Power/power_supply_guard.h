#ifndef POWER_SUPPLY_GUARD_H
#define POWER_SUPPLY_GUARD_H

#include <stdbool.h>

extern volatile bool g_power_supply_fault_latched;

bool PowerSupplyGuard_Init(void);
bool PowerSupplyGuard_IsSafe(void);

#endif
