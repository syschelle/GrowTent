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
  const uint32_t historyStoreIntervalMs = 10UL * 60UL * 1000UL;

  static uint32_t lastSensorReadMs   = 0;
  static uint32_t lastHistoryStoreMs = 0;
  static bool firstHistoryPoint = true;

  for (;;) {
    // --- stack watermark logging (debug) ---
    UBaseType_t freeWords = uxTaskGetStackHighWaterMark(NULL);
    if (freeWords < minFree) minFree = freeWords;

    static uint32_t lastLogMs = 0;
    if (millis() - lastLogMs > 5000) {
      lastLogMs = millis();
      char buf[96];
      snprintf(buf, sizeof(buf),
               "[TASK][Check_Sensor] free=%u words (%u bytes), min=%u words",
               freeWords, freeWords * 4, minFree);
      logPrint(String(buf));
    }

    const uint32_t nowMs = millis();

    // --- read sensors every 10 seconds ---
    if ((nowMs - lastSensorReadMs) >= sensorReadIntervalMs) {
      lastSensorReadMs = nowMs;

      // Read temperature, humidity and VPD
      readSensorData();
    }

    // --- store history every 10 minutes (first point immediately after valid values) ---
    const bool timeForHistory = firstHistoryPoint || ((nowMs - lastHistoryStoreMs) >= historyStoreIntervalMs);

    if (timeForHistory) {
      // Store only if values are valid (avoid NaN points after reboot)
      if (!isnan(cur.temperatureC) &&
          !isnan(cur.humidityPct) &&
          !isnan(cur.vpdKpa)) {


          addReading(cur.temperatureC, cur.humidityPct, cur.vpdKpa);
          logPrint("[Task][HISTORY] Stored history point: "
               "T=" + String(cur.temperatureC, 1) + "C, "
               "H=" + String(cur.humidityPct, 1) + "%, "
               "VPD=" + String(cur.vpdKpa, 2) + "kPa");


        lastHistoryStoreMs = nowMs;
        firstHistoryPoint = false;
      }
    }

    // task delay 10 seconds
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}