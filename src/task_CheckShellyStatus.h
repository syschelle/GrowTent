#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "globals.h"
#include <cstdarg>

extern Preferences preferences;
extern int amountOfWater;

void taskShellyStatus(void *parameter){
  static UBaseType_t minFree = UINT32_MAX;

  struct PollState {
    ShellyValues lastGood;
    bool haveGood = false;
    uint32_t retryAfterMs = 0;
  };

  static PollState mainState;
  static PollState lightState;
  static PollState humidifierState;
  static PollState heaterState;
  static PollState fanState;
  static PollState exhaustState;

  constexpr uint32_t RETRY_BACKOFF_MS = 30000UL;

  auto pollDevice = [&](ShellyDevice& configured, ShellyDevice& runtime, PollState& state) {
    // An empty IP means "not configured". Never poll it and never emit an error.
    if (configured.ip.length() == 0) {
      runtime.values = ShellyValues{};
      state.haveGood = false;
      state.retryAfterMs = 0;
      return;
    }

    const uint32_t now = millis();
    if (state.retryAfterMs != 0 && (int32_t)(now - state.retryAfterMs) < 0) {
      return;
    }

    ShellyValues current = getShellyValues(configured, 0);
    if (current.ok) {
      runtime.values = current;
      state.lastGood = current;
      state.haveGood = true;
      state.retryAfterMs = 0;
      return;
    }

    // Keep the last valid reading on transient failures. Retry later instead of
    // running the same unreachable device into an HTTP timeout every 10 seconds.
    if (state.haveGood) {
      runtime.values = state.lastGood;
    } else {
      runtime.values = ShellyValues{};
    }
    state.retryAfterMs = now + RETRY_BACKOFF_MS;
  };

  for (;;) {
    UBaseType_t freeWords = uxTaskGetStackHighWaterMark(NULL);
    if (freeWords < minFree) {
      minFree = freeWords;
    }

    static uint32_t lastLogMs = 0;
    const uint32_t logIntervalMs = debugLog ? 5000UL : 60000UL;

    if (millis() - lastLogMs > logIntervalMs) {
      lastLogMs = millis();

      char buf[96];
      snprintf(
        buf,
        sizeof(buf),
        "[TASK][CheckShellyStatus] free=%u words (%u bytes), min=%u words",
        freeWords,
        freeWords * sizeof(StackType_t),
        minFree
      );
      logPrint(String(buf));
    }

    // Poll exactly one Shelly per step instead of all configured devices back-to-back.
    // This keeps an unreachable device from creating a multi-second burst of
    // consecutive network timeouts that can make the web UI feel unresponsive.
    static uint8_t pollIndex = 0;
    switch (pollIndex) {
      case 0: pollDevice(settings.shelly.main,       shelly.main,       mainState); break;
      case 1: pollDevice(settings.shelly.light,      shelly.light,      lightState); break;
      case 2: pollDevice(settings.shelly.humidifier, shelly.humidifier, humidifierState); break;
      case 3: pollDevice(settings.shelly.heater,     shelly.heater,     heaterState); break;
      case 4: pollDevice(settings.shelly.fan,        shelly.fan,        fanState); break;
      default: pollDevice(settings.shelly.exhaust,   shelly.exhaust,    exhaustState); break;
    }
    pollIndex = (pollIndex + 1) % 6;

    // Six steps at 1.5 s keep the normal per-device refresh close to the
    // previous ~10 s cadence while avoiding a burst of six HTTP operations.
    vTaskDelay(pdMS_TO_TICKS(1500));
  }
}
