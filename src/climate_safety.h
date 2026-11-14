#pragma once
#include <stdint.h>
constexpr uint32_t CLIMATE_MAX_AGE_MS = 30000UL;
void climateRecordSample(bool valid, uint32_t nowMs);
bool climateReadingsFresh();
uint32_t climateReadingAgeMs(); // UINT32_MAX until the first valid sample
const char* climateReadingStatus();
