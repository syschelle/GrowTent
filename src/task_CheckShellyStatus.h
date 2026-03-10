#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "globals.h"
#include <cstdarg>

extern Preferences preferences;
extern int amountOfWater;

void taskShellyStatus(void *parameter){
  static UBaseType_t minFree = UINT32_MAX;

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

    shelly.main.values = getShellyValues(settings.shelly.main, 0);
    shelly.light.values = getShellyValues(settings.shelly.light, 0);

    // task delay 10 seconds
    vTaskDelay(pdMS_TO_TICKS(10000)); 
  }
}