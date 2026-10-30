#pragma once

#include <Arduino.h>

// Async OTA handoff. HTTP callbacks only queue work; GitHub traffic and
// firmware flashing run in the dedicated OTA worker task. Firmware install
// uses Arduino HTTPUpdate (the proven v0.2.46 transport) while status remains
// available through the AsyncWebServer.
void otaInit();
void otaProcessQueue();

bool otaQueueCheck(bool dryRun, uint32_t& requestId, String& errorOut);
bool otaQueueUpdate(bool dryRun, uint32_t& requestId, String& errorOut);

String otaCheckStatusJson(uint32_t requestId);
String otaUpdateStatusJson(uint32_t requestId);

// True only while the firmware image itself is being streamed/flashed.
// Network polling tasks may use this to avoid competing outbound requests.
bool otaIsUpdateInProgress();


// Exclusive OTA maintenance mode.
// Once a real firmware installation has been acknowledged to the browser,
// application tasks shut themselves down and release their stacks. The main
// loop stops the application web server and releases nonessential services.
// Wi-Fi/system networking, the Async OTA-status endpoint and the dedicated
// OTA worker remain active.
constexpr uint32_t OTA_MAINT_TASK_SENSOR   = 1u << 0;
constexpr uint32_t OTA_MAINT_TASK_BME      = 1u << 1;
constexpr uint32_t OTA_MAINT_TASK_SHELLY   = 1u << 2;
constexpr uint32_t OTA_MAINT_TASK_WATERING = 1u << 3;

void otaRegisterMaintenanceTask(uint32_t taskBit);
bool otaMaintenanceRequested();
void otaNotifyMaintenanceTaskStopped(uint32_t taskBit);
bool otaMaintenanceAllTasksStopped();
uint32_t otaMaintenanceRegisteredMask();
uint32_t otaMaintenanceStoppedMask();

void otaMarkPlatformMaintenanceReady();
bool otaPlatformMaintenanceReady();
