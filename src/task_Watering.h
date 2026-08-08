#pragma once
#include <Arduino.h>
#include "globals.h"
#include "function.h"

void taskWatering(void *parameter) {
  static UBaseType_t minFree = UINT32_MAX;

  for (;;) {
    UBaseType_t freeWords = uxTaskGetStackHighWaterMark(NULL);

    if (freeWords < minFree) {
      minFree = freeWords;
    }

    static uint32_t last = 0;
    if (millis() - last > 5000) {
      last = millis();

      char buf[96];
      snprintf(
        buf,
        sizeof(buf),
        "[TASK][Watering] free=%u words (%u bytes), min=%u words",
        freeWords,
        freeWords * sizeof(StackType_t),
        minFree
      );

      logPrint(String(buf));
    }

    if (irrigation.irrigationRuns <= 0) {
      irrigation.wTimeLeft = "00:00";
      vTaskDelay(pdMS_TO_TICKS(10000));
      continue;
    }

    if (activeRelayCount < 8) {
      logPrint("[IRRIGATION] Aborted: irrigation requires 8 relays.");
      irrigation.irrigationRuns = 0;
      irrigation.wTimeLeft = "00:00";
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    const size_t activePumpCount = enabledIrrigationPumpCount();

    if (activePumpCount == 0) {
      logPrint("[IRRIGATION] Aborted: all irrigation pumps were disabled.");
      irrigation.irrigationRuns = 0;
      irrigation.wTimeLeft = "00:00";
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    irrigation.wTimeLeft = calculateEndtimeWatering();

    size_t pumpsProcessed = 0;

    // Run only the pumps that are enabled in the operating settings.
    // Pump-to-relay mapping is centralized in globals.h to avoid magic numbers here.
    for (size_t pump = 0; pump < IRRIGATION_PUMP_COUNT; ++pump) {
      if (!irrigation.pumpEnabled[pump]) {
        continue;
      }

      const int relayIndex = IRRIGATION_PUMP_RELAY_INDEX[pump];

      setRelay(relayIndex, true);
      vTaskDelay(pdMS_TO_TICKS(secondsToMilliseconds(irrigation.timePerTask)));
      setRelay(relayIndex, false);

      ++pumpsProcessed;

      // Keep the existing short gap, but only between pumps that will actually run.
      if (pumpsProcessed < activePumpCount) {
        vTaskDelay(pdMS_TO_TICKS(250));
      }
    }

    irrigation.irrigationRuns--;

    logPrint("[IRRIGATION] Remaining irrigation runs: " + String(irrigation.irrigationRuns));

    if (irrigation.irrigationRuns <= 0) {
      irrigation.wTimeLeft = "00:00";

      if (language == "de") {
        sendPushover(boxName + "Bewässerung abgeschlossen.", "Bewässerung abgeschlossen.");
        sendGotify(boxName + "Bewässerung abgeschlossen.", "Bewässerung abgeschlossen.");
      } else {
        sendPushover(boxName + "Irrigation completed.", "Irrigation completed.");
        sendGotify(boxName + "Irrigation completed.", "Irrigation completed.");
      }

      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    float level = pingTankLevel(TRIG, ECHO);
    if (level >= 0.0f) {
      tankLevelCm = level;
    }

    logPrint("[IRRIGATION] Remaining irrigation runs: " + String(irrigation.irrigationRuns));

    vTaskDelay(pdMS_TO_TICKS(minutesToMilliseconds(irrigation.betweenTasks)));
  }
}