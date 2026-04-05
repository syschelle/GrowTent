// task_CheckShellyStatus.h

#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "globals.h"
#include <cstdarg>

extern Preferences preferences;

// Forward declaration of ShellySettings (defined in globals.h)
struct PollSlot {
  uint32_t nextAllowedMs = 0;
  uint8_t failStreak = 0;
};

// Context for a Shelly poller, used to avoid code duplication across multiple Shelly devices.
struct ShellyPollCtx {
  ShellyDevice* dev; // settings.shelly.xxx
  ShellyValues* runtimeVal; // shelly.xxx.values
  ShellyValues* lastGood;
  bool* haveGood;
  PollSlot* slot;
  const char* name;
};

// Returns true if poll succeeded and out is updated. On failure, updates backoff slot and logs if needed. Does not modify out.
static bool pollShellyWithBackoff(ShellyDevice& dev, PollSlot& slot, ShellyValues& out) {
  const uint32_t now = millis();

  if ((int32_t)(now - slot.nextAllowedMs) < 0) {
    return false; // still in backoff window
  }

  // if no IP configured, skip immediately but reset backoff (e.g. for unused slots or when IP is not yet set)
  if (dev.ip.length() == 0) {
    slot.failStreak = 0;
    slot.nextAllowedMs = now + 10000UL;
    return false;
  }

  // Try to poll the Shelly device
  ShellyValues v = getShellyValues(dev, 0);
  if (v.ok) {
    slot.failStreak = 0;
    slot.nextAllowedMs = now;
    out = v;
    return true;
  }

  // Poll failed - apply backoff
  if (slot.failStreak < 10) slot.failStreak++;
  uint32_t backoffMs = 2000UL << (slot.failStreak - 1); // 2s,4s,8s...
  if (backoffMs > 60000UL) backoffMs = 60000UL; // max 60s
  slot.nextAllowedMs = now + backoffMs;

  logPrint(String("[SHELLY][BACKOFF] ") + dev.ip +
           " fail=" + String(slot.failStreak) +
           " next=" + String(backoffMs) + "ms");
  return false;
}

// Task: Check Shelly Status
void taskShellyStatus(void *parameter){
  static UBaseType_t minFree = UINT32_MAX;

  static ShellyValues lastGoodMain, lastGoodLight, lastGoodHum, lastGoodHeater, lastGoodFan, lastGoodExhaust;
  static bool haveMain = false, haveLight = false, haveHum = false, haveHeater = false, haveFan = false, haveExhaust = false;
  static PollSlot slotMain, slotLight, slotHum, slotHeater, slotFan, slotExhaust;

  ShellyPollCtx pollers[] = {
    { &settings.shelly.main, &shelly.main.values, &lastGoodMain, &haveMain, &slotMain, "main" },
    { &settings.shelly.light, &shelly.light.values, &lastGoodLight, &haveLight, &slotLight, "light" },
    { &settings.shelly.humidifier, &shelly.humidifier.values, &lastGoodHum, &haveHum, &slotHum, "humidifier" },
    { &settings.shelly.heater, &shelly.heater.values, &lastGoodHeater, &haveHeater, &slotHeater, "heater" },
    { &settings.shelly.fan, &shelly.fan.values, &lastGoodFan, &haveFan, &slotFan, "fan" },
    { &settings.shelly.exhaust, &shelly.exhaust.values, &lastGoodExhaust, &haveExhaust, &slotExhaust, "exhaust" }
  };

  for (;;) {
    UBaseType_t freeWords = uxTaskGetStackHighWaterMark(NULL);
    if (freeWords < minFree) minFree = freeWords;

    static uint32_t lastLogMs = 0;
    const uint32_t logIntervalMs = debugLog ? 5000UL : 60000UL;
    if (millis() - lastLogMs > logIntervalMs) {
      lastLogMs = millis();
      logPrint("[TASK][CheckShellyStatus] free=" + String(freeWords) +
               " words (" + String(freeWords * sizeof(StackType_t)) +
               " bytes), min=" + String(minFree) + " words");
    }

    // Poll each Shelly with backoff. On failure, runtimeVal is not updated but lastGood is kept for a potential fallback.
    for (auto &p : pollers) {
      ShellyValues nowVal;
      if (pollShellyWithBackoff(*p.dev, *p.slot, nowVal)) {
        *p.runtimeVal = nowVal;
        *p.lastGood = nowVal;
        *p.haveGood = true;
      } else if (*p.haveGood) {
        *p.runtimeVal = *p.lastGood; // keep last good result
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}