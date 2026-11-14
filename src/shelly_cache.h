#pragma once
#include <Arduino.h>
#include "freertos/semphr.h"
struct ShellyValues;
struct ShellyDevice;
constexpr uint32_t SHELLY_STATUS_MAX_AGE_MS = 60000UL;
extern SemaphoreHandle_t shellyCacheMutex;
struct ShellyCacheLock {
  bool held;
  explicit ShellyCacheLock(TickType_t wait=pdMS_TO_TICKS(25))
    :held(shellyCacheMutex && xSemaphoreTake(shellyCacheMutex,wait)==pdTRUE) {}
  ~ShellyCacheLock(){if(held)xSemaphoreGive(shellyCacheMutex);}
  ShellyCacheLock(const ShellyCacheLock&)=delete;
  ShellyCacheLock& operator=(const ShellyCacheLock&)=delete;
};
bool initShellyCache();
uint32_t shellyCacheEpoch();
void invalidateShellyCacheLocked(); // caller holds ShellyCacheLock
bool shellyValuesFresh(const ShellyValues& values, uint32_t nowMs);
ShellyValues shellyCachedValues(const ShellyDevice& device);
void expireShellyCache(); // main-loop watchdog, no HTTP
void noteShellySwitch(const String& host, bool on); // state hint, never renews telemetry freshness
