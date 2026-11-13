#pragma once
#include <Arduino.h>
// Network work posts commands; only the Arduino loop mutates the fan GPIO.
bool humidifierFanOwnsRelay(int index);
uint64_t humidifierFanCommandGeneration(const String& host, uint8_t gen);
void humidifierFanCommandResult(uint64_t token, bool on, bool success, uint32_t startedMs);
void humidifierFanRefreshConfig();
void humidifierFanInit();
void humidifierFanTick();
