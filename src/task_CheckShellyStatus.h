// task_CheckShellyStatus.h
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <IPAddress.h>
#include "globals.h"
#include <cstdarg>

extern Preferences preferences;

// Returns true only for non-empty valid IPv4 strings
static bool isValidShellyIp(const String& ip) {
  IPAddress tmp;
  return ip.length() > 0 && tmp.fromString(ip);
}

// Poll helper: skip invalid IPs, keep last good value on failures
static void pollShellyIfValid(
  ShellyDevice& dev,
  ShellyValues& runtimeValues,
  ShellyValues& lastGood,
  bool& haveGood
) {
  // Skip polling if IP is missing/invalid
  if (!isValidShellyIp(dev.ip)) {
    if (haveGood) runtimeValues = lastGood;
    return;
  }

  ShellyValues nowVal = getShellyValues(dev, 0);
  if (nowVal.ok) {
    runtimeValues = nowVal;
    lastGood = nowVal;
    haveGood = true;
  } else if (haveGood) {
    runtimeValues = lastGood;
  }
}

void taskShellyStatus(void *parameter){
  static UBaseType_t minFree = UINT32_MAX;

  static ShellyValues lastGoodMain;
  static ShellyValues lastGoodLight;
  static ShellyValues lastGoodHumidifier;
  static ShellyValues lastGoodHeater;
  static ShellyValues lastGoodFan;
  static ShellyValues lastGoodExhaust;

  static bool haveMainGood = false;
  static bool haveLightGood = false;
  static bool haveHumidifierGood = false;
  static bool haveHeaterGood = false;
  static bool haveFanGood = false;
  static bool haveExhaustGood = false;

  for (;;) {
    UBaseType_t freeWords = uxTaskGetStackHighWaterMark(NULL);
    if (freeWords < minFree) minFree = freeWords;

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

    // Poll all configured Shelly devices (invalid/missing IPs are skipped)
    pollShellyIfValid(settings.shelly.main, shelly.main.values, lastGoodMain, haveMainGood);
    pollShellyIfValid(settings.shelly.light, shelly.light.values, lastGoodLight, haveLightGood);
    pollShellyIfValid(settings.shelly.humidifier, shelly.humidifier.values, lastGoodHumidifier, haveHumidifierGood);
    pollShellyIfValid(settings.shelly.heater, shelly.heater.values, lastGoodHeater, haveHeaterGood);
    pollShellyIfValid(settings.shelly.fan, shelly.fan.values, lastGoodFan, haveFanGood);
    pollShellyIfValid(settings.shelly.exhaust, shelly.exhaust.values, lastGoodExhaust, haveExhaustGood);

    // task delay 10 seconds
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}
