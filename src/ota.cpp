#include "ota.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
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

static SemaphoreHandle_t g_otaMutex = nullptr;
static OtaRuntimeStatus g_otaStatus;
static uint32_t g_otaNextRequestId = 0;
static TaskHandle_t g_otaWorkerTask = nullptr;
static volatile bool g_otaJobQueued = false;
static uint32_t g_otaRestartAtMs = 0;

// HTTPUpdate invokes progress callbacks from inside its blocking download/flash
// path. Keep those callbacks lock-free: taking a FreeRTOS semaphore from the
// same task can make HTTPUpdate stall or become extremely slow on ESP32.
static volatile bool g_otaUpdateStreaming = false;
static volatile int g_otaStreamPercent = 0;
static volatile int g_otaStreamCurrent = 0;
static volatile int g_otaStreamTotal = 0;
static int g_otaKnownFirmwareSize = -1;
static uint32_t g_otaKnownFirmwareSizeAtMs = 0;

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
  if (!g_otaMutex) return;
  if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
    if (g_otaStatus.requestId == requestId) {
      g_otaStatus.state = OtaJobState::Failed;
      g_otaStatus.error = error;
      g_otaStatus.message = "failed";
    }
    xSemaphoreGive(g_otaMutex);
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
      if (firmwareSize > 0) {
        g_otaKnownFirmwareSize = firmwareSize;
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

  const String firmwareUrl =
      String("https://github.com/") + GITHUB_OWNER + "/" + GITHUB_REPO +
      "/releases/latest/download/firmware.bin";

  Serial.println("[OTA] worker update requested");
  Serial.printf("[OTA] preflight core=%d workerStack=%u\n",
                xPortGetCoreID(), (unsigned)OTA_WORKER_STACK_BYTES);
  otaLogMemory("preflight");

  otaSetProgress(requestId, 0, 0, 0, "probing_firmware");
  g_otaStreamPercent = 0;
  g_otaStreamCurrent = 0;
  g_otaStreamTotal = 0;

  // Prefer the exact size from manifest.json. This lets Update.begin() reserve
  // its 4 KiB flash buffer before any firmware HTTPS/TLS connection exists.
  int firmwareSize = -1;
  if (g_otaKnownFirmwareSize > 0 &&
      (uint32_t)(millis() - g_otaKnownFirmwareSizeAtMs) <= 10UL * 60UL * 1000UL) {
    firmwareSize = g_otaKnownFirmwareSize;
  }

  // Fallback for direct /api/ota/update calls without a preceding update check
  // or for older manifests without firmwareSize: perform one metadata GET,
  // close TLS completely, then reserve the Update buffer.
  if (firmwareSize <= 0) {
    Serial.println("[OTA] firmwareSize not cached; probing release asset");
    {
      WiFiClientSecure probeClient;
      probeClient.setInsecure();
      probeClient.setTimeout(60);

      HTTPClient probe;
      probe.setConnectTimeout(8000);
      probe.setTimeout(60000);
      probe.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
      probe.useHTTP10(true);

      if (!probe.begin(probeClient, firmwareUrl)) {
        otaSetFailure(requestId, "firmware_probe_begin_failed");
        return false;
      }

      probe.addHeader("User-Agent", String("GrowTent-ESP32/") + FW_VERSION);
      probe.addHeader("Accept", "application/octet-stream");
      probe.addHeader("Accept-Encoding", "identity");
      probe.addHeader("Connection", "close");

      const int probeCode = probe.GET();
      firmwareSize = probe.getSize();
      Serial.printf("[OTA] probe HTTP=%d contentLength=%d\n", probeCode, firmwareSize);

      if (probeCode <= 0) {
        probe.end();
        probeClient.stop();
        otaSetFailure(requestId, String("firmware_probe_transport_") + probeCode);
        return false;
      }
      if (probeCode != HTTP_CODE_OK) {
        probe.end();
        probeClient.stop();
        otaSetFailure(requestId, String("firmware_probe_http_") + probeCode);
        return false;
      }
      if (firmwareSize <= 0) {
        probe.end();
        probeClient.stop();
        otaSetFailure(requestId, "firmware_probe_size_unknown");
        return false;
      }

      probe.end();
      probeClient.stop();
    }

    // Give the TLS/client destructors a scheduling point so freed heap blocks
    // can coalesce before Update.begin() requests its 4 KiB sector buffer.
    vTaskDelay(pdMS_TO_TICKS(20));
    otaLogMemory("after_probe_close");
  } else {
    Serial.printf("[OTA] firmwareSize from manifest=%d\n", firmwareSize);
  }

  Serial.printf("[OTA] reserving Update buffer for %d bytes before TLS\n", firmwareSize);
  if (!Update.begin((size_t)firmwareSize, U_FLASH)) {
    const char* updateErr = Update.errorString();
    Serial.printf("[OTA] Update.begin failed error=%u text=%s\n",
                  (unsigned)Update.getError(),
                  updateErr ? updateErr : "");
    otaLogMemory("Update.begin_failed");
    otaSetFailure(requestId,
                  Update.getError() != UPDATE_ERROR_OK
                      ? String("update_begin_") + (updateErr ? updateErr : "failed")
                      : "update_begin_alloc_failed");
    return false;
  }

  otaLogMemory("after_Update.begin");

  int lastPercentLogged = -1;
  Update.onProgress([&](size_t current, size_t total) {
    const int percent =
        total > 0 ? constrain((int)((100ULL * current) / total), 0, 100) : 0;

    // Keep the flash-stream callback allocation- and mutex-free.
    g_otaStreamPercent = percent;
    g_otaStreamCurrent = (int)current;
    g_otaStreamTotal = (int)total;

    // Serial output is intentionally sparse to avoid slowing the flash stream.
    if (percent == 100 || lastPercentLogged < 0 || percent >= lastPercentLogged + 5) {
      lastPercentLogged = percent;
      Serial.printf("[OTA] Progress: %d%% (%u / %u)\n",
                    percent, (unsigned)current, (unsigned)total);
    }
  });

  otaSetProgress(requestId, 0, 0, firmwareSize, "download_started");
  g_otaStreamTotal = firmwareSize;
  g_otaUpdateStreaming = true;

  bool updateOk = false;
  String failureReason;

  {
    // Only now create the real HTTPS connection. The 4 KiB Update buffer is
    // already allocated and can no longer be squeezed out by TLS fragmentation.
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(60);

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(60000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.useHTTP10(true);

    if (!http.begin(client, firmwareUrl)) {
      failureReason = "firmware_http_begin_failed";
    } else {
      http.addHeader("User-Agent", String("GrowTent-ESP32/") + FW_VERSION);
      http.addHeader("Accept", "application/octet-stream");
      http.addHeader("Accept-Encoding", "identity");
      http.addHeader("Connection", "close");

      const int code = http.GET();
      const int responseSize = http.getSize();
      Serial.printf("[OTA] download HTTP=%d contentLength=%d expected=%d\n",
                    code, responseSize, firmwareSize);
      otaLogMemory("download_connected");

      if (code <= 0) {
        failureReason = String("firmware_transport_") + code;
      } else if (code != HTTP_CODE_OK) {
        failureReason = String("firmware_http_") + code;
      } else if (responseSize > 0 && responseSize != firmwareSize) {
        failureReason =
            String("firmware_size_changed_") + responseSize + "_expected_" + firmwareSize;
      } else {
        WiFiClient* stream = http.getStreamPtr();
        if (!stream) {
          failureReason = "firmware_stream_unavailable";
        } else {
          const size_t written = Update.writeStream(*stream);
          Serial.printf("[OTA] stream finished written=%u expected=%d updateError=%u\n",
                        (unsigned)written, firmwareSize, (unsigned)Update.getError());

          if (written != (size_t)firmwareSize) {
            failureReason =
                String("firmware_incomplete_") + (unsigned)written + "_of_" + firmwareSize;
          } else if (Update.hasError()) {
            failureReason = String("update_write_") + Update.errorString();
          } else if (!Update.isFinished()) {
            failureReason = "update_not_finished";
          } else if (!Update.end()) {
            failureReason = String("update_end_") + Update.errorString();
          } else {
            updateOk = true;
          }
        }
      }

      http.end();
      client.stop();
    }
  }

  g_otaUpdateStreaming = false;
  otaLogMemory("after_update");

  if (!updateOk) {
    if (Update.isRunning()) Update.abort();
    if (failureReason.isEmpty()) failureReason = "ota_update_failed";
    Serial.printf("[OTA] FAILED: %s\n", failureReason.c_str());
    otaSetFailure(requestId, failureReason);
    return false;
  }

  Serial.println("[OTA] firmware written successfully");
  if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
    if (g_otaStatus.requestId == requestId) {
      g_otaStatus.percent = 100;
      g_otaStatus.currentBytes = firmwareSize;
      g_otaStatus.totalBytes = firmwareSize;
      g_otaStatus.message = "updated_restarting";
      g_otaStatus.state = OtaJobState::Done;
      g_otaRestartAtMs = millis() + 2500UL;
    }
    xSemaphoreGive(g_otaMutex);
  }
  return true;
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
  return otaQueue(OtaJobType::Update, dryRun, requestId, errorOut);
}

static String otaStatusJsonFor(OtaJobType expectedType, uint32_t requestId) {
  otaInit();
  if (!g_otaMutex) return "{\"ok\":false,\"error\":\"handoff_unavailable\"}";
  if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) != pdTRUE)
    return "{\"ok\":false,\"error\":\"handoff_busy\"}";

  const bool matches = requestId != 0 && g_otaStatus.requestId == requestId && g_otaStatus.type == expectedType;
  if (!matches) {
    xSemaphoreGive(g_otaMutex);
    return "{\"ok\":false,\"error\":\"request_not_found\"}";
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
    Serial.println("[OTA] Restarting after successful update");
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
