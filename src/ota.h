#pragma once

#include <Arduino.h>

// Phase 32 OTA handoff. Async HTTP callbacks only queue work; all outbound
// GitHub traffic and firmware flashing run in the dedicated OTA worker task.
void otaInit();
void otaProcessQueue();

bool otaQueueCheck(bool dryRun, uint32_t& requestId, String& errorOut);
bool otaQueueUpdate(bool dryRun, uint32_t& requestId, String& errorOut);

String otaCheckStatusJson(uint32_t requestId);
String otaUpdateStatusJson(uint32_t requestId);

// True only while the firmware image itself is being streamed/flashed.
// Network polling tasks may use this to avoid competing outbound requests.
bool otaIsUpdateInProgress();
