#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "globals.h"
#include "function.h"
#include <cstdarg>

extern Preferences preferences;
extern int amountOfWater;

void taskWatering(void *parameter){
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

    if (irrigation.irrigationRuns > 0) {
        irrigation.wTimeLeft = calculateEndtimeWatering();

        setRelay(5, true); // Relay 6
        delay(secondsToMilliseconds(irrigation.timePerTask)); // Pump on for 10 seconds
        setRelay(5, false);
        setRelay(6, true);
        delay(secondsToMilliseconds(irrigation.timePerTask)); // Pump on for 10 seconds
        setRelay(6, false);
        setRelay(7, true);
        delay(secondsToMilliseconds(irrigation.timePerTask)); // Pump on for 10 seconds
        setRelay(7, false);
        irrigation.irrigationRuns = irrigation.irrigationRuns - 1;
        logPrint("[IRRIGATION] Remaining irrigation runs: " + String(irrigation.irrigationRuns));
        if (irrigation.irrigationRuns == 0) {
          if (language == "de") {
            sendPushover("Bewässerung abgeschlossen.", "Bewässerung abgeschlossen.");
          } else {
            sendPushover("Irrigation completed.", "Irrigation completed.");
          }
        }
    }

    // delay between checks
     if (irrigation.irrigationRuns > 0) {
      tankLevelCm = pingTankLevel(TRIG, ECHO);
      // Log remaining irrigation runs
      logPrint("[IRRIGATION] Remaining irrigation runs: " + String(irrigation.irrigationRuns));
      delay(minutesToMilliseconds(irrigation.betweenTasks)); // wait 5 minutes before next run
    } else {
      irrigation.wTimeLeft = "00:00";
      // task delay 10 seconds
      vTaskDelay(pdMS_TO_TICKS(10000));
    } 
  }
}