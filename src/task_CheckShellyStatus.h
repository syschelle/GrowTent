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

    if (freeWords < minFree) minFree = freeWords;
      static uint32_t last = 0;
      if (millis() - last > 5000) {
        last = millis();

        char buf[96];
        snprintf(
        buf,
        sizeof(buf),
        "[TASK][CheckShellyStatus] free=%u words (%u bytes), min=%u words",
        freeWords,
        freeWords * 4,
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