#include "ota.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
extern volatile bool g_deferredInitComplete;
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"

#if __has_include("build_version.h")
#include "build_version.h"
#endif

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

static const char* GITHUB_OWNER = "syschelle";
static const char* GITHUB_REPO = "GrowTent";
static constexpr int OTA_CHECK_MAX_ATTEMPTS = 2;
static constexpr uint32_t OTA_CHECK_RETRY_DELAY_MS = 750;
static constexpr uint16_t OTA_CHECK_CONNECT_TIMEOUT_MS = 8000;
static constexpr uint16_t OTA_CHECK_RESPONSE_TIMEOUT_MS = 15000;
static constexpr uint32_t OTA_WORKER_STACK_BYTES = 8192;
static constexpr BaseType_t OTA_WORKER_CORE = 1;
static constexpr uint32_t OTA_EXCLUSIVE_RESPONSE_GRACE_MS = 1000UL;
static constexpr uint32_t OTA_EXCLUSIVE_QUIESCE_TIMEOUT_MS = 20000UL;

enum class OtaJobType : uint8_t { None, Check, Update };
enum class OtaJobState : uint8_t { Idle, Queued, Running, Done, Failed };

struct OtaRuntimeStatus {
  OtaJobType type = OtaJobType::None;
  OtaJobState state = OtaJobState::Idle;
  uint32_t requestId = 0;
  bool dryRun = false;
  int percent = 0;
  int currentBytes = 0;
  int totalBytes = 0;
  String message;
  String error;
  String latestVersion;
  String changelog;
  String firmwareUrl;
  bool updateAvailable = false;
  int checkAttempt = 0;
  int manifestHttpCode = 0;
  int manifestContentLength = -1;
  int manifestReceivedLength = 0;
};

struct OtaTerminalSnapshot {
  bool valid = false;
  OtaJobType type = OtaJobType::None;
  OtaJobState state = OtaJobState::Idle;
  uint32_t requestId = 0;
  bool dryRun = false;
  int percent = 0;
  int currentBytes = 0;
  int totalBytes = 0;
  String message;
  String error;
  String latestVersion;
  String firmwareUrl;
  bool updateAvailable = false;
  int checkAttempt = 0;
  int manifestHttpCode = 0;
  int manifestContentLength = -1;
  int manifestReceivedLength = 0;
};

static SemaphoreHandle_t g_otaMutex = nullptr;
static OtaRuntimeStatus g_otaStatus;
static OtaTerminalSnapshot g_otaLastCheckStatus;
static OtaTerminalSnapshot g_otaLastUpdateStatus;
static uint32_t g_otaNextRequestId = 0;
static TaskHandle_t g_otaWorkerTask = nullptr;
static volatile bool g_otaJobQueued = false;
static uint32_t g_otaRestartAtMs = 0;
static volatile bool g_otaRestartAfterSuccessfulUpdate = false;

// Exclusive OTA maintenance state. Bitmasks are protected by a small critical
// section because multiple application tasks may acknowledge shutdown at nearly
// the same time on the two ESP32 cores.
static volatile bool g_otaMaintenanceRequested = false;
static volatile bool g_otaPlatformMaintenanceReady = false;
static volatile uint32_t g_otaMaintenanceRegisteredMask = 0;
static volatile uint32_t g_otaMaintenanceStoppedMask = 0;
static portMUX_TYPE g_otaMaintenanceMux = portMUX_INITIALIZER_UNLOCKED;

// OTA progress is published as simple scalar snapshots. HTTPUpdate invokes
// its progress callback inside the OTA worker; that callback deliberately
// performs no mutex/String work so the Async status endpoint can read the
// live counters without interfering with the firmware stream.
static volatile bool g_otaUpdateStreaming = false;
static volatile int g_otaStreamPercent = 0;
static volatile int g_otaStreamCurrent = 0;
static volatile int g_otaStreamTotal = 0;
static int g_otaKnownFirmwareSize = -1;
static String g_otaKnownFirmwareUrl;
static String g_otaKnownFirmwareVersion;
static uint32_t g_otaKnownFirmwareSizeAtMs = 0;


static void otaSetFailure(uint32_t requestId, const String& error);
static void otaSetProgress(uint32_t requestId, int percent, int current, int total, const char* message);
static void otaLogMemory(const char* stage);

void otaRegisterMaintenanceTask(uint32_t taskBit) {
  portENTER_CRITICAL(&g_otaMaintenanceMux);
  g_otaMaintenanceRegisteredMask |= taskBit;
  portEXIT_CRITICAL(&g_otaMaintenanceMux);
}

bool otaMaintenanceRequested() {
  return g_otaMaintenanceRequested;
}

void otaNotifyMaintenanceTaskStopped(uint32_t taskBit) {
  portENTER_CRITICAL(&g_otaMaintenanceMux);
  g_otaMaintenanceStoppedMask |= taskBit;
  const uint32_t stopped = g_otaMaintenanceStoppedMask;
  const uint32_t registered = g_otaMaintenanceRegisteredMask;
  portEXIT_CRITICAL(&g_otaMaintenanceMux);

  Serial.printf("[OTA MAINT] task stopped bit=0x%02X stopped=0x%02X registered=0x%02X\n",
                (unsigned)taskBit, (unsigned)stopped, (unsigned)registered);
}

uint32_t otaMaintenanceRegisteredMask() {
  portENTER_CRITICAL(&g_otaMaintenanceMux);
  const uint32_t value = g_otaMaintenanceRegisteredMask;
  portEXIT_CRITICAL(&g_otaMaintenanceMux);
  return value;
}

uint32_t otaMaintenanceStoppedMask() {
  portENTER_CRITICAL(&g_otaMaintenanceMux);
  const uint32_t value = g_otaMaintenanceStoppedMask;
  portEXIT_CRITICAL(&g_otaMaintenanceMux);
  return value;
}

bool otaMaintenanceAllTasksStopped() {
  portENTER_CRITICAL(&g_otaMaintenanceMux);
  const uint32_t registered = g_otaMaintenanceRegisteredMask;
  const uint32_t stopped = g_otaMaintenanceStoppedMask;
  portEXIT_CRITICAL(&g_otaMaintenanceMux);
  return (stopped & registered) == registered;
}

void otaMarkPlatformMaintenanceReady() {
  g_otaPlatformMaintenanceReady = true;
}

bool otaPlatformMaintenanceReady() {
  return g_otaPlatformMaintenanceReady;
}

static void otaRequestExclusiveMaintenance() {
  portENTER_CRITICAL(&g_otaMaintenanceMux);
  g_otaMaintenanceStoppedMask = 0;
  g_otaPlatformMaintenanceReady = false;
  g_otaMaintenanceRequested = true;
  const uint32_t registered = g_otaMaintenanceRegisteredMask;
  portEXIT_CRITICAL(&g_otaMaintenanceMux);

  Serial.printf("[OTA MAINT] exclusive mode requested registered=0x%02X\n",
                (unsigned)registered);
}

static bool otaWaitForExclusiveMaintenance(uint32_t requestId) {
  // Let the AsyncWebServer finish sending the POST acknowledgement before the
  // main loop shuts the listener down.
  otaSetProgress(requestId, 0, 0, g_otaKnownFirmwareSize, "preparing_exclusive_ota");
  vTaskDelay(pdMS_TO_TICKS(OTA_EXCLUSIVE_RESPONSE_GRACE_MS));

  otaRequestExclusiveMaintenance();

  const uint32_t startedAt = millis();
  uint32_t lastLogAt = 0;

  while (!otaPlatformMaintenanceReady()) {
    const uint32_t now = millis();

    if (lastLogAt == 0 || (uint32_t)(now - lastLogAt) >= 1000UL) {
      lastLogAt = now;
      Serial.printf(
          "[OTA MAINT] waiting stopped=0x%02X registered=0x%02X platformReady=%s elapsedMs=%u\n",
          (unsigned)otaMaintenanceStoppedMask(),
          (unsigned)otaMaintenanceRegisteredMask(),
          otaPlatformMaintenanceReady() ? "true" : "false",
          (unsigned)(now - startedAt));
    }

    if ((uint32_t)(now - startedAt) >= OTA_EXCLUSIVE_QUIESCE_TIMEOUT_MS) {
      Serial.println("[OTA MAINT] ERROR quiesce timeout");
      otaSetFailure(requestId, "ota_maintenance_timeout");
      return false;
    }

    vTaskDelay(pdMS_TO_TICKS(25));
  }

  // The browser is disconnected at this point. Free status/check strings that
  // are no longer observable and keep only the local target URL/version copies
  // already created by runOtaUpdate().
  if (g_otaMutex && xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    g_otaStatus.changelog = String();
    g_otaLastCheckStatus.latestVersion = String();
    g_otaLastCheckStatus.firmwareUrl = String();
    g_otaLastUpdateStatus.latestVersion = String();
    g_otaLastUpdateStatus.firmwareUrl = String();
    xSemaphoreGive(g_otaMutex);
  }

  g_otaKnownFirmwareUrl = String();
  g_otaKnownFirmwareVersion = String();

  otaLogMemory("exclusive_ready");
  Serial.println("[OTA MAINT] exclusive OTA environment ready");
  return true;
}

static void otaLogMemory(const char* stage) {
  const uint32_t internalFree =
      (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t internalLargest =
      (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  Serial.printf(
      "[OTA HEAP] %s freeHeap=%u maxAlloc=%u internalFree=%u internalLargest=%u stackHighWater=%u\n",
      stage ? stage : "unknown",
      ESP.getFreeHeap(),
      ESP.getMaxAllocHeap(),
      internalFree,
      internalLargest,
      (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}


static String otaJsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\r", "\\r");
  value.replace("\n", "\\n");
  return value;
}

static const char* otaStateName(OtaJobState state) {
  switch (state) {
    case OtaJobState::Queued: return "queued";
    case OtaJobState::Running: return "running";
    case OtaJobState::Done: return "done";
    case OtaJobState::Failed: return "failed";
    default: return "idle";
  }
}

static bool parseSemver(const String& in, int& major, int& minor, int& patch) {
  String s = in;
  s.trim();
  if (s.startsWith("v") || s.startsWith("V")) s.remove(0, 1);
  const int p1 = s.indexOf('.');
  const int p2 = s.indexOf('.', p1 + 1);
  if (p1 <= 0 || p2 <= p1) return false;
  major = s.substring(0, p1).toInt();
  minor = s.substring(p1 + 1, p2).toInt();
  patch = s.substring(p2 + 1).toInt();
  return true;
}

static int compareSemver(const String& a, const String& b) {
  int aMajor, aMinor, aPatch, bMajor, bMinor, bPatch;
  const bool aOk = parseSemver(a, aMajor, aMinor, aPatch);
  const bool bOk = parseSemver(b, bMajor, bMinor, bPatch);
  if (!aOk && bOk) return -1;
  if (aOk && !bOk) return 1;
  if (!aOk && !bOk) return 0;
  if (aMajor != bMajor) return (aMajor < bMajor) ? -1 : 1;
  if (aMinor != bMinor) return (aMinor < bMinor) ? -1 : 1;
  if (aPatch != bPatch) return (aPatch < bPatch) ? -1 : 1;
  return 0;
}

static void otaSetFailure(uint32_t requestId, const String& error) {
  if (g_otaMutex && xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
    if (g_otaStatus.requestId == requestId) {
      g_otaStatus.state = OtaJobState::Failed;
      g_otaStatus.error = error;
      g_otaStatus.message = "failed";
    }
    xSemaphoreGive(g_otaMutex);
  }

  // Once the application has entered exclusive OTA mode, do not try to rebuild
  // the runtime in-place after a failed installer. The old OTA partition remains
  // bootable, so reboot back into it and restore the normal application cleanly.
  if (g_otaMaintenanceRequested && g_otaRestartAtMs == 0) {
    Serial.printf("[OTA MAINT] update failed (%s); rebooting existing firmware\n",
                  error.c_str());
    g_otaRestartAfterSuccessfulUpdate = false;
    g_otaRestartAtMs = millis() + 2000UL;
  }
}

static void otaSetProgress(uint32_t requestId, int percent, int current, int total, const char* message) {
  if (!g_otaMutex) return;
  if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (g_otaStatus.requestId == requestId) {
      g_otaStatus.percent = constrain(percent, 0, 100);
      g_otaStatus.currentBytes = current;
      g_otaStatus.totalBytes = total;
      if (message) g_otaStatus.message = message;
    }
    xSemaphoreGive(g_otaMutex);
  }
}

static void otaSetCheckDiagnostics(uint32_t requestId, int attempt, int httpCode, int contentLength, int receivedLength) {
  if (!g_otaMutex) return;
  if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (g_otaStatus.requestId == requestId) {
      g_otaStatus.checkAttempt = attempt;
      g_otaStatus.manifestHttpCode = httpCode;
      g_otaStatus.manifestContentLength = contentLength;
      g_otaStatus.manifestReceivedLength = receivedLength;
    }
    xSemaphoreGive(g_otaMutex);
  }
}

static bool fetchOtaManifestOnce(uint32_t requestId,
                                 const String& manifestUrl,
                                 int attempt,
                                 String& latestTag,
                                 String& changelog,
                                 String& firmwareUrl,
                                 int& firmwareSize,
                                 String& errorOut) {
  errorOut = "";
  latestTag = "";
  changelog = "";
  firmwareUrl = "";
  firmwareSize = -1;

  if (WiFi.status() != WL_CONNECTED) {
    errorOut = "wifi_not_connected";
    otaSetCheckDiagnostics(requestId, attempt, 0, -1, 0);
    return false;
  }

  Serial.printf("[OTA CHECK] attempt=%d/%d core=%d freeHeap=%u maxAlloc=%u stackHighWater=%u\n",
                attempt,
                OTA_CHECK_MAX_ATTEMPTS,
                xPortGetCoreID(),
                ESP.getFreeHeap(),
                ESP.getMaxAllocHeap(),
                uxTaskGetStackHighWaterMark(nullptr));
  Serial.print("[OTA CHECK] URL: ");
  Serial.println(manifestUrl);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(OTA_CHECK_RESPONSE_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setConnectTimeout(OTA_CHECK_CONNECT_TIMEOUT_MS);
  http.setTimeout(OTA_CHECK_RESPONSE_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);

  if (!http.begin(client, manifestUrl)) {
    Serial.println("[OTA CHECK] http.begin failed");
    otaSetCheckDiagnostics(requestId, attempt, 0, -1, 0);
    errorOut = "http_begin_failed";
    return false;
  }

  http.addHeader("User-Agent", String("GrowTent-ESP32/") + FW_VERSION);
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Connection", "close");
  const int code = http.GET();
  const int contentLength = http.getSize();
  otaSetCheckDiagnostics(requestId, attempt, code, contentLength, 0);

  Serial.printf("[OTA CHECK] HTTP=%d contentLength=%d\n", code, contentLength);
  if (code <= 0) {
    Serial.printf("[OTA CHECK] transport error=%d\n", code);
    http.end();
    errorOut = String("manifest_transport_") + code;
    return false;
  }
  if (code != HTTP_CODE_OK) {
    http.end();
    errorOut = String("manifest_http_") + code;
    return false;
  }

  const String manifestBody = http.getString();
  const int receivedLength = (int)manifestBody.length();
  otaSetCheckDiagnostics(requestId, attempt, code, contentLength, receivedLength);
  http.end();
  Serial.printf("[OTA CHECK] bodyBytes=%d expectedBytes=%d\n", receivedLength, contentLength);

  if (manifestBody.isEmpty()) {
    errorOut = "manifest_empty_body";
    return false;
  }
  if (contentLength > 0 && receivedLength != contentLength) {
    errorOut = String("manifest_incomplete_") + receivedLength + "_of_" + contentLength;
    return false;
  }

  JsonDocument filter;
  filter["version"] = true;
  filter["changelog"] = true;
  filter["firmwareUrl"] = true;
  filter["firmwareSize"] = true;
  JsonDocument doc;
  const DeserializationError jerr =
      deserializeJson(doc, manifestBody, DeserializationOption::Filter(filter));

  if (jerr) {
    Serial.print("[OTA CHECK] JSON parse failed: ");
    Serial.println(jerr.c_str());
    errorOut = String("json_parse_failed:") + jerr.c_str();
    return false;
  }

  latestTag = doc["version"] | "";
  changelog = doc["changelog"] | "";
  firmwareUrl = doc["firmwareUrl"] | "";
  firmwareSize = doc["firmwareSize"] | -1;
  latestTag.trim();

  if (latestTag.isEmpty()) {
    Serial.println("[OTA CHECK] manifest missing version");
    errorOut = "manifest_missing_version";
    return false;
  }
  if (firmwareUrl.isEmpty()) {
    firmwareUrl = String("https://github.com/") + GITHUB_OWNER + "/" + GITHUB_REPO +
                  "/releases/latest/download/firmware.bin";
  }

  Serial.printf("[OTA CHECK] manifest parsed: latest=%s changelogBytes=%u firmwareSize=%d\n",
                latestTag.c_str(), (unsigned)changelog.length(), firmwareSize);
  return true;
}

static bool runOtaCheck(uint32_t requestId, bool dryRun) {
  if (dryRun) {
    if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
      g_otaStatus.latestVersion = FW_VERSION;
      g_otaStatus.changelog = "dry-run";
      g_otaStatus.firmwareUrl = "";
      g_otaStatus.updateAvailable = false;
      g_otaStatus.percent = 100;
      g_otaStatus.message = "check_complete";
      g_otaStatus.state = OtaJobState::Done;
      xSemaphoreGive(g_otaMutex);
    }
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA CHECK] refused: WiFi not connected");
    otaSetFailure(requestId, "wifi_not_connected");
    return false;
  }

  // A real check owns the target selection. Clear any older cached target first
  // so a failed check can never leave a stale firmware URL eligible for install.
  g_otaKnownFirmwareSize = -1;
  g_otaKnownFirmwareUrl = "";
  g_otaKnownFirmwareVersion = "";
  g_otaKnownFirmwareSizeAtMs = 0;

  const String manifestUrl = String("https://github.com/") + GITHUB_OWNER + "/" + GITHUB_REPO +
                             "/releases/latest/download/manifest.json";
  Serial.printf("[OTA CHECK] worker begin id=%lu installed=%s\n",
                (unsigned long)requestId, FW_VERSION);

  String latestTag;
  String changelog;
  String firmwareUrl;
  int firmwareSize = -1;
  String lastError = "manifest_check_failed";
  bool fetched = false;

  for (int attempt = 1; attempt <= OTA_CHECK_MAX_ATTEMPTS; ++attempt) {
    String attemptError;
    if (fetchOtaManifestOnce(requestId, manifestUrl, attempt,
                             latestTag, changelog, firmwareUrl, firmwareSize, attemptError)) {
      fetched = true;
      break;
    }

    lastError = attemptError.length() ? attemptError : "manifest_check_failed";
    Serial.printf("[OTA CHECK] attempt %d failed: %s\n", attempt, lastError.c_str());
    if (attempt < OTA_CHECK_MAX_ATTEMPTS) {
      Serial.printf("[OTA CHECK] retrying in %u ms\n", OTA_CHECK_RETRY_DELAY_MS);
      vTaskDelay(pdMS_TO_TICKS(OTA_CHECK_RETRY_DELAY_MS));
    }
  }

  if (!fetched) {
    Serial.printf("[OTA CHECK] FAILED after %d attempts: %s\n",
                  OTA_CHECK_MAX_ATTEMPTS, lastError.c_str());
    otaSetFailure(requestId, lastError);
    return false;
  }

  const bool updateAvailable = (compareSemver(String(FW_VERSION), latestTag) < 0);
  if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
    if (g_otaStatus.requestId == requestId) {
      g_otaStatus.latestVersion = latestTag;
      g_otaStatus.changelog = changelog;
      g_otaStatus.firmwareUrl = firmwareUrl;
      g_otaStatus.updateAvailable = updateAvailable;
      if (firmwareSize > 0 && !firmwareUrl.isEmpty()) {
        g_otaKnownFirmwareSize = firmwareSize;
        g_otaKnownFirmwareUrl = firmwareUrl;
        g_otaKnownFirmwareVersion = latestTag;
        g_otaKnownFirmwareSizeAtMs = millis();
      }
      g_otaStatus.percent = 100;
      g_otaStatus.message = "check_complete";
      g_otaStatus.state = OtaJobState::Done;
    }
    xSemaphoreGive(g_otaMutex);
  }

  Serial.printf("[OTA CHECK] complete: installed=%s latest=%s updateAvailable=%s\n",
                FW_VERSION,
                latestTag.c_str(),
                updateAvailable ? "true" : "false");
  return true;
}


static bool runOtaUpdate(uint32_t requestId, bool dryRun) {
  if (dryRun) {
    if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
      g_otaStatus.percent = 100;
      g_otaStatus.message = "dry_run_complete";
      g_otaStatus.state = OtaJobState::Done;
      xSemaphoreGive(g_otaMutex);
    }
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    otaSetFailure(requestId, "wifi_not_connected");
    return false;
  }

  // The dashboard performs a check before enabling Install/Reinstall. Require a
  // recent successful check here as well so the update target is version-pinned
  // and an older release can never silently downgrade a newer local firmware.
  const uint32_t targetAgeMs = (uint32_t)(millis() - g_otaKnownFirmwareSizeAtMs);
  if (g_otaKnownFirmwareSize <= 0 ||
      g_otaKnownFirmwareUrl.isEmpty() ||
      g_otaKnownFirmwareVersion.isEmpty() ||
      g_otaKnownFirmwareSizeAtMs == 0 ||
      targetAgeMs > 10UL * 60UL * 1000UL) {
    otaSetFailure(requestId, "update_check_required");
    return false;
  }

  if (compareSemver(String(FW_VERSION), g_otaKnownFirmwareVersion) > 0) {
    Serial.printf("[OTA] refused downgrade installed=%s target=%s\n",
                  FW_VERSION, g_otaKnownFirmwareVersion.c_str());
    otaSetFailure(requestId, "downgrade_refused");
    return false;
  }

  // Copy the target before exclusive maintenance clears the cached check data.
  const String firmwareUrl = g_otaKnownFirmwareUrl;
  const String targetVersion = g_otaKnownFirmwareVersion;
  const int expectedSize = g_otaKnownFirmwareSize;

  Serial.println("[OTA] HTTPUpdate transport requested");
  Serial.printf("[OTA] target=%s expectedSize=%d workerStack=%u core=%d\n",
                targetVersion.c_str(), expectedSize,
                (unsigned)OTA_WORKER_STACK_BYTES, xPortGetCoreID());
  Serial.printf("[OTA] URL: %s\n", firmwareUrl.c_str());
  otaLogMemory("before_exclusive_shutdown");

  if (!otaWaitForExclusiveMaintenance(requestId)) {
    return false;
  }

  otaLogMemory("httpupdate_preflight");

  // Restore the proven Arduino-ESP32 HTTPUpdate transport used by v0.2.46.
  // The Async callback itself never blocks: this code still runs exclusively in
  // the dedicated OTA worker. HTTPUpdate owns redirect handling and Update.begin.
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);

  HTTPUpdate otaUpdater;
  otaUpdater.rebootOnUpdate(false);
  otaUpdater.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  g_otaStreamPercent = 0;
  g_otaStreamCurrent = 0;
  g_otaStreamTotal = expectedSize;
  g_otaUpdateStreaming = true;
  otaSetProgress(requestId, 0, 0, expectedSize, "connecting");

  int lastPercentLogged = -1;

  otaUpdater.onStart([&]() {
    // This callback runs in the OTA worker. Keep it diagnostic-only.
    Serial.println("[OTA] HTTPUpdate start");
    otaLogMemory("httpupdate_start");
  });

  otaUpdater.onProgress([&](int current, int total) {
    int percent = 0;
    if (total > 0) {
      percent = constrain((int)((100LL * current) / total), 0, 100);
    }

    // Intentionally lock-free: no semaphore and no heap text allocation here.
    g_otaStreamPercent = percent;
    g_otaStreamCurrent = current;
    g_otaStreamTotal = total > 0 ? total : expectedSize;

    if (lastPercentLogged < 0 || percent == 100 || percent >= lastPercentLogged + 5) {
      lastPercentLogged = percent;
      Serial.printf("[OTA] Progress: %d%% (%d / %d)\n",
                    percent, current, total);
    }
  });

  otaUpdater.onEnd([&]() {
    Serial.println("[OTA] HTTPUpdate end");
  });

  otaUpdater.onError([&](int error) {
    Serial.printf("[OTA] HTTPUpdate error callback: %d\n", error);
  });

  Serial.println("[OTA] HTTPUpdate download/flash begin");
  const t_httpUpdate_return ret = otaUpdater.update(client, firmwareUrl);
  g_otaUpdateStreaming = false;

  if (ret == HTTP_UPDATE_OK) {
    const int finalTotal =
        g_otaStreamTotal > 0 ? g_otaStreamTotal : expectedSize;
    g_otaStreamPercent = 100;
    g_otaStreamCurrent = finalTotal;
    g_otaStreamTotal = finalTotal;

    otaLogMemory("httpupdate_success");
    Serial.printf("[OTA] HTTPUpdate complete target=%s bytes=%d\n",
                  targetVersion.c_str(), finalTotal);

    if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
      if (g_otaStatus.requestId == requestId) {
        g_otaStatus.percent = 100;
        g_otaStatus.currentBytes = finalTotal;
        g_otaStatus.totalBytes = finalTotal;
        g_otaStatus.message = "updated_restarting";
        g_otaStatus.state = OtaJobState::Done;
        g_otaRestartAfterSuccessfulUpdate = true;
        g_otaRestartAtMs = millis() + 2500UL;
      }
      xSemaphoreGive(g_otaMutex);
    }
    return true;
  }

  if (ret == HTTP_UPDATE_NO_UPDATES) {
    Serial.println("[OTA] HTTPUpdate returned NO_UPDATES; rebooting existing firmware");
    otaSetFailure(requestId, "http_update_no_updates");
    return false;
  }

  String err = otaUpdater.getLastErrorString();
  if (err.isEmpty()) err = "unknown_http_update_error";

  Serial.print("[OTA] HTTPUpdate failed: ");
  Serial.println(err);
  otaLogMemory("httpupdate_failed");

  otaSetFailure(requestId, String("http_update:") + err);
  return false;
}

static void otaWorker(void*) {
  uint32_t requestId = 0;
  OtaJobType type = OtaJobType::None;
  bool dryRun = false;
  if (g_otaMutex && xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
    requestId = g_otaStatus.requestId;
    type = g_otaStatus.type;
    dryRun = g_otaStatus.dryRun;
    g_otaStatus.state = OtaJobState::Running;
    g_otaStatus.message = (type == OtaJobType::Check) ? "checking" : "starting";
    xSemaphoreGive(g_otaMutex);
  }

  Serial.printf("[OTA WORKER] start type=%s id=%lu dryRun=%s core=%d freeHeap=%u\n",
                type == OtaJobType::Check ? "check" : (type == OtaJobType::Update ? "update" : "invalid"),
                (unsigned long)requestId,
                dryRun ? "true" : "false",
                xPortGetCoreID(),
                ESP.getFreeHeap());

  if (type == OtaJobType::Check) runOtaCheck(requestId, dryRun);
  else if (type == OtaJobType::Update) runOtaUpdate(requestId, dryRun);
  else otaSetFailure(requestId, "invalid_job");

  g_otaWorkerTask = nullptr;
  vTaskDelete(nullptr);
}

void otaInit() {
  if (!g_otaMutex) g_otaMutex = xSemaphoreCreateMutex();
}

static bool otaStateIsTerminal(OtaJobState state) {
  return state == OtaJobState::Done || state == OtaJobState::Failed;
}

static void otaArchiveCurrentTerminalLocked() {
  if (!otaStateIsTerminal(g_otaStatus.state) || g_otaStatus.requestId == 0) return;

  OtaTerminalSnapshot* target = nullptr;
  if (g_otaStatus.type == OtaJobType::Check) target = &g_otaLastCheckStatus;
  else if (g_otaStatus.type == OtaJobType::Update) target = &g_otaLastUpdateStatus;
  if (!target) return;

  target->valid = true;
  target->type = g_otaStatus.type;
  target->state = g_otaStatus.state;
  target->requestId = g_otaStatus.requestId;
  target->dryRun = g_otaStatus.dryRun;
  target->percent = g_otaStatus.percent;
  target->currentBytes = g_otaStatus.currentBytes;
  target->totalBytes = g_otaStatus.totalBytes;
  target->message = g_otaStatus.message;
  target->error = g_otaStatus.error;
  target->latestVersion = g_otaStatus.latestVersion;
  target->firmwareUrl = g_otaStatus.firmwareUrl;
  target->updateAvailable = g_otaStatus.updateAvailable;
  target->checkAttempt = g_otaStatus.checkAttempt;
  target->manifestHttpCode = g_otaStatus.manifestHttpCode;
  target->manifestContentLength = g_otaStatus.manifestContentLength;
  target->manifestReceivedLength = g_otaStatus.manifestReceivedLength;

  Serial.printf("[OTA STATUS] archived type=%s id=%lu state=%s\n",
                g_otaStatus.type == OtaJobType::Check ? "check" : "update",
                (unsigned long)g_otaStatus.requestId,
                otaStateName(g_otaStatus.state));
}

static bool otaQueue(OtaJobType type, bool dryRun, uint32_t& requestId, String& errorOut) {
  errorOut = "";
  otaInit();
  if (!g_otaMutex) { errorOut = "handoff_unavailable"; return false; }
  if (WiFi.status() != WL_CONNECTED && !dryRun) { errorOut = "wifi_not_connected"; return false; }

  if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) != pdTRUE) {
    errorOut = "handoff_busy";
    return false;
  }
  const bool busy = g_otaJobQueued || g_otaWorkerTask != nullptr ||
                    g_otaStatus.state == OtaJobState::Queued || g_otaStatus.state == OtaJobState::Running;
  if (busy) {
    xSemaphoreGive(g_otaMutex);
    errorOut = "ota_busy";
    return false;
  }

  // v0.4.6: preserve the previous terminal result before reusing the single
  // active-job slot. This keeps its requestId queryable while the next OTA job
  // is already queued/running, without retaining the large changelog buffer.
  otaArchiveCurrentTerminalLocked();

  requestId = ++g_otaNextRequestId;
  g_otaStatus = OtaRuntimeStatus();
  g_otaStatus.type = type;
  g_otaStatus.state = OtaJobState::Queued;
  g_otaStatus.requestId = requestId;
  g_otaStatus.dryRun = dryRun;
  g_otaStatus.message = "queued";
  g_otaJobQueued = true;
  xSemaphoreGive(g_otaMutex);
  Serial.printf("[OTA QUEUE] type=%s id=%lu dryRun=%s\n",
                type == OtaJobType::Check ? "check" : "update",
                (unsigned long)requestId,
                dryRun ? "true" : "false");
  return true;
}

bool otaQueueCheck(bool dryRun, uint32_t& requestId, String& errorOut) {
  return otaQueue(OtaJobType::Check, dryRun, requestId, errorOut);
}

bool otaQueueUpdate(bool dryRun, uint32_t& requestId, String& errorOut) {
  if (!dryRun && !g_deferredInitComplete) {
    requestId = 0;
    errorOut = "system_initializing";
    return false;
  }
  return otaQueue(OtaJobType::Update, dryRun, requestId, errorOut);
}

static String otaStatusJsonFor(OtaJobType expectedType, uint32_t requestId) {
  otaInit();
  if (!g_otaMutex) return "{\"ok\":false,\"error\":\"handoff_unavailable\"}";
  if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) != pdTRUE)
    return "{\"ok\":false,\"error\":\"handoff_busy\"}";

  const bool liveMatch =
      requestId != 0 &&
      g_otaStatus.requestId == requestId &&
      g_otaStatus.type == expectedType;

  const OtaTerminalSnapshot* archived = nullptr;
  if (!liveMatch && requestId != 0) {
    OtaTerminalSnapshot* candidate =
        expectedType == OtaJobType::Check ? &g_otaLastCheckStatus : &g_otaLastUpdateStatus;
    if (candidate->valid &&
        candidate->requestId == requestId &&
        candidate->type == expectedType) {
      archived = candidate;
    }
  }

  if (!liveMatch && !archived) {
    xSemaphoreGive(g_otaMutex);
    return "{\"ok\":false,\"error\":\"request_not_found\"}";
  }

  if (archived) {
    String json;
    json.reserve(640);
    json = "{\"ok\":true,\"requestId\":" + String(archived->requestId);
    json += ",\"state\":\"" + String(otaStateName(archived->state)) + "\"";
    json += ",\"dryRun\":" + String(archived->dryRun ? "true" : "false");
    json += ",\"percent\":" + String(archived->percent);
    json += ",\"current\":" + String(archived->currentBytes);
    json += ",\"total\":" + String(archived->totalBytes);
    if (expectedType == OtaJobType::Check) {
      json += ",\"attempt\":" + String(archived->checkAttempt);
      json += ",\"manifestHttpCode\":" + String(archived->manifestHttpCode);
      json += ",\"manifestContentLength\":" + String(archived->manifestContentLength);
      json += ",\"manifestReceivedLength\":" + String(archived->manifestReceivedLength);
    }
    json += ",\"message\":\"" + otaJsonEscape(archived->message) + "\"";
    if (archived->error.length()) json += ",\"error\":\"" + otaJsonEscape(archived->error) + "\"";
    if (expectedType == OtaJobType::Check && archived->state == OtaJobState::Done) {
      json += ",\"currentVersion\":\"" + String(FW_VERSION) + "\"";
      json += ",\"latestVersion\":\"" + otaJsonEscape(archived->latestVersion) + "\"";
      json += ",\"updateAvailable\":" + String(archived->updateAvailable ? "true" : "false");
      // Deliberately do not duplicate/archive the potentially multi-kilobyte
      // changelog. Late pollers still receive the complete state/result contract.
      json += ",\"changelog\":\"\"";
      json += ",\"firmwareUrl\":\"" + otaJsonEscape(archived->firmwareUrl) + "\"";
    }
    json += "}";
    xSemaphoreGive(g_otaMutex);
    return json;
  }

  int statusPercent = g_otaStatus.percent;
  int statusCurrent = g_otaStatus.currentBytes;
  int statusTotal = g_otaStatus.totalBytes;
  if (expectedType == OtaJobType::Update &&
      g_otaStatus.state == OtaJobState::Running && g_otaUpdateStreaming) {
    statusPercent = g_otaStreamPercent;
    statusCurrent = g_otaStreamCurrent;
    statusTotal = g_otaStreamTotal;
  }

  String json;
  json.reserve(768 + g_otaStatus.changelog.length());
  json = "{\"ok\":true,\"requestId\":" + String(g_otaStatus.requestId);
  json += ",\"state\":\"" + String(otaStateName(g_otaStatus.state)) + "\"";
  json += ",\"dryRun\":" + String(g_otaStatus.dryRun ? "true" : "false");
  json += ",\"percent\":" + String(statusPercent);
  json += ",\"current\":" + String(statusCurrent);
  json += ",\"total\":" + String(statusTotal);
  if (expectedType == OtaJobType::Check) {
    json += ",\"attempt\":" + String(g_otaStatus.checkAttempt);
    json += ",\"manifestHttpCode\":" + String(g_otaStatus.manifestHttpCode);
    json += ",\"manifestContentLength\":" + String(g_otaStatus.manifestContentLength);
    json += ",\"manifestReceivedLength\":" + String(g_otaStatus.manifestReceivedLength);
  }
  json += ",\"message\":\"" + otaJsonEscape(g_otaStatus.message) + "\"";
  if (g_otaStatus.error.length()) json += ",\"error\":\"" + otaJsonEscape(g_otaStatus.error) + "\"";
  if (expectedType == OtaJobType::Check && g_otaStatus.state == OtaJobState::Done) {
    json += ",\"currentVersion\":\"" + String(FW_VERSION) + "\"";
    json += ",\"latestVersion\":\"" + otaJsonEscape(g_otaStatus.latestVersion) + "\"";
    json += ",\"updateAvailable\":" + String(g_otaStatus.updateAvailable ? "true" : "false");
    json += ",\"changelog\":\"" + otaJsonEscape(g_otaStatus.changelog) + "\"";
    json += ",\"firmwareUrl\":\"" + otaJsonEscape(g_otaStatus.firmwareUrl) + "\"";
  }
  json += "}";
  xSemaphoreGive(g_otaMutex);
  return json;
}

String otaCheckStatusJson(uint32_t requestId) {
  return otaStatusJsonFor(OtaJobType::Check, requestId);
}

String otaUpdateStatusJson(uint32_t requestId) {
  return otaStatusJsonFor(OtaJobType::Update, requestId);
}

bool otaIsUpdateInProgress() {
  return g_otaUpdateStreaming;
}

void otaProcessQueue() {
  otaInit();
  if (g_otaRestartAtMs && (int32_t)(millis() - g_otaRestartAtMs) >= 0) {
    Serial.println(g_otaRestartAfterSuccessfulUpdate
                       ? "[OTA] Restarting after successful update"
                       : "[OTA] Restarting after failed update; existing firmware remains active");
    delay(50);
    ESP.restart();
    return;
  }

  if (!g_otaJobQueued || g_otaWorkerTask != nullptr) return;
  if (!g_otaMutex) return;
  if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
  if (!g_otaJobQueued || g_otaWorkerTask != nullptr) {
    xSemaphoreGive(g_otaMutex);
    return;
  }
  g_otaJobQueued = false;
  xSemaphoreGive(g_otaMutex);

  Serial.printf("[OTA WORKER] launching stack=%u core=%d\n",
                (unsigned)OTA_WORKER_STACK_BYTES, (int)OTA_WORKER_CORE);
  BaseType_t ok = xTaskCreatePinnedToCore(otaWorker, "OTA_Worker", OTA_WORKER_STACK_BYTES,
                                          nullptr, 1, &g_otaWorkerTask, OTA_WORKER_CORE);
  if (ok != pdPASS) {
    const uint32_t failedId = g_otaStatus.requestId;
    g_otaWorkerTask = nullptr;
    Serial.printf("[OTA WORKER] launch FAILED id=%lu\n", (unsigned long)failedId);
    otaSetFailure(failedId, "worker_start_failed");
  }
}
