#include "ota.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_err.h"

// Arduino-ESP32 2.0.17 also ships a WiFiClientSecure-local esp_crt_bundle.h
// whose attach wrapper requires a user-supplied bundle to be
// initialized first. ESP-IDF 4.4's precompiled mbedTLS component still exports
// the native esp_crt_bundle_attach(), which automatically initializes the
// framework-embedded certificate bundle on first use. Declare that native C
// symbol explicitly so the Arduino-local header cannot shadow it.
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);
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
static constexpr int OTA_NATIVE_HTTP_BUFFER_BYTES = 8192;
static constexpr int OTA_NATIVE_BEGIN_MAX_ATTEMPTS = 2;
static constexpr uint32_t OTA_NATIVE_BEGIN_RETRY_DELAY_MS = 750;

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

// OTA progress is published as simple scalar snapshots. The native ESP-IDF
// esp_https_ota_perform() API returns after each HTTP read, so status updates
// and scheduler yields happen between transfer steps rather than inside a
// blocking legacy Arduino stream callback.
static volatile bool g_otaUpdateStreaming = false;
static volatile int g_otaStreamPercent = 0;
static volatile int g_otaStreamCurrent = 0;
static volatile int g_otaStreamTotal = 0;
static int g_otaKnownFirmwareSize = -1;
static String g_otaKnownFirmwareUrl;
static String g_otaKnownFirmwareVersion;
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

static String otaEspError(const char* prefix, esp_err_t err) {
  String out = prefix ? prefix : "esp_error";
  out += "_";
  const char* name = esp_err_to_name(err);
  if (name && *name) {
    out += name;
  } else {
    out += "0x";
    out += String((uint32_t)err, HEX);
  }
  return out;
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
  // and an older "latest" release can never silently downgrade a newer local
  // firmware.
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

  const String firmwareUrl = g_otaKnownFirmwareUrl;
  const String targetVersion = g_otaKnownFirmwareVersion;
  const int expectedSize = g_otaKnownFirmwareSize;
  const String userAgent = String("GrowTent-ESP32/") + FW_VERSION;

  Serial.println("[OTA] native ESP-IDF HTTPS OTA requested");
  Serial.printf("[OTA] target=%s expectedSize=%d workerStack=%u core=%d\n",
                targetVersion.c_str(), expectedSize,
                (unsigned)OTA_WORKER_STACK_BYTES, xPortGetCoreID());
  otaLogMemory("native_preflight");

  otaSetProgress(requestId, 0, 0, expectedSize, "connecting");
  g_otaStreamPercent = 0;
  g_otaStreamCurrent = 0;
  g_otaStreamTotal = expectedSize;
  g_otaUpdateStreaming = true;

  // ESP-IDF 4.4 native OTA:
  // - verifies GitHub/redirect hosts with the built-in full CA bundle
  // - follows redirects internally
  // - writes through ESP-IDF esp_ota_write(), not the legacy Arduino stream path
  // - esp_https_ota_perform() returns after every HTTP read operation
  esp_http_client_config_t httpConfig = {};
  httpConfig.url = firmwareUrl.c_str();
  httpConfig.user_agent = userAgent.c_str();
  httpConfig.timeout_ms = 30000;
  httpConfig.disable_auto_redirect = false;
  httpConfig.max_redirection_count = 8;
  httpConfig.transport_type = HTTP_TRANSPORT_OVER_SSL;
  httpConfig.buffer_size = OTA_NATIVE_HTTP_BUFFER_BYTES;
  httpConfig.buffer_size_tx = OTA_NATIVE_HTTP_BUFFER_BYTES;
  httpConfig.is_async = false;
  httpConfig.use_global_ca_store = false;
  httpConfig.skip_cert_common_name_check = false;
  httpConfig.crt_bundle_attach = esp_crt_bundle_attach;
  httpConfig.keep_alive_enable = false;

  esp_https_ota_config_t otaConfig = {};
  otaConfig.http_config = &httpConfig;
  otaConfig.http_client_init_cb = nullptr;
  otaConfig.bulk_flash_erase = false;
  otaConfig.partial_http_download = false;
  otaConfig.max_http_request_size = 0;

  esp_https_ota_handle_t handle = nullptr;
  esp_err_t err = ESP_FAIL;

  // GitHub release downloads currently redirect to a long, short-lived signed
  // release-assets URL. ESP-IDF/GitHub have documented HTTP 618 failures when
  // redirect headers are handled with small client buffers. Use 8 KiB RX/TX
  // buffers and allow one complete begin retry so a failed redirect always gets
  // a freshly signed URL instead of reusing stale connection state.
  for (int attempt = 1; attempt <= OTA_NATIVE_BEGIN_MAX_ATTEMPTS; ++attempt) {
    handle = nullptr;
    Serial.printf("[OTA] native begin attempt=%d/%d rxBuf=%d txBuf=%d\n",
                  attempt, OTA_NATIVE_BEGIN_MAX_ATTEMPTS,
                  httpConfig.buffer_size, httpConfig.buffer_size_tx);

    err = esp_https_ota_begin(&otaConfig, &handle);
    if (err == ESP_OK && handle != nullptr) {
      break;
    }

    if (handle != nullptr) {
      esp_https_ota_abort(handle);
      handle = nullptr;
    }

    otaLogMemory("native_begin_attempt_failed");
    Serial.printf("[OTA] native begin attempt=%d failed: %s\n",
                  attempt, esp_err_to_name(err));

    if (attempt < OTA_NATIVE_BEGIN_MAX_ATTEMPTS) {
      Serial.printf("[OTA] retrying native begin with fresh GitHub redirect in %u ms\n",
                    (unsigned)OTA_NATIVE_BEGIN_RETRY_DELAY_MS);
      vTaskDelay(pdMS_TO_TICKS(OTA_NATIVE_BEGIN_RETRY_DELAY_MS));
    }
  }

  if (err != ESP_OK || handle == nullptr) {
    g_otaUpdateStreaming = false;
    otaLogMemory("native_begin_failed");
    const String reason = otaEspError("esp_https_ota_begin", err);
    Serial.printf("[OTA] FAILED: %s\n", reason.c_str());
    otaSetFailure(requestId, reason);
    return false;
  }

  otaLogMemory("native_connected");

  int imageSize = esp_https_ota_get_image_size(handle);
  if (imageSize <= 0) imageSize = expectedSize;

  Serial.printf("[OTA] native connection established imageSize=%d expected=%d\n",
                imageSize, expectedSize);

  if (imageSize > 0 && expectedSize > 0 && imageSize != expectedSize) {
    Serial.printf("[OTA] size mismatch imageSize=%d expected=%d\n",
                  imageSize, expectedSize);
    esp_https_ota_abort(handle);
    g_otaUpdateStreaming = false;
    otaSetFailure(requestId,
                  String("firmware_size_changed_") + imageSize +
                  "_expected_" + expectedSize);
    return false;
  }

  const int progressTotal = imageSize > 0 ? imageSize : expectedSize;
  g_otaStreamTotal = progressTotal;
  otaSetProgress(requestId, 0, 0, progressTotal, "download_started");

  int lastPercentLogged = -1;
  esp_err_t performResult = ESP_ERR_HTTPS_OTA_IN_PROGRESS;

  while (performResult == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
    performResult = esp_https_ota_perform(handle);

    int current = esp_https_ota_get_image_len_read(handle);
    if (current < 0) current = 0;

    int total = esp_https_ota_get_image_size(handle);
    if (total <= 0) total = progressTotal;

    int percent = 0;
    if (total > 0) {
      percent = constrain((int)((100LL * current) / total), 0, 100);
    }

    g_otaStreamPercent = percent;
    g_otaStreamCurrent = current;
    g_otaStreamTotal = total;

    // We are between native HTTP read steps here, so it is safe to update the
    // shared UI status and explicitly yield the worker task.
    otaSetProgress(requestId, percent, current, total, "downloading");

    if (lastPercentLogged < 0 || percent == 100 || percent >= lastPercentLogged + 5) {
      lastPercentLogged = percent;
      Serial.printf("[OTA] Progress: %d%% (%d / %d) perform=%s\n",
                    percent, current, total, esp_err_to_name(performResult));
    }

    if (performResult == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  if (performResult != ESP_OK) {
    const String reason = otaEspError("esp_https_ota_perform", performResult);
    Serial.printf("[OTA] FAILED during perform: %s\n", reason.c_str());
    esp_https_ota_abort(handle);
    g_otaUpdateStreaming = false;
    otaLogMemory("native_perform_failed");
    otaSetFailure(requestId, reason);
    return false;
  }

  if (!esp_https_ota_is_complete_data_received(handle)) {
    Serial.println("[OTA] FAILED: incomplete native OTA image");
    esp_https_ota_abort(handle);
    g_otaUpdateStreaming = false;
    otaSetFailure(requestId, "esp_https_ota_incomplete_data");
    return false;
  }

  otaSetProgress(requestId, 100, progressTotal, progressTotal, "finalizing");
  err = esp_https_ota_finish(handle);
  handle = nullptr;
  g_otaUpdateStreaming = false;

  if (err != ESP_OK) {
    const String reason = otaEspError("esp_https_ota_finish", err);
    Serial.printf("[OTA] FAILED during finish: %s\n", reason.c_str());
    otaLogMemory("native_finish_failed");
    otaSetFailure(requestId, reason);
    return false;
  }

  otaLogMemory("native_success");
  Serial.printf("[OTA] native ESP-IDF OTA complete target=%s bytes=%d\n",
                targetVersion.c_str(), progressTotal);

  if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
    if (g_otaStatus.requestId == requestId) {
      g_otaStatus.percent = 100;
      g_otaStatus.currentBytes = progressTotal;
      g_otaStatus.totalBytes = progressTotal;
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
