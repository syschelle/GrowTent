#include "ota.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
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
static constexpr uint32_t OTA_TRANSPORT_CONNECT_TIMEOUT_MS = 5000UL;
static constexpr uint32_t OTA_TRANSPORT_HTTP_TIMEOUT_MS = 30000UL;
static constexpr uint32_t OTA_TRANSPORT_EXCLUSIVE_REBOOT_DELAY_MS = 8000UL;

enum class OtaJobType : uint8_t { None, Check, Update, TransportTest };
enum class OtaJobState : uint8_t { Idle, Queued, Running, Done, Failed };

struct OtaRuntimeStatus {
  OtaJobType type = OtaJobType::None;
  OtaJobState state = OtaJobState::Idle;
  uint32_t requestId = 0;
  bool dryRun = false;
  bool transportExclusive = false;
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


struct OtaTransportMemorySnapshot {
  uint32_t freeHeap = 0;
  uint32_t maxAlloc = 0;
  uint32_t internalFree = 0;
  uint32_t internalLargest = 0;
};

struct OtaTransportDiag {
  bool exclusive = false;
  String firmwareHost;
  String firmwareIp;
  bool dnsFirmwareOk = false;
  uint32_t dnsFirmwareMs = 0;

  bool tlsFirmwareOk = false;
  uint32_t tlsFirmwareMs = 0;
  int tlsFirmwareError = 0;
  String tlsFirmwareErrorText;

  int releaseHttpCode = 0;
  uint32_t releaseHttpMs = 0;
  int releaseContentLength = -1;
  int releaseTlsError = 0;
  String releaseTlsErrorText;

  String redirectHost;
  String redirectIp;
  bool dnsRedirectOk = false;
  uint32_t dnsRedirectMs = 0;

  bool tlsRedirectOk = false;
  uint32_t tlsRedirectMs = 0;
  int tlsRedirectError = 0;
  String tlsRedirectErrorText;

  int assetHttpCode = 0;
  uint32_t assetHttpMs = 0;
  int assetContentLength = -1;
  int assetTlsError = 0;
  String assetTlsErrorText;

  bool coldPreflightRan = false;
  uint32_t coldPreflightMs = 0;
  int coldPreflightHttpCode = 0;
  int coldPreflightContentLength = -1;
  int coldPreflightTlsError = 0;
  String coldPreflightTlsErrorText;
  size_t coldPreflightMd5Chars = 0;
  size_t coldPreflightSha256Chars = 0;
  OtaTransportMemorySnapshot coldBeforeHashes;
  OtaTransportMemorySnapshot coldAfterMd5;
  OtaTransportMemorySnapshot coldAfterSha256;
  OtaTransportMemorySnapshot coldAfterHeaders;
  OtaTransportMemorySnapshot coldAfterRequest;

  bool httpUpdatePreflightRan = false;
  uint32_t httpUpdatePreflightMs = 0;
  int httpUpdatePreflightHttpCode = 0;
  int httpUpdatePreflightContentLength = -1;
  int httpUpdatePreflightTlsError = 0;
  String httpUpdatePreflightTlsErrorText;
  size_t httpUpdatePreflightMd5Chars = 0;
  size_t httpUpdatePreflightSha256Chars = 0;
  OtaTransportMemorySnapshot beforeHashes;
  OtaTransportMemorySnapshot afterMd5;
  OtaTransportMemorySnapshot afterSha256;
  OtaTransportMemorySnapshot afterHeaders;
  OtaTransportMemorySnapshot afterPreflightRequest;

  String errorStage;
  OtaTransportMemorySnapshot before;
  OtaTransportMemorySnapshot ready;
  OtaTransportMemorySnapshot after;
};

static SemaphoreHandle_t g_otaMutex = nullptr;
static OtaRuntimeStatus g_otaStatus;
static OtaTerminalSnapshot g_otaLastCheckStatus;
static OtaTerminalSnapshot g_otaLastUpdateStatus;
static OtaTransportDiag g_otaTransportDiag;
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
static bool runOtaTransportTest(uint32_t requestId, bool exclusiveMode);

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
  // application enters exclusive maintenance mode while :8080 remains alive.
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


static OtaTransportMemorySnapshot otaTransportMemorySnapshot() {
  OtaTransportMemorySnapshot m;
  m.freeHeap = ESP.getFreeHeap();
  m.maxAlloc = ESP.getMaxAllocHeap();
  m.internalFree =
      (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  m.internalLargest =
      (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return m;
}

static const char* otaJobTypeName(OtaJobType type) {
  switch (type) {
    case OtaJobType::Check: return "check";
    case OtaJobType::Update: return "update";
    case OtaJobType::TransportTest: return "transport-test";
    default: return "invalid";
  }
}

static bool otaParseHttpsHost(const String& url, String& hostOut, uint16_t& portOut) {
  hostOut = "";
  portOut = 443;
  const String prefix = "https://";
  if (!url.startsWith(prefix)) return false;

  const int authorityStart = prefix.length();
  int authorityEnd = url.indexOf('/', authorityStart);
  if (authorityEnd < 0) authorityEnd = url.length();
  String authority = url.substring(authorityStart, authorityEnd);
  authority.trim();
  if (authority.isEmpty()) return false;

  const int colon = authority.lastIndexOf(':');
  if (colon > 0) {
    const String portText = authority.substring(colon + 1);
    const int parsed = portText.toInt();
    if (parsed <= 0 || parsed > 65535) return false;
    portOut = (uint16_t)parsed;
    authority = authority.substring(0, colon);
  }

  authority.trim();
  if (authority.isEmpty()) return false;
  hostOut = authority;
  return true;
}

static bool otaTransportDnsProbe(const String& host, String& ipOut, uint32_t& elapsedMs) {
  IPAddress ip;
  const uint32_t started = millis();
  const bool ok = WiFi.hostByName(host.c_str(), ip);
  elapsedMs = (uint32_t)(millis() - started);
  ipOut = ok ? ip.toString() : "";
  Serial.printf("[OTA TEST] DNS host=%s ok=%s ip=%s elapsedMs=%u\n",
                host.c_str(), ok ? "true" : "false",
                ok ? ipOut.c_str() : "-",
                (unsigned)elapsedMs);
  return ok;
}

static bool otaTransportTlsProbe(const String& host,
                                 uint16_t port,
                                 uint32_t& elapsedMs,
                                 int& tlsError,
                                 String& tlsErrorText) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);

  const uint32_t started = millis();
  const bool ok = client.connect(host.c_str(), port, (int32_t)OTA_TRANSPORT_CONNECT_TIMEOUT_MS) == 1;
  elapsedMs = (uint32_t)(millis() - started);

  char errBuf[160] = {};
  tlsError = client.lastError(errBuf, sizeof(errBuf));
  tlsErrorText = (tlsError != 0 && errBuf[0]) ? String(errBuf) : String();
  client.stop();

  Serial.printf("[OTA TEST] TLS host=%s port=%u ok=%s elapsedMs=%u tlsError=%d text=%s\n",
                host.c_str(), (unsigned)port,
                ok ? "true" : "false",
                (unsigned)elapsedMs,
                tlsError,
                tlsErrorText.length() ? tlsErrorText.c_str() : "-");
  return ok;
}

struct OtaTransportHttpProbeResult {
  int code = 0;
  uint32_t elapsedMs = 0;
  int contentLength = -1;
  int tlsError = 0;
  String tlsErrorText;
  String location;
};

static OtaTransportHttpProbeResult otaTransportHttpGetHeadersOnly(const String& url,
                                                                  bool collectLocation) {
  OtaTransportHttpProbeResult result;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);

  HTTPClient http;
  http.setConnectTimeout(OTA_TRANSPORT_CONNECT_TIMEOUT_MS);
  http.setTimeout(OTA_TRANSPORT_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);

  if (!http.begin(client, url)) {
    result.code = 0;
    result.tlsErrorText = "http_begin_failed";
    return result;
  }

  if (collectLocation) {
    const char* headerKeys[] = {"Location"};
    http.collectHeaders(headerKeys, 1);
  }

  http.addHeader("User-Agent", String("GrowTent-ESP32/") + FW_VERSION);
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Connection", "close");

  const uint32_t started = millis();
  // GET is intentional: it reproduces HTTPUpdate's request method. We stop
  // immediately after response headers and never read the firmware body.
  result.code = http.GET();
  result.elapsedMs = (uint32_t)(millis() - started);
  result.contentLength = http.getSize();
  if (collectLocation && result.code > 0) {
    result.location = http.header("Location");
  }

  char errBuf[160] = {};
  result.tlsError = client.lastError(errBuf, sizeof(errBuf));
  if (result.tlsError != 0 && errBuf[0]) result.tlsErrorText = errBuf;

  http.end();
  return result;
}

static void otaTransportPublishTerminal(uint32_t requestId,
                                        const OtaTransportDiag& diag,
                                        bool success,
                                        const String& error,
                                        const char* message) {
  if (g_otaMutex && xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
    if (g_otaStatus.requestId == requestId &&
        g_otaStatus.type == OtaJobType::TransportTest) {
      g_otaTransportDiag = diag;
      g_otaStatus.percent = 100;
      g_otaStatus.message = message ? message : (success ? "transport_test_complete" : "transport_test_failed");
      g_otaStatus.error = error;
      g_otaStatus.state = success ? OtaJobState::Done : OtaJobState::Failed;
    }
    xSemaphoreGive(g_otaMutex);
  }

  if (diag.exclusive && g_otaRestartAtMs == 0) {
    Serial.printf("[OTA TEST] exclusive diagnostic %s; rebooting existing firmware in %u ms\n",
                  success ? "complete" : "failed",
                  (unsigned)OTA_TRANSPORT_EXCLUSIVE_REBOOT_DELAY_MS);
    g_otaRestartAfterSuccessfulUpdate = false;
    g_otaRestartAtMs = millis() + OTA_TRANSPORT_EXCLUSIVE_REBOOT_DELAY_MS;
  }
}

static String otaTransportSketchSha256() {
  const size_t HASH_LEN = 32;
  uint8_t sha256[HASH_LEN] = {0};

  if (esp_partition_get_sha256(esp_ota_get_running_partition(), sha256) != 0) {
    return String();
  }

  char buffer[2 * HASH_LEN + 1];
  for (size_t i = 0; i < HASH_LEN; ++i) {
    uint8_t nibble = (sha256[i] & 0xF0) >> 4;
    buffer[2 * i] = nibble < 10 ? char(nibble + '0') : char(nibble - 10 + 'A');
    nibble = sha256[i] & 0x0F;
    buffer[2 * i + 1] = nibble < 10 ? char(nibble + '0') : char(nibble - 10 + 'A');
  }
  buffer[2 * HASH_LEN] = '\0';
  return String(buffer);
}

struct OtaHttpUpdatePreflightResult {
  bool ran = false;
  uint32_t ms = 0;
  int httpCode = 0;
  int contentLength = -1;
  int tlsError = 0;
  String tlsErrorText;
  size_t md5Chars = 0;
  size_t sha256Chars = 0;
  OtaTransportMemorySnapshot beforeHashes;
  OtaTransportMemorySnapshot afterMd5;
  OtaTransportMemorySnapshot afterSha256;
  OtaTransportMemorySnapshot afterHeaders;
  OtaTransportMemorySnapshot afterRequest;
};

static bool otaRunHttpUpdatePreflight(const String& firmwareUrl,
                                      OtaHttpUpdatePreflightResult& result,
                                      const char* label) {
  result.ran = true;
  result.beforeHashes = otaTransportMemorySnapshot();

  const String sketchMd5 = ESP.getSketchMD5();
  result.md5Chars = sketchMd5.length();
  result.afterMd5 = otaTransportMemorySnapshot();

  const String sketchSha256 = otaTransportSketchSha256();
  result.sha256Chars = sketchSha256.length();
  result.afterSha256 = otaTransportMemorySnapshot();

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);

  HTTPClient http;
  if (!http.begin(client, firmwareUrl)) {
    result.tlsErrorText = "http_begin_failed";
    return false;
  }

  http.useHTTP10(true);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setUserAgent("ESP32-http-Update");
  http.addHeader("Cache-Control", "no-cache");
  http.addHeader("x-ESP32-STA-MAC", WiFi.macAddress());
  http.addHeader("x-ESP32-AP-MAC", WiFi.softAPmacAddress());
  http.addHeader("x-ESP32-free-space", String(ESP.getFreeSketchSpace()));
  http.addHeader("x-ESP32-sketch-size", String(ESP.getSketchSize()));

  if (sketchMd5.length() != 0) {
    http.addHeader("x-ESP32-sketch-md5", sketchMd5);
  }
  if (sketchSha256.length() != 0) {
    http.addHeader("x-ESP32-sketch-sha256", sketchSha256);
  }

  http.addHeader("x-ESP32-chip-size", String(ESP.getFlashChipSize()));
  http.addHeader("x-ESP32-sdk-version", ESP.getSdkVersion());
  http.addHeader("x-ESP32-mode", "sketch");

  const char* headerKeys[] = {"x-MD5"};
  http.collectHeaders(headerKeys, 1);

  result.afterHeaders = otaTransportMemorySnapshot();

  const uint32_t started = millis();
  result.httpCode = http.GET();
  result.ms = (uint32_t)(millis() - started);
  result.contentLength = http.getSize();

  char errBuf[160] = {};
  result.tlsError = client.lastError(errBuf, sizeof(errBuf));
  if (result.tlsError != 0 && errBuf[0]) {
    result.tlsErrorText = errBuf;
  }

  http.end();
  result.afterRequest = otaTransportMemorySnapshot();

  Serial.printf(
      "[OTA TEST] %s code=%d contentLength=%d elapsedMs=%u "
      "md5Chars=%u sha256Chars=%u tlsError=%d text=%s\n",
      label ? label : "HTTPUPDATE PREFLIGHT",
      result.httpCode,
      result.contentLength,
      (unsigned)result.ms,
      (unsigned)result.md5Chars,
      (unsigned)result.sha256Chars,
      result.tlsError,
      result.tlsErrorText.length() ? result.tlsErrorText.c_str() : "-");

  return result.httpCode > 0;
}

static void otaStorePreflightResult(const OtaHttpUpdatePreflightResult& src,
                                    OtaTransportDiag& diag,
                                    bool cold) {
  if (cold) {
    diag.coldPreflightRan = src.ran;
    diag.coldPreflightMs = src.ms;
    diag.coldPreflightHttpCode = src.httpCode;
    diag.coldPreflightContentLength = src.contentLength;
    diag.coldPreflightTlsError = src.tlsError;
    diag.coldPreflightTlsErrorText = src.tlsErrorText;
    diag.coldPreflightMd5Chars = src.md5Chars;
    diag.coldPreflightSha256Chars = src.sha256Chars;
    diag.coldBeforeHashes = src.beforeHashes;
    diag.coldAfterMd5 = src.afterMd5;
    diag.coldAfterSha256 = src.afterSha256;
    diag.coldAfterHeaders = src.afterHeaders;
    diag.coldAfterRequest = src.afterRequest;
  } else {
    diag.httpUpdatePreflightRan = src.ran;
    diag.httpUpdatePreflightMs = src.ms;
    diag.httpUpdatePreflightHttpCode = src.httpCode;
    diag.httpUpdatePreflightContentLength = src.contentLength;
    diag.httpUpdatePreflightTlsError = src.tlsError;
    diag.httpUpdatePreflightTlsErrorText = src.tlsErrorText;
    diag.httpUpdatePreflightMd5Chars = src.md5Chars;
    diag.httpUpdatePreflightSha256Chars = src.sha256Chars;
    diag.beforeHashes = src.beforeHashes;
    diag.afterMd5 = src.afterMd5;
    diag.afterSha256 = src.afterSha256;
    diag.afterHeaders = src.afterHeaders;
    diag.afterPreflightRequest = src.afterRequest;
  }
}

static bool runOtaTransportTest(uint32_t requestId, bool exclusiveMode) {
  OtaTransportDiag diag;
  diag.exclusive = exclusiveMode;
  diag.before = otaTransportMemorySnapshot();

  if (WiFi.status() != WL_CONNECTED) {
    diag.errorStage = "wifi";
    otaTransportPublishTerminal(requestId, diag, false, "wifi_not_connected", "transport_test_failed");
    return false;
  }

  // Reuse the exact version-pinned target selected by the normal OTA check.
  const uint32_t targetAgeMs = (uint32_t)(millis() - g_otaKnownFirmwareSizeAtMs);
  if (g_otaKnownFirmwareUrl.isEmpty() ||
      g_otaKnownFirmwareVersion.isEmpty() ||
      g_otaKnownFirmwareSizeAtMs == 0 ||
      targetAgeMs > 10UL * 60UL * 1000UL) {
    diag.errorStage = "target";
    otaTransportPublishTerminal(requestId, diag, false, "update_check_required", "transport_test_failed");
    return false;
  }

  const String firmwareUrl = g_otaKnownFirmwareUrl;

  uint16_t firmwarePort = 443;
  if (!otaParseHttpsHost(firmwareUrl, diag.firmwareHost, firmwarePort)) {
    diag.errorStage = "firmware_url";
    otaTransportPublishTerminal(requestId, diag, false, "invalid_firmware_url", "transport_test_failed");
    return false;
  }

  Serial.printf("[OTA TEST] begin mode=%s host=%s targetVersion=%s\n",
                exclusiveMode ? "exclusive" : "normal",
                diag.firmwareHost.c_str(),
                g_otaKnownFirmwareVersion.c_str());
  otaSetProgress(requestId, 5, 0, 0, exclusiveMode ? "preparing_exclusive_transport_test" : "transport_test_start");

  if (exclusiveMode) {
    if (!otaWaitForExclusiveMaintenance(requestId)) {
      // otaWaitForExclusiveMaintenance already published the failure/reboot path.
      return false;
    }
  }

  diag.ready = otaTransportMemorySnapshot();
  otaLogMemory(exclusiveMode ? "transport_exclusive_ready" : "transport_normal_ready");

  if (exclusiveMode) {
    otaSetProgress(requestId, 8, 0, 0, "cold_httpupdate_preflight");

    OtaHttpUpdatePreflightResult cold;
    if (!otaRunHttpUpdatePreflight(firmwareUrl, cold, "COLD HTTPUPDATE PREFLIGHT")) {
      otaStorePreflightResult(cold, diag, true);
      diag.errorStage = "cold_httpupdate_preflight";
      diag.after = otaTransportMemorySnapshot();
      otaTransportPublishTerminal(
          requestId, diag, false,
          String("cold_httpupdate_preflight_") + cold.httpCode,
          "transport_test_failed");
      return false;
    }

    otaStorePreflightResult(cold, diag, true);

    if (cold.httpCode != HTTP_CODE_OK) {
      diag.errorStage = "cold_httpupdate_preflight_http";
      diag.after = otaTransportMemorySnapshot();
      otaTransportPublishTerminal(
          requestId, diag, false,
          String("cold_httpupdate_preflight_http_") + cold.httpCode,
          "transport_test_failed");
      return false;
    }
  }

  otaSetProgress(requestId, 10, 0, 0, "dns_firmware_host");
  diag.dnsFirmwareOk =
      otaTransportDnsProbe(diag.firmwareHost, diag.firmwareIp, diag.dnsFirmwareMs);
  if (!diag.dnsFirmwareOk) {
    diag.errorStage = "dns_firmware_host";
    diag.after = otaTransportMemorySnapshot();
    otaTransportPublishTerminal(requestId, diag, false, "dns_firmware_failed", "transport_test_failed");
    return false;
  }

  otaSetProgress(requestId, 25, 0, 0, "tls_firmware_host");
  diag.tlsFirmwareOk =
      otaTransportTlsProbe(diag.firmwareHost, firmwarePort,
                           diag.tlsFirmwareMs,
                           diag.tlsFirmwareError,
                           diag.tlsFirmwareErrorText);
  if (!diag.tlsFirmwareOk) {
    diag.errorStage = "tls_firmware_host";
    diag.after = otaTransportMemorySnapshot();
    otaTransportPublishTerminal(requestId, diag, false, "tls_firmware_failed", "transport_test_failed");
    return false;
  }

  otaSetProgress(requestId, 40, 0, 0, "release_get_headers");
  const OtaTransportHttpProbeResult releaseProbe =
      otaTransportHttpGetHeadersOnly(firmwareUrl, true);
  diag.releaseHttpCode = releaseProbe.code;
  diag.releaseHttpMs = releaseProbe.elapsedMs;
  diag.releaseContentLength = releaseProbe.contentLength;
  diag.releaseTlsError = releaseProbe.tlsError;
  diag.releaseTlsErrorText = releaseProbe.tlsErrorText;

  Serial.printf(
      "[OTA TEST] RELEASE GET code=%d contentLength=%d elapsedMs=%u tlsError=%d text=%s\n",
      diag.releaseHttpCode,
      diag.releaseContentLength,
      (unsigned)diag.releaseHttpMs,
      diag.releaseTlsError,
      diag.releaseTlsErrorText.length() ? diag.releaseTlsErrorText.c_str() : "-");

  if (diag.releaseHttpCode <= 0) {
    diag.errorStage = "release_get";
    diag.after = otaTransportMemorySnapshot();
    otaTransportPublishTerminal(requestId, diag, false,
                                String("release_transport_") + diag.releaseHttpCode,
                                "transport_test_failed");
    return false;
  }

  const String redirectUrl = releaseProbe.location;
  uint16_t redirectPort = 443;
  if (redirectUrl.isEmpty() ||
      !otaParseHttpsHost(redirectUrl, diag.redirectHost, redirectPort)) {
    diag.errorStage = "release_redirect";
    diag.after = otaTransportMemorySnapshot();
    otaTransportPublishTerminal(requestId, diag, false,
                                String("release_redirect_missing_http_") + diag.releaseHttpCode,
                                "transport_test_failed");
    return false;
  }

  // Never print or expose the signed redirect URL/JWT.
  Serial.printf("[OTA TEST] redirect host=%s (signed URL hidden)\n",
                diag.redirectHost.c_str());

  otaSetProgress(requestId, 55, 0, 0, "dns_redirect_host");
  diag.dnsRedirectOk =
      otaTransportDnsProbe(diag.redirectHost, diag.redirectIp, diag.dnsRedirectMs);
  if (!diag.dnsRedirectOk) {
    diag.errorStage = "dns_redirect_host";
    diag.after = otaTransportMemorySnapshot();
    otaTransportPublishTerminal(requestId, diag, false, "dns_redirect_failed", "transport_test_failed");
    return false;
  }

  otaSetProgress(requestId, 70, 0, 0, "tls_redirect_host");
  diag.tlsRedirectOk =
      otaTransportTlsProbe(diag.redirectHost, redirectPort,
                           diag.tlsRedirectMs,
                           diag.tlsRedirectError,
                           diag.tlsRedirectErrorText);
  if (!diag.tlsRedirectOk) {
    diag.errorStage = "tls_redirect_host";
    diag.after = otaTransportMemorySnapshot();
    otaTransportPublishTerminal(requestId, diag, false, "tls_redirect_failed", "transport_test_failed");
    return false;
  }

  otaSetProgress(requestId, 85, 0, 0, "asset_get_headers");
  const OtaTransportHttpProbeResult assetProbe =
      otaTransportHttpGetHeadersOnly(redirectUrl, false);
  diag.assetHttpCode = assetProbe.code;
  diag.assetHttpMs = assetProbe.elapsedMs;
  diag.assetContentLength = assetProbe.contentLength;
  diag.assetTlsError = assetProbe.tlsError;
  diag.assetTlsErrorText = assetProbe.tlsErrorText;
  diag.after = otaTransportMemorySnapshot();

  Serial.printf(
      "[OTA TEST] ASSET GET code=%d contentLength=%d elapsedMs=%u tlsError=%d text=%s\n",
      diag.assetHttpCode,
      diag.assetContentLength,
      (unsigned)diag.assetHttpMs,
      diag.assetTlsError,
      diag.assetTlsErrorText.length() ? diag.assetTlsErrorText.c_str() : "-");

  if (diag.assetHttpCode <= 0) {
    diag.errorStage = "asset_get";
    otaTransportPublishTerminal(requestId, diag, false,
                                String("asset_transport_") + diag.assetHttpCode,
                                "transport_test_failed");
    return false;
  }

  if (diag.assetHttpCode < 200 || diag.assetHttpCode >= 300) {
    diag.errorStage = "asset_http";
    otaTransportPublishTerminal(requestId, diag, false,
                                String("asset_http_") + diag.assetHttpCode,
                                "transport_test_failed");
    return false;
  }

  // Reproduce HTTPUpdate's hashing/header/request preflight without invoking
  // the updater or touching flash.
  otaSetProgress(requestId, 92, 0, 0, "httpupdate_preflight");
  OtaHttpUpdatePreflightResult warm;
  if (!otaRunHttpUpdatePreflight(firmwareUrl, warm, "HTTPUPDATE PREFLIGHT")) {
    otaStorePreflightResult(warm, diag, false);
    diag.errorStage = "httpupdate_preflight";
    diag.after = otaTransportMemorySnapshot();
    otaTransportPublishTerminal(
        requestId, diag, false,
        String("httpupdate_preflight_") + warm.httpCode,
        "transport_test_failed");
    return false;
  }

  otaStorePreflightResult(warm, diag, false);

  if (warm.httpCode != HTTP_CODE_OK) {
    diag.errorStage = "httpupdate_preflight_http";
    diag.after = otaTransportMemorySnapshot();
    otaTransportPublishTerminal(
        requestId, diag, false,
        String("httpupdate_preflight_http_") + warm.httpCode,
        "transport_test_failed");
    return false;
  }

  diag.after = otaTransportMemorySnapshot();
  otaTransportPublishTerminal(requestId, diag, true, "", "transport_test_complete");
  return true;
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
  bool transportExclusive = false;
  if (g_otaMutex && xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
    requestId = g_otaStatus.requestId;
    type = g_otaStatus.type;
    dryRun = g_otaStatus.dryRun;
    transportExclusive = g_otaStatus.transportExclusive;
    g_otaStatus.state = OtaJobState::Running;
    if (type == OtaJobType::Check) g_otaStatus.message = "checking";
    else if (type == OtaJobType::TransportTest) g_otaStatus.message = "transport_test_start";
    else g_otaStatus.message = "starting";
    xSemaphoreGive(g_otaMutex);
  }

  Serial.printf("[OTA WORKER] start type=%s id=%lu dryRun=%s exclusive=%s core=%d freeHeap=%u\n",
                otaJobTypeName(type),
                (unsigned long)requestId,
                dryRun ? "true" : "false",
                transportExclusive ? "true" : "false",
                xPortGetCoreID(),
                ESP.getFreeHeap());

  if (type == OtaJobType::Check) runOtaCheck(requestId, dryRun);
  else if (type == OtaJobType::Update) runOtaUpdate(requestId, dryRun);
  else if (type == OtaJobType::TransportTest) runOtaTransportTest(requestId, transportExclusive);
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
                otaJobTypeName(g_otaStatus.type),
                (unsigned long)g_otaStatus.requestId,
                otaStateName(g_otaStatus.state));
}

static bool otaQueue(OtaJobType type, bool dryRun, bool transportExclusive, uint32_t& requestId, String& errorOut) {
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
  g_otaStatus.transportExclusive = transportExclusive;
  if (type == OtaJobType::TransportTest) {
    g_otaTransportDiag = OtaTransportDiag();
    g_otaTransportDiag.exclusive = transportExclusive;
  }
  g_otaStatus.message = "queued";
  g_otaJobQueued = true;
  xSemaphoreGive(g_otaMutex);
  Serial.printf("[OTA QUEUE] type=%s id=%lu dryRun=%s exclusive=%s\n",
                otaJobTypeName(type),
                (unsigned long)requestId,
                dryRun ? "true" : "false",
                transportExclusive ? "true" : "false");
  return true;
}

bool otaQueueCheck(bool dryRun, uint32_t& requestId, String& errorOut) {
  return otaQueue(OtaJobType::Check, dryRun, false, requestId, errorOut);
}

bool otaQueueUpdate(bool dryRun, uint32_t& requestId, String& errorOut) {
  if (!dryRun && !g_deferredInitComplete) {
    requestId = 0;
    errorOut = "system_initializing";
    return false;
  }
  return otaQueue(OtaJobType::Update, dryRun, false, requestId, errorOut);
}


bool otaQueueTransportTest(bool exclusiveMode, uint32_t& requestId, String& errorOut) {
  if (!g_deferredInitComplete) {
    requestId = 0;
    errorOut = "system_initializing";
    return false;
  }

  const uint32_t targetAgeMs = (uint32_t)(millis() - g_otaKnownFirmwareSizeAtMs);
  if (g_otaKnownFirmwareUrl.isEmpty() ||
      g_otaKnownFirmwareVersion.isEmpty() ||
      g_otaKnownFirmwareSizeAtMs == 0 ||
      targetAgeMs > 10UL * 60UL * 1000UL) {
    requestId = 0;
    errorOut = "update_check_required";
    return false;
  }

  return otaQueue(OtaJobType::TransportTest, false, exclusiveMode, requestId, errorOut);
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

String otaTransportTestStatusJson(uint32_t requestId) {
  otaInit();
  if (!g_otaMutex) return "{\"ok\":false,\"error\":\"handoff_unavailable\"}";
  if (xSemaphoreTake(g_otaMutex, pdMS_TO_TICKS(250)) != pdTRUE)
    return "{\"ok\":false,\"error\":\"handoff_busy\"}";

  if (requestId == 0 ||
      g_otaStatus.requestId != requestId ||
      g_otaStatus.type != OtaJobType::TransportTest) {
    xSemaphoreGive(g_otaMutex);
    return "{\"ok\":false,\"error\":\"request_not_found\"}";
  }

  String json;
  json.reserve(1900);
  json = "{\"ok\":true,\"requestId\":" + String(g_otaStatus.requestId);
  json += ",\"state\":\"" + String(otaStateName(g_otaStatus.state)) + "\"";
  json += ",\"exclusive\":" + String(g_otaStatus.transportExclusive ? "true" : "false");
  json += ",\"percent\":" + String(g_otaStatus.percent);
  json += ",\"message\":\"" + otaJsonEscape(g_otaStatus.message) + "\"";
  if (g_otaStatus.error.length()) {
    json += ",\"error\":\"" + otaJsonEscape(g_otaStatus.error) + "\"";
  }

  if (otaStateIsTerminal(g_otaStatus.state)) {
    const OtaTransportDiag& d = g_otaTransportDiag;
    json += ",\"errorStage\":\"" + otaJsonEscape(d.errorStage) + "\"";

    json += ",\"firmwareHost\":\"" + otaJsonEscape(d.firmwareHost) + "\"";
    json += ",\"dnsFirmware\":{\"ok\":" + String(d.dnsFirmwareOk ? "true" : "false");
    json += ",\"ip\":\"" + otaJsonEscape(d.firmwareIp) + "\"";
    json += ",\"ms\":" + String(d.dnsFirmwareMs) + "}";

    json += ",\"tlsFirmware\":{\"ok\":" + String(d.tlsFirmwareOk ? "true" : "false");
    json += ",\"ms\":" + String(d.tlsFirmwareMs);
    json += ",\"error\":" + String(d.tlsFirmwareError);
    json += ",\"text\":\"" + otaJsonEscape(d.tlsFirmwareErrorText) + "\"}";

    json += ",\"releaseRequest\":{\"httpCode\":" + String(d.releaseHttpCode);
    json += ",\"ms\":" + String(d.releaseHttpMs);
    json += ",\"contentLength\":" + String(d.releaseContentLength);
    json += ",\"tlsError\":" + String(d.releaseTlsError);
    json += ",\"tlsText\":\"" + otaJsonEscape(d.releaseTlsErrorText) + "\"}";

    json += ",\"redirectHost\":\"" + otaJsonEscape(d.redirectHost) + "\"";
    json += ",\"dnsRedirect\":{\"ok\":" + String(d.dnsRedirectOk ? "true" : "false");
    json += ",\"ip\":\"" + otaJsonEscape(d.redirectIp) + "\"";
    json += ",\"ms\":" + String(d.dnsRedirectMs) + "}";

    json += ",\"tlsRedirect\":{\"ok\":" + String(d.tlsRedirectOk ? "true" : "false");
    json += ",\"ms\":" + String(d.tlsRedirectMs);
    json += ",\"error\":" + String(d.tlsRedirectError);
    json += ",\"text\":\"" + otaJsonEscape(d.tlsRedirectErrorText) + "\"}";

    json += ",\"assetRequest\":{\"httpCode\":" + String(d.assetHttpCode);
    json += ",\"ms\":" + String(d.assetHttpMs);
    json += ",\"contentLength\":" + String(d.assetContentLength);
    json += ",\"tlsError\":" + String(d.assetTlsError);
    json += ",\"tlsText\":\"" + otaJsonEscape(d.assetTlsErrorText) + "\"}";


    json += ",\"coldHttpUpdatePreflight\":{\"ran\":" + String(d.coldPreflightRan ? "true" : "false");
    json += ",\"httpCode\":" + String(d.coldPreflightHttpCode);
    json += ",\"ms\":" + String(d.coldPreflightMs);
    json += ",\"contentLength\":" + String(d.coldPreflightContentLength);
    json += ",\"tlsError\":" + String(d.coldPreflightTlsError);
    json += ",\"tlsText\":\"" + otaJsonEscape(d.coldPreflightTlsErrorText) + "\"";
    json += ",\"md5Chars\":" + String((unsigned)d.coldPreflightMd5Chars);
    json += ",\"sha256Chars\":" + String((unsigned)d.coldPreflightSha256Chars);
    json += "}";

    json += ",\"coldPreflightMemory\":{";
    json += "\"beforeHashes\":{\"freeHeap\":" + String(d.coldBeforeHashes.freeHeap);
    json += ",\"maxAlloc\":" + String(d.coldBeforeHashes.maxAlloc);
    json += ",\"internalFree\":" + String(d.coldBeforeHashes.internalFree);
    json += ",\"internalLargest\":" + String(d.coldBeforeHashes.internalLargest) + "}";
    json += ",\"afterMd5\":{\"freeHeap\":" + String(d.coldAfterMd5.freeHeap);
    json += ",\"maxAlloc\":" + String(d.coldAfterMd5.maxAlloc);
    json += ",\"internalFree\":" + String(d.coldAfterMd5.internalFree);
    json += ",\"internalLargest\":" + String(d.coldAfterMd5.internalLargest) + "}";
    json += ",\"afterSha256\":{\"freeHeap\":" + String(d.coldAfterSha256.freeHeap);
    json += ",\"maxAlloc\":" + String(d.coldAfterSha256.maxAlloc);
    json += ",\"internalFree\":" + String(d.coldAfterSha256.internalFree);
    json += ",\"internalLargest\":" + String(d.coldAfterSha256.internalLargest) + "}";
    json += ",\"afterHeaders\":{\"freeHeap\":" + String(d.coldAfterHeaders.freeHeap);
    json += ",\"maxAlloc\":" + String(d.coldAfterHeaders.maxAlloc);
    json += ",\"internalFree\":" + String(d.coldAfterHeaders.internalFree);
    json += ",\"internalLargest\":" + String(d.coldAfterHeaders.internalLargest) + "}";
    json += ",\"afterRequest\":{\"freeHeap\":" + String(d.coldAfterRequest.freeHeap);
    json += ",\"maxAlloc\":" + String(d.coldAfterRequest.maxAlloc);
    json += ",\"internalFree\":" + String(d.coldAfterRequest.internalFree);
    json += ",\"internalLargest\":" + String(d.coldAfterRequest.internalLargest) + "}}";

    json += ",\"httpUpdatePreflight\":{\"ran\":" + String(d.httpUpdatePreflightRan ? "true" : "false");
    json += ",\"httpCode\":" + String(d.httpUpdatePreflightHttpCode);
    json += ",\"ms\":" + String(d.httpUpdatePreflightMs);
    json += ",\"contentLength\":" + String(d.httpUpdatePreflightContentLength);
    json += ",\"tlsError\":" + String(d.httpUpdatePreflightTlsError);
    json += ",\"tlsText\":\"" + otaJsonEscape(d.httpUpdatePreflightTlsErrorText) + "\"";
    json += ",\"md5Chars\":" + String((unsigned)d.httpUpdatePreflightMd5Chars);
    json += ",\"sha256Chars\":" + String((unsigned)d.httpUpdatePreflightSha256Chars);
    json += "}";

    json += ",\"preflightMemory\":{";
    json += "\"beforeHashes\":{\"freeHeap\":" + String(d.beforeHashes.freeHeap);
    json += ",\"maxAlloc\":" + String(d.beforeHashes.maxAlloc);
    json += ",\"internalFree\":" + String(d.beforeHashes.internalFree);
    json += ",\"internalLargest\":" + String(d.beforeHashes.internalLargest) + "}";
    json += ",\"afterMd5\":{\"freeHeap\":" + String(d.afterMd5.freeHeap);
    json += ",\"maxAlloc\":" + String(d.afterMd5.maxAlloc);
    json += ",\"internalFree\":" + String(d.afterMd5.internalFree);
    json += ",\"internalLargest\":" + String(d.afterMd5.internalLargest) + "}";
    json += ",\"afterSha256\":{\"freeHeap\":" + String(d.afterSha256.freeHeap);
    json += ",\"maxAlloc\":" + String(d.afterSha256.maxAlloc);
    json += ",\"internalFree\":" + String(d.afterSha256.internalFree);
    json += ",\"internalLargest\":" + String(d.afterSha256.internalLargest) + "}";
    json += ",\"afterHeaders\":{\"freeHeap\":" + String(d.afterHeaders.freeHeap);
    json += ",\"maxAlloc\":" + String(d.afterHeaders.maxAlloc);
    json += ",\"internalFree\":" + String(d.afterHeaders.internalFree);
    json += ",\"internalLargest\":" + String(d.afterHeaders.internalLargest) + "}";
    json += ",\"afterRequest\":{\"freeHeap\":" + String(d.afterPreflightRequest.freeHeap);
    json += ",\"maxAlloc\":" + String(d.afterPreflightRequest.maxAlloc);
    json += ",\"internalFree\":" + String(d.afterPreflightRequest.internalFree);
    json += ",\"internalLargest\":" + String(d.afterPreflightRequest.internalLargest) + "}}";

    json += ",\"memory\":{";
    json += "\"before\":{\"freeHeap\":" + String(d.before.freeHeap);
    json += ",\"maxAlloc\":" + String(d.before.maxAlloc);
    json += ",\"internalFree\":" + String(d.before.internalFree);
    json += ",\"internalLargest\":" + String(d.before.internalLargest) + "}";
    json += ",\"ready\":{\"freeHeap\":" + String(d.ready.freeHeap);
    json += ",\"maxAlloc\":" + String(d.ready.maxAlloc);
    json += ",\"internalFree\":" + String(d.ready.internalFree);
    json += ",\"internalLargest\":" + String(d.ready.internalLargest) + "}";
    json += ",\"after\":{\"freeHeap\":" + String(d.after.freeHeap);
    json += ",\"maxAlloc\":" + String(d.after.maxAlloc);
    json += ",\"internalFree\":" + String(d.after.internalFree);
    json += ",\"internalLargest\":" + String(d.after.internalLargest) + "}}";
  }

  json += "}";
  xSemaphoreGive(g_otaMutex);
  return json;
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
