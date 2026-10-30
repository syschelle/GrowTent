#pragma once
#include <Arduino.h>
#include "globals.h"
#include "function.h"

// Wait in short slices so a dashboard stop request can interrupt an active
// pump pulse or the pause between irrigation runs promptly.
static bool irrigationDelayInterruptible(uint32_t durationMs) {
  const uint32_t startedAt = millis();

  while ((uint32_t)(millis() - startedAt) < durationMs) {
    if (irrigationStopRequested || irrigation.irrigationRuns <= 0) {
      return false;
    }

    const uint32_t elapsed = (uint32_t)(millis() - startedAt);
    const uint32_t remaining = (elapsed < durationMs) ? (durationMs - elapsed) : 0;
    const uint32_t sliceMs = (remaining > 100U) ? 100U : remaining;

    if (sliceMs == 0U) break;
    vTaskDelay(pdMS_TO_TICKS(sliceMs));
  }

  return !(irrigationStopRequested || irrigation.irrigationRuns <= 0);
}

static void acknowledgeIrrigationStop() {
  stopAllIrrigationPumps();
  irrigationStartNotifyPending = false;
  irrigation.irrigationRuns = 0;
  irrigation.wTimeLeft = "00:00";
  irrigationStopRequested = false;
  logPrint("[IRRIGATION] Stop acknowledged by watering task.");
}

void taskWatering(void *parameter) {
  static UBaseType_t minFreeBytes = UINT32_MAX;

  for (;;) {
    UBaseType_t freeBytes = uxTaskGetStackHighWaterMark(NULL);

    if (freeBytes < minFreeBytes) {
      minFreeBytes = freeBytes;
    }

    static uint32_t last = 0;
    if (millis() - last > 5000) {
      last = millis();

      char buf[96];
      snprintf(
        buf,
        sizeof(buf),
        "[TASK][Watering] free=%u bytes, min=%u bytes",
        freeBytes,
        minFreeBytes
      );

      logPrint(String(buf));
    }

    if (irrigation.irrigationRuns <= 0) {
      irrigation.wTimeLeft = "00:00";

      if (irrigationStopRequested) {
        acknowledgeIrrigationStop();
      }

      // Short idle sleep keeps start/stop response snappy without adding a
      // busy loop or another task.
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (activeRelayCount < 8) {
      logPrint("[IRRIGATION] Aborted: irrigation requires 8 relays.");
      irrigationStartNotifyPending = false;
      irrigation.irrigationRuns = 0;
      irrigation.wTimeLeft = "00:00";
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    const size_t activePumpCount = enabledIrrigationPumpCount();

    if (activePumpCount == 0) {
      logPrint("[IRRIGATION] Aborted: all irrigation pumps were disabled.");
      irrigationStartNotifyPending = false;
      irrigation.irrigationRuns = 0;
      irrigation.wTimeLeft = "00:00";
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    // Async start notifications are delivered here before any irrigation pump
    // can be energized. Network notification latency therefore cannot block the
    // main loop's Flood-event processing while a pump is already running.
    if (irrigationStartNotifyPending) {
      const bool startedViaApi = irrigationStartNotifyViaApi;
      irrigationStartNotifyPending = false;

      if (language == "de") {
        if (startedViaApi) {
          sendPushover(
            boxName + "Bewässerung über API gestartet. Dauer: " + calculateEndtimeWatering(),
            boxName + "Bewässerung über API gestartet."
          );
        } else {
          sendPushover(
            boxName + "Bewässerung startet. Dauer: " + calculateEndtimeWatering(),
            boxName + "Bewässerung startet."
          );
        }
        sendGotify(
          boxName + "Bewässerung startet. Dauer: " + calculateEndtimeWatering(),
          boxName + "Bewässerung startet."
        );
      } else {
        if (startedViaApi) {
          sendPushover(
            boxName + "Irrigation started via API. Duration: " + calculateEndtimeWatering(),
            boxName + "Irrigation started via API."
          );
        } else {
          sendPushover(
            boxName + "Irrigation started. Duration: " + calculateEndtimeWatering(),
            boxName + "Irrigation started."
          );
        }
        sendGotify(
          boxName + "Irrigation started. Duration: " + calculateEndtimeWatering(),
          boxName + "Irrigation started."
        );
      }

      // A Flood alarm or dashboard/API stop may have arrived while notification
      // network I/O was in progress. Re-check before the first pump can start.
      if (irrigationStopRequested || irrigation.irrigationRuns <= 0) {
        acknowledgeIrrigationStop();
        continue;
      }
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

      if (irrigationStopRequested || irrigation.irrigationRuns <= 0) {
        break;
      }

      setRelay(relayIndex, true);

      const bool pulseCompleted = irrigationDelayInterruptible(
        secondsToMilliseconds(irrigation.timePerTask)
      );

      setRelay(relayIndex, false);

      if (!pulseCompleted) {
        break;
      }

      ++pumpsProcessed;

      // Keep the existing short gap, but only between pumps that will actually run.
      if (pumpsProcessed < activePumpCount) {
        if (!irrigationDelayInterruptible(250)) {
          break;
        }
      }
    }

    if (irrigationStopRequested || irrigation.irrigationRuns <= 0) {
      acknowledgeIrrigationStop();
      continue;
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

    if (!irrigationDelayInterruptible(
          minutesToMilliseconds(irrigation.betweenTasks)
        )) {
      acknowledgeIrrigationStop();
      continue;
    }
  }
}