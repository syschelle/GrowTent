//check ever 10 seconds the current vpd with target vpd, if current vpd higer than taget vpd then power on the humidifyer shelly
//after 11 second the shelly for the humidifyer turns automaticly off. Configure that in the Webinterface oft the shelly (auto off).
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <Adafruit_BME280.h>
#include "globals.h"
#include <cstdarg>

extern Preferences preferences;
extern bool bmeAvailable;
extern Adafruit_BME280 bme;

void taskCheckBMESensor(void *parameter) {
  static UBaseType_t minFree = UINT32_MAX;

  // Read sensors every 10 seconds
  const uint32_t sensorReadIntervalMs = 10UL * 1000UL;

  // Store history only every 10 minutes
  const uint32_t historyStoreIntervalMs = HISTORY_INTERVAL_SEC * 1000UL;

  static uint32_t lastSensorReadMs   = 0;
  static uint32_t lastHistoryStoreMs = 0;
  static bool firstHistoryPoint = true;
  static uint32_t lastLogMs = 0;
  static uint32_t lastTankPingMs = 0;
  const uint32_t tankPingIntervalMs = 2UL * 60UL * 60UL * 1000UL; // 2h

  for (;;) {
    // --- stack watermark logging (debug) ---
    UBaseType_t freeWords = uxTaskGetStackHighWaterMark(NULL);

    if (freeWords < minFree) {
      minFree = freeWords;
    }

    const uint32_t logIntervalMs = debugLog ? 5000UL : 60000UL;

    if (millis() - lastLogMs > logIntervalMs) {
      lastLogMs = millis();

      char buf[96];

      snprintf(
        buf,
        sizeof(buf),
        "[TASK][Check_Sensor] free=%u words (%u bytes), min=%u words",
        freeWords,
        freeWords * 4,
        minFree
      );

      logPrint(String(buf));
    }

    const uint32_t nowMs = millis();

    // --- read sensors every 10 seconds ---
    if ((nowMs - lastSensorReadMs) >= sensorReadIntervalMs) {
      lastSensorReadMs = nowMs;

      // Read temperature, humidity and VPD
      readSensorData();
      controlHeaterByTemperature();
      controlHumidifierByVPD();
      applyRelaySchedules();

      // Tank auto-ping (8x only):
      // - immediate ping after boot / missing initial value
      // - then every 2 hours
      if (activeRelayCount == 8) {
        float cm = pingTankLevel(TRIG, ECHO);
        if (cm >= 0.0f) {
          tankLevelCm = cm;
          if (irrigation.tank.max != 0 && irrigation.tank.max != irrigation.tank.min) {
            tankLevel = calculateTankPercent(tankLevelCm, irrigation.tank.min, irrigation.tank.max);
          }
        }
      }
    }

    // task delay 10 seconds
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}