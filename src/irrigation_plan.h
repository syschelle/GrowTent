#pragma once
#include <Arduino.h>
// Implemented in the main translation unit. No NVS/network writes from watering hooks.
bool irrigationPlanEnabled();
bool irrigationPlanOwnsCycle();
void irrigationPlanCycleFinished(bool success);
bool irrigationPlanRefreshShelly(bool force);
void irrigationPlanInit();
void irrigationPlanTick();

bool irrigationPlanFactoryReset();

String irrigationPlanStatusField(const char* key);
