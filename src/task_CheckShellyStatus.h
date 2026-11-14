#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "globals.h"
#include "ota.h"
#include <cstdarg>

extern Preferences preferences;
extern int amountOfWater;

void taskShellyStatus(void *parameter){
  static UBaseType_t minFreeBytes = UINT32_MAX;

  struct PollState { uint32_t epoch=0,retryAfterMs=0; bool backingOff=false; };

  static PollState mainState;
  static PollState lightState;
  static PollState humidifierState;
  static PollState heaterState;
  static PollState fanState;
  static PollState exhaustState;
  static PollState heatmatState;

  constexpr uint32_t RETRY_BACKOFF_MS = 30000UL;

  auto pollDevice = [&](ShellyDevice& configured, ShellyDevice& runtime, PollState& state) {
    {
      ShellyCacheLock lock;if(!lock.held)return;
      const uint32_t epoch=shellyCacheEpoch();
      if(state.epoch!=epoch){state.epoch=epoch;state.backingOff=false;}
      if(configured.ip.isEmpty()){configured.values=ShellyValues{};runtime.values=ShellyValues{};state.backingOff=false;return;}
      if(state.backingOff && int32_t(millis()-state.retryAfterMs)<0)return;
    }
    getShellyValues(configured,0);
    ShellyCacheLock lock;if(!lock.held)return;
    // Mirror authoritative state, never restore a private lastGood copy.
    runtime.values=configured.values;
    if(state.epoch!=shellyCacheEpoch()){state.epoch=shellyCacheEpoch();state.backingOff=false;return;}
    state.backingOff=!shellyValuesFresh(configured.values,millis());
    if(state.backingOff)state.retryAfterMs=millis()+RETRY_BACKOFF_MS;
  };

  for (;;) {
    if (otaMaintenanceRequested()) {
      Serial.println("[OTA MAINT] Shelly task releasing stack");
      otaNotifyMaintenanceTaskStopped(OTA_MAINT_TASK_SHELLY);
      vTaskDelete(nullptr);
      return;
    }

    UBaseType_t freeBytes = uxTaskGetStackHighWaterMark(NULL);
    if (freeBytes < minFreeBytes) {
      minFreeBytes = freeBytes;
    }

    static uint32_t lastLogMs = 0;
    const uint32_t logIntervalMs = debugLog ? 5000UL : 60000UL;

    if (millis() - lastLogMs > logIntervalMs) {
      lastLogMs = millis();

      char buf[96];
      snprintf(
        buf,
        sizeof(buf),
        "[TASK][CheckShellyStatus] free=%u bytes, min=%u bytes",
        freeBytes,
        minFreeBytes
      );
      logPrint(String(buf));
    }

    // Do not start a second outbound HTTP transaction while the OTA worker is
    // streaming the firmware from GitHub. Local sensor/watering control keeps
    // running; only Shelly status polling is briefly paused.
    if (otaIsUpdateInProgress()) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (processClimateSafetyShellyOff()) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    irrigationPlanRefreshShelly(false);
    humidifierFanRefreshConfig();

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
      case 5: pollDevice(settings.shelly.exhaust,    shelly.exhaust,    exhaustState); break;
      default: pollDevice(settings.shelly.heatmat,   shelly.heatmat,    heatmatState); break;
    }
    pollIndex = (pollIndex + 1) % 7;

    // Seven steps at 1.4 s keep the per-device refresh close to the existing
    // ~10 s cadence while avoiding a burst of consecutive HTTP operations.
    // Empty IPs are skipped without any network request.
    vTaskDelay(pdMS_TO_TICKS(1400));
  }
}
