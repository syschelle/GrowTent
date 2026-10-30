// main.cpp (optimized fast boot)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#define ASYNCWEBSERVER_NO_GLOBAL_HTTP_METHODS
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <time.h>
#include <LittleFS.h>
#include <deque>
#include <cstring>
#include <cfloat>
#include <memory>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// global, functions, html code, js code and css code includes
#include "globals.h"
#include "function.h"
#include "helper.h"
#include "index_html.h"
#include "style_css.h"
#include "java_script.h"
#include "ota.h"

// Debug variable registry ("Variables" page)
#include "vars_registry.h"

// tasks
#include "task_Check_Sensor.h"
#include "task_CheckShellyStatus.h"
#include "task_Watering.h"

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  Serial.printf("\n*** STACK OVERFLOW in task: %s ***\n", pcTaskName ? pcTaskName : "(null)");
  Serial.flush();
  esp_restart();
}

Preferences preferences;
// ESPAsyncWebServer migration target. Port 8080 is the permanent application
// Port 8080 is the permanent application server. Port 80 is reserved only for
// the Async captive portal while Wi-Fi provisioning is active.
AsyncWebServer asyncServer(8080);
AsyncWebServer provisioningServer(80);
DNSServer provisioningDns;

// Phase 32: AP provisioning ownership. Port 80 is Async only while the captive
// portal is active; normal STA operation exposes only the permanent :8080 server.
static bool g_wifiProvisioningActive = false;
static bool g_wifiProvisioningServerStarted = false;
static bool g_wifiProvisioningFallbackFromStoredCredentials = false;
static volatile uint32_t g_wifiProvisioningFallbackAutoStops = 0;
static bool g_eraseWifiBeforeRestart = false;
static SemaphoreHandle_t g_wifiProvisioningMutex = nullptr;
static volatile bool g_wifiProvisioningBusy = false;
static volatile bool g_wifiScanRequested = false;
static bool g_wifiScanInProgress = false;
static uint32_t g_wifiLastScanMs = 0;
static String g_wifiScanJson = "[]";

enum class WifiProvisionState : uint8_t { Idle, Queued, Connecting, Connected, Failed };
struct WifiProvisionPending {
  bool pending = false;
  bool processing = false;
  uint32_t requestId = 0;
  String ssid;
  String password;
  uint32_t startedMs = 0;
};
static WifiProvisionPending g_wifiProvisionPending;
static WifiProvisionState g_wifiProvisionState = WifiProvisionState::Idle;
static uint32_t g_wifiProvisionNextRequestId = 0;
static uint32_t g_wifiProvisionLastRequestId = 0;
static String g_wifiProvisionSsid;
static String g_wifiProvisionError;
static String g_wifiProvisionIp;
static String g_wifiProvisionGateway;
static int g_wifiProvisionRssi = 0;
static bool g_wifiProvisionFinishRequested = false;
static volatile uint32_t g_wifiProvisionRequests = 0;
static volatile uint32_t g_wifiProvisionFailures = 0;
static volatile uint32_t g_wifiProvisionQueued = 0;
static volatile uint32_t g_wifiProvisionConnected = 0;

// define weblog buffer variable
std::deque<String> logBuffer;

// ShellySettings is still used as a global across runtime.h/task headers
ShellySettings shelly;

// BME280 and DS18B20 sensor
SensorReadings cur;
Targets target;

// Global OneWire + DallasTemperature instance for DS18B20
OneWire oneWire(DS18B20_PIN);

// Definition for the extern declared in runtime.h
DallasTemperature sensors(&oneWire);

// Task handle for sensor task
TaskHandle_t sensorTaskHandle = nullptr;

// Forward declarations
bool startSoftAP();

// -------------------- Grow Diary (CSV in LittleFS) --------------------
static const char* DIARY_PATH = "/growdiary.csv";

// -------------------- LittleFS recovery API --------------------
// OTA-safe: normal boot never formats LittleFS. Formatting is only possible
// through explicit user-triggered endpoints with a confirmation token.

// Parse "YYYY-MM-DD" -> time_t (local midnight). Returns 0 if invalid.
static time_t parseYmdToLocalMidnight(const String& ymd) {
  if (ymd.length() < 10) return 0;
  const int y = ymd.substring(0, 4).toInt();
  const int m = ymd.substring(5, 7).toInt();
  const int d = ymd.substring(8, 10).toInt();
  if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return 0;

  struct tm tmv {};
  tmv.tm_year = y - 1900;
  tmv.tm_mon  = m - 1;
  tmv.tm_mday = d;
  tmv.tm_hour = 0;
  tmv.tm_min  = 0;
  tmv.tm_sec  = 0;
  tmv.tm_isdst = -1; // let libc determine DST

  // mktime interprets tm as local time
  return mktime(&tmv);
}

static int daysSince(const String& startYmd, time_t nowLocal) {
  const time_t start = parseYmdToLocalMidnight(startYmd);
  if (start <= 0 || nowLocal <= 0) return -1;

  const double diffSec = difftime(nowLocal, start);
  const int diffDays = (int)floor(diffSec / 86400.0);
  return diffDays;
}

// CSV-safe: remove newlines and escape quotes by doubling them.
static String csvEscape(const String& in) {
  String s = in;
  s.replace("\r", " ");
  s.replace("\n", " ");
  s.replace("\"", "\"\"");
  return "\"" + s + "\"";
}

// CSV: split a line into fields (supports quotes and commas inside quoted fields)
static bool csvSplitLine(const String& line, String* out, int outMax, int& outCount) {
  outCount = 0;
  bool inQuotes = false;
  String cur;
  cur.reserve(line.length());

  for (size_t i = 0; i < (size_t)line.length(); i++) {
    const char c = line[i];
    if (c == '"') {
      // doubled quote inside quoted field -> literal quote
      if (inQuotes && (i + 1 < (size_t)line.length()) && line[i + 1] == '"') {
        cur += '"';
        i++;
      } else {
        inQuotes = !inQuotes;
      }
      continue;
    }
    if (c == ',' && !inQuotes) {
      if (outCount < outMax) out[outCount] = cur;
      outCount++;
      cur = "";
      continue;
    }
    cur += c;
  }

  if (outCount < outMax) out[outCount] = cur;
  outCount++;
  return outCount > 0;
}

// CSV: remove surrounding quotes already handled by csvSplitLine; trim and keep as-is
String csvFieldToString(const String& s) {
  String out = s;
  out.trim();
  return out;
}

// ---- Grow Diary storage helpers ----
// We store each diary row with a stable numeric id in column 0.
// New schema (8 fields):
//   id, tsLocal, phase, growDay, growWeek, phaseDay, phaseWeek, note
// Old schema (7 fields) without id is migrated on first access.

static uint32_t fnv1a32(const String& s) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < (size_t)s.length(); i++) {
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  return h;
}

static bool ensureDiaryHasId() {
  if (!ensureFsMounted()) return false;
  if (!LittleFS.exists(DIARY_PATH)) return true;

  File f = LittleFS.open(DIARY_PATH, FILE_READ);
  if (!f) return false;

  String first = f.readStringUntil('\n');
  first.trim();
  f.close();

  // Quick check: if first field looks like a numeric id and there are 8 fields, we're done.
  String fields[12]; int n = 0;
  int cnt = 0;
  if (!csvSplitLine(first, fields, 12, cnt)) return true; // empty file is fine

  // If file starts with a proper header "id,ts_local,...", it's already new-format.
  if (first.startsWith("id,")) return true;

  if (cnt >= 8) {
    const String id0 = csvFieldToString(fields[0]);
    bool allDigits = id0.length() > 0;
    for (size_t i = 0; i < (size_t)id0.length(); i++) {
      if (id0[i] < '0' || id0[i] > '9') { allDigits = false; break; }
    }
    if (allDigits) return true;
  }

  // Migrate old 7-field rows -> prepend id
  File in = LittleFS.open(DIARY_PATH, FILE_READ);
  if (!in) return false;

  const char* TMP = "/growdiary.tmp";
  if (LittleFS.exists(TMP)) LittleFS.remove(TMP);
  File out = LittleFS.open(TMP, FILE_WRITE);
  if (!out) { in.close(); return false; }

  uint32_t lineNo = 0;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;

    String flds[12]; int c = 0;
    if (!csvSplitLine(line, flds, 12, c) || c < 7) continue;

    // If this row already looks like new format (>=8 fields AND first field numeric) -> keep as-is
    if (c >= 8) {
      String id0 = csvFieldToString(flds[0]);
      bool allDigits = id0.length() > 0;
      for (size_t i = 0; i < (size_t)id0.length(); i++) {
        if (id0[i] < '0' || id0[i] > '9') { allDigits = false; break; }
      }
      if (allDigits) {
        out.println(line);
        continue;
      }
    }

    const String tsLocal = csvFieldToString(flds[0]);
    const String phase   = csvFieldToString(flds[1]);
    const String note    = csvFieldToString(flds[6]);

    uint32_t id = fnv1a32(tsLocal + "|" + phase + "|" + note + "|" + String(lineNo++));
    String row;
    row.reserve(line.length() + 16);
    row += String(id);
    row += ",";
    row += line; // rest of old row
    out.println(row);
  }
  in.close();
  out.close();

  // Replace old file
  LittleFS.remove(DIARY_PATH);
  return LittleFS.rename(TMP, DIARY_PATH);
}

static bool rewriteDiaryWithMutation(const String& idStr, bool doDelete, const String& newPhase, const String& newNote) {
  if (!ensureDiaryHasId()) return false;
  if (!LittleFS.exists(DIARY_PATH)) return false;

  File in = LittleFS.open(DIARY_PATH, FILE_READ);
  if (!in) return false;

  const char* TMP = "/growdiary.tmp";
  if (LittleFS.exists(TMP)) LittleFS.remove(TMP);
  File out = LittleFS.open(TMP, FILE_WRITE);
  if (!out) { in.close(); return false; }

  bool changed = false;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;

    String flds[12]; int c = 0;
    if (!csvSplitLine(line, flds, 12, c) || c < 8) {
      out.println(line);
      continue;
    }

    const String rid = csvFieldToString(flds[0]);
    if (rid != idStr) {
      out.println(line);
      continue;
    }

    // matched id
    if (doDelete) {
      changed = true;
      continue; // skip line
    }

    // Update: keep all columns except phase (col 2) and note (col 7)
    // Schema: 0 id, 1 tsLocal, 2 phase, 3 growDay, 4 growWeek, 5 phaseDay, 6 phaseWeek, 7 note
    String row;
    row.reserve(line.length() + 16);
    row += rid; row += ",";
    row += csvEscape(csvFieldToString(flds[1])); row += ","; // tsLocal (re-escaped to normalize)
    row += csvEscape(newPhase.length() ? newPhase : csvFieldToString(flds[2])); row += ",";
    row += csvFieldToString(flds[3]); row += ",";
    row += csvFieldToString(flds[4]); row += ",";
    row += csvFieldToString(flds[5]); row += ",";
    row += csvFieldToString(flds[6]); row += ",";
    row += csvEscape(newNote.length() ? newNote : csvFieldToString(flds[7]));
    out.println(row);
    changed = true;
  }

  in.close();
  out.close();

  if (!changed) {
    LittleFS.remove(TMP);
    return false;
  }

  LittleFS.remove(DIARY_PATH);
  return LittleFS.rename(TMP, DIARY_PATH);
}



// Parse local timestamp "YYYY-MM-DD HH:MM:SS" -> unix seconds (local time). Returns 0 on failure.
static time_t parseLocalTimestamp(const String& tsLocal) {
  if (tsLocal.length() < 19) return 0;
  const int y = tsLocal.substring(0, 4).toInt();
  const int m = tsLocal.substring(5, 7).toInt();
  const int d = tsLocal.substring(8, 10).toInt();
  const int hh = tsLocal.substring(11, 13).toInt();
  const int mm = tsLocal.substring(14, 16).toInt();
  const int ss = tsLocal.substring(17, 19).toInt();
  if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return 0;

  struct tm tmv {};
  tmv.tm_year = y - 1900;
  tmv.tm_mon  = m - 1;
  tmv.tm_mday = d;
  tmv.tm_hour = hh;
  tmv.tm_min  = mm;
  tmv.tm_sec  = ss;
  tmv.tm_isdst = -1;
  return mktime(&tmv);
}

// GET /api/diary/list  -> returns JSON list for UI
// POST /api/diary/add
// Accepts either JSON body {"note":"...","phase":"grow|flower|dry"} or form fields note=...&phase=...

// POST /api/diary/delete
// Accepts JSON {"id":"123"} or form id=123
// POST /api/diary/update
// Accepts JSON {"id":"123","note":"...","phase":"grow|flower|dry"} or form fields
// GET /api/diary.csv  -> download the diary CSV
// POST /api/diary/clear
// Manual, user-triggered recovery action:
// - requires confirmation token
// - formats LittleFS
// - mounts LittleFS again
// This intentionally deletes the diary and any other LittleFS files, but never runs automatically.

// -------------------- Phase 25 storage mutation helpers --------------------
// These helpers contain the actual LittleFS mutation work used by the Async
// main-loop handoff. Async request callbacks never call them directly.
static bool queuedDiaryAdd(
    String note, String phaseStr, String lang, String metaStr, String& errOut) {
  errOut = "";
  note.trim();
  phaseStr.trim();
  lang.trim();
  metaStr.trim();
  if (note.length() > 400) note = note.substring(0, 400);
  if (metaStr.length() > 240) metaStr = metaStr.substring(0, 240);

  int phase = settings.grow.currentPhase;
  if (phaseStr == "grow") phase = 1;
  else if (phaseStr == "flower") phase = 2;
  else if (phaseStr == "dry") phase = 3;
  const char* phaseCsv = (phase == 2) ? "flower" : (phase == 3) ? "dry" : "grow";

  time_t nowT = time(nullptr);
  struct tm nowLocalTm {};
  localtime_r(&nowT, &nowLocalTm);
  struct tm midnightTm = nowLocalTm;
  midnightTm.tm_hour = 0; midnightTm.tm_min = 0; midnightTm.tm_sec = 0;
  time_t nowLocalMidnight = mktime(&midnightTm);

  const int growDiff = daysSince(startDate, nowLocalMidnight);
  const int growDay  = (growDiff >= 0) ? (growDiff + 1) : 0;
  const int growWeek = (growDay > 0) ? ((growDay - 1) / 7 + 1) : 0;
  String phaseStart = startDate;
  if (phase == 2 && startFlowering.length() >= 10) phaseStart = startFlowering;
  if (phase == 3 && startDrying.length() >= 10) phaseStart = startDrying;
  const int phaseDiff = daysSince(phaseStart, nowLocalMidnight);
  const int phaseDay  = (phaseDiff >= 0) ? (phaseDiff + 1) : 0;
  const int phaseWeek = (phaseDay > 0) ? ((phaseDay - 1) / 7 + 1) : 0;

  String meta = metaStr;
  if (!meta.length()) {
    String langCode = lang;
    langCode.toLowerCase();
    if (langCode != "en") langCode = "de";
    String phaseName = phaseStr;
    phaseName.toLowerCase();
    auto phaseLabel = [&](const String& code) -> String {
      if (langCode == "en") {
        if (code == "flower") return "Flower";
        if (code == "dry") return "Dry";
        return "Grow";
      }
      if (code == "flower") return "Blüte";
      if (code == "dry") return "Trocknen";
      return "Wachstum";
    };
    const String wLabel = (langCode == "en") ? "Week" : "Woche";
    const String dLabel = (langCode == "en") ? "Day" : "Tag";
    meta.reserve(96);
    meta += "Grow: " + wLabel + " " + String(growWeek) + " " + dLabel + " " + String(growDay);
    meta += " | " + phaseLabel(phaseName) + ": " + wLabel + " " + String(phaseWeek) + " " + dLabel + " " + String(phaseDay);
  }
  if (meta.length()) note = note.length() ? ("(" + meta + ")  " + note) : meta;

  char tsBuf[32];
  snprintf(tsBuf, sizeof(tsBuf), "%04d-%02d-%02d %02d:%02d:%02d",
           nowLocalTm.tm_year + 1900, nowLocalTm.tm_mon + 1, nowLocalTm.tm_mday,
           nowLocalTm.tm_hour, nowLocalTm.tm_min, nowLocalTm.tm_sec);

  if (!ensureFsMounted()) { errOut = "fs_not_mounted"; return false; }
  if (!ensureDiaryHasId()) { errOut = "schema"; return false; }
  const bool exists = LittleFS.exists(DIARY_PATH);
  File f = LittleFS.open(DIARY_PATH, FILE_APPEND);
  if (!f) { errOut = "open"; return false; }
  if (!exists || f.size() == 0) {
    f.println("id,ts_local,phase,grow_day,grow_week,phase_day,phase_week,note");
  }

  String row;
  row.reserve(640);
  const uint32_t diaryId = ((uint32_t)time(nullptr) << 6) ^ (uint32_t)(millis() & 0x3Fu);
  row += String(diaryId); row += ","; row += tsBuf; row += ","; row += phaseCsv; row += ",";
  row += String(growDay); row += ","; row += String(growWeek); row += ",";
  row += String(phaseDay); row += ","; row += String(phaseWeek); row += ","; row += csvEscape(note);
  const size_t written = f.println(row);
  f.close();
  if (written == 0) { errOut = "write"; return false; }
  return true;
}

static bool queuedDiaryUpdate(const String& id, String note, String phaseStr, String& errOut) {
  errOut = "";
  note.trim(); phaseStr.trim();
  if (note.length() > 400) note = note.substring(0, 400);
  String phaseNorm = phaseStr; phaseNorm.toLowerCase();
  if (!(phaseNorm == "grow" || phaseNorm == "flower" || phaseNorm == "dry")) phaseNorm = "";
  if (!rewriteDiaryWithMutation(id, false, phaseNorm, note)) {
    errOut = "not_found_or_write_failed";
    return false;
  }
  return true;
}

static bool queuedDiaryDelete(const String& id, String& errOut) {
  errOut = "";
  if (!rewriteDiaryWithMutation(id, true, "", "")) {
    errOut = "not_found_or_write_failed";
    return false;
  }
  return true;
}

// Strictly read-only guard used by the automated Phase-25 regression. It never
// calls ensureFsMounted() or ensureDiaryHasId(), so it cannot mount/format/migrate.
static bool diaryIdExistsReadOnly(const String& wantedId) {
  if (!isFsMounted() || !LittleFS.exists(DIARY_PATH)) return false;
  File f = LittleFS.open(DIARY_PATH, FILE_READ);
  if (!f) return false;
  bool firstLine = true;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    if (firstLine) {
      firstLine = false;
      if (line.startsWith("id,") || line.startsWith("ts_local,")) continue;
    }
    String fields[8]; int count = 0;
    if (!csvSplitLine(line, fields, 8, count) || count < 8) continue;
    if (csvFieldToString(fields[0]) == wantedId) { f.close(); return true; }
  }
  f.close();
  return false;
}

// -------------------- State API caching (registry -> JSON) --------------------
static String g_apiStateCache;
static uint32_t g_apiStateCacheMs = 0;
static const uint32_t API_STATE_CACHE_TTL_MS = 5000;

static void buildApiStateJson(String& json) {
  // The API is machine-consumed. Keep the payload compact so state/stats use
  // less heap, less airtime and fewer TCP segments under burst load. JSON
  // semantics are unchanged; only insignificant whitespace is removed.
  json = "";
  json.reserve(6144);

  json += "{";

  bool first = true;
  for (size_t i = 0; i < VARS_COUNT; i++) {
    // Heating-mat telemetry is optional: do not expose any heatmat tags until
    // an IP has actually been configured. This also keeps older backends clean.
    const char* key = VARS[i].key;
    if (settings.shelly.heatmat.ip.isEmpty() &&
        (strncmp(key, "settings.shelly.heatmat.", 24) == 0 ||
         strncmp(key, "cur.shelly.heatmat.", 19) == 0)) {
      continue;
    }

    if (!first) json += ",";
    first = false;
    json += "\"";
    json += key;
    json += "\":";
    json += VARS[i].get();
  }

  json += "}";
}

// -------------------- Async WebServer migration - phase 29 system settings dashboard migration --------------------
// IMPORTANT: TCP/8080 is now the permanent target port for the Async server.
// The synchronous WebServer on TCP/80 is retained only as temporary migration
// scaffolding/comparison and must not receive new functionality.
// Previously validated presentation-only and safe system settings remain on 8080.
// Phase 26 keeps all previously migrated settings and the validated chunked
// state/stats/static-asset transports. Relay actions now use a strict Async
// callback -> queue -> normal main-loop hardware handoff. Heating source + relay use the same
// persist -> verify -> queue -> main-loop handoff pattern as grow/heater tuning.
// A source change safely disables the previous heater output in the main loop
// before the new runtime configuration becomes active. The large static JS/CSS delivery path still uses
// bounded callback/chunked flow control. Shelly configuration remains NVS-only:
// IP address + generation for the seven existing roles, plus the non-secret
// Shelly username. Async callbacks never perform Shelly HTTP/RPC I/O or relay
// switching directly. Phase 20 may only switch an already configured previous heater OFF
// from the normal main loop before a source change is activated.
//
// Test URLs:
//   http://<esp-ip>:8080/
//   http://<esp-ip>:8080/style.css
//   http://<esp-ip>:8080/script.js
//   http://<esp-ip>:8080/api/state
//   http://<esp-ip>:8080/api/stats   (temporary compatibility alias to /api/state)
//   http://<esp-ip>:8080/api/hint
//   http://<esp-ip>:8080/api/fs/status
//   http://<esp-ip>:8080/api/logbuffer
//   http://<esp-ip>:8080/log
//   http://<esp-ip>:8080/download/log  (dashboard compatibility alias)
//   http://<esp-ip>:8080/api/diary/list
//   http://<esp-ip>:8080/api/diary.csv
//   http://<esp-ip>:8080/api/settings/theme       (GET/POST, persistent UI-only setting)
//   http://<esp-ip>:8080/api/settings/language    (GET/POST, persistent UI-only setting)
//   http://<esp-ip>:8080/api/settings/unit        (GET/POST, persistent UI-only setting)
//   http://<esp-ip>:8080/api/settings/time-format (GET/POST, persistent UI-only setting)
//   http://<esp-ip>:8080/api/settings/box-name    (GET/POST, persistent UI-only setting)
//   http://<esp-ip>:8080/api/settings/debug       (GET/POST, persistent non-control setting)
//   http://<esp-ip>:8080/api/settings/ds18b20-name (GET/POST, display-name setting)
//   http://<esp-ip>:8080/api/settings/power-price (GET/POST, display/cost setting)
//   http://<esp-ip>:8080/api/settings/relay-name/1..5 (GET/POST, display labels only)
//   http://<esp-ip>:8080/api/settings/ntp-server   (GET/POST, persisted; restart required)
//   http://<esp-ip>:8080/api/settings/time-zone    (GET/POST, persisted; restart required)
//   http://<esp-ip>:8080/api/settings/shelly/main       (GET/POST, IP + generation; restart required)
//   http://<esp-ip>:8080/api/settings/shelly/light      (GET/POST, IP + generation; restart required)
//   http://<esp-ip>:8080/api/settings/shelly/humidifier (GET/POST, IP + generation; restart required)
//   http://<esp-ip>:8080/api/settings/shelly/heater     (GET/POST, IP + generation; restart required)
//   http://<esp-ip>:8080/api/settings/shelly/fan        (GET/POST, IP + generation; restart required)
//   http://<esp-ip>:8080/api/settings/shelly/exhaust    (GET/POST, IP + generation; restart required)
//   http://<esp-ip>:8080/api/settings/shelly/heatmat    (GET/POST, IP + generation; restart required)
//   http://<esp-ip>:8080/api/settings/shelly/auth       (GET/POST username only; password is never exposed)
//   http://<esp-ip>:8080/api/settings/heating-tuning   (GET/POST, runtime main-loop handoff)
//   http://<esp-ip>:8080/api/settings/heating-output   (GET/POST, source + relay, runtime main-loop handoff)
//   http://<esp-ip>:8080/api/settings/light-schedule    (GET/POST, ON time + day hours, runtime main-loop handoff)
//   http://<esp-ip>:8080/api/settings/relay-schedule/1..5 (GET/POST, enabled + minute window + light gating, runtime main-loop handoff)
//   http://<esp-ip>:8080/api/relay/1..5/state (GET current cached state / POST explicit state via main-loop handoff)
//   http://<esp-ip>:8080/relay/1..5/toggle (POST dashboard-compatible toggle via main-loop handoff)
//   http://<esp-ip>:8080/api/async/health

// Request-local state for streaming the large HTML template without using the
// library's %PLACEHOLDER% parser. The template contains ordinary percent signs,
// so the generic template processor can misinterpret unrelated page content.
// This mirrors the proven newline-aligned chunk strategy used by port 80.
struct AsyncMainPageStreamState {
  size_t sourcePos = 0;
  String pending;
  size_t pendingPos = 0;
  bool failed = false;
};

static bool prepareAsyncMainPageChunk(const std::shared_ptr<AsyncMainPageStreamState>& state) {
  if (!state || state->failed) return false;

  constexpr size_t SOURCE_CHUNK_TARGET = 6144;
  const char* cursor = htmlPage + state->sourcePos;
  if (*cursor == '\0') return false;

  size_t len = 0;
  while (cursor[len] && len < SOURCE_CHUNK_TARGET) {
    ++len;
  }

  // Keep %PLACEHOLDER% tokens together just like the established synchronous
  // chunked sender by preferring a newline boundary.
  if (cursor[len] != '\0') {
    size_t cut = len;
    while (cut > 0 && cursor[cut - 1] != '\n') {
      --cut;
    }
    if (cut > 0) len = cut;
  }

  state->pending = "";
  if (!state->pending.reserve(len + 512)) {
    state->failed = true;
    Serial.println("[ASYNC] HTML chunk allocation failed");
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    state->pending += cursor[i];
  }

  applyMainPageReplacements(state->pending);
  state->pendingPos = 0;
  state->sourcePos += len;
  return true;
}

static size_t fillAsyncMainPage(
    const std::shared_ptr<AsyncMainPageStreamState>& state,
    uint8_t* buffer,
    size_t maxLen) {
  if (!state || !buffer || maxLen == 0 || state->failed) return 0;

  size_t written = 0;

  while (written < maxLen) {
    if (state->pendingPos < state->pending.length()) {
      const size_t remaining = state->pending.length() - state->pendingPos;
      const size_t copyLen = std::min(maxLen - written, remaining);
      memcpy(buffer + written, state->pending.c_str() + state->pendingPos, copyLen);
      state->pendingPos += copyLen;
      written += copyLen;

      if (state->pendingPos >= state->pending.length()) {
        state->pending = "";
        state->pendingPos = 0;
      }
      continue;
    }

    if (!prepareAsyncMainPageChunk(state)) break;
  }

  return written;
}

// Dedicated Async read cache. A short 1 s TTL coalesces simultaneous
// dashboard/backend reads while keeping the permanent API fresh. Phase 32 no
// longer has a synchronous WebServer; this cache is owned solely by :8080.
static constexpr uint32_t ASYNC_STATE_CACHE_TTL_MS = 1000UL;
static SemaphoreHandle_t g_asyncStateCacheMutex = nullptr;
static String g_asyncStateCache;
static uint32_t g_asyncStateCacheMs = 0;
// Large health JSON uses the same stable direct-buffer lifetime model as the
// state cache. Phase 32 grew /api/async/health beyond one AsyncTCP send window;
// keeping the body in a persistent buffer avoids AsyncBasicResponse tail stalls.
static SemaphoreHandle_t g_asyncHealthCacheMutex = nullptr;
static String g_asyncHealthCache;
static volatile uint32_t g_asyncHealthActiveReaders = 0;
// Generation invalidation cannot be lost when the cache mutex is busy. A relay
// action increments the generation in loop(); any cache snapshot built before
// that action is rejected on the next /api/state or /api/stats request.
static volatile uint32_t g_asyncStateGeneration = 1;
static uint32_t g_asyncStateCacheGeneration = 0;
static volatile uint32_t g_asyncStateCacheHits = 0;
static volatile uint32_t g_asyncStateCacheMisses = 0;
static volatile uint32_t g_asyncStateCacheFallbacks = 0;
static volatile uint32_t g_asyncStateResponseBufferFailures = 0;
// Number of in-flight /api/state + /api/stats responses that currently read
// directly from g_asyncStateCache. The cache buffer is never mutated while
// this value is non-zero, which makes the native fixed-length direct-buffer
// response safe without a request-local 5-6 kB body copy. GCC atomics keep disconnect
// callbacks lock-free across AsyncTCP cores.
static volatile uint32_t g_asyncStateActiveReaders = 0;
static volatile uint32_t g_asyncStateDeferredRefreshes = 0;
static volatile uint32_t g_asyncRootRequests = 0;

// Persistent Async write counters retained for regression coverage.
// Phase 32 keeps the validated state/stats and static-asset transports
// plus all prior restart-applied settings. Shelly device/auth writes below are NVS-only; runtime
// settings remain unchanged until restart, so no network I/O or control-task
// reconfiguration is initiated from an Async callback.
static volatile uint32_t g_asyncLogClearRequests = 0;
static volatile uint32_t g_asyncSystemSettingsChangedLogs = 0;
static volatile uint32_t g_asyncThemeWriteRequests = 0;
static volatile uint32_t g_asyncThemeWriteFailures = 0;
static volatile uint32_t g_asyncLanguageWriteRequests = 0;
static volatile uint32_t g_asyncLanguageWriteFailures = 0;
static volatile uint32_t g_asyncUnitWriteRequests = 0;
static volatile uint32_t g_asyncUnitWriteFailures = 0;
static volatile uint32_t g_asyncTimeFormatWriteRequests = 0;
static volatile uint32_t g_asyncTimeFormatWriteFailures = 0;
static volatile uint32_t g_asyncBoxNameWriteRequests = 0;
static volatile uint32_t g_asyncBoxNameWriteFailures = 0;
static volatile uint32_t g_asyncDebugWriteRequests = 0;
static volatile uint32_t g_asyncDebugWriteFailures = 0;
static volatile uint32_t g_asyncDs18NameWriteRequests = 0;
static volatile uint32_t g_asyncDs18NameWriteFailures = 0;
static volatile uint32_t g_asyncPowerPriceWriteRequests = 0;
static volatile uint32_t g_asyncPowerPriceWriteFailures = 0;
static volatile uint32_t g_asyncRelayNameWriteRequests = 0;
static volatile uint32_t g_asyncRelayNameWriteFailures = 0;
static volatile uint32_t g_asyncNtpServerWriteRequests = 0;
static volatile uint32_t g_asyncNtpServerWriteFailures = 0;
static volatile uint32_t g_asyncTimeZoneWriteRequests = 0;
static volatile uint32_t g_asyncTimeZoneWriteFailures = 0;
static volatile uint32_t g_asyncDs18EnableWriteRequests = 0;
static volatile uint32_t g_asyncDs18EnableWriteFailures = 0;
static volatile uint32_t g_asyncRelayCountWriteRequests = 0;
static volatile uint32_t g_asyncRelayCountWriteFailures = 0;
static volatile uint32_t g_asyncShellyDeviceWriteRequests = 0;
static volatile uint32_t g_asyncShellyDeviceWriteFailures = 0;
static volatile uint32_t g_asyncShellyAuthWriteRequests = 0;
static volatile uint32_t g_asyncShellyAuthWriteFailures = 0;

// Phase 17: first runtime-applied Async control settings. The Async callback never
// writes the live grow-control variables directly. It persists a validated triplet
// to NVS and queues one pending snapshot. The normal Arduino loop owns the actual
// runtime assignment from the normal Arduino loop.
struct AsyncGrowTargetsPending {
  bool writeInProgress = false;
  bool pending = false;
  uint32_t requestId = 0;
  float targetTemperature = 22.0f;
  float targetVPD = 1.0f;
  float offsetLeafTemperature = -1.5f;
};

static SemaphoreHandle_t g_asyncGrowTargetsMutex = nullptr;
static AsyncGrowTargetsPending g_asyncGrowTargetsPending;
static uint32_t g_asyncGrowTargetsNextRequestId = 0;
static volatile uint32_t g_asyncGrowTargetsWriteRequests = 0;
static volatile uint32_t g_asyncGrowTargetsWriteFailures = 0;
static volatile uint32_t g_asyncGrowTargetsQueued = 0;
static volatile uint32_t g_asyncGrowTargetsApplied = 0;
static volatile uint32_t g_asyncGrowTargetsLastQueuedId = 0;
static volatile uint32_t g_asyncGrowTargetsLastAppliedId = 0;

// Phase 18: heater-controller tuning uses the same persist -> verify -> queue ->
// main-loop apply pattern as the proven Phase 17 grow-target handoff. No Async
// callback mutates the live controller settings directly.
struct AsyncHeatingTuningPending {
  bool writeInProgress = false;
  bool pending = false;
  uint32_t requestId = 0;
  float hysteresisC = 0.25f;
  float predictSeconds = 180.0f;
  int minOnSeconds = 120;
  int minOffSeconds = 120;
};

static SemaphoreHandle_t g_asyncHeatingTuningMutex = nullptr;
static AsyncHeatingTuningPending g_asyncHeatingTuningPending;
static uint32_t g_asyncHeatingTuningNextRequestId = 0;
static volatile uint32_t g_asyncHeatingTuningWriteRequests = 0;
static volatile uint32_t g_asyncHeatingTuningWriteFailures = 0;
static volatile uint32_t g_asyncHeatingTuningQueued = 0;
static volatile uint32_t g_asyncHeatingTuningApplied = 0;
static volatile uint32_t g_asyncHeatingTuningLastQueuedId = 0;
static volatile uint32_t g_asyncHeatingTuningLastAppliedId = 0;

// Phase 20: heating source/output configuration. The Async callback only validates,
// persists and queues a snapshot. The normal loop owns both the safety shutdown of
// the previous heater output and the live settings mutation.
struct AsyncHeatingOutputPending {
  bool writeInProgress = false;
  bool pending = false;
  uint32_t requestId = 0;
  int sourceType = 0;
  int relay = 0;
  int oldStoredSourceType = 0;
  int oldStoredRelay = 0;
};

static SemaphoreHandle_t g_asyncHeatingOutputMutex = nullptr;
static AsyncHeatingOutputPending g_asyncHeatingOutputPending;
static uint32_t g_asyncHeatingOutputNextRequestId = 0;
static volatile uint32_t g_asyncHeatingOutputWriteRequests = 0;
static volatile uint32_t g_asyncHeatingOutputWriteFailures = 0;
static volatile uint32_t g_asyncHeatingOutputQueued = 0;
static volatile uint32_t g_asyncHeatingOutputApplied = 0;
static volatile uint32_t g_asyncHeatingOutputApplyFailures = 0;
static volatile uint32_t g_asyncHeatingOutputRollbackFailures = 0;
static volatile uint32_t g_asyncHeatingOutputLastQueuedId = 0;
static volatile uint32_t g_asyncHeatingOutputLastAppliedId = 0;
static volatile uint32_t g_asyncHeatingOutputLastFailedId = 0;

// Phase 21: grow-light ON time + photoperiod. Async callbacks only validate,
// persist and queue. Runtime mutation and optional Shelly schedule programming
// happen from the normal loop. The automated test mutates only when no runtime
// Shelly light IP is configured, so test execution cannot alter a real light.
struct AsyncLightSchedulePending {
  bool writeInProgress = false;
  bool pending = false;
  uint32_t requestId = 0;
  String onTime;
  int dayHours = 18;
  String oldStoredOnTime;
  int oldStoredDayHours = 18;
  String oldRuntimeOnTime;
  int oldRuntimeDayHours = 18;
};

static SemaphoreHandle_t g_asyncLightScheduleMutex = nullptr;
static AsyncLightSchedulePending g_asyncLightSchedulePending;
static uint32_t g_asyncLightScheduleNextRequestId = 0;
static volatile uint32_t g_asyncLightScheduleWriteRequests = 0;
static volatile uint32_t g_asyncLightScheduleWriteFailures = 0;
static volatile uint32_t g_asyncLightScheduleQueued = 0;
static volatile uint32_t g_asyncLightScheduleApplied = 0;
static volatile uint32_t g_asyncLightScheduleApplyFailures = 0;
static volatile uint32_t g_asyncLightScheduleRollbackFailures = 0;
static volatile uint32_t g_asyncLightScheduleLastQueuedId = 0;
static volatile uint32_t g_asyncLightScheduleLastAppliedId = 0;
static volatile uint32_t g_asyncLightScheduleLastFailedId = 0;

// Phase 22: ESP relay minute schedules (relays 1..5). The Async callback only
// validates, persists and queues one schedule snapshot. Runtime mutation happens
// from loop(). The automated regression test only mutates a schedule that is
// disabled in BOTH stored and runtime state, and keeps it disabled throughout,
// so the test itself cannot cause a relay transition.
struct AsyncRelaySchedulePending {
  bool writeInProgress = false;
  bool pending = false;
  uint32_t requestId = 0;
  uint8_t relayIndex = 0;
  RelaySchedule value;
  RelaySchedule oldStored;
};

static SemaphoreHandle_t g_asyncRelayScheduleMutex = nullptr;
static AsyncRelaySchedulePending g_asyncRelaySchedulePending;
static uint32_t g_asyncRelayScheduleNextRequestId = 0;
static volatile uint32_t g_asyncRelayScheduleWriteRequests = 0;
static volatile uint32_t g_asyncRelayScheduleWriteFailures = 0;
static volatile uint32_t g_asyncRelayScheduleQueued = 0;
static volatile uint32_t g_asyncRelayScheduleApplied = 0;
static volatile uint32_t g_asyncRelayScheduleApplyFailures = 0;
static volatile uint32_t g_asyncRelayScheduleRollbackFailures = 0;
static volatile uint32_t g_asyncRelayScheduleLastQueuedId = 0;
static volatile uint32_t g_asyncRelayScheduleLastAppliedId = 0;
static volatile uint32_t g_asyncRelayScheduleLastFailedId = 0;

// Phase 23: manual ESP relay actions (relays 1..5). Async callbacks never touch
// GPIO. They only validate and enqueue an action. The normal Arduino loop reads
// the physical relay state, performs setRelay()/digitalWrite through the existing
// relay mutex path, verifies the result, and invalidates the Async state cache.
// The automated regression uses only an explicit OFF request on an already-OFF,
// unscheduled, non-heater relay so it cannot energize connected hardware.
struct AsyncRelayActionPending {
  bool pending = false;
  uint32_t requestId = 0;
  uint8_t relayIndex = 0;
  bool targetState = false;
  bool toggle = false;
  bool noTransitionOnly = false;
};

static SemaphoreHandle_t g_asyncRelayActionMutex = nullptr;
static AsyncRelayActionPending g_asyncRelayActionPending;
static uint32_t g_asyncRelayActionNextRequestId = 0;
static volatile uint32_t g_asyncRelayActionRequests = 0;
static volatile uint32_t g_asyncRelayActionFailures = 0;
static volatile uint32_t g_asyncRelayActionQueued = 0;
static volatile uint32_t g_asyncRelayActionApplied = 0;
static volatile uint32_t g_asyncRelayActionApplyFailures = 0;
static volatile uint32_t g_asyncRelayActionTransitions = 0;
static volatile uint32_t g_asyncRelayActionNoops = 0;
static volatile uint32_t g_asyncRelayActionLastQueuedId = 0;
static volatile uint32_t g_asyncRelayActionLastAppliedId = 0;
static volatile uint32_t g_asyncRelayActionLastFailedId = 0;


// Phase 24: irrigation actions and Flood events. Async callbacks validate and
// enqueue only. The normal Arduino loop applies irrigation start/stop, manual
// pump pulses, tank pings, Flood state persistence and Flood-triggered stops.
// Sequential automatic pump execution remains exclusively in taskWatering().
enum class AsyncWateringActionType : uint8_t {
  None = 0,
  StartApi = 1,
  StartDashboard = 2,
  Stop = 3,
  ToggleDashboard = 4,
  PumpPulse = 5,
  TankPing = 6
};

struct AsyncWateringPending {
  bool pending = false;
  uint32_t requestId = 0;
  AsyncWateringActionType type = AsyncWateringActionType::None;
  uint8_t pumpSlot = 0;
  uint32_t pulseMs = 10000UL;
  bool noTransitionOnly = false;
};

struct AsyncFloodEventPending {
  bool pending = false;
  uint32_t requestId = 0;
  bool alarm = false;
  int batteryPct = -1;
  time_t eventEpoch = 0;
};

static SemaphoreHandle_t g_asyncWateringMutex = nullptr;
static AsyncWateringPending g_asyncWateringPending;
static uint32_t g_asyncWateringNextRequestId = 0;
static volatile uint32_t g_asyncWateringRequests = 0;
static volatile uint32_t g_asyncWateringFailures = 0;
static volatile uint32_t g_asyncWateringQueued = 0;
static volatile uint32_t g_asyncWateringApplied = 0;
static volatile uint32_t g_asyncWateringApplyFailures = 0;
static volatile uint32_t g_asyncWateringStarts = 0;
static volatile uint32_t g_asyncWateringStops = 0;
static volatile uint32_t g_asyncWateringNoops = 0;
static volatile uint32_t g_asyncWateringPumpPulses = 0;
static volatile uint32_t g_asyncWateringTankPings = 0;
static volatile uint32_t g_asyncWateringLastQueuedId = 0;
static volatile uint32_t g_asyncWateringLastAppliedId = 0;
static volatile uint32_t g_asyncWateringLastFailedId = 0;

static SemaphoreHandle_t g_asyncFloodEventMutex = nullptr;
static AsyncFloodEventPending g_asyncFloodEventPending;
static uint32_t g_asyncFloodEventNextRequestId = 0;
static volatile uint32_t g_asyncFloodEventRequests = 0;
static volatile uint32_t g_asyncFloodEventFailures = 0;
static volatile uint32_t g_asyncFloodEventQueued = 0;
static volatile uint32_t g_asyncFloodEventApplied = 0;
static volatile uint32_t g_asyncFloodEventApplyFailures = 0;
static volatile uint32_t g_asyncFloodStops = 0;
static volatile uint32_t g_asyncFloodEventLastQueuedId = 0;
static volatile uint32_t g_asyncFloodEventLastAppliedId = 0;
static volatile uint32_t g_asyncFloodEventLastFailedId = 0;


// Phase 25: Grow Diary mutations + explicit LittleFS recovery. Async callbacks
// validate and enqueue only. All LittleFS write/delete/format operations execute
// from the normal Arduino loop. The regression test uses a guarded delete of an
// impossible ID and refuses the action if it would mutate real data.
enum class AsyncStorageActionType : uint8_t {
  None = 0, DiaryAdd = 1, DiaryUpdate = 2, DiaryDelete = 3, DiaryClear = 4, FsFormat = 5
};

struct AsyncStoragePending {
  bool pending = false;
  bool processing = false;
  uint32_t requestId = 0;
  AsyncStorageActionType type = AsyncStorageActionType::None;
  String id;
  String note;
  String phase;
  String lang;
  String meta;
  bool noMutationOnly = false;
};

static SemaphoreHandle_t g_asyncStorageMutex = nullptr;
static AsyncStoragePending g_asyncStoragePending;
static uint32_t g_asyncStorageNextRequestId = 0;
static volatile uint32_t g_asyncStorageRequests = 0;
static volatile uint32_t g_asyncStorageFailures = 0;
static volatile uint32_t g_asyncStorageQueued = 0;
static volatile uint32_t g_asyncStorageApplied = 0;
static volatile uint32_t g_asyncStorageApplyFailures = 0;
static volatile uint32_t g_asyncStorageNoops = 0;
static volatile uint32_t g_asyncStorageLastQueuedId = 0;
static volatile uint32_t g_asyncStorageLastAppliedId = 0;
static volatile uint32_t g_asyncStorageLastFailedId = 0;
static String g_asyncStorageLastError;

// Phase 26: final Betriebseinstellungen decomposition. The browser no longer
// posts the broad legacy /saverunsettings payload. Two remaining domains are
// migrated here: grow/phase/min-VPD profile and irrigation configuration.
// Async callbacks validate -> persist/verify -> queue only. Runtime mutation
// remains in loop(), matching the safety ownership used by phases 17-25.
struct AsyncRunProfileValues {
  String startDate;
  String floweringStart;
  String dryingStart;
  int currentPhase = 1;
  bool minVpdMonitoring = false;
  float minVPD = 0.06f;
  float vpdHysteresis = 0.05f;
};

struct AsyncRunProfilePending {
  bool writeInProgress = false;
  bool pending = false;
  uint32_t requestId = 0;
  AsyncRunProfileValues value;
};

static SemaphoreHandle_t g_asyncRunProfileMutex = nullptr;
static AsyncRunProfilePending g_asyncRunProfilePending;
static uint32_t g_asyncRunProfileNextRequestId = 0;
static volatile uint32_t g_asyncRunProfileWriteRequests = 0;
static volatile uint32_t g_asyncRunProfileWriteFailures = 0;
static volatile uint32_t g_asyncRunProfileQueued = 0;
static volatile uint32_t g_asyncRunProfileApplied = 0;
static volatile uint32_t g_asyncRunProfileApplyFailures = 0;
static volatile uint32_t g_asyncRunProfileRollbackFailures = 0;
static volatile uint32_t g_asyncRunProfileLastQueuedId = 0;
static volatile uint32_t g_asyncRunProfileLastAppliedId = 0;
static volatile uint32_t g_asyncRunProfileLastFailedId = 0;

struct AsyncIrrigationConfigValues {
  float amountOfWater = 20.0f;
  int timePerTask = 10;
  int betweenTasks = 5;
  float amount = 500.0f;
  bool pumpEnabled[3] = {true, true, true};
  bool stopOnFloodAlarm = false;
  float tankMin = 0.0f;
  float tankMax = 0.0f;
};

struct AsyncIrrigationConfigPending {
  bool writeInProgress = false;
  bool pending = false;
  uint32_t requestId = 0;
  AsyncIrrigationConfigValues value;
  AsyncIrrigationConfigValues oldStored;
};

static SemaphoreHandle_t g_asyncIrrigationConfigMutex = nullptr;
static AsyncIrrigationConfigPending g_asyncIrrigationConfigPending;
static uint32_t g_asyncIrrigationConfigNextRequestId = 0;
static volatile uint32_t g_asyncIrrigationConfigWriteRequests = 0;
static volatile uint32_t g_asyncIrrigationConfigWriteFailures = 0;
static volatile uint32_t g_asyncIrrigationConfigQueued = 0;
static volatile uint32_t g_asyncIrrigationConfigApplied = 0;
static volatile uint32_t g_asyncIrrigationConfigApplyFailures = 0;
static volatile uint32_t g_asyncIrrigationConfigRollbackFailures = 0;
static volatile uint32_t g_asyncIrrigationConfigLastQueuedId = 0;
static volatile uint32_t g_asyncIrrigationConfigLastAppliedId = 0;
static volatile uint32_t g_asyncIrrigationConfigLastFailedId = 0;

// Phase 27: Shelly runtime actions. Async callbacks validate and enqueue only.
// All Shelly HTTP/RPC traffic and energy-counter persistence remain owned by
// the normal Arduino loop. One action at a time is serialized so toggle,
// schedule refresh and energy reset cannot overlap. Automated regression uses
// dryRun=1, which exercises the queue/main-loop/status path without any network
// access, Shelly state change or Preferences mutation.
enum class AsyncShellyActionType : uint8_t {
  None = 0,
  Toggle = 1,
  RefreshSchedule = 2,
  ResetEnergy = 3,
  ReloadConfig = 4
};

struct AsyncShellyActionPending {
  bool pending = false;
  bool processing = false;
  bool configWriteInProgress = false;
  uint32_t requestId = 0;
  AsyncShellyActionType type = AsyncShellyActionType::None;
  uint8_t roleIndex = 255;
  bool dryRun = false;
  bool programLightSchedule = false;
  uint8_t detectGenerationMask = 0;
};

static SemaphoreHandle_t g_asyncShellyActionMutex = nullptr;
static AsyncShellyActionPending g_asyncShellyActionPending;
static uint32_t g_asyncShellyActionNextRequestId = 0;
static volatile uint32_t g_asyncShellyActionRequests = 0;
static volatile uint32_t g_asyncShellyActionFailures = 0;
static volatile uint32_t g_asyncShellyActionQueued = 0;
static volatile uint32_t g_asyncShellyActionApplied = 0;
static volatile uint32_t g_asyncShellyActionApplyFailures = 0;
static volatile uint32_t g_asyncShellyActionDryRuns = 0;
static volatile uint32_t g_asyncShellyActionNetworkOps = 0;
static volatile uint32_t g_asyncShellyActionLastQueuedId = 0;
static volatile uint32_t g_asyncShellyActionLastAppliedId = 0;
static volatile uint32_t g_asyncShellyActionLastFailedId = 0;
static volatile uint32_t g_asyncShellyConfigWriteRequests = 0;
static volatile uint32_t g_asyncShellyConfigWriteFailures = 0;
static volatile uint32_t g_asyncShellyConfigRollbackFailures = 0;
static AsyncShellyActionType g_asyncShellyActionLastType = AsyncShellyActionType::None;
static uint8_t g_asyncShellyActionLastRole = 255;
static String g_asyncShellyActionLastError;
static String g_asyncShellyActionLastResultJson;

// Phase 29: system-settings dashboard restart handoff. The dashboard persists
// each setting through the already validated dedicated Async preference routes,
// then queues exactly one restart request. The Async callback never calls
// ESP.restart() directly; the normal Arduino loop owns restart scheduling.
struct AsyncSystemRestartPending {
  bool pending = false;
  bool processing = false;
  uint32_t requestId = 0;
  bool dryRun = false;
};

static SemaphoreHandle_t g_asyncSystemRestartMutex = nullptr;
static AsyncSystemRestartPending g_asyncSystemRestartPending;
static uint32_t g_asyncSystemRestartNextRequestId = 0;
static volatile uint32_t g_asyncSystemRestartRequests = 0;
static volatile uint32_t g_asyncSystemRestartFailures = 0;
static volatile uint32_t g_asyncSystemRestartQueued = 0;
static volatile uint32_t g_asyncSystemRestartApplied = 0;
static volatile uint32_t g_asyncSystemRestartDryRuns = 0;
static volatile uint32_t g_asyncSystemRestartLastQueuedId = 0;
static volatile uint32_t g_asyncSystemRestartLastAppliedId = 0;
static bool g_asyncSystemRestartLastDryRun = false;

// Phase 30: destructive factory reset is queued from the Async callback and
// executed only by the normal Arduino loop. AP Wi-Fi provisioning remains a
// deliberate TCP/80 exception and is not part of this handoff.
struct AsyncFactoryResetPending {
  bool pending = false;
  bool processing = false;
  uint32_t requestId = 0;
  bool dryRun = false;
};

static SemaphoreHandle_t g_asyncFactoryResetMutex = nullptr;
static AsyncFactoryResetPending g_asyncFactoryResetPending;
static uint32_t g_asyncFactoryResetNextRequestId = 0;
static volatile uint32_t g_asyncFactoryResetRequests = 0;
static volatile uint32_t g_asyncFactoryResetFailures = 0;
static volatile uint32_t g_asyncFactoryResetQueued = 0;
static volatile uint32_t g_asyncFactoryResetApplied = 0;
static volatile uint32_t g_asyncFactoryResetApplyFailures = 0;
static volatile uint32_t g_asyncFactoryResetDryRuns = 0;
static volatile uint32_t g_asyncFactoryResetLastQueuedId = 0;
static volatile uint32_t g_asyncFactoryResetLastAppliedId = 0;
static volatile uint32_t g_asyncFactoryResetLastFailedId = 0;
static bool g_asyncFactoryResetLastDryRun = false;

// Phase 32: the final user-facing legacy STA routes use explicit Async
// handoffs. Request callbacks only validate/copy input and queue work; main-loop
// code owns Preferences/runtime/network side effects for message settings and
// New Grow. OTA uses its own worker task started from otaProcessQueue().
struct AsyncMessageSettingsPending {
  bool pending = false;
  bool processing = false;
  uint32_t requestId = 0;
  bool dryRun = false;
  bool pushoverEnabled = false;
  bool gotifyEnabled = false;
  String pushoverUser;
  String pushoverApp;
  String pushoverDevice;
  String gotifyServer;
  String gotifyToken;
};
static SemaphoreHandle_t g_asyncMessageSettingsMutex = nullptr;
static AsyncMessageSettingsPending g_asyncMessageSettingsPending;
static uint32_t g_asyncMessageSettingsNextRequestId = 0;
static volatile uint32_t g_asyncMessageSettingsRequests = 0;
static volatile uint32_t g_asyncMessageSettingsFailures = 0;
static volatile uint32_t g_asyncMessageSettingsApplied = 0;
static volatile uint32_t g_asyncMessageSettingsDryRuns = 0;
static volatile uint32_t g_asyncMessageSettingsLastAppliedId = 0;
static volatile uint32_t g_asyncMessageSettingsLastFailedId = 0;
static String g_asyncMessageSettingsLastError;

struct AsyncNewGrowPending {
  bool pending = false;
  bool processing = false;
  uint32_t requestId = 0;
  bool dryRun = false;
};
static SemaphoreHandle_t g_asyncNewGrowMutex = nullptr;
static AsyncNewGrowPending g_asyncNewGrowPending;
static uint32_t g_asyncNewGrowNextRequestId = 0;
static volatile uint32_t g_asyncNewGrowRequests = 0;
static volatile uint32_t g_asyncNewGrowFailures = 0;
static volatile uint32_t g_asyncNewGrowApplied = 0;
static volatile uint32_t g_asyncNewGrowDryRuns = 0;
static volatile uint32_t g_asyncNewGrowLastAppliedId = 0;
static volatile uint32_t g_asyncNewGrowLastFailedId = 0;
static String g_asyncNewGrowLastError;
static String g_asyncNewGrowLastStartDate;
static bool g_asyncNewGrowResetMain = false;
static bool g_asyncNewGrowResetLight = false;
static bool g_asyncNewGrowResetHumidifier = false;

static void addAsyncCommonHeaders(AsyncWebServerResponse* response, bool cacheStatic = false) {
  if (!response) return;
  response->addHeader("X-GrowTent-WebServer", "async-phase32");
  if (cacheStatic) {
    response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
  } else {
    response->addHeader("Cache-Control", "no-store");
  }
}

static void handleAsyncRoot(AsyncWebServerRequest* request) {
  if (!request) return;
  ++g_asyncRootRequests;

  if (g_wifiProvisioningActive && WiFi.status() != WL_CONNECTED) {
    // The dedicated captive portal owns TCP/80 in AP mode. Keep :8080 as the
    // permanent application port, but guide setup clients to the portal.
    const String target = String("http://") + WiFi.softAPIP().toString() + "/";
    AsyncWebServerResponse* response = request->beginResponse(302, "text/plain; charset=utf-8", "Wi-Fi setup");
    response->addHeader("Location", target);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  updateGrowTimeValues();

  auto state = std::make_shared<AsyncMainPageStreamState>();
  AsyncWebServerResponse* response =
    request->beginChunkedResponse(
      "text/html; charset=utf-8",
      [state](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
        (void)index;
        return fillAsyncMainPage(state, buffer, maxLen);
      });

  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-HTML-Chunk", "6144");
  request->send(response);
}

static void initAsyncPhase13ReadCache() {
  if (!g_asyncStateCacheMutex) {
    g_asyncStateCacheMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncHealthCacheMutex) {
    g_asyncHealthCacheMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncGrowTargetsMutex) {
    g_asyncGrowTargetsMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncHeatingTuningMutex) {
    g_asyncHeatingTuningMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncHeatingOutputMutex) {
    g_asyncHeatingOutputMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncLightScheduleMutex) {
    g_asyncLightScheduleMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncRelayScheduleMutex) {
    g_asyncRelayScheduleMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncRelayActionMutex) {
    g_asyncRelayActionMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncWateringMutex) {
    g_asyncWateringMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncFloodEventMutex) {
    g_asyncFloodEventMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncStorageMutex) {
    g_asyncStorageMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncRunProfileMutex) {
    g_asyncRunProfileMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncIrrigationConfigMutex) {
    g_asyncIrrigationConfigMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncShellyActionMutex) {
    g_asyncShellyActionMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncSystemRestartMutex) {
    g_asyncSystemRestartMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncFactoryResetMutex) {
    g_asyncFactoryResetMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncMessageSettingsMutex) {
    g_asyncMessageSettingsMutex = xSemaphoreCreateMutex();
  }
  if (!g_asyncNewGrowMutex) {
    g_asyncNewGrowMutex = xSemaphoreCreateMutex();
  }
  otaInit();
  // The current state payload is about 5.4 kB. Reserve some headroom once so
  // periodic refreshes do not repeatedly fragment the heap.
  g_asyncStateCache.reserve(8192);
  g_asyncHealthCache.reserve(8192);
}

static uint32_t asyncHealthActiveReaders() {
  return __atomic_load_n(&g_asyncHealthActiveReaders, __ATOMIC_ACQUIRE);
}

static void retainAsyncHealthReader() {
  __atomic_add_fetch(&g_asyncHealthActiveReaders, 1U, __ATOMIC_ACQ_REL);
}

static void releaseAsyncHealthReader() {
  uint32_t current = __atomic_load_n(&g_asyncHealthActiveReaders, __ATOMIC_ACQUIRE);
  while (current > 0) {
    if (__atomic_compare_exchange_n(
          &g_asyncHealthActiveReaders, &current, current - 1U, false,
          __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
      return;
    }
  }
}

static uint32_t asyncStateActiveReaders() {
  return __atomic_load_n(&g_asyncStateActiveReaders, __ATOMIC_ACQUIRE);
}

static void retainAsyncStateReader() {
  __atomic_add_fetch(&g_asyncStateActiveReaders, 1U, __ATOMIC_ACQ_REL);
}

static void releaseAsyncStateReader() {
  uint32_t current = __atomic_load_n(&g_asyncStateActiveReaders, __ATOMIC_ACQUIRE);
  while (current > 0) {
    if (__atomic_compare_exchange_n(
          &g_asyncStateActiveReaders, &current, current - 1U, false,
          __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
      return;
    }
  }
}

static void expireAsyncStateCacheMetadata() {
  if (g_asyncStateCacheMutex &&
      xSemaphoreTake(g_asyncStateCacheMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    // Do not clear or otherwise mutate the shared JSON buffer here. Active
    // fixed-length callback responses may still reference it. Metadata expiry
    // makes the next reader rebuild once the active-reader count reaches zero.
    g_asyncStateCacheMs = 0;
    g_asyncStateCacheGeneration = 0;
    xSemaphoreGive(g_asyncStateCacheMutex);
  }
}

static void handleAsyncStateResponse(AsyncWebServerRequest* request, bool isStats) {
  if (!request) return;

  bool cacheHit = false;
  bool cacheStaleShared = false;
  size_t sourceLength = 0;
  const char* sourcePtr = nullptr;

  if (!g_asyncStateCacheMutex ||
      xSemaphoreTake(g_asyncStateCacheMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    ++g_asyncStateCacheFallbacks;
    AsyncWebServerResponse* busyResponse = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"state_cache_busy\"}");
    addAsyncCommonHeaders(busyResponse, false);
    busyResponse->addHeader("X-GrowTent-State-Cache", "busy");
    busyResponse->addHeader("X-GrowTent-State-Transport", "shared-cache-direct-buffer-fixed-length");
    busyResponse->addHeader("X-GrowTent-State-Block-Max", "0");
    busyResponse->addHeader("X-GrowTent-State-Format", "compact");
    busyResponse->addHeader("X-GrowTent-Endpoint", isStats ? "stats" : "state");
    request->send(busyResponse);
    return;
  }

  const uint32_t now = millis();
  uint32_t snapshotGeneration = g_asyncStateGeneration;
  bool fresh = g_asyncStateCache.length() > 0 &&
               g_asyncStateCacheGeneration == snapshotGeneration &&
               (uint32_t)(now - g_asyncStateCacheMs) <= ASYNC_STATE_CACHE_TTL_MS;

  if (fresh) {
    ++g_asyncStateCacheHits;
    cacheHit = true;
  } else if (asyncStateActiveReaders() == 0) {
    // No response currently references the cache buffer, so it can be rebuilt
    // in place without a request-local body allocation.
    buildApiStateJson(g_asyncStateCache);
    const uint32_t generationAfterBuild = g_asyncStateGeneration;
    if (generationAfterBuild != snapshotGeneration) {
      snapshotGeneration = generationAfterBuild;
      buildApiStateJson(g_asyncStateCache);
    }
    g_asyncStateCacheMs = now;
    g_asyncStateCacheGeneration = snapshotGeneration;
    ++g_asyncStateCacheMisses;
  } else {
    // Keep the current snapshot immutable until every in-flight callback has
    // disconnected. Serving one slightly stale snapshot is preferable to
    // mutating the buffer behind active network responses or allocating N
    // separate ~5-6 kB copies during a burst.
    ++g_asyncStateCacheHits;
    ++g_asyncStateDeferredRefreshes;
    cacheHit = true;
    cacheStaleShared = true;
  }

  sourceLength = g_asyncStateCache.length();
  if (sourceLength > 0) {
    sourcePtr = g_asyncStateCache.c_str();
    retainAsyncStateReader();
  }
  xSemaphoreGive(g_asyncStateCacheMutex);

  if (!sourcePtr || sourceLength == 0) {
    ++g_asyncStateResponseBufferFailures;
    AsyncWebServerResponse* errorResponse = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"state_snapshot_unavailable\"}");
    addAsyncCommonHeaders(errorResponse, false);
    errorResponse->addHeader("X-GrowTent-State-Cache", cacheStaleShared ? "stale-shared" : (cacheHit ? "hit" : "miss"));
    errorResponse->addHeader("X-GrowTent-State-Transport", "shared-cache-direct-buffer-fixed-length");
    errorResponse->addHeader("X-GrowTent-State-Block-Max", "0");
    errorResponse->addHeader("X-GrowTent-State-Format", "compact");
    errorResponse->addHeader("X-GrowTent-Endpoint", isStats ? "stats" : "state");
    request->send(errorResponse);
    return;
  }

  // Use ESPAsyncWebServer's native fixed-length byte-buffer response instead
  // of AsyncCallbackResponse. The shared cache remains immutable for the full
  // connection lifetime (reader count below), so AsyncProgmemResponse can read
  // it directly without a request-local ~5-6 kB body allocation. This also
  // avoids the callback tail stall observed when the body no longer fits into
  // the first AsyncTCP send window (v0.3.03: 5341-byte body, repeated 5330-byte
  // partial transfers under parallel state/stats load).
  AsyncWebServerResponse* response = request->beginResponse(
    200,
    "application/json; charset=utf-8",
    reinterpret_cast<const uint8_t*>(sourcePtr),
    sourceLength);

  if (!response) {
    releaseAsyncStateReader();
    ++g_asyncStateResponseBufferFailures;
    AsyncWebServerResponse* errorResponse = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"state_response_unavailable\"}");
    addAsyncCommonHeaders(errorResponse, false);
    errorResponse->addHeader("X-GrowTent-State-Cache", cacheStaleShared ? "stale-shared" : (cacheHit ? "hit" : "miss"));
    errorResponse->addHeader("X-GrowTent-State-Transport", "shared-cache-direct-buffer-fixed-length");
    errorResponse->addHeader("X-GrowTent-State-Block-Max", "0");
    errorResponse->addHeader("X-GrowTent-State-Format", "compact");
    errorResponse->addHeader("X-GrowTent-Endpoint", isStats ? "stats" : "state");
    request->send(errorResponse);
    return;
  }

  request->onDisconnect([]() { releaseAsyncStateReader(); });

  addAsyncCommonHeaders(response, false);
  response->addHeader("Connection", "close");
  response->addHeader("X-GrowTent-State-Cache", cacheStaleShared ? "stale-shared" : (cacheHit ? "hit" : "miss"));
  response->addHeader("X-GrowTent-State-Transport", "shared-cache-direct-buffer-fixed-length");
  response->addHeader("X-GrowTent-State-Block-Max", "0");
  response->addHeader("X-GrowTent-State-Format", "compact");
  response->addHeader("X-GrowTent-State-Source-Length", String(sourceLength));
  response->addHeader("X-GrowTent-Endpoint", isStats ? "stats" : "state");
  request->send(response);
}

static void handleAsyncState(AsyncWebServerRequest* request) {
  handleAsyncStateResponse(request, false);
}

static void handleAsyncStats(AsyncWebServerRequest* request) {
  handleAsyncStateResponse(request, true);
}

static void handleAsyncHint(AsyncWebServerRequest* request) {
  if (!request) return;

  String escapedKey = hintKey;
  escapedKey.replace("\\", "\\\\");
  escapedKey.replace("\"", "\\\"");

  String json;
  json.reserve(64 + escapedKey.length() + hintDetailsJson.length());
  json += "{\"id\":";
  json += String(hintId);
  json += ",\"key\":\"";
  json += escapedKey;
  json += "\",\"details\":";
  json += hintDetailsJson;
  json += "}";

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  request->send(response);
}

static void handleAsyncFsStatus(AsyncWebServerRequest* request) {
  if (!request) return;

  // Read-only mirror of the established port-80 status endpoint. It deliberately
  // never attempts a new mount or format operation.
  const bool ok = isFsMounted();
  String json = "{\"ok\":";
  json += ok ? "true" : "false";
  json += ",\"mountTried\":";
  json += fsMountWasTried() ? "true" : "false";

  if (ok) {
    json += ",\"mounted\":true";
    json += ",\"total\":" + String((uint32_t)LittleFS.totalBytes());
    json += ",\"used\":" + String((uint32_t)LittleFS.usedBytes());
    json += ",\"diaryExists\":";
    json += LittleFS.exists(DIARY_PATH) ? "true" : "false";
  } else {
    json += ",\"mounted\":false";
    json += ",\"mountFailed\":";
    json += fsMountFailed() ? "true" : "false";
    json += ",\"err\":\"fs_not_mounted\"";
  }
  json += "}";

  AsyncWebServerResponse* response =
    request->beginResponse(ok ? 200 : 503, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  request->send(response);
}

static bool buildAsyncLogSnapshot(String& out) {
  out = "";
  if (!logBufferMutex || xSemaphoreTake(logBufferMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }

  // The buffer is capped at 50 lines. Copy it while holding the mutex, then
  // release the mutex BEFORE the network response is queued/sent. This is the
  // key difference from the legacy synchronous log endpoint.
  size_t approx = 0;
  for (const auto& line : logBuffer) approx += line.length() + 1;
  out.reserve(approx + 1);
  for (const auto& line : logBuffer) {
    out += line;
    out += '\n';
  }

  xSemaphoreGive(logBufferMutex);
  return true;
}

static void handleAsyncLogBuffer(AsyncWebServerRequest* request, bool download) {
  if (!request) return;

  String snapshot;
  if (!buildAsyncLogSnapshot(snapshot)) {
    AsyncWebServerResponse* response =
      request->beginResponse(503, "text/plain; charset=utf-8", "log busy");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  AsyncWebServerResponse* response =
    request->beginResponse(200, "text/plain; charset=utf-8", snapshot);
  addAsyncCommonHeaders(response, false);
  if (download) {
    response->addHeader("Content-Disposition", "attachment; filename=weblog.txt");
  }
  request->send(response);
}


static String asyncJsonEscape(const String& in) {
  String out = in;
  out.replace("\\", "\\\\");
  out.replace("\"", "\\\"");
  out.replace("\r", " ");
  out.replace("\n", " ");
  return out;
}

// Read-only diary list for the staged AsyncWebServer sidecar.
// IMPORTANT: Unlike the legacy handler this never calls ensureFsMounted() and
// never runs ensureDiaryHasId(), because both can mutate persistent storage.
// Read requests remain non-mutating; Phase 25 queues all diary mutations to the main loop.
static void handleAsyncDiaryList(AsyncWebServerRequest* request) {
  if (!request) return;

  if (!isFsMounted() || !LittleFS.exists(DIARY_PATH)) {
    AsyncWebServerResponse* response =
      request->beginResponse(200, "application/json; charset=utf-8", "{\"items\":[]}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  File f = LittleFS.open(DIARY_PATH, FILE_READ);
  if (!f) {
    AsyncWebServerResponse* response =
      request->beginResponse(500, "application/json; charset=utf-8", "{\"items\":[],\"err\":\"open\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const int MAX_ITEMS = 50;
  String items[MAX_ITEMS];
  int count = 0;
  bool firstLine = true;
  bool newSchema = true;
  uint32_t fallbackLineNo = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;

    if (firstLine) {
      firstLine = false;
      if (line.startsWith("id,")) {
        newSchema = true;
        continue;
      }
      if (line.startsWith("ts_local,")) {
        newSchema = false;
        continue;
      }
      // No recognizable header: parse the first line as data without writing
      // anything back to LittleFS.
    }

    String fields[8];
    int outCount = 0;
    if (!csvSplitLine(line, fields, 8, outCount)) continue;

    String diaryId;
    String tsLocal;
    String phase;
    String note;

    if (newSchema && outCount >= 8) {
      diaryId = csvFieldToString(fields[0]);
      tsLocal = csvFieldToString(fields[1]);
      phase   = csvFieldToString(fields[2]);
      note    = csvFieldToString(fields[7]);
    } else if (!newSchema && outCount >= 7) {
      tsLocal = csvFieldToString(fields[0]);
      phase   = csvFieldToString(fields[1]);
      note    = csvFieldToString(fields[6]);
      // Use the exact same deterministic ID formula as ensureDiaryHasId().
      // This keeps edit/delete IDs stable if the first mutation migrates an old
      // seven-column diary file in the main loop.
      diaryId = String(fnv1a32(tsLocal + "|" + phase + "|" + note + "|" + String(fallbackLineNo++)));
    } else {
      continue;
    }

    const time_t ts = parseLocalTimestamp(tsLocal);
    String preview = note;
    preview.trim();

    String obj;
    obj.reserve(256 + note.length());
    obj += "{\"id\":\"" + asyncJsonEscape(diaryId) + "\",\"date\":\"" + asyncJsonEscape(tsLocal) + "\"";
    if (ts > 0) obj += ",\"ts\":" + String((uint32_t)ts);
    if (phase.length()) obj += ",\"phase\":\"" + asyncJsonEscape(phase) + "\"";
    if (note.length()) obj += ",\"note\":\"" + asyncJsonEscape(note) + "\"";
    if (preview.length()) obj += ",\"preview\":\"" + asyncJsonEscape(preview) + "\"";
    obj += "}";

    if (count < MAX_ITEMS) {
      items[count++] = obj;
    } else {
      for (int i = 1; i < MAX_ITEMS; i++) items[i - 1] = items[i];
      items[MAX_ITEMS - 1] = obj;
    }
  }
  f.close();

  String json;
  size_t reserveLen = 32;
  for (int i = 0; i < count; i++) reserveLen += items[i].length() + 1;
  json.reserve(reserveLen);
  json += "{\"items\":[";
  for (int i = 0; i < count; i++) {
    if (i) json += ',';
    json += items[i];
  }
  json += "]}";

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  request->send(response);
}

static void handleAsyncDiaryDownload(AsyncWebServerRequest* request) {
  if (!request) return;

  if (!isFsMounted() || !LittleFS.exists(DIARY_PATH)) {
    AsyncWebServerResponse* response = request->beginResponse(
      200,
      "text/csv; charset=utf-8",
      "ts_local,phase,grow_day,grow_week,phase_day,phase_week,note\n");
    addAsyncCommonHeaders(response, false);
    response->addHeader("Content-Disposition", "attachment; filename=\"growdiary.csv\"");
    request->send(response);
    return;
  }

  AsyncWebServerResponse* response =
    request->beginResponse(LittleFS, DIARY_PATH, "text/csv; charset=utf-8", true);
  addAsyncCommonHeaders(response, false);
  request->send(response);
}


// -------------------- Phase 5: first controlled async write paths --------------------
// Real migrated write route: clear the in-RAM web log buffer. This mirrors the
// legacy /api/logbuffer/clear semantics but sends the Async response only after
// the mutex has been released.
static void handleAsyncClearLog(AsyncWebServerRequest* request) {
  if (!request) return;

  if (!logBufferMutex || xSemaphoreTake(logBufferMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    AsyncWebServerResponse* response =
      request->beginResponse(503, "text/plain; charset=utf-8", "log busy");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const size_t clearedLines = logBuffer.size();
  logBuffer.clear();
  xSemaphoreGive(logBufferMutex);

  ++g_asyncLogClearRequests;

  AsyncWebServerResponse* response =
    request->beginResponse(204, "text/plain; charset=utf-8", "");
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Log-Cleared-Lines", String((uint32_t)clearedLines));
  request->send(response);
}

// Small request-local JSON body collector used by the dedicated Async settings
// POST routes. AsyncWebServerRequest frees _tempObject with free() when the
// request is destroyed, so this remains a fixed POD allocation.
struct AsyncJsonBody {
  size_t len;
  bool tooLarge;
  char data[1025];
};

static void handleAsyncJsonBody(
    AsyncWebServerRequest* request,
    uint8_t* data,
    size_t len,
    size_t index,
    size_t total) {
  if (!request) return;

  if (index == 0) {
    AsyncJsonBody* body =
      static_cast<AsyncJsonBody*>(calloc(1, sizeof(AsyncJsonBody)));
    if (!body) return;
    body->tooLarge = total > 1024;
    request->_tempObject = body;
  }

  auto* body = static_cast<AsyncJsonBody*>(request->_tempObject);
  if (!body || body->tooLarge) return;

  const size_t available = sizeof(body->data) - 1 - body->len;
  const size_t copyLen = std::min(len, available);
  if (copyLen > 0) {
    memcpy(body->data + body->len, data, copyLen);
    body->len += copyLen;
    body->data[body->len] = '\0';
  }

  if (copyLen != len) {
    body->tooLarge = true;
  }
}


// -------------------- Phase 7: safe persistent UI Preferences writes --------------------
// Phase 6 proved the theme write path. Phase 7 expands the exact same pattern to
// language, temperature-unit preference and time-format preference. These are
// presentation-only keys: no relay, pump, Shelly, sensor, irrigation, grow-control,
// Wi-Fi or OTA state is touched. A request-local Preferences instance avoids
// sharing begin()/end() state with the legacy port-80 settings handler.
static bool readAsyncStoredUiSetting(const char* key, const String& fallback, String& stored) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  stored = prefGetStringOrDefault(prefs, key, fallback);
  prefs.end();
  return true;
}

static void sendAsyncUiSettingState(
    AsyncWebServerRequest* request,
    const char* setting,
    const char* key,
    const String& runtimeValue,
    volatile uint32_t* failureCounter,
    int code = 200) {
  if (!request) return;

  String stored;
  if (!readAsyncStoredUiSetting(key, runtimeValue, stored)) {
    if (failureCounter) ++(*failureCounter);
    AsyncWebServerResponse* response =
      request->beginResponse(500, "application/json; charset=utf-8",
                             "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  String json;
  json.reserve(176);
  json += "{\"ok\":true,\"setting\":\"";
  json += setting;
  json += "\",\"stored\":\"";
  json += asyncJsonEscape(stored);
  json += "\",\"runtime\":\"";
  json += asyncJsonEscape(runtimeValue);
  json += "\",\"restartRequired\":";
  json += (stored == runtimeValue ? "false" : "true");
  json += "}";

  AsyncWebServerResponse* response =
    request->beginResponse(code, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Prefs-Source", "nvs");
  request->send(response);
}

static bool isAllowedAsyncUiValue(const char* setting, const String& value) {
  if (strcmp(setting, "theme") == 0) {
    return value == "light" || value == "dark";
  }
  if (strcmp(setting, "language") == 0) {
    return value == "de" || value == "en";
  }
  if (strcmp(setting, "unit") == 0) {
    // Keep both historical/default values and current form values restorable.
    return value == "metric" || value == "imperial" || value == "C" || value == "F";
  }
  if (strcmp(setting, "timeFormat") == 0) {
    // Existing firmware has used both 24h/12h defaults and 24/12 form values.
    return value == "24h" || value == "12h" || value == "24" || value == "12";
  }
  if (strcmp(setting, "boxName") == 0) {
    // Keep the first free-form persistent Async setting deliberately bounded.
    // String::length() counts UTF-8 bytes; 64 bytes is enough for the UI while
    // preventing oversized NVS/UI values. Reject ASCII control characters.
    if (value.length() == 0 || value.length() > 64) return false;
    for (size_t i = 0; i < value.length(); ++i) {
      const uint8_t c = static_cast<uint8_t>(value[i]);
      if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
  }
  if (strcmp(setting, "ds18b20Name") == 0) {
    // Match the existing UI's generous display-name allowance while bounding the
    // request. Empty is valid because the legacy form also permits it.
    if (value.length() > 400) return false;
    for (size_t i = 0; i < value.length(); ++i) {
      const uint8_t c = static_cast<uint8_t>(value[i]);
      if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
  }
  if (strncmp(setting, "relayName", 9) == 0) {
    // Existing relay-name inputs use maxlength=15. Empty labels remain legal to
    // preserve legacy behaviour.
    if (value.length() > 15) return false;
    for (size_t i = 0; i < value.length(); ++i) {
      const uint8_t c = static_cast<uint8_t>(value[i]);
      if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
  }
  if (strcmp(setting, "ntpServer") == 0) {
    // Legacy /savesettings accepts a free-form string. Keep empty values
    // restorable and reject only oversized/control-character input. 253 bytes
    // covers the DNS host-name limit while still allowing IPv4/IPv6 literals.
    if (value.length() > 253) return false;
    for (size_t i = 0; i < value.length(); ++i) {
      const uint8_t c = static_cast<uint8_t>(value[i]);
      if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
  }
  if (strcmp(setting, "timeZoneInfo") == 0) {
    // POSIX TZ strings are kept opaque just like the legacy settings form.
    // Empty historical values remain restorable; only size/control characters
    // are constrained to keep the JSON/NVS write bounded.
    if (value.length() > 255) return false;
    for (size_t i = 0; i < value.length(); ++i) {
      const uint8_t c = static_cast<uint8_t>(value[i]);
      if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
  }
  return false;
}

static const char* asyncUiInvalidError(const char* setting) {
  if (strcmp(setting, "theme") == 0) return "invalid_theme";
  if (strcmp(setting, "language") == 0) return "invalid_language";
  if (strcmp(setting, "unit") == 0) return "invalid_unit";
  if (strcmp(setting, "timeFormat") == 0) return "invalid_time_format";
  if (strcmp(setting, "boxName") == 0) return "invalid_box_name";
  if (strcmp(setting, "ds18b20Name") == 0) return "invalid_ds18b20_name";
  if (strncmp(setting, "relayName", 9) == 0) return "invalid_relay_name";
  if (strcmp(setting, "ntpServer") == 0) return "invalid_ntp_server";
  if (strcmp(setting, "timeZoneInfo") == 0) return "invalid_time_zone";
  return "invalid_setting";
}

static void logAsyncSystemSettingChanged(const char* setting) {
  if (!setting || !setting[0]) return;
  ++g_asyncSystemSettingsChangedLogs;
  logPrint(String("[ASYNC][SYSTEM SETTINGS] ") + setting + " changed");
}

static void handleAsyncUiSettingPost(
    AsyncWebServerRequest* request,
    const char* setting,
    const char* jsonField,
    const char* key,
    const String& runtimeValue,
    volatile uint32_t* requestCounter,
    volatile uint32_t* failureCounter) {
  if (!request) return;

  auto* body = static_cast<AsyncJsonBody*>(request->_tempObject);
  if (!body) {
    AsyncWebServerResponse* response =
      request->beginResponse(400, "application/json; charset=utf-8",
                             "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (body->tooLarge) {
    AsyncWebServerResponse* response =
      request->beginResponse(413, "application/json; charset=utf-8",
                             "{\"ok\":false,\"err\":\"body_too_large\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc[jsonField].isNull()) {
    AsyncWebServerResponse* response =
      request->beginResponse(400, "application/json; charset=utf-8",
                             "{\"ok\":false,\"err\":\"bad_json\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const String requested = doc[jsonField].as<String>();
  if (!isAllowedAsyncUiValue(setting, requested)) {
    String json = "{\"ok\":false,\"err\":\"";
    json += asyncUiInvalidError(setting);
    json += "\"}";
    AsyncWebServerResponse* response =
      request->beginResponse(400, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (requestCounter) ++(*requestCounter);

  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) {
    if (failureCounter) ++(*failureCounter);
    AsyncWebServerResponse* response =
      request->beginResponse(500, "application/json; charset=utf-8",
                             "{\"ok\":false,\"err\":\"prefs_open_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const String previous = prefGetStringOrDefault(prefs, key, runtimeValue);
  const bool changed = previous != requested;
  const size_t written = prefs.putString(key, requested);
  const String verify = prefs.getString(key, "");
  prefs.end();

  if ((requested.length() > 0 && written == 0) || verify != requested) {
    if (failureCounter) ++(*failureCounter);
    AsyncWebServerResponse* response =
      request->beginResponse(500, "application/json; charset=utf-8",
                             "{\"ok\":false,\"err\":\"prefs_verify_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (changed) {
    logAsyncSystemSettingChanged(setting);
  }

  // Do not mutate global runtime Strings from the Async task. The established
  // general-settings flow applies these values after restart. This also avoids
  // races with the HTML renderer and tasks that read `language`.
  sendAsyncUiSettingState(request, setting, key, runtimeValue, failureCounter, 200);
}

static void handleAsyncThemeGet(AsyncWebServerRequest* request) {
  sendAsyncUiSettingState(request, "theme", KEY_THEME, theme, &g_asyncThemeWriteFailures, 200);
}
static void handleAsyncThemePost(AsyncWebServerRequest* request) {
  handleAsyncUiSettingPost(request, "theme", "theme", KEY_THEME, theme,
                           &g_asyncThemeWriteRequests, &g_asyncThemeWriteFailures);
}

static void handleAsyncLanguageGet(AsyncWebServerRequest* request) {
  sendAsyncUiSettingState(request, "language", KEY_LANG, language, &g_asyncLanguageWriteFailures, 200);
}
static void handleAsyncLanguagePost(AsyncWebServerRequest* request) {
  handleAsyncUiSettingPost(request, "language", "language", KEY_LANG, language,
                           &g_asyncLanguageWriteRequests, &g_asyncLanguageWriteFailures);
}

static void handleAsyncUnitGet(AsyncWebServerRequest* request) {
  sendAsyncUiSettingState(request, "unit", KEY_UNIT, unit, &g_asyncUnitWriteFailures, 200);
}
static void handleAsyncUnitPost(AsyncWebServerRequest* request) {
  handleAsyncUiSettingPost(request, "unit", "unit", KEY_UNIT, unit,
                           &g_asyncUnitWriteRequests, &g_asyncUnitWriteFailures);
}

static void handleAsyncTimeFormatGet(AsyncWebServerRequest* request) {
  sendAsyncUiSettingState(request, "timeFormat", KEY_TFMT, timeFormat, &g_asyncTimeFormatWriteFailures, 200);
}
static void handleAsyncTimeFormatPost(AsyncWebServerRequest* request) {
  handleAsyncUiSettingPost(request, "timeFormat", "timeFormat", KEY_TFMT, timeFormat,
                           &g_asyncTimeFormatWriteRequests, &g_asyncTimeFormatWriteFailures);
}

static void handleAsyncBoxNameGet(AsyncWebServerRequest* request) {
  sendAsyncUiSettingState(request, "boxName", KEY_NAME, settings.ui.boxName, &g_asyncBoxNameWriteFailures, 200);
}
static void handleAsyncBoxNamePost(AsyncWebServerRequest* request) {
  handleAsyncUiSettingPost(request, "boxName", "boxName", KEY_NAME, settings.ui.boxName,
                           &g_asyncBoxNameWriteRequests, &g_asyncBoxNameWriteFailures);
}

static void handleAsyncNtpServerGet(AsyncWebServerRequest* request) {
  sendAsyncUiSettingState(request, "ntpServer", KEY_NTPSRV, ntpServer,
                          &g_asyncNtpServerWriteFailures, 200);
}
static void handleAsyncNtpServerPost(AsyncWebServerRequest* request) {
  handleAsyncUiSettingPost(request, "ntpServer", "ntpServer", KEY_NTPSRV, ntpServer,
                           &g_asyncNtpServerWriteRequests, &g_asyncNtpServerWriteFailures);
}

static void handleAsyncTimeZoneGet(AsyncWebServerRequest* request) {
  sendAsyncUiSettingState(request, "timeZoneInfo", KEY_TZINFO, tzInfo,
                          &g_asyncTimeZoneWriteFailures, 200);
}
static void handleAsyncTimeZonePost(AsyncWebServerRequest* request) {
  handleAsyncUiSettingPost(request, "timeZoneInfo", "timeZoneInfo", KEY_TZINFO, tzInfo,
                           &g_asyncTimeZoneWriteRequests, &g_asyncTimeZoneWriteFailures);
}

static void handleAsyncDs18NameGet(AsyncWebServerRequest* request) {
  sendAsyncUiSettingState(request, "ds18b20Name", KEY_DS18NAME, DS18B20Name,
                          &g_asyncDs18NameWriteFailures, 200);
}
static void handleAsyncDs18NamePost(AsyncWebServerRequest* request) {
  handleAsyncUiSettingPost(request, "ds18b20Name", "ds18b20Name", KEY_DS18NAME, DS18B20Name,
                           &g_asyncDs18NameWriteRequests, &g_asyncDs18NameWriteFailures);
}

static const char* asyncRelayNameKey(size_t idx) {
  static const char* const keys[5] = {KEY_RELAY_1, KEY_RELAY_2, KEY_RELAY_3, KEY_RELAY_4, KEY_RELAY_5};
  return idx < 5 ? keys[idx] : nullptr;
}

static void handleAsyncRelayNameGet(AsyncWebServerRequest* request, size_t idx) {
  if (idx >= 5) return;
  String setting = String("relayName") + String(idx + 1);
  sendAsyncUiSettingState(request, setting.c_str(), asyncRelayNameKey(idx), settings.relay.name[idx],
                          &g_asyncRelayNameWriteFailures, 200);
}

static void handleAsyncRelayNamePost(AsyncWebServerRequest* request, size_t idx) {
  if (idx >= 5) return;
  String setting = String("relayName") + String(idx + 1);
  handleAsyncUiSettingPost(request, setting.c_str(), "relayName", asyncRelayNameKey(idx), settings.relay.name[idx],
                           &g_asyncRelayNameWriteRequests, &g_asyncRelayNameWriteFailures);
}

static bool readAsyncStoredBool(const char* key, bool fallback, bool& stored) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  stored = prefGetBoolOrDefault(prefs, key, fallback);
  prefs.end();
  return true;
}

static void sendAsyncDebugState(AsyncWebServerRequest* request) {
  bool stored = debugLog;
  if (!readAsyncStoredBool(KEY_DEBUG_ENABLED, debugLog, stored)) {
    ++g_asyncDebugWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }
  String json = "{\"ok\":true,\"setting\":\"debug\",\"stored\":";
  json += stored ? "true" : "false";
  json += ",\"runtime\":";
  json += debugLog ? "true" : "false";
  json += ",\"restartRequired\":";
  json += stored == debugLog ? "false" : "true";
  json += "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Prefs-Source", "nvs");
  request->send(response);
}

static void handleAsyncDebugPost(AsyncWebServerRequest* request) {
  auto* body = static_cast<AsyncJsonBody*>(request ? request->_tempObject : nullptr);
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8", body && body->tooLarge ?
      "{\"ok\":false,\"err\":\"body_too_large\"}" : "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["debug"].isNull() || !doc["debug"].is<bool>()) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_debug\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const bool requested = doc["debug"].as<bool>();
  ++g_asyncDebugWriteRequests;
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) {
    ++g_asyncDebugWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_open_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const bool previous = prefGetBoolOrDefault(prefs, KEY_DEBUG_ENABLED, debugLog);
  const bool changed = previous != requested;
  const size_t written = prefs.putBool(KEY_DEBUG_ENABLED, requested);
  const bool verify = prefs.getBool(KEY_DEBUG_ENABLED, !requested);
  prefs.end();
  if (written == 0 || verify != requested) {
    ++g_asyncDebugWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_verify_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (changed) {
    logAsyncSystemSettingChanged("debug");
  }
  sendAsyncDebugState(request);
}

// Phase 14: restart-applied hardware-adjacent settings. These handlers persist and
// verify NVS only; they deliberately do NOT mutate DS18B20 or activeRelayCount at
// runtime. A normal restart applies the saved values through readPreferences()/boot.
static void sendAsyncDs18EnabledState(AsyncWebServerRequest* request) {
  if (!request) return;
  bool stored = DS18B20;
  if (!readAsyncStoredBool(KEY_DS18B20ENABLE, DS18B20, stored)) {
    ++g_asyncDs18EnableWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  String json = "{\"ok\":true,\"setting\":\"ds18b20Enabled\",\"stored\":";
  json += stored ? "true" : "false";
  json += ",\"runtime\":";
  json += DS18B20 ? "true" : "false";
  json += ",\"restartRequired\":";
  json += stored == DS18B20 ? "false" : "true";
  json += "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false); response->addHeader("X-GrowTent-Prefs-Source", "nvs"); request->send(response);
}

static void handleAsyncDs18EnabledPost(AsyncWebServerRequest* request) {
  auto* body = static_cast<AsyncJsonBody*>(request ? request->_tempObject : nullptr);
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8", body && body->tooLarge ?
      "{\"ok\":false,\"err\":\"body_too_large\"}" : "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["ds18b20Enabled"].isNull() || !doc["ds18b20Enabled"].is<bool>()) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_ds18b20_enabled\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const bool requested = doc["ds18b20Enabled"].as<bool>();
  ++g_asyncDs18EnableWriteRequests;
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) {
    ++g_asyncDs18EnableWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_open_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const bool previous = prefGetBoolOrDefault(prefs, KEY_DS18B20ENABLE, DS18B20);
  const bool changed = previous != requested;
  const size_t written = prefs.putBool(KEY_DS18B20ENABLE, requested);
  const bool verify = prefs.getBool(KEY_DS18B20ENABLE, !requested);
  prefs.end();
  if (written == 0 || verify != requested) {
    ++g_asyncDs18EnableWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_verify_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (changed) {
    logAsyncSystemSettingChanged("ds18b20Enabled");
  }
  sendAsyncDs18EnabledState(request);
}

static bool readAsyncStoredInt(const char* key, int fallback, int& stored) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  stored = prefGetIntOrDefault(prefs, key, fallback);
  prefs.end();
  return true;
}

static void sendAsyncRelayCountState(AsyncWebServerRequest* request) {
  if (!request) return;
  int stored = activeRelayCount;
  if (!readAsyncStoredInt(KEY_RELAYCOUNT, activeRelayCount, stored)) {
    ++g_asyncRelayCountWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  String json = "{\"ok\":true,\"setting\":\"activeRelayCount\",\"stored\":" + String(stored);
  json += ",\"runtime\":" + String(activeRelayCount);
  json += ",\"restartRequired\":";
  json += stored == activeRelayCount ? "false" : "true";
  json += "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false); response->addHeader("X-GrowTent-Prefs-Source", "nvs"); request->send(response);
}

static void handleAsyncRelayCountPost(AsyncWebServerRequest* request) {
  auto* body = static_cast<AsyncJsonBody*>(request ? request->_tempObject : nullptr);
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8", body && body->tooLarge ?
      "{\"ok\":false,\"err\":\"body_too_large\"}" : "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["activeRelayCount"].isNull() || !doc["activeRelayCount"].is<int>()) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_active_relay_count\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const int requested = doc["activeRelayCount"].as<int>();
  if (requested != 4 && requested != 8) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_active_relay_count\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  ++g_asyncRelayCountWriteRequests;
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) {
    ++g_asyncRelayCountWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_open_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const int previous = prefGetIntOrDefault(prefs, KEY_RELAYCOUNT, activeRelayCount);
  const bool changed = previous != requested;
  const size_t written = prefs.putInt(KEY_RELAYCOUNT, requested);
  const int verify = prefs.getInt(KEY_RELAYCOUNT, -1);
  prefs.end();
  if (written == 0 || verify != requested) {
    ++g_asyncRelayCountWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_verify_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (changed) {
    logAsyncSystemSettingChanged("activeRelayCount");
  }
  sendAsyncRelayCountState(request);
}


// Phase 15: Shelly configuration persistence. Device roles are implicit in the
// endpoint path and map directly to the existing settings.shelly.* structures.
// Writes are NVS-only and never mutate the running ShellyDevice objects, so the
// existing Shelly status task continues using the boot-loaded configuration.
static const char* asyncShellyRoleName(uint8_t idx) {
  switch (idx) {
    case 0: return "main";
    case 1: return "light";
    case 2: return "humidifier";
    case 3: return "heater";
    case 4: return "fan";
    case 5: return "exhaust";
    case 6: return "heatmat";
    default: return nullptr;
  }
}

static const char* asyncShellyIpKey(uint8_t idx) {
  switch (idx) {
    case 0: return KEY_SHELLYMAINIP;
    case 1: return KEY_SHELLYLIGHTIP;
    case 2: return KEY_SHELLYHUMIP;
    case 3: return KEY_SHELLYHEATERIP;
    case 4: return KEY_SHELLYFANIP;
    case 5: return KEY_SHELLYEXHAUSTIP;
    case 6: return KEY_SHELLYHEATMATIP;
    default: return nullptr;
  }
}

static const char* asyncShellyGenKey(uint8_t idx) {
  switch (idx) {
    case 0: return KEY_SHELLYMAINGEN;
    case 1: return KEY_SHELLYLIGHTGEN;
    case 2: return KEY_SHELLYHUMGEN;
    case 3: return KEY_SHELLYHEATERGEN;
    case 4: return KEY_SHELLYFANGEN;
    case 5: return KEY_SHELLYEXHAUSTGEN;
    case 6: return KEY_SHELLYHEATMATGEN;
    default: return nullptr;
  }
}

static const ShellyDevice* asyncShellyRuntimeDevice(uint8_t idx) {
  switch (idx) {
    case 0: return &settings.shelly.main;
    case 1: return &settings.shelly.light;
    case 2: return &settings.shelly.humidifier;
    case 3: return &settings.shelly.heater;
    case 4: return &settings.shelly.fan;
    case 5: return &settings.shelly.exhaust;
    case 6: return &settings.shelly.heatmat;
    default: return nullptr;
  }
}

static ShellyDevice* asyncShellyRuntimeDeviceMutable(uint8_t idx) {
  switch (idx) {
    case 0: return &settings.shelly.main;
    case 1: return &settings.shelly.light;
    case 2: return &settings.shelly.humidifier;
    case 3: return &settings.shelly.heater;
    case 4: return &settings.shelly.fan;
    case 5: return &settings.shelly.exhaust;
    case 6: return &settings.shelly.heatmat;
    default: return nullptr;
  }
}

static bool readAsyncStoredShellyDevice(uint8_t idx, String& ip, int& gen) {
  const ShellyDevice* runtime = asyncShellyRuntimeDevice(idx);
  const char* ipKey = asyncShellyIpKey(idx);
  const char* genKey = asyncShellyGenKey(idx);
  if (!runtime || !ipKey || !genKey) return false;

  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  ip = prefGetStringOrDefault(prefs, ipKey, runtime->ip);
  gen = prefGetIntOrDefault(prefs, genKey, runtime->gen);
  prefs.end();
  return true;
}

static void sendAsyncShellyDeviceState(AsyncWebServerRequest* request, uint8_t idx) {
  if (!request) return;
  const ShellyDevice* runtime = asyncShellyRuntimeDevice(idx);
  const char* role = asyncShellyRoleName(idx);
  if (!runtime || !role) {
    AsyncWebServerResponse* response = request->beginResponse(404, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"unknown_shelly_role\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  String storedIp;
  int storedGen = runtime->gen;
  if (!readAsyncStoredShellyDevice(idx, storedIp, storedGen)) {
    ++g_asyncShellyDeviceWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  String json;
  json.reserve(256);
  json += "{\"ok\":true,\"setting\":\"shellyDevice\",\"role\":\"";
  json += role;
  json += "\",\"storedIp\":\"" + asyncJsonEscape(storedIp) + "\"";
  json += ",\"storedGen\":" + String(storedGen);
  json += ",\"runtimeIp\":\"" + asyncJsonEscape(runtime->ip) + "\"";
  json += ",\"runtimeGen\":" + String(runtime->gen);
  json += ",\"restartRequired\":";
  json += (storedIp == runtime->ip && storedGen == runtime->gen) ? "false" : "true";
  json += "}";

  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Prefs-Source", "nvs");
  request->send(response);
}

static bool normalizeAsyncShellyIpv4(String& ip) {
  ip.trim();
  if (ip.isEmpty()) return true;
  if (ip.length() > 15) return false;
  IPAddress parsed;
  if (!parsed.fromString(ip)) return false;
  ip = parsed.toString();
  return true;
}

static void handleAsyncShellyDevicePost(AsyncWebServerRequest* request, uint8_t idx) {
  if (!request) return;
  const ShellyDevice* runtime = asyncShellyRuntimeDevice(idx);
  const char* ipKey = asyncShellyIpKey(idx);
  const char* genKey = asyncShellyGenKey(idx);
  if (!runtime || !ipKey || !genKey) {
    AsyncWebServerResponse* response = request->beginResponse(404, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"unknown_shelly_role\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  auto* body = static_cast<AsyncJsonBody*>(request->_tempObject);
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8", body && body->tooLarge ?
      "{\"ok\":false,\"err\":\"body_too_large\"}" : "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["ip"].isNull() || !doc["ip"].is<const char*>() ||
      doc["gen"].isNull() || !doc["gen"].is<int>()) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_shelly_device\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  String requestedIp = doc["ip"].as<String>();
  const int requestedGen = doc["gen"].as<int>();
  if (!normalizeAsyncShellyIpv4(requestedIp)) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_shelly_ip\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  // Generation 0 exists as the firmware's unconfigured/default representation and
  // must remain restorable. Configured devices normally use Gen1/Gen2/Gen3.
  if (requestedGen < 0 || requestedGen > 4) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_shelly_gen\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  ++g_asyncShellyDeviceWriteRequests;
  String oldIp;
  int oldGen = runtime->gen;
  if (!readAsyncStoredShellyDevice(idx, oldIp, oldGen)) {
    ++g_asyncShellyDeviceWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) {
    ++g_asyncShellyDeviceWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_open_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  prefs.putString(ipKey, requestedIp);
  prefs.putInt(genKey, requestedGen);
  const String verifyIp = prefs.getString(ipKey, "__verify_failed__");
  const int verifyGen = prefs.getInt(genKey, -99);
  if (verifyIp != requestedIp || verifyGen != requestedGen) {
    // Best-effort rollback prevents a partially written device tuple.
    prefs.putString(ipKey, oldIp);
    prefs.putInt(genKey, oldGen);
    prefs.end();
    ++g_asyncShellyDeviceWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_verify_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  prefs.end();
  sendAsyncShellyDeviceState(request, idx);
}

static void sendAsyncShellyAuthState(AsyncWebServerRequest* request) {
  if (!request) return;
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) {
    ++g_asyncShellyAuthWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const String storedUser = prefGetStringOrDefault(prefs, KEY_SHELLYUSERNAME, settings.shelly.username);
  const String storedPassword = prefGetStringOrDefault(prefs, KEY_SHELLYPASSWORD, settings.shelly.password);
  prefs.end();

  String json;
  json.reserve(256);
  json += "{\"ok\":true,\"setting\":\"shellyAuth\",\"storedUsername\":\"";
  json += asyncJsonEscape(storedUser);
  json += "\",\"runtimeUsername\":\"" + asyncJsonEscape(settings.shelly.username) + "\"";
  json += ",\"storedPasswordSet\":";
  json += storedPassword.isEmpty() ? "false" : "true";
  json += ",\"runtimePasswordSet\":";
  json += settings.shelly.password.isEmpty() ? "false" : "true";
  json += ",\"passwordRestartRequired\":";
  json += storedPassword == settings.shelly.password ? "false" : "true";
  json += ",\"restartRequired\":";
  json += (storedUser == settings.shelly.username && storedPassword == settings.shelly.password) ? "false" : "true";
  json += "}";

  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Prefs-Source", "nvs");
  request->send(response);
}

static void handleAsyncShellyAuthPost(AsyncWebServerRequest* request) {
  if (!request) return;
  auto* body = static_cast<AsyncJsonBody*>(request->_tempObject);
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8", body && body->tooLarge ?
      "{\"ok\":false,\"err\":\"body_too_large\"}" : "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["username"].isNull() || !doc["username"].is<const char*>()) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_shelly_username\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  String requested = doc["username"].as<String>();
  if (requested.length() > 128) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_shelly_username\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  for (size_t i = 0; i < requested.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(requested[i]);
    if (c < 0x20 || c == 0x7F) {
      AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
        "{\"ok\":false,\"err\":\"invalid_shelly_username\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return;
    }
  }

  ++g_asyncShellyAuthWriteRequests;
  String oldUsername = settings.shelly.username;
  {
    Preferences readPrefs;
    if (readPrefs.begin(PREF_NS, true)) {
      oldUsername = prefGetStringOrDefault(readPrefs, KEY_SHELLYUSERNAME, settings.shelly.username);
      readPrefs.end();
    }
  }
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) {
    ++g_asyncShellyAuthWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_open_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  prefs.putString(KEY_SHELLYUSERNAME, requested);
  const String verify = prefs.getString(KEY_SHELLYUSERNAME, "__verify_failed__");
  if (verify != requested) {
    prefs.putString(KEY_SHELLYUSERNAME, oldUsername);
    prefs.end();
    ++g_asyncShellyAuthWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_verify_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  prefs.end();
  sendAsyncShellyAuthState(request);
}

static bool readAsyncStoredFloat(const char* key, float fallback, float& stored) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  stored = prefGetFloatOrDefault(prefs, key, fallback);
  prefs.end();
  return true;
}

static void sendAsyncPowerPriceState(AsyncWebServerRequest* request) {
  float stored = powerPriceKwhEur;
  if (!readAsyncStoredFloat(KEY_POWER_PRICE_KWH, powerPriceKwhEur, stored)) {
    ++g_asyncPowerPriceWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  float diff = stored - powerPriceKwhEur; if (diff < 0.0f) diff = -diff;
  String json = "{\"ok\":true,\"setting\":\"powerPrice\",\"stored\":" + String(stored, 3);
  json += ",\"runtime\":" + String(powerPriceKwhEur, 3);
  json += ",\"restartRequired\":"; json += diff > 0.0005f ? "true" : "false"; json += "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false); response->addHeader("X-GrowTent-Prefs-Source", "nvs"); request->send(response);
}

static void handleAsyncPowerPricePost(AsyncWebServerRequest* request) {
  auto* body = static_cast<AsyncJsonBody*>(request ? request->_tempObject : nullptr);
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8", body && body->tooLarge ?
      "{\"ok\":false,\"err\":\"body_too_large\"}" : "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["powerPrice"].isNull() || !doc["powerPrice"].is<double>()) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_power_price\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const double requestedDouble = doc["powerPrice"].as<double>();
  if (requestedDouble < 0.0 || requestedDouble > static_cast<double>(FLT_MAX)) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_power_price\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const float requested = static_cast<float>(requestedDouble);
  ++g_asyncPowerPriceWriteRequests;
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) {
    ++g_asyncPowerPriceWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_open_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const float previous = prefGetFloatOrDefault(prefs, KEY_POWER_PRICE_KWH, powerPriceKwhEur);
  float previousDiff = previous - requested; if (previousDiff < 0.0f) previousDiff = -previousDiff;
  const bool changed = previousDiff > 0.0005f;
  const size_t written = prefs.putFloat(KEY_POWER_PRICE_KWH, requested);
  const float verify = prefs.getFloat(KEY_POWER_PRICE_KWH, -1.0f);
  prefs.end();
  float diff = verify - requested; if (diff < 0.0f) diff = -diff;
  if (written == 0 || diff > 0.0005f) {
    ++g_asyncPowerPriceWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_verify_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (changed) {
    logAsyncSystemSettingChanged("powerPrice");
  }
  sendAsyncPowerPriceState(request);
}

// -------------------- Phase 17: runtime-applied grow targets --------------------
static bool readAsyncGrowTargetsStored(float& targetTemp, float& targetVpd, float& leafOffset) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  targetTemp = prefGetFloatOrDefault(prefs, KEY_TARGETTEMP, settings.grow.targetTemperature);
  targetVpd = prefGetFloatOrDefault(prefs, KEY_TARGETVPD, settings.grow.targetVPD);
  leafOffset = prefGetFloatOrDefault(prefs, KEY_LEAFTEMP, settings.grow.offsetLeafTemperature);
  prefs.end();
  return true;
}

static bool asyncGrowTargetsControlOutputsConfigured() {
  if (settings.heating.sourceType != 0) return true;
  if (settings.shelly.humidifier.ip.length() > 0) return true;
  if (settings.shelly.exhaust.ip.length() > 0) return true;
  return false;
}

static void sendAsyncGrowTargetsState(AsyncWebServerRequest* request) {
  if (!request) return;

  float storedTemp = settings.grow.targetTemperature;
  float storedVpd = settings.grow.targetVPD;
  float storedLeaf = settings.grow.offsetLeafTemperature;
  if (!readAsyncGrowTargetsStored(storedTemp, storedVpd, storedLeaf)) {
    ++g_asyncGrowTargetsWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  bool pending = false;
  bool writeInProgress = false;
  uint32_t pendingId = 0;
  if (g_asyncGrowTargetsMutex &&
      xSemaphoreTake(g_asyncGrowTargetsMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncGrowTargetsPending.pending;
    writeInProgress = g_asyncGrowTargetsPending.writeInProgress;
    pendingId = g_asyncGrowTargetsPending.requestId;
    xSemaphoreGive(g_asyncGrowTargetsMutex);
  }

  String json;
  json.reserve(768);
  json += "{\"ok\":true,\"setting\":\"growTargets\"";
  json += ",\"stored\":{\"targetTemperature\":" + String(storedTemp, 2);
  json += ",\"targetVPD\":" + String(storedVpd, 3);
  json += ",\"offsetLeafTemperature\":" + String(storedLeaf, 2) + "}";
  json += ",\"runtime\":{\"targetTemperature\":" + String(settings.grow.targetTemperature, 2);
  json += ",\"targetVPD\":" + String(settings.grow.targetVPD, 3);
  json += ",\"offsetLeafTemperature\":" + String(settings.grow.offsetLeafTemperature, 2) + "}";
  json += ",\"pending\":"; json += pending ? "true" : "false";
  json += ",\"writeInProgress\":"; json += writeInProgress ? "true" : "false";
  json += ",\"pendingRequestId\":" + String(pendingId);
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncGrowTargetsLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncGrowTargetsLastAppliedId);
  json += ",\"heatingSource\":" + String(settings.heating.sourceType);
  json += ",\"controlOutputsConfigured\":";
  json += asyncGrowTargetsControlOutputsConfigured() ? "true" : "false";
  json += "}";

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static bool restoreAsyncGrowTargetsPrefs(float oldTemp, float oldVpd, float oldLeaf) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) return false;
  prefs.putFloat(KEY_TARGETTEMP, oldTemp);
  prefs.putFloat(KEY_TARGETVPD, oldVpd);
  prefs.putFloat(KEY_LEAFTEMP, oldLeaf);
  const float verifyTemp = prefs.getFloat(KEY_TARGETTEMP, NAN);
  const float verifyVpd = prefs.getFloat(KEY_TARGETVPD, NAN);
  const float verifyLeaf = prefs.getFloat(KEY_LEAFTEMP, NAN);
  prefs.end();
  return isfinite(verifyTemp) && isfinite(verifyVpd) && isfinite(verifyLeaf) &&
         fabsf(verifyTemp - oldTemp) <= 0.0005f &&
         fabsf(verifyVpd - oldVpd) <= 0.0005f &&
         fabsf(verifyLeaf - oldLeaf) <= 0.0005f;
}

static void clearAsyncGrowTargetsWriteReservation() {
  if (!g_asyncGrowTargetsMutex) return;
  if (xSemaphoreTake(g_asyncGrowTargetsMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncGrowTargetsPending.writeInProgress = false;
    xSemaphoreGive(g_asyncGrowTargetsMutex);
  }
}

static void handleAsyncGrowTargetsPost(AsyncWebServerRequest* request) {
  auto* body = static_cast<AsyncJsonBody*>(request ? request->_tempObject : nullptr);
  if (!request) return;
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(
      body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8",
      body && body->tooLarge ?
        "{\"ok\":false,\"err\":\"body_too_large\"}" :
        "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["targetTemperature"].isNull() || doc["targetVPD"].isNull() ||
      doc["offsetLeafTemperature"].isNull() ||
      !doc["targetTemperature"].is<double>() || !doc["targetVPD"].is<double>() ||
      !doc["offsetLeafTemperature"].is<double>()) {
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_grow_targets\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const double tempD = doc["targetTemperature"].as<double>();
  const double vpdD = doc["targetVPD"].as<double>();
  const double leafD = doc["offsetLeafTemperature"].as<double>();
  if (!isfinite(tempD) || !isfinite(vpdD) || !isfinite(leafD) ||
      tempD < 18.0 || tempD > 30.0 ||
      vpdD < 0.50 || vpdD > 3.50 ||
      leafD < -3.0 || leafD > 0.0) {
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"grow_targets_out_of_range\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (!g_asyncGrowTargetsMutex) {
    ++g_asyncGrowTargetsWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  uint32_t requestId = 0;
  if (xSemaphoreTake(g_asyncGrowTargetsMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncGrowTargetsWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_busy\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }
  if (g_asyncGrowTargetsPending.writeInProgress || g_asyncGrowTargetsPending.pending) {
    xSemaphoreGive(g_asyncGrowTargetsMutex);
    AsyncWebServerResponse* response = request->beginResponse(
      409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_apply_pending\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }
  g_asyncGrowTargetsPending.writeInProgress = true;
  requestId = ++g_asyncGrowTargetsNextRequestId;
  g_asyncGrowTargetsPending.requestId = requestId;
  xSemaphoreGive(g_asyncGrowTargetsMutex);

  ++g_asyncGrowTargetsWriteRequests;
  const float requestedTemp = static_cast<float>(tempD);
  const float requestedVpd = static_cast<float>(vpdD);
  const float requestedLeaf = static_cast<float>(leafD);

  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) {
    clearAsyncGrowTargetsWriteReservation();
    ++g_asyncGrowTargetsWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_open_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const float oldTemp = prefGetFloatOrDefault(prefs, KEY_TARGETTEMP, settings.grow.targetTemperature);
  const float oldVpd = prefGetFloatOrDefault(prefs, KEY_TARGETVPD, settings.grow.targetVPD);
  const float oldLeaf = prefGetFloatOrDefault(prefs, KEY_LEAFTEMP, settings.grow.offsetLeafTemperature);
  prefs.putFloat(KEY_TARGETTEMP, requestedTemp);
  prefs.putFloat(KEY_TARGETVPD, requestedVpd);
  prefs.putFloat(KEY_LEAFTEMP, requestedLeaf);
  const float verifyTemp = prefs.getFloat(KEY_TARGETTEMP, NAN);
  const float verifyVpd = prefs.getFloat(KEY_TARGETVPD, NAN);
  const float verifyLeaf = prefs.getFloat(KEY_LEAFTEMP, NAN);
  prefs.end();

  const bool verified = isfinite(verifyTemp) && isfinite(verifyVpd) && isfinite(verifyLeaf) &&
                        fabsf(verifyTemp - requestedTemp) <= 0.0005f &&
                        fabsf(verifyVpd - requestedVpd) <= 0.0005f &&
                        fabsf(verifyLeaf - requestedLeaf) <= 0.0005f;
  if (!verified) {
    const bool rollbackOk = restoreAsyncGrowTargetsPrefs(oldTemp, oldVpd, oldLeaf);
    clearAsyncGrowTargetsWriteReservation();
    ++g_asyncGrowTargetsWriteFailures;
    String json = "{\"ok\":false,\"err\":\"prefs_verify_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false";
    json += "}";
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  bool queued = false;
  if (xSemaphoreTake(g_asyncGrowTargetsMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncGrowTargetsPending.targetTemperature = requestedTemp;
    g_asyncGrowTargetsPending.targetVPD = requestedVpd;
    g_asyncGrowTargetsPending.offsetLeafTemperature = requestedLeaf;
    g_asyncGrowTargetsPending.pending = true;
    g_asyncGrowTargetsPending.writeInProgress = false;
    g_asyncGrowTargetsLastQueuedId = requestId;
    ++g_asyncGrowTargetsQueued;
    queued = true;
    xSemaphoreGive(g_asyncGrowTargetsMutex);
  }

  if (!queued) {
    const bool rollbackOk = restoreAsyncGrowTargetsPrefs(oldTemp, oldVpd, oldLeaf);
    clearAsyncGrowTargetsWriteReservation();
    ++g_asyncGrowTargetsWriteFailures;
    String json = "{\"ok\":false,\"err\":\"runtime_queue_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false";
    json += "}";
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  String json;
  json.reserve(320);
  json += "{\"ok\":true,\"setting\":\"growTargets\",\"requestId\":" + String(requestId);
  json += ",\"stored\":{\"targetTemperature\":" + String(requestedTemp, 2);
  json += ",\"targetVPD\":" + String(requestedVpd, 3);
  json += ",\"offsetLeafTemperature\":" + String(requestedLeaf, 2) + "}";
  json += ",\"applyQueued\":true,\"applyContext\":\"main-loop\"}";
  AsyncWebServerResponse* response = request->beginResponse(
    202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void processAsyncGrowTargetsApply() {
  if (!g_asyncGrowTargetsMutex) return;

  AsyncGrowTargetsPending pending;
  bool hasPending = false;
  if (xSemaphoreTake(g_asyncGrowTargetsMutex, 0) == pdTRUE) {
    if (g_asyncGrowTargetsPending.pending) {
      pending = g_asyncGrowTargetsPending;
      g_asyncGrowTargetsPending.pending = false;
      hasPending = true;
    }
    xSemaphoreGive(g_asyncGrowTargetsMutex);
  }
  if (!hasPending) return;

  // Apply in the normal loop context. Keep both the Settings model and the
  // legacy mirrored globals synchronized exactly like handleSaveRunsettings().
  settings.grow.targetTemperature = pending.targetTemperature;
  targetTemperature = pending.targetTemperature;
  settings.grow.targetVPD = pending.targetVPD;
  targetVPD = pending.targetVPD;
  settings.grow.offsetLeafTemperature = pending.offsetLeafTemperature;
  offsetLeafTemperature = pending.offsetLeafTemperature;

  g_asyncGrowTargetsLastAppliedId = pending.requestId;
  ++g_asyncGrowTargetsApplied;

  // Force the next Async state read to rebuild immediately instead of exposing
  // the old one-second cache snapshot after a successful runtime apply.
  expireAsyncStateCacheMetadata();

  logPrint("[ASYNC][GROW] runtime targets applied id=" + String(pending.requestId) +
           " temp=" + String(pending.targetTemperature, 1) +
           " vpd=" + String(pending.targetVPD, 2) +
           " leafOffset=" + String(pending.offsetLeafTemperature, 1));
}


// -------------------- Phase 18: runtime-applied heater tuning --------------------
static bool readAsyncHeatingTuningStored(
    float& hysteresisC,
    float& predictSeconds,
    int& minOnSeconds,
    int& minOffSeconds) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  hysteresisC = prefGetFloatOrDefault(prefs, KEY_HEAT_HYSTERESIS, settings.heating.hysteresisC);
  predictSeconds = prefGetFloatOrDefault(prefs, KEY_HEAT_PREDICT, settings.heating.predictSeconds);
  minOnSeconds = prefGetIntOrDefault(prefs, KEY_HEAT_MIN_ON, settings.heating.minOnSeconds);
  minOffSeconds = prefGetIntOrDefault(prefs, KEY_HEAT_MIN_OFF, settings.heating.minOffSeconds);
  prefs.end();
  return true;
}

static bool restoreAsyncHeatingTuningPrefs(
    float hysteresisC,
    float predictSeconds,
    int minOnSeconds,
    int minOffSeconds) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) return false;
  prefs.putFloat(KEY_HEAT_HYSTERESIS, hysteresisC);
  prefs.putFloat(KEY_HEAT_PREDICT, predictSeconds);
  prefs.putInt(KEY_HEAT_MIN_ON, minOnSeconds);
  prefs.putInt(KEY_HEAT_MIN_OFF, minOffSeconds);
  const float verifyHyst = prefs.getFloat(KEY_HEAT_HYSTERESIS, NAN);
  const float verifyPredict = prefs.getFloat(KEY_HEAT_PREDICT, NAN);
  const int verifyMinOn = prefs.getInt(KEY_HEAT_MIN_ON, -1);
  const int verifyMinOff = prefs.getInt(KEY_HEAT_MIN_OFF, -1);
  prefs.end();
  return isfinite(verifyHyst) && isfinite(verifyPredict) &&
         fabsf(verifyHyst - hysteresisC) <= 0.0005f &&
         fabsf(verifyPredict - predictSeconds) <= 0.0005f &&
         verifyMinOn == minOnSeconds &&
         verifyMinOff == minOffSeconds;
}

static void clearAsyncHeatingTuningWriteReservation() {
  if (!g_asyncHeatingTuningMutex) return;
  if (xSemaphoreTake(g_asyncHeatingTuningMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncHeatingTuningPending.writeInProgress = false;
    xSemaphoreGive(g_asyncHeatingTuningMutex);
  }
}

static void sendAsyncHeatingTuningState(AsyncWebServerRequest* request) {
  if (!request) return;

  float storedHyst = settings.heating.hysteresisC;
  float storedPredict = settings.heating.predictSeconds;
  int storedMinOn = settings.heating.minOnSeconds;
  int storedMinOff = settings.heating.minOffSeconds;
  if (!readAsyncHeatingTuningStored(storedHyst, storedPredict, storedMinOn, storedMinOff)) {
    ++g_asyncHeatingTuningWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  bool pending = false;
  bool writeInProgress = false;
  uint32_t pendingId = 0;
  if (g_asyncHeatingTuningMutex &&
      xSemaphoreTake(g_asyncHeatingTuningMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncHeatingTuningPending.pending;
    writeInProgress = g_asyncHeatingTuningPending.writeInProgress;
    pendingId = g_asyncHeatingTuningPending.requestId;
    xSemaphoreGive(g_asyncHeatingTuningMutex);
  }

  String json;
  json.reserve(896);
  json += "{\"ok\":true,\"setting\":\"heatingTuning\"";
  json += ",\"stored\":{\"hysteresisC\":" + String(storedHyst, 2);
  json += ",\"predictSeconds\":" + String(storedPredict, 1);
  json += ",\"minOnSeconds\":" + String(storedMinOn);
  json += ",\"minOffSeconds\":" + String(storedMinOff) + "}";
  json += ",\"runtime\":{\"hysteresisC\":" + String(settings.heating.hysteresisC, 2);
  json += ",\"predictSeconds\":" + String(settings.heating.predictSeconds, 1);
  json += ",\"minOnSeconds\":" + String(settings.heating.minOnSeconds);
  json += ",\"minOffSeconds\":" + String(settings.heating.minOffSeconds) + "}";
  json += ",\"pending\":"; json += pending ? "true" : "false";
  json += ",\"writeInProgress\":"; json += writeInProgress ? "true" : "false";
  json += ",\"pendingRequestId\":" + String(pendingId);
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncHeatingTuningLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncHeatingTuningLastAppliedId);
  json += ",\"heatingSource\":" + String(settings.heating.sourceType);
  json += ",\"safeForAutomatedMutation\":";
  json += settings.heating.sourceType == 0 ? "true" : "false";
  json += "}";

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void handleAsyncHeatingTuningPost(AsyncWebServerRequest* request) {
  auto* body = static_cast<AsyncJsonBody*>(request ? request->_tempObject : nullptr);
  if (!request) return;
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(
      body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8",
      body && body->tooLarge ?
        "{\"ok\":false,\"err\":\"body_too_large\"}" :
        "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["hysteresisC"].isNull() || doc["predictSeconds"].isNull() ||
      doc["minOnSeconds"].isNull() || doc["minOffSeconds"].isNull() ||
      !doc["hysteresisC"].is<double>() || !doc["predictSeconds"].is<double>() ||
      !doc["minOnSeconds"].is<int>() || !doc["minOffSeconds"].is<int>()) {
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_heating_tuning\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const double hystD = doc["hysteresisC"].as<double>();
  const double predictD = doc["predictSeconds"].as<double>();
  const int minOn = doc["minOnSeconds"].as<int>();
  const int minOff = doc["minOffSeconds"].as<int>();
  if (!isfinite(hystD) || !isfinite(predictD) ||
      hystD < 0.05 || hystD > 2.0 ||
      predictD < 0.0 || predictD > 600.0 ||
      minOn < 30 || minOn > 1800 ||
      minOff < 30 || minOff > 1800) {
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"heating_tuning_out_of_range\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (!g_asyncHeatingTuningMutex) {
    ++g_asyncHeatingTuningWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  uint32_t requestId = 0;
  if (xSemaphoreTake(g_asyncHeatingTuningMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncHeatingTuningWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_busy\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }
  if (g_asyncHeatingTuningPending.writeInProgress || g_asyncHeatingTuningPending.pending) {
    xSemaphoreGive(g_asyncHeatingTuningMutex);
    AsyncWebServerResponse* response = request->beginResponse(
      409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_apply_pending\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }
  g_asyncHeatingTuningPending.writeInProgress = true;
  requestId = ++g_asyncHeatingTuningNextRequestId;
  g_asyncHeatingTuningPending.requestId = requestId;
  xSemaphoreGive(g_asyncHeatingTuningMutex);

  ++g_asyncHeatingTuningWriteRequests;
  const float requestedHyst = static_cast<float>(hystD);
  const float requestedPredict = static_cast<float>(predictD);

  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) {
    clearAsyncHeatingTuningWriteReservation();
    ++g_asyncHeatingTuningWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_open_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const float oldHyst = prefGetFloatOrDefault(prefs, KEY_HEAT_HYSTERESIS, settings.heating.hysteresisC);
  const float oldPredict = prefGetFloatOrDefault(prefs, KEY_HEAT_PREDICT, settings.heating.predictSeconds);
  const int oldMinOn = prefGetIntOrDefault(prefs, KEY_HEAT_MIN_ON, settings.heating.minOnSeconds);
  const int oldMinOff = prefGetIntOrDefault(prefs, KEY_HEAT_MIN_OFF, settings.heating.minOffSeconds);

  prefs.putFloat(KEY_HEAT_HYSTERESIS, requestedHyst);
  prefs.putFloat(KEY_HEAT_PREDICT, requestedPredict);
  prefs.putInt(KEY_HEAT_MIN_ON, minOn);
  prefs.putInt(KEY_HEAT_MIN_OFF, minOff);
  const float verifyHyst = prefs.getFloat(KEY_HEAT_HYSTERESIS, NAN);
  const float verifyPredict = prefs.getFloat(KEY_HEAT_PREDICT, NAN);
  const int verifyMinOn = prefs.getInt(KEY_HEAT_MIN_ON, -1);
  const int verifyMinOff = prefs.getInt(KEY_HEAT_MIN_OFF, -1);
  prefs.end();

  const bool verified = isfinite(verifyHyst) && isfinite(verifyPredict) &&
                        fabsf(verifyHyst - requestedHyst) <= 0.0005f &&
                        fabsf(verifyPredict - requestedPredict) <= 0.0005f &&
                        verifyMinOn == minOn && verifyMinOff == minOff;
  if (!verified) {
    const bool rollbackOk =
      restoreAsyncHeatingTuningPrefs(oldHyst, oldPredict, oldMinOn, oldMinOff);
    clearAsyncHeatingTuningWriteReservation();
    ++g_asyncHeatingTuningWriteFailures;
    String json = "{\"ok\":false,\"err\":\"prefs_verify_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false";
    json += "}";
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  bool queued = false;
  if (xSemaphoreTake(g_asyncHeatingTuningMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncHeatingTuningPending.hysteresisC = requestedHyst;
    g_asyncHeatingTuningPending.predictSeconds = requestedPredict;
    g_asyncHeatingTuningPending.minOnSeconds = minOn;
    g_asyncHeatingTuningPending.minOffSeconds = minOff;
    g_asyncHeatingTuningPending.pending = true;
    g_asyncHeatingTuningPending.writeInProgress = false;
    g_asyncHeatingTuningLastQueuedId = requestId;
    ++g_asyncHeatingTuningQueued;
    queued = true;
    xSemaphoreGive(g_asyncHeatingTuningMutex);
  }

  if (!queued) {
    const bool rollbackOk =
      restoreAsyncHeatingTuningPrefs(oldHyst, oldPredict, oldMinOn, oldMinOff);
    clearAsyncHeatingTuningWriteReservation();
    ++g_asyncHeatingTuningWriteFailures;
    String json = "{\"ok\":false,\"err\":\"runtime_queue_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false";
    json += "}";
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  String json;
  json.reserve(384);
  json += "{\"ok\":true,\"setting\":\"heatingTuning\",\"requestId\":" + String(requestId);
  json += ",\"stored\":{\"hysteresisC\":" + String(requestedHyst, 2);
  json += ",\"predictSeconds\":" + String(requestedPredict, 1);
  json += ",\"minOnSeconds\":" + String(minOn);
  json += ",\"minOffSeconds\":" + String(minOff) + "}";
  json += ",\"applyQueued\":true,\"applyContext\":\"main-loop\"}";
  AsyncWebServerResponse* response = request->beginResponse(
    202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void processAsyncHeatingTuningApply() {
  if (!g_asyncHeatingTuningMutex) return;

  AsyncHeatingTuningPending pending;
  bool hasPending = false;
  if (xSemaphoreTake(g_asyncHeatingTuningMutex, 0) == pdTRUE) {
    if (g_asyncHeatingTuningPending.pending) {
      pending = g_asyncHeatingTuningPending;
      g_asyncHeatingTuningPending.pending = false;
      hasPending = true;
    }
    xSemaphoreGive(g_asyncHeatingTuningMutex);
  }
  if (!hasPending) return;

  settings.heating.hysteresisC = pending.hysteresisC;
  settings.heating.predictSeconds = pending.predictSeconds;
  settings.heating.minOnSeconds = pending.minOnSeconds;
  settings.heating.minOffSeconds = pending.minOffSeconds;

  g_asyncHeatingTuningLastAppliedId = pending.requestId;
  ++g_asyncHeatingTuningApplied;

  expireAsyncStateCacheMetadata();

  logPrint("[ASYNC][HEAT] tuning applied id=" + String(pending.requestId) +
           " hyst=" + String(pending.hysteresisC, 2) +
           " predictSec=" + String(pending.predictSeconds, 1) +
           " minOnSec=" + String(pending.minOnSeconds) +
           " minOffSec=" + String(pending.minOffSeconds));
}

// -------------------- Phase 20: runtime-applied heating source/output --------------------
static bool readAsyncHeatingOutputStored(int& sourceType, int& relay) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  sourceType = prefGetIntOrDefault(prefs, KEY_HEATING_SOURCE, settings.heating.sourceType);
  relay = prefGetIntOrDefault(prefs, KEY_HEATING_RELAY, settings.heating.Relay);
  prefs.end();
  return true;
}

static bool restoreAsyncHeatingOutputPrefs(int sourceType, int relay) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) return false;
  const size_t wroteSource = prefs.putInt(KEY_HEATING_SOURCE, sourceType);
  const size_t wroteRelay = prefs.putInt(KEY_HEATING_RELAY, relay);
  const int verifySource = prefs.getInt(KEY_HEATING_SOURCE, -999);
  const int verifyRelay = prefs.getInt(KEY_HEATING_RELAY, -999);
  prefs.end();
  return wroteSource > 0 && wroteRelay > 0 &&
         verifySource == sourceType && verifyRelay == relay;
}

static void clearAsyncHeatingOutputWriteReservation() {
  if (!g_asyncHeatingOutputMutex) return;
  if (xSemaphoreTake(g_asyncHeatingOutputMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncHeatingOutputPending.writeInProgress = false;
    xSemaphoreGive(g_asyncHeatingOutputMutex);
  }
}

static bool isValidHeatingOutputConfig(int sourceType, int relay) {
  if (sourceType < 0 || sourceType > 2) return false;
  if (sourceType == 1) {
    const int maxHeatingRelay = (activeRelayCount < 4) ? activeRelayCount : 4;
    return relay >= 1 && relay <= maxHeatingRelay;
  }
  return relay == 0;
}

static void sendAsyncHeatingOutputState(AsyncWebServerRequest* request) {
  if (!request) return;

  int storedSource = settings.heating.sourceType;
  int storedRelay = settings.heating.Relay;
  if (!readAsyncHeatingOutputStored(storedSource, storedRelay)) {
    ++g_asyncHeatingOutputWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  bool pending = false;
  bool writeInProgress = false;
  uint32_t pendingId = 0;
  if (g_asyncHeatingOutputMutex &&
      xSemaphoreTake(g_asyncHeatingOutputMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncHeatingOutputPending.pending;
    writeInProgress = g_asyncHeatingOutputPending.writeInProgress;
    pendingId = g_asyncHeatingOutputPending.requestId;
    xSemaphoreGive(g_asyncHeatingOutputMutex);
  }

  const bool shellyHeaterConfigured = !settings.shelly.heater.ip.isEmpty();
  const bool safeForAutomatedMutation =
    settings.heating.sourceType == 0 && settings.heating.Relay == 0 && !shellyHeaterConfigured;

  String json;
  json.reserve(1024);
  json += "{\"ok\":true,\"setting\":\"heatingOutput\"";
  json += ",\"stored\":{\"sourceType\":" + String(storedSource);
  json += ",\"relay\":" + String(storedRelay) + "}";
  json += ",\"runtime\":{\"sourceType\":" + String(settings.heating.sourceType);
  json += ",\"relay\":" + String(settings.heating.Relay) + "}";
  json += ",\"activeRelayCount\":" + String(activeRelayCount);
  json += ",\"shellyHeaterConfigured\":";
  json += shellyHeaterConfigured ? "true" : "false";
  json += ",\"safeForAutomatedMutation\":";
  json += safeForAutomatedMutation ? "true" : "false";
  json += ",\"pending\":"; json += pending ? "true" : "false";
  json += ",\"writeInProgress\":"; json += writeInProgress ? "true" : "false";
  json += ",\"pendingRequestId\":" + String(pendingId);
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncHeatingOutputLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncHeatingOutputLastAppliedId);
  json += ",\"lastFailedId\":" + String((uint32_t)g_asyncHeatingOutputLastFailedId);
  json += "}";

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void handleAsyncHeatingOutputPost(AsyncWebServerRequest* request) {
  auto* body = static_cast<AsyncJsonBody*>(request ? request->_tempObject : nullptr);
  if (!request) return;
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(
      body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8",
      body && body->tooLarge ?
        "{\"ok\":false,\"err\":\"body_too_large\"}" :
        "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["sourceType"].isNull() || doc["relay"].isNull() ||
      !doc["sourceType"].is<int>() || !doc["relay"].is<int>()) {
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_heating_output\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const int requestedSource = doc["sourceType"].as<int>();
  const int requestedRelay = doc["relay"].as<int>();
  if (!isValidHeatingOutputConfig(requestedSource, requestedRelay)) {
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"heating_output_out_of_range\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (!g_asyncHeatingOutputMutex) {
    ++g_asyncHeatingOutputWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  uint32_t requestId = 0;
  if (xSemaphoreTake(g_asyncHeatingOutputMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncHeatingOutputWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_busy\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }
  if (g_asyncHeatingOutputPending.writeInProgress || g_asyncHeatingOutputPending.pending) {
    xSemaphoreGive(g_asyncHeatingOutputMutex);
    AsyncWebServerResponse* response = request->beginResponse(
      409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_apply_pending\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }
  g_asyncHeatingOutputPending.writeInProgress = true;
  requestId = ++g_asyncHeatingOutputNextRequestId;
  g_asyncHeatingOutputPending.requestId = requestId;
  xSemaphoreGive(g_asyncHeatingOutputMutex);

  ++g_asyncHeatingOutputWriteRequests;
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) {
    clearAsyncHeatingOutputWriteReservation();
    ++g_asyncHeatingOutputWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_open_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const int oldStoredSource =
    prefGetIntOrDefault(prefs, KEY_HEATING_SOURCE, settings.heating.sourceType);
  const int oldStoredRelay =
    prefGetIntOrDefault(prefs, KEY_HEATING_RELAY, settings.heating.Relay);

  // A normal Betriebseinstellungen save includes the current heating selection.
  // Do not force an already-active heater OFF when the selection did not change.
  if (requestedSource == oldStoredSource && requestedRelay == oldStoredRelay &&
      requestedSource == settings.heating.sourceType && requestedRelay == settings.heating.Relay) {
    prefs.end();
    clearAsyncHeatingOutputWriteReservation();
    String json = "{\"ok\":true,\"setting\":\"heatingOutput\",\"noChange\":true,\"applyQueued\":false}";
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const size_t wroteSource = prefs.putInt(KEY_HEATING_SOURCE, requestedSource);
  const size_t wroteRelay = prefs.putInt(KEY_HEATING_RELAY, requestedRelay);
  const int verifySource = prefs.getInt(KEY_HEATING_SOURCE, -999);
  const int verifyRelay = prefs.getInt(KEY_HEATING_RELAY, -999);
  prefs.end();

  if (wroteSource == 0 || wroteRelay == 0 ||
      verifySource != requestedSource || verifyRelay != requestedRelay) {
    const bool rollbackOk =
      restoreAsyncHeatingOutputPrefs(oldStoredSource, oldStoredRelay);
    clearAsyncHeatingOutputWriteReservation();
    ++g_asyncHeatingOutputWriteFailures;
    if (!rollbackOk) ++g_asyncHeatingOutputRollbackFailures;
    String json = "{\"ok\":false,\"err\":\"prefs_verify_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false";
    json += "}";
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  bool queued = false;
  if (xSemaphoreTake(g_asyncHeatingOutputMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncHeatingOutputPending.sourceType = requestedSource;
    g_asyncHeatingOutputPending.relay = requestedRelay;
    g_asyncHeatingOutputPending.oldStoredSourceType = oldStoredSource;
    g_asyncHeatingOutputPending.oldStoredRelay = oldStoredRelay;
    g_asyncHeatingOutputPending.pending = true;
    g_asyncHeatingOutputPending.writeInProgress = false;
    g_asyncHeatingOutputLastQueuedId = requestId;
    ++g_asyncHeatingOutputQueued;
    queued = true;
    xSemaphoreGive(g_asyncHeatingOutputMutex);
  }

  if (!queued) {
    const bool rollbackOk =
      restoreAsyncHeatingOutputPrefs(oldStoredSource, oldStoredRelay);
    clearAsyncHeatingOutputWriteReservation();
    ++g_asyncHeatingOutputWriteFailures;
    if (!rollbackOk) ++g_asyncHeatingOutputRollbackFailures;
    String json = "{\"ok\":false,\"err\":\"runtime_queue_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false";
    json += "}";
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  String json;
  json.reserve(384);
  json += "{\"ok\":true,\"setting\":\"heatingOutput\",\"requestId\":" + String(requestId);
  json += ",\"stored\":{\"sourceType\":" + String(requestedSource);
  json += ",\"relay\":" + String(requestedRelay) + "}";
  json += ",\"applyQueued\":true,\"applyContext\":\"main-loop\"}";
  AsyncWebServerResponse* response = request->beginResponse(
    202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static bool safelyForceHeatingOutputOff(int sourceType, int relay, const char* context) {
  if (sourceType == 0) return true;

  if (sourceType == 1) {
    if (relay < 1 || relay > activeRelayCount || relayPins[relay - 1] < 0) {
      logPrint("[ASYNC][HEATCFG] refusing reconfigure: invalid ESP heater relay=" +
               String(relay) + " context=" + String(context ? context : "unknown"));
      return false;
    }

    bool locked = false;
    if (relayMutex) {
      locked = xSemaphoreTake(relayMutex, pdMS_TO_TICKS(100)) == pdTRUE;
      if (!locked) {
        logPrint("[ASYNC][HEATCFG] refusing reconfigure: relay mutex busy context=" +
                 String(context ? context : "unknown"));
        return false;
      }
    }

    const int idx = relay - 1;
    digitalWrite(relayPins[idx], LOW);
    relayStates[idx] = false;
    if (locked) xSemaphoreGive(relayMutex);
    logPrint("[ASYNC][HEATCFG] ESP heater relay " + String(relay) +
             " forced OFF context=" + String(context ? context : "unknown"));
    return true;
  }

  if (sourceType == 2) {
    if (settings.shelly.heater.ip.isEmpty()) return true;
    const bool switchedOff = shellySwitchSet(
      settings.shelly.heater.ip,
      settings.shelly.heater.gen,
      false,
      0,
      80);
    if (!switchedOff) {
      logPrint("[ASYNC][HEATCFG] refusing reconfigure: failed to switch Shelly heater OFF context=" +
               String(context ? context : "unknown"));
      return false;
    }
    shelly.heater.values.isOn = false;
    shelly.heater.values.ok = true;
    logPrint("[ASYNC][HEATCFG] Shelly heater forced OFF context=" +
             String(context ? context : "unknown"));
    return true;
  }

  logPrint("[ASYNC][HEATCFG] refusing reconfigure: unknown source=" + String(sourceType) +
           " context=" + String(context ? context : "unknown"));
  return false;
}

static void processAsyncHeatingOutputApply() {
  if (!g_asyncHeatingOutputMutex) return;

  AsyncHeatingOutputPending pending;
  bool hasPending = false;
  if (xSemaphoreTake(g_asyncHeatingOutputMutex, 0) == pdTRUE) {
    if (g_asyncHeatingOutputPending.pending) {
      pending = g_asyncHeatingOutputPending;
      g_asyncHeatingOutputPending.pending = false;
      hasPending = true;
    }
    xSemaphoreGive(g_asyncHeatingOutputMutex);
  }
  if (!hasPending) return;

  const bool changesRuntime =
    settings.heating.sourceType != pending.sourceType ||
    settings.heating.Relay != pending.relay;

  bool outputsSafe = true;
  if (changesRuntime && settings.heating.sourceType != 0) {
    outputsSafe = safelyForceHeatingOutputOff(
      settings.heating.sourceType, settings.heating.Relay, "previous-source");
  }
  if (outputsSafe && changesRuntime && pending.sourceType != 0) {
    outputsSafe = safelyForceHeatingOutputOff(
      pending.sourceType, pending.relay, "new-source");
  }

  if (!outputsSafe) {
    const bool rollbackOk = restoreAsyncHeatingOutputPrefs(
      pending.oldStoredSourceType, pending.oldStoredRelay);
    ++g_asyncHeatingOutputApplyFailures;
    g_asyncHeatingOutputLastFailedId = pending.requestId;
    if (!rollbackOk) ++g_asyncHeatingOutputRollbackFailures;
    logPrint("[ASYNC][HEATCFG] runtime apply FAILED id=" + String(pending.requestId) +
             " oldSource=" + String(settings.heating.sourceType) +
             " oldRelay=" + String(settings.heating.Relay) +
             " requestedSource=" + String(pending.sourceType) +
             " requestedRelay=" + String(pending.relay) +
             " rollbackOk=" + String(rollbackOk ? "true" : "false"));
    return;
  }

  settings.heating.sourceType = pending.sourceType;
  settings.heating.Relay = pending.relay;
  heatingRelay = pending.relay;

  g_asyncHeatingOutputLastAppliedId = pending.requestId;
  ++g_asyncHeatingOutputApplied;

  expireAsyncStateCacheMetadata();

  logPrint("[ASYNC][HEATCFG] runtime output applied id=" + String(pending.requestId) +
           " source=" + String(pending.sourceType) +
           " relay=" + String(pending.relay));
}

// -------------------- Phase 21: runtime-applied grow-light schedule --------------------
static bool isValidLightOnTime(const String& value) {
  if (value.length() != 5 || value[2] != ':') return false;
  if (!isDigit(value[0]) || !isDigit(value[1]) ||
      !isDigit(value[3]) || !isDigit(value[4])) return false;
  const int hh = (value[0] - '0') * 10 + (value[1] - '0');
  const int mm = (value[3] - '0') * 10 + (value[4] - '0');
  return hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59;
}

static bool readAsyncLightScheduleStored(String& onTime, int& dayHours) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  onTime = prefGetStringOrDefault(prefs, KEY_LIGHT_ON_TIME, settings.grow.lightOnTime);
  dayHours = prefGetIntOrDefault(prefs, KEY_LIGHT_DAY_HOURS, settings.grow.lightDayHours);
  prefs.end();
  return true;
}

static bool restoreAsyncLightSchedulePrefs(const String& onTime, int dayHours) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) return false;
  const size_t wroteTime = prefs.putString(KEY_LIGHT_ON_TIME, onTime);
  const size_t wroteHours = prefs.putInt(KEY_LIGHT_DAY_HOURS, dayHours);
  const String verifyTime = prefs.getString(KEY_LIGHT_ON_TIME, "__missing__");
  const int verifyHours = prefs.getInt(KEY_LIGHT_DAY_HOURS, -999);
  prefs.end();
  return wroteTime > 0 && wroteHours > 0 &&
         verifyTime == onTime && verifyHours == dayHours;
}

static void clearAsyncLightScheduleWriteReservation() {
  if (!g_asyncLightScheduleMutex) return;
  if (xSemaphoreTake(g_asyncLightScheduleMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncLightSchedulePending.writeInProgress = false;
    xSemaphoreGive(g_asyncLightScheduleMutex);
  }
}

static void sendAsyncLightScheduleState(AsyncWebServerRequest* request) {
  if (!request) return;

  String storedOnTime = settings.grow.lightOnTime;
  int storedDayHours = settings.grow.lightDayHours;
  if (!readAsyncLightScheduleStored(storedOnTime, storedDayHours)) {
    ++g_asyncLightScheduleWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  bool pending = false;
  bool writeInProgress = false;
  uint32_t pendingId = 0;
  if (g_asyncLightScheduleMutex &&
      xSemaphoreTake(g_asyncLightScheduleMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncLightSchedulePending.pending;
    writeInProgress = g_asyncLightSchedulePending.writeInProgress;
    pendingId = g_asyncLightSchedulePending.requestId;
    xSemaphoreGive(g_asyncLightScheduleMutex);
  }

  const bool shellyLightConfigured = !settings.shelly.light.ip.isEmpty();
  const bool safeForAutomatedMutation = !shellyLightConfigured;

  String json;
  json.reserve(960);
  json += "{\"ok\":true,\"setting\":\"lightSchedule\"";
  json += ",\"stored\":{\"onTime\":\"" + asyncJsonEscape(storedOnTime) + "\"";
  json += ",\"dayHours\":" + String(storedDayHours) + "}";
  json += ",\"runtime\":{\"onTime\":\"" + asyncJsonEscape(settings.grow.lightOnTime) + "\"";
  json += ",\"dayHours\":" + String(settings.grow.lightDayHours) + "}";
  json += ",\"shellyLightConfigured\":";
  json += shellyLightConfigured ? "true" : "false";
  json += ",\"safeForAutomatedMutation\":";
  json += safeForAutomatedMutation ? "true" : "false";
  json += ",\"pending\":"; json += pending ? "true" : "false";
  json += ",\"writeInProgress\":"; json += writeInProgress ? "true" : "false";
  json += ",\"pendingRequestId\":" + String(pendingId);
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncLightScheduleLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncLightScheduleLastAppliedId);
  json += ",\"lastFailedId\":" + String((uint32_t)g_asyncLightScheduleLastFailedId);
  json += "}";

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void handleAsyncLightSchedulePost(AsyncWebServerRequest* request) {
  auto* body = static_cast<AsyncJsonBody*>(request ? request->_tempObject : nullptr);
  if (!request) return;
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(
      body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8",
      body && body->tooLarge ?
        "{\"ok\":false,\"err\":\"body_too_large\"}" :
        "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["onTime"].isNull() || doc["dayHours"].isNull() ||
      !doc["onTime"].is<const char*>() || !doc["dayHours"].is<int>()) {
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_light_schedule\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  String requestedOnTime = doc["onTime"].as<const char*>();
  requestedOnTime.trim();
  const int requestedDayHours = doc["dayHours"].as<int>();
  if (!isValidLightOnTime(requestedOnTime) ||
      requestedDayHours < 1 || requestedDayHours > 20) {
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"light_schedule_out_of_range\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (!g_asyncLightScheduleMutex) {
    ++g_asyncLightScheduleWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  uint32_t requestId = 0;
  if (xSemaphoreTake(g_asyncLightScheduleMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncLightScheduleWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_busy\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }
  if (g_asyncLightSchedulePending.writeInProgress || g_asyncLightSchedulePending.pending) {
    xSemaphoreGive(g_asyncLightScheduleMutex);
    AsyncWebServerResponse* response = request->beginResponse(
      409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_apply_pending\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }
  g_asyncLightSchedulePending.writeInProgress = true;
  requestId = ++g_asyncLightScheduleNextRequestId;
  g_asyncLightSchedulePending.requestId = requestId;
  xSemaphoreGive(g_asyncLightScheduleMutex);

  ++g_asyncLightScheduleWriteRequests;
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) {
    clearAsyncLightScheduleWriteReservation();
    ++g_asyncLightScheduleWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_open_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const String oldStoredOnTime =
    prefGetStringOrDefault(prefs, KEY_LIGHT_ON_TIME, settings.grow.lightOnTime);
  const int oldStoredDayHours =
    prefGetIntOrDefault(prefs, KEY_LIGHT_DAY_HOURS, settings.grow.lightDayHours);
  const String oldRuntimeOnTime = settings.grow.lightOnTime;
  const int oldRuntimeDayHours = settings.grow.lightDayHours;

  const size_t wroteTime = prefs.putString(KEY_LIGHT_ON_TIME, requestedOnTime);
  const size_t wroteHours = prefs.putInt(KEY_LIGHT_DAY_HOURS, requestedDayHours);
  const String verifyTime = prefs.getString(KEY_LIGHT_ON_TIME, "__missing__");
  const int verifyHours = prefs.getInt(KEY_LIGHT_DAY_HOURS, -999);
  prefs.end();

  if (wroteTime == 0 || wroteHours == 0 ||
      verifyTime != requestedOnTime || verifyHours != requestedDayHours) {
    const bool rollbackOk =
      restoreAsyncLightSchedulePrefs(oldStoredOnTime, oldStoredDayHours);
    clearAsyncLightScheduleWriteReservation();
    ++g_asyncLightScheduleWriteFailures;
    if (!rollbackOk) ++g_asyncLightScheduleRollbackFailures;
    String json = "{\"ok\":false,\"err\":\"prefs_verify_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false";
    json += "}";
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  bool queued = false;
  if (xSemaphoreTake(g_asyncLightScheduleMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncLightSchedulePending.onTime = requestedOnTime;
    g_asyncLightSchedulePending.dayHours = requestedDayHours;
    g_asyncLightSchedulePending.oldStoredOnTime = oldStoredOnTime;
    g_asyncLightSchedulePending.oldStoredDayHours = oldStoredDayHours;
    g_asyncLightSchedulePending.oldRuntimeOnTime = oldRuntimeOnTime;
    g_asyncLightSchedulePending.oldRuntimeDayHours = oldRuntimeDayHours;
    g_asyncLightSchedulePending.pending = true;
    g_asyncLightSchedulePending.writeInProgress = false;
    g_asyncLightScheduleLastQueuedId = requestId;
    ++g_asyncLightScheduleQueued;
    queued = true;
    xSemaphoreGive(g_asyncLightScheduleMutex);
  }

  if (!queued) {
    const bool rollbackOk =
      restoreAsyncLightSchedulePrefs(oldStoredOnTime, oldStoredDayHours);
    clearAsyncLightScheduleWriteReservation();
    ++g_asyncLightScheduleWriteFailures;
    if (!rollbackOk) ++g_asyncLightScheduleRollbackFailures;
    String json = "{\"ok\":false,\"err\":\"runtime_queue_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false";
    json += "}";
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  String json;
  json.reserve(384);
  json += "{\"ok\":true,\"setting\":\"lightSchedule\",\"requestId\":" + String(requestId);
  json += ",\"stored\":{\"onTime\":\"" + asyncJsonEscape(requestedOnTime) + "\"";
  json += ",\"dayHours\":" + String(requestedDayHours) + "}";
  json += ",\"applyQueued\":true,\"applyContext\":\"main-loop\"}";
  AsyncWebServerResponse* response = request->beginResponse(
    202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void processAsyncLightScheduleApply() {
  if (!g_asyncLightScheduleMutex) return;

  AsyncLightSchedulePending pending;
  bool hasPending = false;
  if (xSemaphoreTake(g_asyncLightScheduleMutex, 0) == pdTRUE) {
    if (g_asyncLightSchedulePending.pending) {
      pending = g_asyncLightSchedulePending;
      g_asyncLightSchedulePending.pending = false;
      hasPending = true;
    }
    xSemaphoreGive(g_asyncLightScheduleMutex);
  }
  if (!hasPending) return;

  settings.grow.lightOnTime = pending.onTime;
  settings.grow.lightDayHours = pending.dayHours;
  lightOnTime = pending.onTime;
  lightDayHours = pending.dayHours;

  const bool shellyConfigured = !settings.shelly.light.ip.isEmpty();
  bool scheduleApplied = true;
  if (shellyConfigured) {
    scheduleApplied = applyGrowLightSchedule();
  }

  if (!scheduleApplied) {
    settings.grow.lightOnTime = pending.oldRuntimeOnTime;
    settings.grow.lightDayHours = pending.oldRuntimeDayHours;
    lightOnTime = pending.oldRuntimeOnTime;
    lightDayHours = pending.oldRuntimeDayHours;

    const bool prefsRollbackOk = restoreAsyncLightSchedulePrefs(
      pending.oldStoredOnTime, pending.oldStoredDayHours);
    // Best effort: if the failed programming attempt partially changed the
    // Shelly schedule, restore the previous runtime schedule as well.
    const bool shellyRollbackOk = applyGrowLightSchedule();

    ++g_asyncLightScheduleApplyFailures;
    g_asyncLightScheduleLastFailedId = pending.requestId;
    if (!prefsRollbackOk || !shellyRollbackOk) {
      ++g_asyncLightScheduleRollbackFailures;
    }
    logPrint("[ASYNC][LIGHT] schedule apply FAILED id=" + String(pending.requestId) +
             " requestedOn=" + pending.onTime +
             " requestedHours=" + String(pending.dayHours) +
             " prefsRollbackOk=" + String(prefsRollbackOk ? "true" : "false") +
             " shellyRollbackOk=" + String(shellyRollbackOk ? "true" : "false"));
    return;
  }

  g_scheduleResolved = false;
  g_scheduleInitStartMs = millis();
  g_lastScheduleFetchMs = 0;
  g_asyncLightScheduleLastAppliedId = pending.requestId;
  ++g_asyncLightScheduleApplied;

  expireAsyncStateCacheMetadata();

  logPrint("[ASYNC][LIGHT] runtime schedule applied id=" + String(pending.requestId) +
           " on=" + pending.onTime +
           " dayHours=" + String(pending.dayHours) +
           " shellyProgrammed=" + String(shellyConfigured ? "true" : "false"));
}


static const char* asyncRelayScheduleOnKey(uint8_t idx) {
  switch (idx) {
    case 0: return KEY_RELAY_START_1;
    case 1: return KEY_RELAY_START_2;
    case 2: return KEY_RELAY_START_3;
    case 3: return KEY_RELAY_START_4;
    case 4: return KEY_RELAY_START_5;
    default: return nullptr;
  }
}

static const char* asyncRelayScheduleOffKey(uint8_t idx) {
  switch (idx) {
    case 0: return KEY_RELAY_END_1;
    case 1: return KEY_RELAY_END_2;
    case 2: return KEY_RELAY_END_3;
    case 3: return KEY_RELAY_END_4;
    case 4: return KEY_RELAY_END_5;
    default: return nullptr;
  }
}

static bool readAsyncRelayScheduleStored(uint8_t idx, RelaySchedule& out) {
  if (idx >= 5) return false;
  const char* keyOn = asyncRelayScheduleOnKey(idx);
  const char* keyOff = asyncRelayScheduleOffKey(idx);
  if (!keyOn || !keyOff) return false;

  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  const String keyEn = "relay_enable_" + String((int)idx + 1);
  const String keyILO = "ilo_" + String((int)idx + 1);
  out.enabled = prefGetBoolOrDefault(prefs, keyEn.c_str(), settings.relay.schedule[idx].enabled);
  out.ifLightOff = prefGetBoolOrDefault(prefs, keyILO.c_str(), settings.relay.schedule[idx].ifLightOff);
  out.onMin = prefGetIntOrDefault(prefs, keyOn, settings.relay.schedule[idx].onMin);
  out.offMin = prefGetIntOrDefault(prefs, keyOff, settings.relay.schedule[idx].offMin);
  prefs.end();
  return true;
}

static bool restoreAsyncRelaySchedulePrefs(uint8_t idx, const RelaySchedule& value) {
  if (idx >= 5) return false;
  const char* keyOn = asyncRelayScheduleOnKey(idx);
  const char* keyOff = asyncRelayScheduleOffKey(idx);
  if (!keyOn || !keyOff) return false;

  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) return false;
  const String keyEn = "relay_enable_" + String((int)idx + 1);
  const String keyILO = "ilo_" + String((int)idx + 1);
  const size_t wroteEn = prefs.putBool(keyEn.c_str(), value.enabled);
  const size_t wroteILO = prefs.putBool(keyILO.c_str(), value.ifLightOff);
  const size_t wroteOn = prefs.putInt(keyOn, value.onMin);
  const size_t wroteOff = prefs.putInt(keyOff, value.offMin);
  const bool verifyEn = prefs.getBool(keyEn.c_str(), !value.enabled);
  const bool verifyILO = prefs.getBool(keyILO.c_str(), !value.ifLightOff);
  const int verifyOn = prefs.getInt(keyOn, -999);
  const int verifyOff = prefs.getInt(keyOff, -999);
  prefs.end();
  return wroteEn > 0 && wroteILO > 0 && wroteOn > 0 && wroteOff > 0 &&
         verifyEn == value.enabled && verifyILO == value.ifLightOff &&
         verifyOn == value.onMin && verifyOff == value.offMin;
}

static void clearAsyncRelayScheduleWriteReservation() {
  if (!g_asyncRelayScheduleMutex) return;
  if (xSemaphoreTake(g_asyncRelayScheduleMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncRelaySchedulePending.writeInProgress = false;
    xSemaphoreGive(g_asyncRelayScheduleMutex);
  }
}

static bool isValidRelayScheduleValue(const RelaySchedule& value) {
  return value.onMin >= 0 && value.onMin <= 59 &&
         value.offMin >= 0 && value.offMin <= 59;
}

static void sendAsyncRelayScheduleState(AsyncWebServerRequest* request, uint8_t idx) {
  if (!request) return;
  if (idx >= 5) {
    AsyncWebServerResponse* response = request->beginResponse(
      404, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_relay\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  RelaySchedule stored;
  if (!readAsyncRelayScheduleStored(idx, stored)) {
    ++g_asyncRelayScheduleWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const RelaySchedule runtime = settings.relay.schedule[idx];
  const uint8_t activeScheduledRelays = (activeRelayCount < 5) ? activeRelayCount : 5;
  const bool active = idx < activeScheduledRelays;
  // Test safety requires both copies disabled; restoring the original then also
  // remains disabled and therefore cannot create a relay transition.
  const bool safeForAutomatedMutation = active && !stored.enabled && !runtime.enabled;

  bool pending = false;
  bool writeInProgress = false;
  uint32_t pendingId = 0;
  uint8_t pendingRelay = 255;
  if (g_asyncRelayScheduleMutex &&
      xSemaphoreTake(g_asyncRelayScheduleMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncRelaySchedulePending.pending;
    writeInProgress = g_asyncRelaySchedulePending.writeInProgress;
    pendingId = g_asyncRelaySchedulePending.requestId;
    pendingRelay = g_asyncRelaySchedulePending.relayIndex;
    xSemaphoreGive(g_asyncRelayScheduleMutex);
  }

  String json;
  json.reserve(1024);
  json += "{\"ok\":true,\"setting\":\"relaySchedule\",\"relay\":" + String((int)idx + 1);
  json += ",\"active\":"; json += active ? "true" : "false";
  json += ",\"stored\":{\"enabled\":"; json += stored.enabled ? "true" : "false";
  json += ",\"ifLightOff\":"; json += stored.ifLightOff ? "true" : "false";
  json += ",\"onMin\":" + String(stored.onMin);
  json += ",\"offMin\":" + String(stored.offMin) + "}";
  json += ",\"runtime\":{\"enabled\":"; json += runtime.enabled ? "true" : "false";
  json += ",\"ifLightOff\":"; json += runtime.ifLightOff ? "true" : "false";
  json += ",\"onMin\":" + String(runtime.onMin);
  json += ",\"offMin\":" + String(runtime.offMin) + "}";
  json += ",\"safeForAutomatedMutation\":"; json += safeForAutomatedMutation ? "true" : "false";
  json += ",\"pending\":"; json += pending ? "true" : "false";
  json += ",\"writeInProgress\":"; json += writeInProgress ? "true" : "false";
  json += ",\"pendingRequestId\":" + String(pendingId);
  json += ",\"pendingRelay\":" + String(pendingRelay == 255 ? 0 : (int)pendingRelay + 1);
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncRelayScheduleLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncRelayScheduleLastAppliedId);
  json += ",\"lastFailedId\":" + String((uint32_t)g_asyncRelayScheduleLastFailedId);
  json += "}";

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void handleAsyncRelaySchedulePost(AsyncWebServerRequest* request, uint8_t idx) {
  if (!request) return;
  if (idx >= 5) {
    AsyncWebServerResponse* response = request->beginResponse(
      404, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_relay\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const uint8_t activeScheduledRelays = (activeRelayCount < 5) ? activeRelayCount : 5;
  if (idx >= activeScheduledRelays) {
    AsyncWebServerResponse* response = request->beginResponse(
      409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"relay_not_active\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  auto* body = static_cast<AsyncJsonBody*>(request->_tempObject);
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(
      body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8",
      body && body->tooLarge ?
        "{\"ok\":false,\"err\":\"body_too_large\"}" :
        "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["enabled"].isNull() || doc["ifLightOff"].isNull() ||
      doc["onMin"].isNull() || doc["offMin"].isNull() ||
      !doc["enabled"].is<bool>() || !doc["ifLightOff"].is<bool>() ||
      !doc["onMin"].is<int>() || !doc["offMin"].is<int>()) {
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_relay_schedule\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  RelaySchedule requested;
  requested.enabled = doc["enabled"].as<bool>();
  requested.ifLightOff = doc["ifLightOff"].as<bool>();
  requested.onMin = doc["onMin"].as<int>();
  requested.offMin = doc["offMin"].as<int>();
  if (!isValidRelayScheduleValue(requested)) {
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"relay_schedule_out_of_range\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (!g_asyncRelayScheduleMutex) {
    ++g_asyncRelayScheduleWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  uint32_t requestId = 0;
  if (xSemaphoreTake(g_asyncRelayScheduleMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncRelayScheduleWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_busy\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }
  if (g_asyncRelaySchedulePending.writeInProgress || g_asyncRelaySchedulePending.pending) {
    xSemaphoreGive(g_asyncRelayScheduleMutex);
    AsyncWebServerResponse* response = request->beginResponse(
      409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_apply_pending\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }
  g_asyncRelaySchedulePending.writeInProgress = true;
  requestId = ++g_asyncRelayScheduleNextRequestId;
  g_asyncRelaySchedulePending.requestId = requestId;
  g_asyncRelaySchedulePending.relayIndex = idx;
  xSemaphoreGive(g_asyncRelayScheduleMutex);

  ++g_asyncRelayScheduleWriteRequests;
  RelaySchedule oldStored;
  if (!readAsyncRelayScheduleStored(idx, oldStored)) {
    clearAsyncRelayScheduleWriteReservation();
    ++g_asyncRelayScheduleWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }
  if (!restoreAsyncRelaySchedulePrefs(idx, requested)) {
    const bool rollbackOk = restoreAsyncRelaySchedulePrefs(idx, oldStored);
    clearAsyncRelayScheduleWriteReservation();
    ++g_asyncRelayScheduleWriteFailures;
    if (!rollbackOk) ++g_asyncRelayScheduleRollbackFailures;
    String json = "{\"ok\":false,\"err\":\"prefs_verify_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false";
    json += "}";
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  bool queued = false;
  if (xSemaphoreTake(g_asyncRelayScheduleMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncRelaySchedulePending.relayIndex = idx;
    g_asyncRelaySchedulePending.value = requested;
    g_asyncRelaySchedulePending.oldStored = oldStored;
    g_asyncRelaySchedulePending.pending = true;
    g_asyncRelaySchedulePending.writeInProgress = false;
    g_asyncRelayScheduleLastQueuedId = requestId;
    ++g_asyncRelayScheduleQueued;
    queued = true;
    xSemaphoreGive(g_asyncRelayScheduleMutex);
  }

  if (!queued) {
    const bool rollbackOk = restoreAsyncRelaySchedulePrefs(idx, oldStored);
    clearAsyncRelayScheduleWriteReservation();
    ++g_asyncRelayScheduleWriteFailures;
    if (!rollbackOk) ++g_asyncRelayScheduleRollbackFailures;
    String json = "{\"ok\":false,\"err\":\"runtime_queue_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false";
    json += "}";
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  String json;
  json.reserve(448);
  json += "{\"ok\":true,\"setting\":\"relaySchedule\",\"relay\":" + String((int)idx + 1);
  json += ",\"requestId\":" + String(requestId);
  json += ",\"stored\":{\"enabled\":"; json += requested.enabled ? "true" : "false";
  json += ",\"ifLightOff\":"; json += requested.ifLightOff ? "true" : "false";
  json += ",\"onMin\":" + String(requested.onMin);
  json += ",\"offMin\":" + String(requested.offMin) + "}";
  json += ",\"applyQueued\":true,\"applyContext\":\"main-loop\"}";
  AsyncWebServerResponse* response = request->beginResponse(
    202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void processAsyncRelayScheduleApply() {
  if (!g_asyncRelayScheduleMutex) return;

  AsyncRelaySchedulePending pending;
  bool hasPending = false;
  if (xSemaphoreTake(g_asyncRelayScheduleMutex, 0) == pdTRUE) {
    if (g_asyncRelaySchedulePending.pending) {
      pending = g_asyncRelaySchedulePending;
      g_asyncRelaySchedulePending.pending = false;
      hasPending = true;
    }
    xSemaphoreGive(g_asyncRelayScheduleMutex);
  }
  if (!hasPending) return;

  if (pending.relayIndex >= 5 || !isValidRelayScheduleValue(pending.value)) {
    const bool rollbackOk = restoreAsyncRelaySchedulePrefs(pending.relayIndex, pending.oldStored);
    ++g_asyncRelayScheduleApplyFailures;
    g_asyncRelayScheduleLastFailedId = pending.requestId;
    if (!rollbackOk) ++g_asyncRelayScheduleRollbackFailures;
    logPrint("[ASYNC][RELAY-SCHED] apply FAILED id=" + String(pending.requestId) +
             " relay=" + String((int)pending.relayIndex + 1) +
             " rollbackOk=" + String(rollbackOk ? "true" : "false"));
    return;
  }

  RelaySchedule& runtime = settings.relay.schedule[pending.relayIndex];
  // Disable first so a concurrent scheduler pass can never observe a partially
  // updated *enabled* schedule. Then publish all fields and enabled last.
  runtime.enabled = false;
  runtime.ifLightOff = pending.value.ifLightOff;
  runtime.onMin = pending.value.onMin;
  runtime.offMin = pending.value.offMin;
  runtime.enabled = pending.value.enabled;

  g_asyncRelayScheduleLastAppliedId = pending.requestId;
  ++g_asyncRelayScheduleApplied;

  expireAsyncStateCacheMetadata();

  logPrint("[ASYNC][RELAY-SCHED] runtime schedule applied id=" + String(pending.requestId) +
           " relay=" + String((int)pending.relayIndex + 1) +
           " enabled=" + String(pending.value.enabled ? "true" : "false") +
           " ifLightOff=" + String(pending.value.ifLightOff ? "true" : "false") +
           " onMin=" + String(pending.value.onMin) +
           " offMin=" + String(pending.value.offMin));
}

static bool asyncRelayActionActive(uint8_t idx) {
  return idx < 5 && idx < activeRelayCount && relayPins[idx] >= 0;
}

static bool readAsyncRelayCachedState(uint8_t idx, bool& state) {
  if (!asyncRelayActionActive(idx) || !relayMutex) return false;
  if (xSemaphoreTake(relayMutex, pdMS_TO_TICKS(25)) != pdTRUE) return false;
  state = relayStates[idx];
  xSemaphoreGive(relayMutex);
  return true;
}

static void invalidateAsyncStateCacheAfterRelayAction() {
  // Generation is the correctness mechanism. The shared JSON buffer itself is
  // intentionally left untouched while active callback responses may reference
  // it; metadata expiry forces a rebuild as soon as the readers have drained.
  ++g_asyncStateGeneration;
  if (g_asyncStateGeneration == 0) g_asyncStateGeneration = 1;

  expireAsyncStateCacheMetadata();
}

static void sendAsyncRelayActionState(AsyncWebServerRequest* request, uint8_t idx) {
  if (!request) return;
  if (idx >= 5) {
    AsyncWebServerResponse* response = request->beginResponse(
      404, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_relay\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const bool active = asyncRelayActionActive(idx);
  bool state = false;
  bool stateReadable = false;
  if (active) stateReadable = readAsyncRelayCachedState(idx, state);

  bool pending = false;
  uint32_t pendingId = 0;
  uint8_t pendingRelay = 255;
  bool pendingTarget = false;
  bool pendingToggle = false;
  bool pendingNoTransitionOnly = false;
  if (g_asyncRelayActionMutex &&
      xSemaphoreTake(g_asyncRelayActionMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncRelayActionPending.pending;
    pendingId = g_asyncRelayActionPending.requestId;
    pendingRelay = g_asyncRelayActionPending.relayIndex;
    pendingTarget = g_asyncRelayActionPending.targetState;
    pendingToggle = g_asyncRelayActionPending.toggle;
    pendingNoTransitionOnly = g_asyncRelayActionPending.noTransitionOnly;
    xSemaphoreGive(g_asyncRelayActionMutex);
  }

  const bool scheduleEnabled = active ? settings.relay.schedule[idx].enabled : false;
  const bool heaterAssigned = active && settings.heating.sourceType == 1 &&
                              settings.heating.Relay == (int)idx + 1;
  const bool safeForAutomatedOffNoop = active && stateReadable && !state &&
                                       !scheduleEnabled && !heaterAssigned;

  String json;
  json.reserve(640);
  json += "{\"ok\":true,\"relay\":" + String((int)idx + 1);
  json += ",\"active\":"; json += active ? "true" : "false";
  json += ",\"stateReadable\":"; json += stateReadable ? "true" : "false";
  json += ",\"state\":"; json += state ? "true" : "false";
  json += ",\"scheduleEnabled\":"; json += scheduleEnabled ? "true" : "false";
  json += ",\"heaterAssigned\":"; json += heaterAssigned ? "true" : "false";
  json += ",\"safeForAutomatedOffNoop\":"; json += safeForAutomatedOffNoop ? "true" : "false";
  json += ",\"pending\":"; json += pending ? "true" : "false";
  json += ",\"pendingRequestId\":" + String(pendingId);
  json += ",\"pendingRelay\":" + String(pendingRelay == 255 ? 0 : (int)pendingRelay + 1);
  json += ",\"pendingTarget\":"; json += pendingTarget ? "true" : "false";
  json += ",\"pendingToggle\":"; json += pendingToggle ? "true" : "false";
  json += ",\"pendingNoTransitionOnly\":"; json += pendingNoTransitionOnly ? "true" : "false";
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncRelayActionLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncRelayActionLastAppliedId);
  json += ",\"lastFailedId\":" + String((uint32_t)g_asyncRelayActionLastFailedId);
  json += "}";

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void queueAsyncRelayAction(AsyncWebServerRequest* request,
                                  uint8_t idx,
                                  bool targetState,
                                  bool toggle,
                                  bool noTransitionOnly) {
  if (!request) return;
  if (!asyncRelayActionActive(idx)) {
    AsyncWebServerResponse* response = request->beginResponse(
      idx >= 5 ? 404 : 409,
      "application/json; charset=utf-8",
      idx >= 5 ?
        "{\"ok\":false,\"err\":\"invalid_relay\"}" :
        "{\"ok\":false,\"err\":\"relay_not_active\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (!g_asyncRelayActionMutex) {
    ++g_asyncRelayActionFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (xSemaphoreTake(g_asyncRelayActionMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncRelayActionFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_busy\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (g_asyncRelayActionPending.pending) {
    xSemaphoreGive(g_asyncRelayActionMutex);
    AsyncWebServerResponse* response = request->beginResponse(
      409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_apply_pending\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const uint32_t requestId = ++g_asyncRelayActionNextRequestId;
  g_asyncRelayActionPending.pending = true;
  g_asyncRelayActionPending.requestId = requestId;
  g_asyncRelayActionPending.relayIndex = idx;
  g_asyncRelayActionPending.targetState = targetState;
  g_asyncRelayActionPending.toggle = toggle;
  g_asyncRelayActionPending.noTransitionOnly = noTransitionOnly;
  g_asyncRelayActionLastQueuedId = requestId;
  ++g_asyncRelayActionRequests;
  ++g_asyncRelayActionQueued;
  xSemaphoreGive(g_asyncRelayActionMutex);

  String json;
  json.reserve(256);
  json += "{\"ok\":true,\"relay\":" + String((int)idx + 1);
  json += ",\"requestId\":" + String(requestId);
  json += ",\"action\":\"" + String(toggle ? "toggle" : "set") + "\"";
  if (!toggle) {
    json += ",\"state\":";
    json += targetState ? "true" : "false";
  }
  json += ",\"noTransitionOnly\":";
  json += noTransitionOnly ? "true" : "false";
  json += ",\"applyQueued\":true,\"applyContext\":\"main-loop\"}";

  AsyncWebServerResponse* response = request->beginResponse(
    202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void handleAsyncRelaySetPost(AsyncWebServerRequest* request, uint8_t idx) {
  if (!request) return;
  if (idx >= 5) {
    queueAsyncRelayAction(request, idx, false, false, false);
    return;
  }
  if (!asyncRelayActionActive(idx)) {
    queueAsyncRelayAction(request, idx, false, false, false);
    return;
  }

  auto* body = static_cast<AsyncJsonBody*>(request->_tempObject);
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(
      body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8",
      body && body->tooLarge ?
        "{\"ok\":false,\"err\":\"body_too_large\"}" :
        "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["state"].isNull() || !doc["state"].is<bool>()) {
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_relay_state\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  bool noTransitionOnly = false;
  if (!doc["noTransitionOnly"].isNull()) {
    if (!doc["noTransitionOnly"].is<bool>()) {
      AsyncWebServerResponse* response = request->beginResponse(
        400, "application/json; charset=utf-8",
        "{\"ok\":false,\"err\":\"invalid_transition_guard\"}");
      addAsyncCommonHeaders(response, false);
      request->send(response);
      return;
    }
    noTransitionOnly = doc["noTransitionOnly"].as<bool>();
  }

  queueAsyncRelayAction(request, idx, doc["state"].as<bool>(), false, noTransitionOnly);
}

static void handleAsyncRelayTogglePost(AsyncWebServerRequest* request, uint8_t idx) {
  // Preserve the legacy physical-toggle semantics: the desired state is resolved
  // from the actual GPIO only in loop(), immediately before the write.
  queueAsyncRelayAction(request, idx, false, true, false);
}

static void processAsyncRelayActionApply() {
  if (!g_asyncRelayActionMutex) return;

  AsyncRelayActionPending pending;
  bool hasPending = false;
  if (xSemaphoreTake(g_asyncRelayActionMutex, 0) == pdTRUE) {
    if (g_asyncRelayActionPending.pending) {
      pending = g_asyncRelayActionPending;
      g_asyncRelayActionPending.pending = false;
      hasPending = true;
    }
    xSemaphoreGive(g_asyncRelayActionMutex);
  }
  if (!hasPending) return;

  if (!asyncRelayActionActive(pending.relayIndex)) {
    ++g_asyncRelayActionApplyFailures;
    g_asyncRelayActionLastFailedId = pending.requestId;
    logPrint("[ASYNC][RELAY] apply FAILED id=" + String(pending.requestId) +
             " relay=" + String((int)pending.relayIndex + 1) +
             " err=relay_not_active");
    return;
  }

  const bool before = isRelayOn(pending.relayIndex);
  const bool targetState = pending.toggle ? !before : pending.targetState;

  // Automated regression can demand a strict no-transition action. Resolve the
  // physical GPIO first and refuse the request before any write if the cached
  // precheck was stale. Normal dashboard/API actions do not set this guard.
  if (pending.noTransitionOnly && before != targetState) {
    ++g_asyncRelayActionApplyFailures;
    g_asyncRelayActionLastFailedId = pending.requestId;
    invalidateAsyncStateCacheAfterRelayAction();
    logPrint("[ASYNC][RELAY] guarded apply REFUSED id=" + String(pending.requestId) +
             " relay=" + String((int)pending.relayIndex + 1) +
             " target=" + String(targetState ? "true" : "false") +
             " physicalBefore=" + String(before ? "true" : "false"));
    return;
  }

  // Hardware mutation is intentionally confined to the normal Arduino loop.
  // Even a no-op OFF->OFF test goes through setRelay(), exercising the real
  // relay mutex/GPIO path without energizing the output.
  setRelay(pending.relayIndex, targetState);
  const bool after = isRelayOn(pending.relayIndex);

  if (after != targetState) {
    ++g_asyncRelayActionApplyFailures;
    g_asyncRelayActionLastFailedId = pending.requestId;
    invalidateAsyncStateCacheAfterRelayAction();
    logPrint("[ASYNC][RELAY] apply FAILED id=" + String(pending.requestId) +
             " relay=" + String((int)pending.relayIndex + 1) +
             " action=" + String(pending.toggle ? "toggle" : "set") +
             " target=" + String(targetState ? "true" : "false") +
             " actual=" + String(after ? "true" : "false"));
    return;
  }

  if (before != after) {
    ++g_asyncRelayActionTransitions;
  } else {
    ++g_asyncRelayActionNoops;
  }
  ++g_asyncRelayActionApplied;
  g_asyncRelayActionLastAppliedId = pending.requestId;
  invalidateAsyncStateCacheAfterRelayAction();

  logPrint("[ASYNC][RELAY] applied id=" + String(pending.requestId) +
           " relay=" + String((int)pending.relayIndex + 1) +
           " action=" + String(pending.toggle ? "toggle" : "set") +
           " before=" + String(before ? "true" : "false") +
           " after=" + String(after ? "true" : "false") +
           " transition=" + String(before != after ? "true" : "false"));
}

static bool anyAsyncIrrigationPumpOnOrArmed() {
  for (size_t pump = 0; pump < IRRIGATION_PUMP_COUNT; ++pump) {
    const int idx = IRRIGATION_PUMP_RELAY_INDEX[pump];
    if (idx < 0 || idx >= activeRelayCount || relayPins[idx] < 0) continue;
    if (relayActive[idx] || isRelayOn(idx)) return true;
  }
  return false;
}

static bool asyncIrrigationFloodBlocked() {
  return irrigation.stopOnFloodAlarm && settings.flood.enabled &&
         floodState.known && floodState.alarm;
}

static void sendAsyncWateringState(AsyncWebServerRequest* request) {
  if (!request) return;
  bool pending = false;
  uint32_t pendingId = 0;
  uint8_t pendingType = 0;
  if (g_asyncWateringMutex && xSemaphoreTake(g_asyncWateringMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncWateringPending.pending;
    pendingId = g_asyncWateringPending.requestId;
    pendingType = static_cast<uint8_t>(g_asyncWateringPending.type);
    xSemaphoreGive(g_asyncWateringMutex);
  }

  const bool running = irrigation.irrigationRuns > 0;
  const bool anyPumpOn = anyAsyncIrrigationPumpOnOrArmed();
  String json;
  json.reserve(768);
  json += "{\"ok\":true";
  json += ",\"running\":"; json += running ? "true" : "false";
  json += ",\"runsLeft\":" + String(irrigation.irrigationRuns);
  json += ",\"timeLeft\":\"" + asyncJsonEscape(irrigation.wTimeLeft) + "\"";
  json += ",\"stopRequested\":"; json += irrigationStopRequested ? "true" : "false";
  json += ",\"activePumpCount\":" + String((unsigned int)enabledIrrigationPumpCount());
  json += ",\"anyPumpOn\":"; json += anyPumpOn ? "true" : "false";
  json += ",\"floodBlocked\":"; json += asyncIrrigationFloodBlocked() ? "true" : "false";
  json += ",\"safeForAutomatedStopNoop\":"; json += (!running && !anyPumpOn) ? "true" : "false";
  json += ",\"pending\":"; json += pending ? "true" : "false";
  json += ",\"pendingRequestId\":" + String(pendingId);
  json += ",\"pendingType\":" + String(pendingType);
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncWateringLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncWateringLastAppliedId);
  json += ",\"lastFailedId\":" + String((uint32_t)g_asyncWateringLastFailedId);
  json += "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void queueAsyncWateringAction(AsyncWebServerRequest* request,
                                     AsyncWateringActionType type,
                                     uint8_t pumpSlot = 0,
                                     uint32_t pulseMs = 10000UL,
                                     bool noTransitionOnly = false) {
  if (!request) return;
  ++g_asyncWateringRequests;
  if (!g_asyncWateringMutex) {
    ++g_asyncWateringFailures;
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"runtime_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (type == AsyncWateringActionType::PumpPulse &&
      (pumpSlot >= IRRIGATION_PUMP_COUNT || pulseMs < 100UL || pulseMs > 60000UL)) {
    ++g_asyncWateringFailures;
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_pump_pulse\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (xSemaphoreTake(g_asyncWateringMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncWateringFailures;
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"runtime_handoff_busy\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (g_asyncWateringPending.pending) {
    xSemaphoreGive(g_asyncWateringMutex); ++g_asyncWateringFailures;
    AsyncWebServerResponse* response = request->beginResponse(409, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"runtime_apply_pending\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const uint32_t requestId = ++g_asyncWateringNextRequestId;
  g_asyncWateringPending.pending = true;
  g_asyncWateringPending.requestId = requestId;
  g_asyncWateringPending.type = type;
  g_asyncWateringPending.pumpSlot = pumpSlot;
  g_asyncWateringPending.pulseMs = pulseMs;
  g_asyncWateringPending.noTransitionOnly = noTransitionOnly;
  g_asyncWateringLastQueuedId = requestId;
  ++g_asyncWateringQueued;
  xSemaphoreGive(g_asyncWateringMutex);

  String json = "{\"ok\":true,\"requestId\":" + String(requestId) + ",\"actionType\":" + String((int)type);
  if (type == AsyncWateringActionType::PumpPulse) {
    json += ",\"pump\":" + String((int)pumpSlot + 1) + ",\"pulseMs\":" + String(pulseMs);
  }
  json += ",\"noTransitionOnly\":" + String(noTransitionOnly ? "true" : "false") + ",\"applyQueued\":true,\"applyContext\":\"main-loop\"}";
  AsyncWebServerResponse* response = request->beginResponse(202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void handleAsyncWateringActionPost(AsyncWebServerRequest* request) {
  if (!request) return;
  auto* body = static_cast<AsyncJsonBody*>(request->_tempObject);
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(body && body->tooLarge ? 413 : 400, "application/json; charset=utf-8", body && body->tooLarge ? "{\"ok\":false,\"err\":\"body_too_large\"}" : "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || doc["action"].isNull() || !doc["action"].is<const char*>()) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_watering_action\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  String action = doc["action"].as<String>(); action.trim(); action.toLowerCase();
  bool guard = false;
  if (!doc["noTransitionOnly"].isNull()) {
    if (!doc["noTransitionOnly"].is<bool>()) {
        AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_transition_guard\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return;
    }
    guard = doc["noTransitionOnly"].as<bool>();
  }
  if (action == "start") queueAsyncWateringAction(request, AsyncWateringActionType::StartApi);
  else if (action == "stop") queueAsyncWateringAction(request, AsyncWateringActionType::Stop, 0, 10000UL, guard);
  else if (action == "toggle") queueAsyncWateringAction(request, AsyncWateringActionType::ToggleDashboard);
  else if (action == "pingtank") queueAsyncWateringAction(request, AsyncWateringActionType::TankPing);
  else {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_watering_action\"}");
    addAsyncCommonHeaders(response, false); request->send(response);
  }
}

static bool startAsyncIrrigationNow(bool startedViaApi, String& error) {
  if (irrigation.irrigationRuns > 0) { error = "already_running"; return false; }
  if (activeRelayCount < 8) { error = "requires_8_relays"; return false; }
  if (asyncIrrigationFloodBlocked()) { error = "flood_alarm_active"; return false; }
  if (enabledIrrigationPumpCount() == 0) { error = "no_pumps_enabled"; return false; }
  if (anyAsyncIrrigationPumpOnOrArmed()) { error = "pump_already_active"; return false; }
  if (irrigation.timePerTask <= 0 || irrigation.betweenTasks <= 0 || irrigation.amountOfWater <= 0.0f || irrigation.amount <= 0.0f) { error = "invalid_configuration"; return false; }
  const float mlPerTaskPerPot = (irrigation.amountOfWater / 10.0f) * (float)irrigation.timePerTask;
  if (mlPerTaskPerPot <= 0.0f) { error = "invalid_ml_per_task"; return false; }
  const int runs = (int)ceilf(irrigation.amount / mlPerTaskPerPot);
  if (runs <= 0) { error = "invalid_run_count"; return false; }
  irrigationStopRequested = false;
  irrigationStartNotifyViaApi = startedViaApi;
  irrigationStartNotifyPending = true;
  irrigation.irrigationRuns = runs;
  irrigation.wTimeLeft = calculateEndtimeWatering();
  logPrint("[ASYNC][IRRIGATION] start applied runs=" + String(runs) + " source=" + String(startedViaApi ? "api" : "dashboard") + " notify=watering-task");
  return true;
}

static void processAsyncWateringActionApply() {
  if (!g_asyncWateringMutex) return;
  AsyncWateringPending pending; bool hasPending = false;
  if (xSemaphoreTake(g_asyncWateringMutex, 0) == pdTRUE) {
    if (g_asyncWateringPending.pending) { pending = g_asyncWateringPending; g_asyncWateringPending.pending = false; hasPending = true; }
    xSemaphoreGive(g_asyncWateringMutex);
  }
  if (!hasPending) return;
  bool ok = false; bool noop = false; String error;
  switch (pending.type) {
    case AsyncWateringActionType::StartApi:
      ok = startAsyncIrrigationNow(true, error); if (ok) ++g_asyncWateringStarts; break;
    case AsyncWateringActionType::StartDashboard:
      ok = startAsyncIrrigationNow(false, error); if (ok) ++g_asyncWateringStarts; break;
    case AsyncWateringActionType::ToggleDashboard:
      if (irrigation.irrigationRuns > 0) { requestIrrigationStop("async dashboard"); ok = true; ++g_asyncWateringStops; }
      else { ok = startAsyncIrrigationNow(false, error); if (ok) ++g_asyncWateringStarts; }
      break;
    case AsyncWateringActionType::Stop: {
      const bool runningBefore = irrigation.irrigationRuns > 0;
      const bool pumpOnBefore = anyAsyncIrrigationPumpOnOrArmed();
      if (pending.noTransitionOnly && (runningBefore || pumpOnBefore)) { error = "guarded_transition_refused"; break; }
      requestIrrigationStop("async api"); ok = true;
      if (runningBefore || pumpOnBefore) ++g_asyncWateringStops;
      else { ++g_asyncWateringNoops; noop = true; }
      break;
    }
    case AsyncWateringActionType::PumpPulse: {
      if (pending.pumpSlot >= IRRIGATION_PUMP_COUNT) { error = "invalid_pump"; break; }
      const int idx = IRRIGATION_PUMP_RELAY_INDEX[pending.pumpSlot];
      if (idx < 0 || idx >= activeRelayCount || relayPins[idx] < 0) { error = "pump_not_active"; break; }
      if (!irrigation.pumpEnabled[pending.pumpSlot]) { error = "pump_disabled"; break; }
      if (irrigation.irrigationRuns > 0) { error = "watering_running"; break; }
      if (anyAsyncIrrigationPumpOnOrArmed()) { error = "another_pump_active"; break; }
      setRelay(idx, true);
      if (!isRelayOn(idx)) { error = "pump_gpio_verify_failed"; break; }
      relayOffTime[idx] = millis() + pending.pulseMs;
      relayActive[idx] = true;
      ++g_asyncWateringPumpPulses; ok = true;
      logPrint("[ASYNC][PUMP] relay " + String(idx + 1) + " ON for " + String(pending.pulseMs) + "ms");
      break;
    }
    case AsyncWateringActionType::TankPing: {
      const float level = pingTankLevel(TRIG, ECHO);
      if (!isfinite(level) || level < 0.0f) { error = "tank_ping_failed"; break; }
      tankLevelCm = level;
      if (irrigation.tank.max != 0 && irrigation.tank.max != irrigation.tank.min) tankLevel = calculateTankPercent(tankLevelCm, irrigation.tank.min, irrigation.tank.max);
      ++g_asyncWateringTankPings; ok = true; break;
    }
    default: error = "invalid_action"; break;
  }
  if (!ok) {
    ++g_asyncWateringApplyFailures; g_asyncWateringLastFailedId = pending.requestId;
    logPrint("[ASYNC][IRRIGATION] apply FAILED id=" + String(pending.requestId) + " err=" + error);
  } else {
    ++g_asyncWateringApplied; g_asyncWateringLastAppliedId = pending.requestId;
    logPrint("[ASYNC][IRRIGATION] applied id=" + String(pending.requestId) + " type=" + String((int)pending.type) + " noop=" + String(noop ? "true" : "false"));
  }
  invalidateAsyncStateCacheAfterRelayAction();
}

static bool asyncRequestParam(AsyncWebServerRequest* request, const char* name, String& value) {
  if (!request || !name) return false;
  if (request->hasParam(name)) { value = request->getParam(name)->value(); return true; }
  if (request->hasParam(name, true)) { value = request->getParam(name, true)->value(); return true; }
  return false;
}

static void handleAsyncFloodEvent(AsyncWebServerRequest* request) {
  if (!request) return;
  ++g_asyncFloodEventRequests;
  if (!settings.flood.enabled || settings.flood.webhookToken.isEmpty()) {
    AsyncWebServerResponse* response = request->beginResponse(404, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"disabled\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  String token;
  if (!asyncRequestParam(request, "token", token) || token != settings.flood.webhookToken) {
    logPrint("[FLOOD][ASYNC] rejected webhook: invalid token");
    AsyncWebServerResponse* response = request->beginResponse(403, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"forbidden\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  String alarmArg;
  if (!asyncRequestParam(request, "alarm", alarmArg)) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"missing_alarm\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  alarmArg.trim(); alarmArg.toLowerCase();
  bool alarm = false;
  if (alarmArg == "1" || alarmArg == "true" || alarmArg == "on" || alarmArg == "flood") alarm = true;
  else if (alarmArg == "0" || alarmArg == "false" || alarmArg == "off" || alarmArg == "dry") alarm = false;
  else {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"invalid_alarm\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  int batteryPct = -1; String batteryArg;
  if (asyncRequestParam(request, "battery", batteryArg)) {
    batteryArg.trim();
    if (batteryArg.length() && batteryArg != "null") {
      char* endPtr = nullptr; const long parsed = strtol(batteryArg.c_str(), &endPtr, 10);
      if (endPtr == batteryArg.c_str() || *endPtr != '\0' || parsed < 0 || parsed > 100) {
            AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"invalid_battery\"}");
        addAsyncCommonHeaders(response, false); request->send(response); return;
      }
      batteryPct = (int)parsed;
    }
  }
  time_t eventEpoch = time(nullptr); String tsArg;
  if (asyncRequestParam(request, "ts", tsArg)) {
    tsArg.trim();
    if (tsArg.length() && tsArg != "null") {
      char* endPtr = nullptr; const long long parsed = strtoll(tsArg.c_str(), &endPtr, 10);
      if (endPtr == tsArg.c_str() || *endPtr != '\0' || parsed < 1609459200LL) {
            AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"invalid_timestamp\"}");
        addAsyncCommonHeaders(response, false); request->send(response); return;
      }
      eventEpoch = (time_t)parsed;
    }
  }
  if (!g_asyncFloodEventMutex || xSemaphoreTake(g_asyncFloodEventMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncFloodEventFailures;
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"runtime_handoff_busy\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (g_asyncFloodEventPending.pending) {
    xSemaphoreGive(g_asyncFloodEventMutex); ++g_asyncFloodEventFailures;
    AsyncWebServerResponse* response = request->beginResponse(409, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"runtime_apply_pending\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const uint32_t requestId = ++g_asyncFloodEventNextRequestId;
  g_asyncFloodEventPending.pending = true; g_asyncFloodEventPending.requestId = requestId;
  g_asyncFloodEventPending.alarm = alarm; g_asyncFloodEventPending.batteryPct = batteryPct; g_asyncFloodEventPending.eventEpoch = eventEpoch;
  g_asyncFloodEventLastQueuedId = requestId; ++g_asyncFloodEventQueued;
  xSemaphoreGive(g_asyncFloodEventMutex);
  String json = "{\"ok\":true,\"requestId\":" + String(requestId) + ",\"alarm\":" + String(alarm ? "true" : "false") + ",\"applyQueued\":true,\"applyContext\":\"main-loop\"}";
  AsyncWebServerResponse* response = request->beginResponse(202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false); response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff"); request->send(response);
}

static void processAsyncFloodEventApply() {
  if (!g_asyncFloodEventMutex) return;
  AsyncFloodEventPending pending; bool hasPending = false;
  if (xSemaphoreTake(g_asyncFloodEventMutex, 0) == pdTRUE) {
    if (g_asyncFloodEventPending.pending) { pending = g_asyncFloodEventPending; g_asyncFloodEventPending.pending = false; hasPending = true; }
    xSemaphoreGive(g_asyncFloodEventMutex);
  }
  if (!hasPending) return;
  const bool changed = !floodState.known || floodState.alarm != pending.alarm;
  const bool batteryChanged = pending.batteryPct >= 0 && floodState.batteryPct != pending.batteryPct;
  floodState.known = true; floodState.alarm = pending.alarm;
  if (pending.batteryPct >= 0) floodState.batteryPct = pending.batteryPct;
  const String timestamp = floodTimestampFromEpoch(pending.eventEpoch);
  if (!timestamp.isEmpty()) floodState.lastSeen = timestamp;
  if (pending.alarm && irrigation.stopOnFloodAlarm && irrigation.irrigationRuns > 0) { requestIrrigationStop("Flood alarm async"); ++g_asyncFloodStops; }
  bool persistOk = true;
  if (changed || batteryChanged) {
    Preferences floodPrefs; persistOk = floodPrefs.begin(PREF_NS, false);
    if (persistOk) {
      persistOk = floodPrefs.putBool(KEY_FLOOD_KNOWN, floodState.known) > 0;
      persistOk = (floodPrefs.putBool(KEY_FLOOD_ALARM, floodState.alarm) > 0) && persistOk;
      if (floodState.batteryPct >= 0) persistOk = (floodPrefs.putInt(KEY_FLOOD_BATTERY, floodState.batteryPct) > 0) && persistOk;
      if (!floodState.lastSeen.isEmpty()) persistOk = (floodPrefs.putString(KEY_FLOOD_LAST_SEEN, floodState.lastSeen) > 0) && persistOk;

      // Strict readback verification. A Flood alarm is still kept in RAM and
      // can stop irrigation even if persistence fails; safety state must never
      // be rolled back merely because NVS is unavailable.
      if (persistOk) {
        persistOk = floodPrefs.getBool(KEY_FLOOD_KNOWN, !floodState.known) == floodState.known;
        persistOk = (floodPrefs.getBool(KEY_FLOOD_ALARM, !floodState.alarm) == floodState.alarm) && persistOk;
        if (floodState.batteryPct >= 0) {
          persistOk = (floodPrefs.getInt(KEY_FLOOD_BATTERY, -999) == floodState.batteryPct) && persistOk;
        }
        if (!floodState.lastSeen.isEmpty()) {
          persistOk = (floodPrefs.getString(KEY_FLOOD_LAST_SEEN, "") == floodState.lastSeen) && persistOk;
        }
      }
      floodPrefs.end();
    }
  }
  if (!persistOk) {
    ++g_asyncFloodEventApplyFailures; g_asyncFloodEventLastFailedId = pending.requestId;
    logPrint("[FLOOD][ASYNC] apply FAILED id=" + String(pending.requestId) + " err=persist");
  } else {
    ++g_asyncFloodEventApplied; g_asyncFloodEventLastAppliedId = pending.requestId;
    if (changed) logPrint(String("[FLOOD][ASYNC] state changed: ") + (pending.alarm ? "ALARM" : "DRY"));
    if (batteryChanged) logPrint("[FLOOD][ASYNC] battery: " + String(floodState.batteryPct) + "%");
  }
  invalidateAsyncStateCacheAfterRelayAction();
}


// -------------------- Phase 25: queued LittleFS mutations --------------------
static const char* asyncStorageTypeName(AsyncStorageActionType type) {
  switch (type) {
    case AsyncStorageActionType::DiaryAdd: return "diary_add";
    case AsyncStorageActionType::DiaryUpdate: return "diary_update";
    case AsyncStorageActionType::DiaryDelete: return "diary_delete";
    case AsyncStorageActionType::DiaryClear: return "diary_clear";
    case AsyncStorageActionType::FsFormat: return "fs_format";
    default: return "none";
  }
}

static bool queueAsyncStorageAction(AsyncWebServerRequest* request, AsyncStorageActionType type) {
  if (!request) return false;
  auto* body = static_cast<AsyncJsonBody*>(request->_tempObject);
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(
      body && body->tooLarge ? 413 : 400, "application/json; charset=utf-8",
      body && body->tooLarge ? "{\"ok\":false,\"err\":\"body_too_large\"}" : "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body->data, body->len)) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"bad_json\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return false;
  }

  AsyncStoragePending candidate;
  candidate.type = type;
  if (type == AsyncStorageActionType::DiaryAdd) {
    candidate.note = (const char*)(doc["note"] | "");
    candidate.phase = (const char*)(doc["phase"] | "");
    candidate.lang = (const char*)(doc["lang"] | "");
    candidate.meta = (const char*)(doc["meta"] | "");
    candidate.note.trim(); candidate.phase.trim(); candidate.lang.trim(); candidate.meta.trim();
    if (candidate.note.length() > 400 || candidate.meta.length() > 240 ||
        (candidate.phase.length() && candidate.phase != "grow" && candidate.phase != "flower" && candidate.phase != "dry") ||
        (candidate.lang.length() && candidate.lang != "de" && candidate.lang != "en")) {
      AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_diary_add\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return false;
    }
  } else if (type == AsyncStorageActionType::DiaryUpdate) {
    candidate.id = (const char*)(doc["id"] | "");
    candidate.note = (const char*)(doc["note"] | "");
    candidate.phase = (const char*)(doc["phase"] | "");
    candidate.id.trim(); candidate.note.trim(); candidate.phase.trim();
    if (!candidate.id.length() || candidate.id.length() > 32 || candidate.note.length() > 400 ||
        (!candidate.note.length() && !candidate.phase.length()) ||
        (candidate.phase.length() && candidate.phase != "grow" && candidate.phase != "flower" && candidate.phase != "dry")) {
      AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_diary_update\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return false;
    }
  } else if (type == AsyncStorageActionType::DiaryDelete) {
    candidate.id = (const char*)(doc["id"] | "");
    candidate.id.trim();
    candidate.noMutationOnly = doc["noMutationOnly"] | false;
    if (!candidate.id.length() || candidate.id.length() > 32) {
      AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_diary_delete\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return false;
    }
  } else if (type == AsyncStorageActionType::DiaryClear || type == AsyncStorageActionType::FsFormat) {
    const String confirm = (const char*)(doc["confirm"] | "");
    if (confirm != "FORMAT_LITTLEFS") {
      AsyncWebServerResponse* response = request->beginResponse(403, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"confirmation_required\",\"confirm\":\"FORMAT_LITTLEFS\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return false;
    }
  }

  if (!g_asyncStorageMutex || xSemaphoreTake(g_asyncStorageMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncStorageFailures;
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"storage_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return false;
  }
  if (g_asyncStoragePending.pending || g_asyncStoragePending.processing) {
    xSemaphoreGive(g_asyncStorageMutex);
    ++g_asyncStorageFailures;
    AsyncWebServerResponse* response = request->beginResponse(409, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"storage_action_pending\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return false;
  }

  const uint32_t requestId = ++g_asyncStorageNextRequestId;
  candidate.requestId = requestId;
  candidate.pending = true;
  candidate.processing = false;
  g_asyncStoragePending = candidate;
  g_asyncStorageLastQueuedId = requestId;
  ++g_asyncStorageRequests; ++g_asyncStorageQueued;
  xSemaphoreGive(g_asyncStorageMutex);

  String json = "{\"ok\":true,\"queued\":true,\"requestId\":" + String(requestId) +
                ",\"action\":\"" + String(asyncStorageTypeName(type)) + "\"}";
  AsyncWebServerResponse* response = request->beginResponse(202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false); response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff"); request->send(response);
  return true;
}

static void handleAsyncStorageStatus(AsyncWebServerRequest* request) {
  if (!request) return;
  uint32_t wanted = 0;
  if (request->hasParam("requestId")) wanted = request->getParam("requestId")->value().toInt();
  bool pending = false, processing = false; uint32_t activeId = 0; String lastError;
  if (g_asyncStorageMutex && xSemaphoreTake(g_asyncStorageMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncStoragePending.pending;
    processing = g_asyncStoragePending.processing;
    activeId = g_asyncStoragePending.requestId;
    lastError = g_asyncStorageLastError;
    xSemaphoreGive(g_asyncStorageMutex);
  }
  String state = "unknown";
  if (wanted && activeId == wanted && (pending || processing)) state = processing ? "processing" : "pending";
  else if (wanted && g_asyncStorageLastAppliedId == wanted) state = "applied";
  else if (wanted && g_asyncStorageLastFailedId == wanted) state = "failed";
  String json = "{\"ok\":true,\"requestId\":" + String(wanted) + ",\"state\":\"" + state + "\"";
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncStorageLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncStorageLastAppliedId);
  json += ",\"lastFailedId\":" + String((uint32_t)g_asyncStorageLastFailedId);
  if (state == "failed" && lastError.length()) json += ",\"err\":\"" + asyncJsonEscape(lastError) + "\"";
  json += "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false); request->send(response);
}

static void processAsyncStorageActionApply() {
  if (!g_asyncStorageMutex) return;
  AsyncStoragePending action; bool have = false;
  if (xSemaphoreTake(g_asyncStorageMutex, 0) == pdTRUE) {
    if (g_asyncStoragePending.pending && !g_asyncStoragePending.processing) {
      g_asyncStoragePending.pending = false;
      g_asyncStoragePending.processing = true;
      action = g_asyncStoragePending;
      have = true;
    }
    xSemaphoreGive(g_asyncStorageMutex);
  }
  if (!have) return;

  bool ok = false; bool noOp = false; String err;
  switch (action.type) {
    case AsyncStorageActionType::DiaryAdd:
      ok = queuedDiaryAdd(action.note, action.phase, action.lang, action.meta, err); break;
    case AsyncStorageActionType::DiaryUpdate:
      ok = queuedDiaryUpdate(action.id, action.note, action.phase, err); break;
    case AsyncStorageActionType::DiaryDelete:
      if (action.noMutationOnly) {
        if (diaryIdExistsReadOnly(action.id)) { err = "guard_would_mutate"; ok = false; }
        else { ok = true; noOp = true; }
      } else ok = queuedDiaryDelete(action.id, err);
      break;
    case AsyncStorageActionType::DiaryClear:
    case AsyncStorageActionType::FsFormat:
      ok = formatFsForRecovery(); if (!ok) err = "format_failed"; break;
    default:
      err = "invalid_action"; break;
  }

  if (xSemaphoreTake(g_asyncStorageMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncStoragePending.processing = false;
    g_asyncStorageLastError = ok ? "" : err;
    if (ok) {
      ++g_asyncStorageApplied; g_asyncStorageLastAppliedId = action.requestId;
      if (noOp) ++g_asyncStorageNoops;
    } else {
      ++g_asyncStorageApplyFailures; g_asyncStorageLastFailedId = action.requestId;
    }
    xSemaphoreGive(g_asyncStorageMutex);
  }
  if (ok) logPrint("[ASYNC][STORAGE] applied id=" + String(action.requestId) + " action=" + asyncStorageTypeName(action.type) + (noOp ? " noop=true" : ""));
  else logPrint("[ASYNC][STORAGE] FAILED id=" + String(action.requestId) + " action=" + asyncStorageTypeName(action.type) + " err=" + err);
  invalidateAsyncStateCacheAfterRelayAction();
}


// -------------------- Phase 26: run profile + irrigation config --------------------
static bool isValidIsoDateValue(const String& value, bool allowEmpty) {
  if (value.length() == 0) return allowEmpty;
  if (value.length() != 10 || value.charAt(4) != '-' || value.charAt(7) != '-') return false;
  int y = 0, m = 0, d = 0;
  if (sscanf(value.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
  if (y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) return false;
  return true;
}

static float normalizedRunProfileMinVpd(float value) {
  return (isfinite(value) && value >= 0.0f && value <= 0.30f) ? value : 0.06f;
}

static float normalizedRunProfileHysteresis(float value) {
  return (isfinite(value) && value >= 0.0f && value <= 0.10f) ? value : 0.05f;
}

static int normalizedRunProfilePhase(int value) {
  return (value >= 1 && value <= 3) ? value : 1;
}

static String normalizedRunProfileDate(const String& value) {
  String out = value;
  out.trim();
  return isValidIsoDateValue(out, true) ? out : String();
}

static bool readAsyncRunProfileStored(AsyncRunProfileValues& out) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  out.startDate = normalizedRunProfileDate(prefGetStringOrDefault(prefs, KEY_STARTDATE, startDate));
  out.floweringStart = normalizedRunProfileDate(prefGetStringOrDefault(prefs, KEY_FLOWERDATE, startFlowering));
  out.dryingStart = normalizedRunProfileDate(prefGetStringOrDefault(prefs, KEY_DRYINGDATE, startDrying));
  out.currentPhase = normalizedRunProfilePhase(
    prefGetIntOrDefault(prefs, KEY_CURRENTPHASE, settings.grow.currentPhase));
  out.minVpdMonitoring = prefGetBoolOrDefault(prefs, KEY_MINVPD_MON, settings.grow.minVpdMonEnabled);
  out.minVPD = normalizedRunProfileMinVpd(
    prefGetFloatOrDefault(prefs, KEY_MINVPD, settings.grow.minVPD));
  out.vpdHysteresis = normalizedRunProfileHysteresis(
    prefGetFloatOrDefault(prefs, KEY_HYSTERESIS, settings.grow.vpdHysteresis));
  prefs.end();
  return true;
}

static bool writeAsyncRunProfilePrefs(const AsyncRunProfileValues& v) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) return false;
  prefs.putString(KEY_STARTDATE, v.startDate);
  prefs.putString(KEY_FLOWERDATE, v.floweringStart);
  prefs.putString(KEY_DRYINGDATE, v.dryingStart);
  prefs.putInt(KEY_CURRENTPHASE, v.currentPhase);
  prefs.putBool(KEY_MINVPD_MON, v.minVpdMonitoring);
  prefs.putFloat(KEY_MINVPD, v.minVPD);
  prefs.putFloat(KEY_HYSTERESIS, v.vpdHysteresis);
  const String a = prefs.getString(KEY_STARTDATE, "__bad__");
  const String b = prefs.getString(KEY_FLOWERDATE, "__bad__");
  const String c = prefs.getString(KEY_DRYINGDATE, "__bad__");
  const int phase = prefs.getInt(KEY_CURRENTPHASE, -1);
  const bool mon = prefs.getBool(KEY_MINVPD_MON, !v.minVpdMonitoring);
  const float minVpd = prefs.getFloat(KEY_MINVPD, NAN);
  const float hyst = prefs.getFloat(KEY_HYSTERESIS, NAN);
  prefs.end();
  return a == v.startDate && b == v.floweringStart && c == v.dryingStart &&
         phase == v.currentPhase && mon == v.minVpdMonitoring &&
         isfinite(minVpd) && isfinite(hyst) &&
         fabsf(minVpd - v.minVPD) <= 0.0005f &&
         fabsf(hyst - v.vpdHysteresis) <= 0.0005f;
}

static void clearAsyncRunProfileWriteReservation() {
  if (!g_asyncRunProfileMutex) return;
  if (xSemaphoreTake(g_asyncRunProfileMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncRunProfilePending.writeInProgress = false;
    xSemaphoreGive(g_asyncRunProfileMutex);
  }
}

static void sendAsyncRunProfileState(AsyncWebServerRequest* request) {
  if (!request) return;
  AsyncRunProfileValues stored;
  if (!readAsyncRunProfileStored(stored)) {
    ++g_asyncRunProfileWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  bool pending = false, writing = false; uint32_t pendingId = 0;
  if (g_asyncRunProfileMutex && xSemaphoreTake(g_asyncRunProfileMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncRunProfilePending.pending;
    writing = g_asyncRunProfilePending.writeInProgress;
    pendingId = g_asyncRunProfilePending.requestId;
    xSemaphoreGive(g_asyncRunProfileMutex);
  }
  String json; json.reserve(1024);
  json += "{\"ok\":true,\"setting\":\"runProfile\"";
  json += ",\"stored\":{\"startDate\":\"" + asyncJsonEscape(stored.startDate) + "\"";
  json += ",\"floweringStart\":\"" + asyncJsonEscape(stored.floweringStart) + "\"";
  json += ",\"dryingStart\":\"" + asyncJsonEscape(stored.dryingStart) + "\"";
  json += ",\"currentPhase\":" + String(stored.currentPhase);
  json += ",\"minVpdMonitoring\":" + String(stored.minVpdMonitoring ? "true" : "false");
  json += ",\"minVPD\":" + String(stored.minVPD, 3);
  json += ",\"vpdHysteresis\":" + String(stored.vpdHysteresis, 3) + "}";
  json += ",\"runtime\":{\"startDate\":\"" + asyncJsonEscape(startDate) + "\"";
  json += ",\"floweringStart\":\"" + asyncJsonEscape(startFlowering) + "\"";
  json += ",\"dryingStart\":\"" + asyncJsonEscape(startDrying) + "\"";
  json += ",\"currentPhase\":" + String(settings.grow.currentPhase);
  json += ",\"minVpdMonitoring\":" + String(settings.grow.minVpdMonEnabled ? "true" : "false");
  json += ",\"minVPD\":" + String(settings.grow.minVPD, 3);
  json += ",\"vpdHysteresis\":" + String(settings.grow.vpdHysteresis, 3) + "}";
  json += ",\"pending\":" + String(pending ? "true" : "false");
  json += ",\"writeInProgress\":" + String(writing ? "true" : "false");
  json += ",\"pendingRequestId\":" + String(pendingId);
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncRunProfileLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncRunProfileLastAppliedId);
  json += ",\"lastFailedId\":" + String((uint32_t)g_asyncRunProfileLastFailedId) + "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void handleAsyncRunProfilePost(AsyncWebServerRequest* request) {
  if (!request) return;
  auto* body = static_cast<AsyncJsonBody*>(request->_tempObject);
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8", body && body->tooLarge ?
      "{\"ok\":false,\"err\":\"body_too_large\"}" : "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || !doc["startDate"].is<const char*>() || !doc["floweringStart"].is<const char*>() ||
      !doc["dryingStart"].is<const char*>() || !doc["currentPhase"].is<int>() ||
      !doc["minVpdMonitoring"].is<bool>() || !doc["minVPD"].is<double>() ||
      !doc["vpdHysteresis"].is<double>()) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_run_profile\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  AsyncRunProfileValues requested;
  requested.startDate = doc["startDate"].as<String>(); requested.startDate.trim();
  requested.floweringStart = doc["floweringStart"].as<String>(); requested.floweringStart.trim();
  requested.dryingStart = doc["dryingStart"].as<String>(); requested.dryingStart.trim();
  requested.currentPhase = doc["currentPhase"].as<int>();
  requested.minVpdMonitoring = doc["minVpdMonitoring"].as<bool>();
  requested.minVPD = (float)doc["minVPD"].as<double>();
  requested.vpdHysteresis = (float)doc["vpdHysteresis"].as<double>();
  const char* invalidField = nullptr;
  if (!isValidIsoDateValue(requested.startDate, true)) invalidField = "startDate";
  else if (!isValidIsoDateValue(requested.floweringStart, true)) invalidField = "floweringStart";
  else if (!isValidIsoDateValue(requested.dryingStart, true)) invalidField = "dryingStart";
  else if (requested.currentPhase < 1 || requested.currentPhase > 3) invalidField = "currentPhase";
  else if (!isfinite(requested.minVPD) || requested.minVPD < 0.0f || requested.minVPD > 0.30f) invalidField = "minVPD";
  else if (!isfinite(requested.vpdHysteresis) || requested.vpdHysteresis < 0.0f || requested.vpdHysteresis > 0.10f) invalidField = "vpdHysteresis";
  if (invalidField) {
    String json = "{\"ok\":false,\"err\":\"run_profile_out_of_range\",\"field\":\"";
    json += invalidField;
    json += "\"}";
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (!g_asyncRunProfileMutex) {
    ++g_asyncRunProfileWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  uint32_t requestId = 0;
  if (xSemaphoreTake(g_asyncRunProfileMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncRunProfileWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_busy\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (g_asyncRunProfilePending.writeInProgress || g_asyncRunProfilePending.pending) {
    xSemaphoreGive(g_asyncRunProfileMutex);
    AsyncWebServerResponse* response = request->beginResponse(409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_apply_pending\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  g_asyncRunProfilePending.writeInProgress = true;
  requestId = ++g_asyncRunProfileNextRequestId;
  g_asyncRunProfilePending.requestId = requestId;
  xSemaphoreGive(g_asyncRunProfileMutex);

  ++g_asyncRunProfileWriteRequests;
  AsyncRunProfileValues oldStored;
  if (!readAsyncRunProfileStored(oldStored)) {
    clearAsyncRunProfileWriteReservation();
    ++g_asyncRunProfileWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (!writeAsyncRunProfilePrefs(requested)) {
    const bool rollbackOk = writeAsyncRunProfilePrefs(oldStored);
    clearAsyncRunProfileWriteReservation();
    ++g_asyncRunProfileWriteFailures;
    if (!rollbackOk) ++g_asyncRunProfileRollbackFailures;
    String json = "{\"ok\":false,\"err\":\"prefs_verify_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false"; json += "}";
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  bool queued = false;
  if (xSemaphoreTake(g_asyncRunProfileMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncRunProfilePending.value = requested;
    g_asyncRunProfilePending.pending = true;
    g_asyncRunProfilePending.writeInProgress = false;
    g_asyncRunProfileLastQueuedId = requestId;
    ++g_asyncRunProfileQueued;
    queued = true;
    xSemaphoreGive(g_asyncRunProfileMutex);
  }
  if (!queued) {
    const bool rollbackOk = writeAsyncRunProfilePrefs(oldStored);
    clearAsyncRunProfileWriteReservation();
    ++g_asyncRunProfileWriteFailures;
    if (!rollbackOk) ++g_asyncRunProfileRollbackFailures;
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_queue_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  String json = "{\"ok\":true,\"setting\":\"runProfile\",\"requestId\":" + String(requestId) +
                ",\"applyQueued\":true,\"applyContext\":\"main-loop\"}";
  AsyncWebServerResponse* response = request->beginResponse(202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false); response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void processAsyncRunProfileApply() {
  if (!g_asyncRunProfileMutex) return;
  AsyncRunProfilePending pending; bool hasPending = false;
  if (xSemaphoreTake(g_asyncRunProfileMutex, 0) == pdTRUE) {
    if (g_asyncRunProfilePending.pending) {
      pending = g_asyncRunProfilePending;
      g_asyncRunProfilePending.pending = false;
      hasPending = true;
    }
    xSemaphoreGive(g_asyncRunProfileMutex);
  }
  if (!hasPending) return;
  startDate = pending.value.startDate;
  startFlowering = pending.value.floweringStart;
  startDrying = pending.value.dryingStart;
  settings.grow.currentPhase = pending.value.currentPhase;
  curPhase = pending.value.currentPhase;
  settings.grow.minVpdMonEnabled = pending.value.minVpdMonitoring;
  settings.grow.minVPD = pending.value.minVPD;
  settings.grow.vpdHysteresis = pending.value.vpdHysteresis;
  updateGrowTimeValues();
  g_asyncRunProfileLastAppliedId = pending.requestId;
  ++g_asyncRunProfileApplied;
  invalidateAsyncStateCacheAfterRelayAction();
  logPrint("[ASYNC][RUNPROFILE] applied id=" + String(pending.requestId) +
           " phase=" + String(pending.value.currentPhase) +
           " minVpdMon=" + String(pending.value.minVpdMonitoring ? "true" : "false"));
}

static bool readAsyncIrrigationConfigStored(AsyncIrrigationConfigValues& out) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  out.amountOfWater = prefGetFloatOrDefault(prefs, KEY_AMOUNTOFWATER, irrigation.amountOfWater);
  out.timePerTask = prefGetIntOrDefault(prefs, KEY_TIMEPERTASK, irrigation.timePerTask);
  out.betweenTasks = prefGetIntOrDefault(prefs, KEY_BETWEENTASKS, irrigation.betweenTasks);
  out.amount = prefGetFloatOrDefault(prefs, KEY_IRRIGATION, irrigation.amount);
  out.pumpEnabled[0] = prefGetBoolOrDefault(prefs, KEY_PUMP1_ENABLED, irrigation.pumpEnabled[0]);
  out.pumpEnabled[1] = prefGetBoolOrDefault(prefs, KEY_PUMP2_ENABLED, irrigation.pumpEnabled[1]);
  out.pumpEnabled[2] = prefGetBoolOrDefault(prefs, KEY_PUMP3_ENABLED, irrigation.pumpEnabled[2]);
  out.stopOnFloodAlarm = prefGetBoolOrDefault(prefs, KEY_IRRIGATION_STOP_ON_FLOOD, irrigation.stopOnFloodAlarm);
  out.tankMin = prefGetFloatOrDefault(prefs, KEY_MINTANK, irrigation.tank.min);
  out.tankMax = prefGetFloatOrDefault(prefs, KEY_MAXTANK, irrigation.tank.max);
  prefs.end();
  return true;
}

static bool writeAsyncIrrigationConfigPrefs(const AsyncIrrigationConfigValues& v) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) return false;
  prefs.putFloat(KEY_AMOUNTOFWATER, v.amountOfWater);
  prefs.putInt(KEY_TIMEPERTASK, v.timePerTask);
  prefs.putInt(KEY_BETWEENTASKS, v.betweenTasks);
  prefs.putFloat(KEY_IRRIGATION, v.amount);
  prefs.putBool(KEY_PUMP1_ENABLED, v.pumpEnabled[0]);
  prefs.putBool(KEY_PUMP2_ENABLED, v.pumpEnabled[1]);
  prefs.putBool(KEY_PUMP3_ENABLED, v.pumpEnabled[2]);
  prefs.putBool(KEY_IRRIGATION_STOP_ON_FLOOD, v.stopOnFloodAlarm);
  prefs.putFloat(KEY_MINTANK, v.tankMin);
  prefs.putFloat(KEY_MAXTANK, v.tankMax);
  const float amountOfWater = prefs.getFloat(KEY_AMOUNTOFWATER, NAN);
  const int timePerTask = prefs.getInt(KEY_TIMEPERTASK, -1);
  const int betweenTasks = prefs.getInt(KEY_BETWEENTASKS, -1);
  const float amount = prefs.getFloat(KEY_IRRIGATION, NAN);
  const bool p1 = prefs.getBool(KEY_PUMP1_ENABLED, !v.pumpEnabled[0]);
  const bool p2 = prefs.getBool(KEY_PUMP2_ENABLED, !v.pumpEnabled[1]);
  const bool p3 = prefs.getBool(KEY_PUMP3_ENABLED, !v.pumpEnabled[2]);
  const bool stopFlood = prefs.getBool(KEY_IRRIGATION_STOP_ON_FLOOD, !v.stopOnFloodAlarm);
  const float tankMin = prefs.getFloat(KEY_MINTANK, NAN);
  const float tankMax = prefs.getFloat(KEY_MAXTANK, NAN);
  prefs.end();
  return isfinite(amountOfWater) && fabsf(amountOfWater - v.amountOfWater) <= 0.0005f &&
         timePerTask == v.timePerTask && betweenTasks == v.betweenTasks &&
         isfinite(amount) && fabsf(amount - v.amount) <= 0.0005f &&
         p1 == v.pumpEnabled[0] && p2 == v.pumpEnabled[1] && p3 == v.pumpEnabled[2] &&
         stopFlood == v.stopOnFloodAlarm && isfinite(tankMin) && isfinite(tankMax) &&
         fabsf(tankMin - v.tankMin) <= 0.0005f && fabsf(tankMax - v.tankMax) <= 0.0005f;
}

static bool asyncIrrigationRuntimeBusy() {
  if (irrigation.irrigationRuns > 0) return true;
  bool pumpOn = false;
  if (relayMutex && xSemaphoreTake(relayMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    if (activeRelayCount >= 8) pumpOn = relayStates[5] || relayStates[6] || relayStates[7];
    xSemaphoreGive(relayMutex);
  } else if (activeRelayCount >= 8) {
    pumpOn = relayStates[5] || relayStates[6] || relayStates[7];
  }
  return pumpOn;
}

static void clearAsyncIrrigationConfigWriteReservation() {
  if (!g_asyncIrrigationConfigMutex) return;
  if (xSemaphoreTake(g_asyncIrrigationConfigMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncIrrigationConfigPending.writeInProgress = false;
    xSemaphoreGive(g_asyncIrrigationConfigMutex);
  }
}

static void sendAsyncIrrigationConfigState(AsyncWebServerRequest* request) {
  if (!request) return;
  AsyncIrrigationConfigValues stored;
  if (!readAsyncIrrigationConfigStored(stored)) {
    ++g_asyncIrrigationConfigWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  bool pending = false, writing = false; uint32_t pendingId = 0;
  if (g_asyncIrrigationConfigMutex && xSemaphoreTake(g_asyncIrrigationConfigMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncIrrigationConfigPending.pending;
    writing = g_asyncIrrigationConfigPending.writeInProgress;
    pendingId = g_asyncIrrigationConfigPending.requestId;
    xSemaphoreGive(g_asyncIrrigationConfigMutex);
  }
  String json; json.reserve(1200);
  json += "{\"ok\":true,\"setting\":\"irrigationConfig\"";
  json += ",\"stored\":{\"amountOfWater\":" + String(stored.amountOfWater, 2);
  json += ",\"timePerTask\":" + String(stored.timePerTask);
  json += ",\"betweenTasks\":" + String(stored.betweenTasks);
  json += ",\"amount\":" + String(stored.amount, 2);
  json += ",\"pump1Enabled\":" + String(stored.pumpEnabled[0] ? "true" : "false");
  json += ",\"pump2Enabled\":" + String(stored.pumpEnabled[1] ? "true" : "false");
  json += ",\"pump3Enabled\":" + String(stored.pumpEnabled[2] ? "true" : "false");
  json += ",\"stopOnFloodAlarm\":" + String(stored.stopOnFloodAlarm ? "true" : "false");
  json += ",\"tankMin\":" + String(stored.tankMin, 2) + ",\"tankMax\":" + String(stored.tankMax, 2) + "}";
  json += ",\"runtime\":{\"amountOfWater\":" + String(irrigation.amountOfWater, 2);
  json += ",\"timePerTask\":" + String(irrigation.timePerTask);
  json += ",\"betweenTasks\":" + String(irrigation.betweenTasks);
  json += ",\"amount\":" + String(irrigation.amount, 2);
  json += ",\"pump1Enabled\":" + String(irrigation.pumpEnabled[0] ? "true" : "false");
  json += ",\"pump2Enabled\":" + String(irrigation.pumpEnabled[1] ? "true" : "false");
  json += ",\"pump3Enabled\":" + String(irrigation.pumpEnabled[2] ? "true" : "false");
  json += ",\"stopOnFloodAlarm\":" + String(irrigation.stopOnFloodAlarm ? "true" : "false");
  json += ",\"tankMin\":" + String(irrigation.tank.min, 2) + ",\"tankMax\":" + String(irrigation.tank.max, 2) + "}";
  json += ",\"runtimeBusy\":" + String(asyncIrrigationRuntimeBusy() ? "true" : "false");
  json += ",\"pending\":" + String(pending ? "true" : "false");
  json += ",\"writeInProgress\":" + String(writing ? "true" : "false");
  json += ",\"pendingRequestId\":" + String(pendingId);
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncIrrigationConfigLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncIrrigationConfigLastAppliedId);
  json += ",\"lastFailedId\":" + String((uint32_t)g_asyncIrrigationConfigLastFailedId) + "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false); response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void handleAsyncIrrigationConfigPost(AsyncWebServerRequest* request) {
  if (!request) return;
  auto* body = static_cast<AsyncJsonBody*>(request->_tempObject);
  if (!body || body->tooLarge) {
    AsyncWebServerResponse* response = request->beginResponse(body && body->tooLarge ? 413 : 400,
      "application/json; charset=utf-8", body && body->tooLarge ?
      "{\"ok\":false,\"err\":\"body_too_large\"}" : "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || !doc["amountOfWater"].is<double>() || !doc["timePerTask"].is<int>() ||
      !doc["betweenTasks"].is<int>() || !doc["amount"].is<double>() ||
      !doc["pump1Enabled"].is<bool>() || !doc["pump2Enabled"].is<bool>() || !doc["pump3Enabled"].is<bool>() ||
      !doc["stopOnFloodAlarm"].is<bool>() || !doc["tankMin"].is<double>() || !doc["tankMax"].is<double>()) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_irrigation_config\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  AsyncIrrigationConfigValues requested;
  requested.amountOfWater = (float)doc["amountOfWater"].as<double>();
  requested.timePerTask = doc["timePerTask"].as<int>();
  requested.betweenTasks = doc["betweenTasks"].as<int>();
  requested.amount = (float)doc["amount"].as<double>();
  requested.pumpEnabled[0] = doc["pump1Enabled"].as<bool>();
  requested.pumpEnabled[1] = doc["pump2Enabled"].as<bool>();
  requested.pumpEnabled[2] = doc["pump3Enabled"].as<bool>();
  requested.stopOnFloodAlarm = doc["stopOnFloodAlarm"].as<bool>();
  requested.tankMin = (float)doc["tankMin"].as<double>();
  requested.tankMax = (float)doc["tankMax"].as<double>();
  if (!isfinite(requested.amountOfWater) || requested.amountOfWater < 10.0f || requested.amountOfWater > 500.0f ||
      requested.timePerTask < 1 || requested.timePerTask > 10 ||
      requested.betweenTasks < 1 || requested.betweenTasks > 10 ||
      !isfinite(requested.amount) || requested.amount < 100.0f || requested.amount > 3000.0f ||
      !isfinite(requested.tankMin) || !isfinite(requested.tankMax) ||
      requested.tankMin < 0.0f || requested.tankMin > 1000.0f || requested.tankMax < 0.0f || requested.tankMax > 1000.0f) {
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"irrigation_config_out_of_range\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (requested.tankMin > requested.tankMax) {
    const float tmp = requested.tankMin; requested.tankMin = requested.tankMax; requested.tankMax = tmp;
  }
  if (asyncIrrigationRuntimeBusy()) {
    AsyncWebServerResponse* response = request->beginResponse(409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"irrigation_runtime_busy\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (!g_asyncIrrigationConfigMutex) {
    ++g_asyncIrrigationConfigWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  uint32_t requestId = 0;
  if (xSemaphoreTake(g_asyncIrrigationConfigMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncIrrigationConfigWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_handoff_busy\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (g_asyncIrrigationConfigPending.writeInProgress || g_asyncIrrigationConfigPending.pending) {
    xSemaphoreGive(g_asyncIrrigationConfigMutex);
    AsyncWebServerResponse* response = request->beginResponse(409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_apply_pending\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  g_asyncIrrigationConfigPending.writeInProgress = true;
  requestId = ++g_asyncIrrigationConfigNextRequestId;
  g_asyncIrrigationConfigPending.requestId = requestId;
  xSemaphoreGive(g_asyncIrrigationConfigMutex);

  ++g_asyncIrrigationConfigWriteRequests;
  AsyncIrrigationConfigValues oldStored;
  if (!readAsyncIrrigationConfigStored(oldStored)) {
    clearAsyncIrrigationConfigWriteReservation();
    ++g_asyncIrrigationConfigWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (!writeAsyncIrrigationConfigPrefs(requested)) {
    const bool rollbackOk = writeAsyncIrrigationConfigPrefs(oldStored);
    clearAsyncIrrigationConfigWriteReservation();
    ++g_asyncIrrigationConfigWriteFailures;
    if (!rollbackOk) ++g_asyncIrrigationConfigRollbackFailures;
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"prefs_verify_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  bool queued = false;
  if (xSemaphoreTake(g_asyncIrrigationConfigMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncIrrigationConfigPending.value = requested;
    g_asyncIrrigationConfigPending.oldStored = oldStored;
    g_asyncIrrigationConfigPending.pending = true;
    g_asyncIrrigationConfigPending.writeInProgress = false;
    g_asyncIrrigationConfigLastQueuedId = requestId;
    ++g_asyncIrrigationConfigQueued;
    queued = true;
    xSemaphoreGive(g_asyncIrrigationConfigMutex);
  }
  if (!queued) {
    const bool rollbackOk = writeAsyncIrrigationConfigPrefs(oldStored);
    clearAsyncIrrigationConfigWriteReservation();
    ++g_asyncIrrigationConfigWriteFailures;
    if (!rollbackOk) ++g_asyncIrrigationConfigRollbackFailures;
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"runtime_queue_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  String json = "{\"ok\":true,\"setting\":\"irrigationConfig\",\"requestId\":" + String(requestId) +
                ",\"applyQueued\":true,\"applyContext\":\"main-loop\"}";
  AsyncWebServerResponse* response = request->beginResponse(202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false); response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void processAsyncIrrigationConfigApply() {
  if (!g_asyncIrrigationConfigMutex) return;
  AsyncIrrigationConfigPending pending; bool hasPending = false;
  if (xSemaphoreTake(g_asyncIrrigationConfigMutex, 0) == pdTRUE) {
    if (g_asyncIrrigationConfigPending.pending) {
      pending = g_asyncIrrigationConfigPending;
      g_asyncIrrigationConfigPending.pending = false;
      hasPending = true;
    }
    xSemaphoreGive(g_asyncIrrigationConfigMutex);
  }
  if (!hasPending) return;
  if (asyncIrrigationRuntimeBusy()) {
    const bool rollbackOk = writeAsyncIrrigationConfigPrefs(pending.oldStored);
    ++g_asyncIrrigationConfigApplyFailures;
    if (!rollbackOk) ++g_asyncIrrigationConfigRollbackFailures;
    g_asyncIrrigationConfigLastFailedId = pending.requestId;
    logPrint("[ASYNC][IRRIGATION-CONFIG] FAILED id=" + String(pending.requestId) +
             " err=runtime_busy rollback=" + String(rollbackOk ? "ok" : "failed"));
    return;
  }
  irrigation.amountOfWater = pending.value.amountOfWater;
  irrigation.timePerTask = pending.value.timePerTask;
  irrigation.betweenTasks = pending.value.betweenTasks;
  irrigation.amount = pending.value.amount;
  irrigation.pumpEnabled[0] = pending.value.pumpEnabled[0];
  irrigation.pumpEnabled[1] = pending.value.pumpEnabled[1];
  irrigation.pumpEnabled[2] = pending.value.pumpEnabled[2];
  irrigation.stopOnFloodAlarm = pending.value.stopOnFloodAlarm;
  irrigation.tank.min = pending.value.tankMin;
  irrigation.tank.max = pending.value.tankMax;
  ++g_asyncIrrigationConfigApplied;
  g_asyncIrrigationConfigLastAppliedId = pending.requestId;
  invalidateAsyncStateCacheAfterRelayAction();
  logPrint("[ASYNC][IRRIGATION-CONFIG] applied id=" + String(pending.requestId) +
           " amount=" + String(pending.value.amount, 1) +
           " pumps=" + String(pending.value.pumpEnabled[0] ? "1" : "0") +
           String(pending.value.pumpEnabled[1] ? "1" : "0") +
           String(pending.value.pumpEnabled[2] ? "1" : "0"));
}

static const char* asyncShellyActionTypeName(AsyncShellyActionType type) {
  switch (type) {
    case AsyncShellyActionType::Toggle: return "toggle";
    case AsyncShellyActionType::RefreshSchedule: return "refreshSchedule";
    case AsyncShellyActionType::ResetEnergy: return "resetEnergy";
    case AsyncShellyActionType::ReloadConfig: return "reloadConfig";
    default: return "none";
  }
}

static bool asyncShellyActionRoleAllowed(AsyncShellyActionType type, uint8_t roleIndex) {
  if (type == AsyncShellyActionType::Toggle) return roleIndex <= 5;
  if (type == AsyncShellyActionType::RefreshSchedule) return roleIndex >= 1 && roleIndex <= 5;
  if (type == AsyncShellyActionType::ResetEnergy) return true;
  if (type == AsyncShellyActionType::ReloadConfig) return true;
  return false;
}

static bool asyncShellyDryRunRequested(AsyncWebServerRequest* request) {
  if (!request || !request->hasParam("dryRun")) return false;
  String v = request->getParam("dryRun")->value();
  v.trim(); v.toLowerCase();
  return v == "1" || v == "true" || v == "yes";
}


struct AsyncShellyDashboardValues {
  String ip[7];
  int gen[7] = {0,0,0,0,0,0,0};
  String username;
  String password;
  bool floodEnabled = false;
  String floodToken;
  String lightOnTime;
  int lightDayHours = 18;
};

static bool readAsyncShellyDashboardStored(AsyncShellyDashboardValues& out) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, true)) return false;
  for (uint8_t i = 0; i < 7; ++i) {
    const ShellyDevice* runtime = asyncShellyRuntimeDevice(i);
    const char* ipKey = asyncShellyIpKey(i);
    const char* genKey = asyncShellyGenKey(i);
    if (!runtime || !ipKey || !genKey) { prefs.end(); return false; }
    out.ip[i] = prefGetStringOrDefault(prefs, ipKey, runtime->ip);
    out.gen[i] = prefGetIntOrDefault(prefs, genKey, runtime->gen);
  }
  out.username = prefGetStringOrDefault(prefs, KEY_SHELLYUSERNAME, settings.shelly.username);
  out.password = prefGetStringOrDefault(prefs, KEY_SHELLYPASSWORD, settings.shelly.password);
  out.floodEnabled = prefGetBoolOrDefault(prefs, KEY_FLOOD_ENABLED, settings.flood.enabled);
  out.floodToken = prefGetStringOrDefault(prefs, KEY_FLOOD_TOKEN, settings.flood.webhookToken);
  out.lightOnTime = prefGetStringOrDefault(prefs, KEY_LIGHT_ON_TIME, settings.grow.lightOnTime);
  out.lightDayHours = prefGetIntOrDefault(prefs, KEY_LIGHT_DAY_HOURS, settings.grow.lightDayHours);
  prefs.end();
  return true;
}


static void sendAsyncShellyDashboardState(AsyncWebServerRequest* request) {
  if (!request) return;
  AsyncShellyDashboardValues stored;
  if (!readAsyncShellyDashboardStored(stored)) {
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  static const char* roles[7] = {"main","light","humidifier","heater","fan","exhaust","heatmat"};
  String json;
  json.reserve(1536);
  json += "{\"ok\":true,\"devices\":{";
  for (uint8_t i = 0; i < 7; ++i) {
    if (i) json += ",";
    json += "\"" + String(roles[i]) + "\":{\"ip\":\"" + asyncJsonEscape(stored.ip[i]) + "\",\"gen\":" + String(stored.gen[i]) + "}";
  }
  json += "},\"username\":\"" + asyncJsonEscape(stored.username) + "\"";
  json += ",\"passwordSet\":"; json += stored.password.isEmpty() ? "false" : "true";
  json += ",\"floodEnabled\":"; json += stored.floodEnabled ? "true" : "false";
  json += ",\"floodToken\":\"" + asyncJsonEscape(stored.floodToken) + "\"";
  json += ",\"lightOnTime\":\"" + asyncJsonEscape(stored.lightOnTime) + "\"";
  json += ",\"lightDayHours\":" + String(stored.lightDayHours);
  json += "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Prefs-Source", "nvs");
  request->send(response);
}

static bool writeAsyncShellyDashboardPrefs(const AsyncShellyDashboardValues& v) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) return false;
  bool ok = true;
  for (uint8_t i = 0; i < 7; ++i) {
    const char* ipKey = asyncShellyIpKey(i);
    const char* genKey = asyncShellyGenKey(i);
    if (!ipKey || !genKey) { ok = false; break; }
    prefs.putString(ipKey, v.ip[i]);
    prefs.putInt(genKey, v.gen[i]);
  }
  if (ok) {
    prefs.putString(KEY_SHELLYUSERNAME, v.username);
    prefs.putString(KEY_SHELLYPASSWORD, v.password);
    prefs.putBool(KEY_FLOOD_ENABLED, v.floodEnabled);
    prefs.putString(KEY_FLOOD_TOKEN, v.floodToken);
    prefs.putString(KEY_LIGHT_ON_TIME, v.lightOnTime);
    prefs.putInt(KEY_LIGHT_DAY_HOURS, v.lightDayHours);

    for (uint8_t i = 0; i < 7 && ok; ++i) {
      const char* ipKey = asyncShellyIpKey(i);
      const char* genKey = asyncShellyGenKey(i);
      ok = prefs.getString(ipKey, "__verify_failed__") == v.ip[i] &&
           prefs.getInt(genKey, -99) == v.gen[i];
    }
    ok = ok && prefs.getString(KEY_SHELLYUSERNAME, "__verify_failed__") == v.username;
    ok = ok && prefs.getString(KEY_SHELLYPASSWORD, "__verify_failed__") == v.password;
    ok = ok && prefs.getBool(KEY_FLOOD_ENABLED, !v.floodEnabled) == v.floodEnabled;
    ok = ok && prefs.getString(KEY_FLOOD_TOKEN, "__verify_failed__") == v.floodToken;
    ok = ok && prefs.getString(KEY_LIGHT_ON_TIME, "__verify_failed__") == v.lightOnTime;
    ok = ok && prefs.getInt(KEY_LIGHT_DAY_HOURS, -99) == v.lightDayHours;
  }
  prefs.end();
  return ok;
}

static bool validateAsyncShellyCredentialString(const String& value) {
  if (value.length() > 128) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if (c < 0x20 || c == 0x7F) return false;
  }
  return true;
}

static void clearAsyncShellyConfigWriteReservation() {
  if (!g_asyncShellyActionMutex) return;
  if (xSemaphoreTake(g_asyncShellyActionMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    g_asyncShellyActionPending.configWriteInProgress = false;
    xSemaphoreGive(g_asyncShellyActionMutex);
  }
}

static bool persistAsyncDetectedShellyGeneration(uint8_t idx, int detectedGen) {
  const char* genKey = asyncShellyGenKey(idx);
  if (!genKey || detectedGen < 1 || detectedGen > 4) return false;
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) return false;
  prefs.putInt(genKey, detectedGen);
  const bool ok = prefs.getInt(genKey, -99) == detectedGen;
  prefs.end();
  return ok;
}

static bool reloadAsyncShellyRuntimeFromPreferences(String& resultJson, String& errOut,
                                                    uint8_t detectGenerationMask = 0) {
  AsyncShellyDashboardValues stored;
  if (!readAsyncShellyDashboardStored(stored)) {
    errOut = "prefs_read_failed";
    resultJson = "{\"ok\":false,\"err\":\"prefs_read_failed\"}";
    return false;
  }

  static const char* roles[7] = {"main","light","humidifier","heater","fan","exhaust","heatmat"};
  bool requiredDetectionFailed = false;
  bool detectionPersistFailed = false;
  String failedRoles;
  String detectedJson = "{";
  bool firstDetected = true;

  // Generation discovery happens here in normal main-loop context. A changed
  // or newly configured IP carries generation 0 and therefore requires a
  // successful discovery. An unchanged known device keeps its last valid
  // generation if it is temporarily unreachable during a later save.
  for (uint8_t i = 0; i < 7; ++i) {
    if (!(detectGenerationMask & (1U << i)) || stored.ip[i].isEmpty()) continue;

    const int previousGen = stored.gen[i];
    int detectedGen = 0;
    int httpCode = -1;
    String model;
    ++g_asyncShellyActionNetworkOps;
    const bool detected = detectShellyGeneration(
      stored.ip[i], 80, stored.username, stored.password,
      detectedGen, model, httpCode);

    if (detected) {
      if (!persistAsyncDetectedShellyGeneration(i, detectedGen)) {
        detectionPersistFailed = true;
        if (failedRoles.length()) failedRoles += ",";
        failedRoles += roles[i];
        logPrint("[SHELLY][DETECT] persist failed role=" + String(roles[i]) +
                 " ip=" + stored.ip[i] + " Gen" + String(detectedGen));
      } else {
        stored.gen[i] = detectedGen;
        logPrint("[SHELLY][DETECT] role=" + String(roles[i]) + " ip=" + stored.ip[i] +
                 " detected=Gen" + String(detectedGen) +
                 (model.length() ? " model=" + model : String("")));
      }
    } else {
      if (previousGen == 0) {
        requiredDetectionFailed = true;
        if (failedRoles.length()) failedRoles += ",";
        failedRoles += roles[i];
        stored.gen[i] = 0;
        logPrint("[SHELLY][DETECT] FAILED role=" + String(roles[i]) +
                 " ip=" + stored.ip[i] + " HTTP=" + String(httpCode) +
                 " generation remains unknown");
      } else {
        logPrint("[SHELLY][DETECT] warning role=" + String(roles[i]) +
                 " ip=" + stored.ip[i] + " HTTP=" + String(httpCode) +
                 " keeping Gen" + String(previousGen));
      }
    }

    if (!firstDetected) detectedJson += ",";
    firstDetected = false;
    detectedJson += "\"" + String(roles[i]) + "\":" + String(stored.gen[i]);
  }
  detectedJson += "}";

  for (uint8_t i = 0; i < 7; ++i) {
    ShellyDevice* dev = asyncShellyRuntimeDeviceMutable(i);
    if (!dev) {
      errOut = "unknown_shelly_role";
      resultJson = "{\"ok\":false,\"err\":\"unknown_shelly_role\"}";
      return false;
    }
    const bool changed = dev->ip != stored.ip[i] || dev->gen != stored.gen[i];
    dev->ip = stored.ip[i];
    dev->gen = stored.gen[i];
    if (changed) {
      dev->values = ShellyValues{};
      for (int d = 0; d < 7; ++d) {
        dev->schedules.days[d].onHour = -1;
        dev->schedules.days[d].onMinute = -1;
        dev->schedules.days[d].offHour = -1;
        dev->schedules.days[d].offMinute = -1;
      }
    }
  }

  settings.shelly.username = stored.username;
  settings.shelly.password = stored.password;
  settings.flood.enabled = stored.floodEnabled;
  settings.flood.webhookToken = stored.floodToken;
  settings.grow.lightOnTime = stored.lightOnTime;
  settings.grow.lightDayHours = stored.lightDayHours;
  lightOnTime = stored.lightOnTime;
  lightDayHours = stored.lightDayHours;

  g_scheduleResolved = false;
  g_scheduleInitStartMs = millis();
  g_lastScheduleFetchMs = 0;

  resultJson = "{\"runtimeReloaded\":true,\"networkAccess\":";
  resultJson += detectGenerationMask ? "true" : "false";
  resultJson += ",\"detectedGenerations\":" + detectedJson;
  if (failedRoles.length()) resultJson += ",\"detectionFailedRoles\":\"" + asyncJsonEscape(failedRoles) + "\"";
  resultJson += "}";

  if (detectionPersistFailed) {
    errOut = "shelly_generation_persist_failed";
    return false;
  }
  if (requiredDetectionFailed) {
    errOut = "shelly_generation_detect_failed";
    return false;
  }
  return true;
}

static void handleAsyncShellyDashboardSave(AsyncWebServerRequest* request) {
  if (!request) return;
  logPrint("[ASYNC][SHELLY-CONFIG] POST /api/settings/shelly-dashboard received");

  auto* body = static_cast<AsyncJsonBody*>(request->_tempObject);
  if (!body || body->tooLarge) {
    logPrint(String("[ASYNC][SHELLY-CONFIG] rejected err=") + (body && body->tooLarge ? "body_too_large" : "missing_body"));
    AsyncWebServerResponse* response = request->beginResponse(
      body && body->tooLarge ? 413 : 400, "application/json; charset=utf-8",
      body && body->tooLarge ? "{\"ok\":false,\"err\":\"body_too_large\"}" :
                              "{\"ok\":false,\"err\":\"missing_body\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body->data, body->len);
  if (err || !doc["devices"].is<JsonObject>() ||
      doc["username"].isNull() || !doc["username"].is<const char*>() ||
      doc["floodEnabled"].isNull() || !doc["floodEnabled"].is<bool>()) {
    logPrint("[ASYNC][SHELLY-CONFIG] rejected err=invalid_shelly_settings");
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_shelly_settings\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  static const char* roles[7] = {"main","light","humidifier","heater","fan","exhaust","heatmat"};
  AsyncShellyDashboardValues requested;
  AsyncShellyDashboardValues oldStored;
  if (!readAsyncShellyDashboardStored(oldStored)) {
    logPrint("[ASYNC][SHELLY-CONFIG] failed err=prefs_read_failed");
    ++g_asyncShellyConfigWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      500, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"prefs_read_failed\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  requested = oldStored;

  bool detectGenerations = true;
  if (!doc["detectGenerations"].isNull()) {
    if (!doc["detectGenerations"].is<bool>()) {
      logPrint("[ASYNC][SHELLY-CONFIG] rejected err=invalid_detect_generations");
      AsyncWebServerResponse* response = request->beginResponse(
        400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_detect_generations\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return;
    }
    detectGenerations = doc["detectGenerations"].as<bool>();
  }

  uint8_t detectGenerationMask = 0;
  JsonObject devices = doc["devices"].as<JsonObject>();
  for (uint8_t i = 0; i < 7; ++i) {
    JsonVariant node = devices[roles[i]];
    if (!node.is<JsonObject>() || node["ip"].isNull() || !node["ip"].is<const char*>()) {
      logPrint(String("[ASYNC][SHELLY-CONFIG] rejected err=invalid_shelly_device role=") + roles[i]);
      AsyncWebServerResponse* response = request->beginResponse(
        400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_shelly_device\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return;
    }
    requested.ip[i] = node["ip"].as<String>();
    if (!normalizeAsyncShellyIpv4(requested.ip[i])) {
      logPrint(String("[ASYNC][SHELLY-CONFIG] rejected err=invalid_shelly_device role=") + roles[i]);
      AsyncWebServerResponse* response = request->beginResponse(
        400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_shelly_device\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return;
    }

    if (requested.ip[i].isEmpty()) {
      requested.gen[i] = 0;
    } else {
      const bool sameIp = requested.ip[i] == oldStored.ip[i];
      const bool oldGenKnown = oldStored.gen[i] >= 1 && oldStored.gen[i] <= 4;
      requested.gen[i] = (sameIp && oldGenKnown) ? oldStored.gen[i] : 0;
      if (detectGenerations) detectGenerationMask |= (1U << i);
    }
  }

  requested.username = doc["username"].as<String>();
  if (!validateAsyncShellyCredentialString(requested.username)) {
    logPrint("[ASYNC][SHELLY-CONFIG] rejected err=invalid_shelly_username");
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_shelly_username\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (!doc["password"].isNull()) {
    if (!doc["password"].is<const char*>()) {
      logPrint("[ASYNC][SHELLY-CONFIG] rejected err=invalid_shelly_password");
      AsyncWebServerResponse* response = request->beginResponse(
        400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_shelly_password\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return;
    }
    requested.password = doc["password"].as<String>();
    if (!validateAsyncShellyCredentialString(requested.password)) {
      logPrint("[ASYNC][SHELLY-CONFIG] rejected err=invalid_shelly_password");
      AsyncWebServerResponse* response = request->beginResponse(
        400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_shelly_password\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return;
    }
  }

  requested.floodEnabled = doc["floodEnabled"].as<bool>();
  if (!doc["floodToken"].isNull()) {
    if (!doc["floodToken"].is<const char*>()) {
      logPrint("[ASYNC][SHELLY-CONFIG] rejected err=invalid_flood_token");
      AsyncWebServerResponse* response = request->beginResponse(
        400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_flood_token\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return;
    }
    requested.floodToken = doc["floodToken"].as<String>();
    requested.floodToken.trim();
  }
  if (requested.floodEnabled && requested.floodToken.isEmpty()) {
    requested.floodToken = generateFloodWebhookToken();
  }
  if (!requested.floodToken.isEmpty() && !isValidFloodWebhookToken(requested.floodToken)) {
    logPrint("[ASYNC][SHELLY-CONFIG] rejected err=invalid_flood_token");
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_flood_token\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  // Light schedule is optional for a Shelly dashboard save. A user who only
  // defines/changes a Shelly device must not be forced to provide light timing.
  // If one light field is supplied, both must be supplied and valid.
  const bool hasLightOnTime = !doc["lightOnTime"].isNull();
  const bool hasLightDayHours = !doc["lightDayHours"].isNull();
  const bool lightScheduleProvided = hasLightOnTime || hasLightDayHours;
  if (lightScheduleProvided) {
    if (!hasLightOnTime || !hasLightDayHours ||
        !doc["lightOnTime"].is<const char*>() || !doc["lightDayHours"].is<int>()) {
      logPrint("[ASYNC][SHELLY-CONFIG] rejected err=incomplete_light_schedule");
      AsyncWebServerResponse* response = request->beginResponse(
        400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"incomplete_light_schedule\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return;
    }

    requested.lightOnTime = doc["lightOnTime"].as<String>();
    requested.lightOnTime.trim();
    requested.lightDayHours = doc["lightDayHours"].as<int>();
    if (!isValidLightOnTime(requested.lightOnTime) ||
        requested.lightDayHours < 1 || requested.lightDayHours > 20) {
      logPrint("[ASYNC][SHELLY-CONFIG] rejected err=invalid_light_schedule");
      AsyncWebServerResponse* response = request->beginResponse(
        400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_light_schedule\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return;
    }
  }

  bool programLightSchedule = false;
  if (!doc["programLightSchedule"].isNull()) {
    if (!doc["programLightSchedule"].is<bool>()) {
      logPrint("[ASYNC][SHELLY-CONFIG] rejected err=invalid_program_light_schedule");
      AsyncWebServerResponse* response = request->beginResponse(
        400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_program_light_schedule\"}");
      addAsyncCommonHeaders(response, false); request->send(response); return;
    }
    // Never program a schedule from an unrelated Shelly save. Programming is
    // only allowed when the request also carries an explicit valid schedule.
    programLightSchedule = lightScheduleProvided && doc["programLightSchedule"].as<bool>();
  }

  if (!g_asyncShellyActionMutex ||
      xSemaphoreTake(g_asyncShellyActionMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    logPrint("[ASYNC][SHELLY-CONFIG] failed err=shelly_handoff_unavailable");
    ++g_asyncShellyConfigWriteFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"shelly_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (g_asyncShellyActionPending.pending || g_asyncShellyActionPending.processing ||
      g_asyncShellyActionPending.configWriteInProgress) {
    logPrint("[ASYNC][SHELLY-CONFIG] rejected err=shelly_action_pending");
    xSemaphoreGive(g_asyncShellyActionMutex);
    AsyncWebServerResponse* response = request->beginResponse(
      409, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"shelly_action_pending\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  g_asyncShellyActionPending.configWriteInProgress = true;
  const uint32_t requestId = ++g_asyncShellyActionNextRequestId;
  g_asyncShellyActionPending.requestId = requestId;
  xSemaphoreGive(g_asyncShellyActionMutex);

  ++g_asyncShellyConfigWriteRequests;
  if (!writeAsyncShellyDashboardPrefs(requested)) {
    const bool rollbackOk = writeAsyncShellyDashboardPrefs(oldStored);
    logPrint(String("[ASYNC][SHELLY-CONFIG] failed err=prefs_verify_failed rollbackOk=") +
             (rollbackOk ? "true" : "false"));
    clearAsyncShellyConfigWriteReservation();
    ++g_asyncShellyConfigWriteFailures;
    if (!rollbackOk) ++g_asyncShellyConfigRollbackFailures;
    String json = "{\"ok\":false,\"err\":\"prefs_verify_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false"; json += "}";
    AsyncWebServerResponse* response = request->beginResponse(500, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  bool queued = false;
  if (xSemaphoreTake(g_asyncShellyActionMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    if (g_asyncShellyActionPending.configWriteInProgress &&
        !g_asyncShellyActionPending.pending && !g_asyncShellyActionPending.processing) {
      g_asyncShellyActionPending.configWriteInProgress = false;
      g_asyncShellyActionPending.pending = true;
      g_asyncShellyActionPending.processing = false;
      g_asyncShellyActionPending.requestId = requestId;
      g_asyncShellyActionPending.type = AsyncShellyActionType::ReloadConfig;
      g_asyncShellyActionPending.roleIndex = 255;
      g_asyncShellyActionPending.dryRun = false;
      g_asyncShellyActionPending.programLightSchedule = programLightSchedule;
      g_asyncShellyActionPending.detectGenerationMask = detectGenerationMask;
      g_asyncShellyActionLastQueuedId = requestId;
      ++g_asyncShellyActionRequests;
      ++g_asyncShellyActionQueued;
      queued = true;
    }
    xSemaphoreGive(g_asyncShellyActionMutex);
  }
  if (!queued) {
    const bool rollbackOk = writeAsyncShellyDashboardPrefs(oldStored);
    logPrint(String("[ASYNC][SHELLY-CONFIG] failed err=runtime_queue_failed rollbackOk=") +
             (rollbackOk ? "true" : "false"));
    clearAsyncShellyConfigWriteReservation();
    ++g_asyncShellyConfigWriteFailures;
    if (!rollbackOk) ++g_asyncShellyConfigRollbackFailures;
    String json = "{\"ok\":false,\"err\":\"runtime_queue_failed\",\"rollbackOk\":";
    json += rollbackOk ? "true" : "false"; json += "}";
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  String json;
  json.reserve(384);
  json += "{\"ok\":true,\"queued\":true,\"requestId\":" + String(requestId);
  json += ",\"action\":\"reloadConfig\",\"applyQueued\":true,\"applyContext\":\"main-loop\"";
  json += ",\"programLightSchedule\":"; json += programLightSchedule ? "true" : "false";
  json += ",\"detectGenerations\":"; json += detectGenerations ? "true" : "false";
  json += ",\"floodToken\":\"" + asyncJsonEscape(requested.floodToken) + "\"}";
  logPrint(String("[ASYNC][SHELLY-CONFIG] queued id=") + String(requestId) +
           " programLightSchedule=" + String(programLightSchedule ? "true" : "false") +
           " detectMask=0x" + String(detectGenerationMask, HEX));

  AsyncWebServerResponse* response = request->beginResponse(202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void queueAsyncShellyAction(AsyncWebServerRequest* request,
                                   AsyncShellyActionType type,
                                   uint8_t roleIndex = 255) {
  if (!request) return;
  if (!asyncShellyActionRoleAllowed(type, roleIndex)) {
    ++g_asyncShellyActionFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      404, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"invalid_shelly_action\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  const bool dryRun = asyncShellyDryRunRequested(request);
  const ShellyDevice* runtime = roleIndex <= 6 ? asyncShellyRuntimeDevice(roleIndex) : nullptr;
  if (!dryRun && type != AsyncShellyActionType::ResetEnergy &&
      type != AsyncShellyActionType::ReloadConfig && (!runtime || runtime->ip.isEmpty())) {
    ++g_asyncShellyActionFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"shelly_not_configured\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  if (!g_asyncShellyActionMutex ||
      xSemaphoreTake(g_asyncShellyActionMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncShellyActionFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"shelly_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  if (g_asyncShellyActionPending.pending || g_asyncShellyActionPending.processing ||
      g_asyncShellyActionPending.configWriteInProgress) {
    xSemaphoreGive(g_asyncShellyActionMutex);
    ++g_asyncShellyActionFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"shelly_action_pending\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  const uint32_t requestId = ++g_asyncShellyActionNextRequestId;
  g_asyncShellyActionPending.pending = true;
  g_asyncShellyActionPending.processing = false;
  g_asyncShellyActionPending.requestId = requestId;
  g_asyncShellyActionPending.type = type;
  g_asyncShellyActionPending.roleIndex = roleIndex;
  g_asyncShellyActionPending.dryRun = dryRun;
  g_asyncShellyActionPending.programLightSchedule = false;
  g_asyncShellyActionPending.detectGenerationMask = 0;
  g_asyncShellyActionLastQueuedId = requestId;
  ++g_asyncShellyActionRequests;
  ++g_asyncShellyActionQueued;
  xSemaphoreGive(g_asyncShellyActionMutex);

  String json;
  json.reserve(256);
  json += "{\"ok\":true,\"queued\":true,\"requestId\":" + String(requestId);
  json += ",\"action\":\"" + String(asyncShellyActionTypeName(type)) + "\"";
  if (roleIndex <= 6) {
    const char* role = asyncShellyRoleName(roleIndex);
    json += ",\"role\":\"" + String(role ? role : "unknown") + "\"";
  }
  json += ",\"dryRun\":"; json += dryRun ? "true" : "false";
  json += ",\"applyQueued\":true,\"applyContext\":\"main-loop\"}";
  AsyncWebServerResponse* response = request->beginResponse(202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static void handleAsyncShellyActionStatus(AsyncWebServerRequest* request) {
  if (!request) return;
  uint32_t wanted = 0;
  if (request->hasParam("requestId")) wanted = request->getParam("requestId")->value().toInt();

  bool pending = false, processing = false;
  uint32_t activeId = 0;
  AsyncShellyActionType activeType = AsyncShellyActionType::None;
  uint8_t activeRole = 255;
  bool activeDryRun = false;
  String lastError, lastResult;
  AsyncShellyActionType lastType = AsyncShellyActionType::None;
  uint8_t lastRole = 255;
  if (g_asyncShellyActionMutex &&
      xSemaphoreTake(g_asyncShellyActionMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncShellyActionPending.pending;
    processing = g_asyncShellyActionPending.processing;
    activeId = g_asyncShellyActionPending.requestId;
    activeType = g_asyncShellyActionPending.type;
    activeRole = g_asyncShellyActionPending.roleIndex;
    activeDryRun = g_asyncShellyActionPending.dryRun;
    lastError = g_asyncShellyActionLastError;
    lastResult = g_asyncShellyActionLastResultJson;
    lastType = g_asyncShellyActionLastType;
    lastRole = g_asyncShellyActionLastRole;
    xSemaphoreGive(g_asyncShellyActionMutex);
  }

  String state = "unknown";
  AsyncShellyActionType resultType = lastType;
  uint8_t resultRole = lastRole;
  bool resultDryRun = false;
  if (wanted && activeId == wanted && (pending || processing)) {
    state = processing ? "processing" : "pending";
    resultType = activeType;
    resultRole = activeRole;
    resultDryRun = activeDryRun;
  } else if (wanted && g_asyncShellyActionLastAppliedId == wanted) {
    state = "applied";
  } else if (wanted && g_asyncShellyActionLastFailedId == wanted) {
    state = "failed";
  }

  String json;
  json.reserve(768);
  json += "{\"ok\":true,\"requestId\":" + String(wanted) + ",\"state\":\"" + state + "\"";
  json += ",\"action\":\"" + String(asyncShellyActionTypeName(resultType)) + "\"";
  if (resultRole <= 6) {
    const char* role = asyncShellyRoleName(resultRole);
    json += ",\"role\":\"" + String(role ? role : "unknown") + "\"";
  }
  if (state == "pending" || state == "processing") {
    json += ",\"dryRun\":"; json += resultDryRun ? "true" : "false";
  }
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncShellyActionLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncShellyActionLastAppliedId);
  json += ",\"lastFailedId\":" + String((uint32_t)g_asyncShellyActionLastFailedId);
  if ((state == "applied" || state == "failed") && lastResult.length()) json += ",\"result\":" + lastResult;
  if (state == "failed" && lastError.length()) json += ",\"err\":\"" + asyncJsonEscape(lastError) + "\"";
  json += "}";

  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  response->addHeader("X-GrowTent-Runtime-Apply", "main-loop-handoff");
  request->send(response);
}

static bool performAsyncShellyEnergyReset(String& resultJson, String& errOut) {
  Preferences prefs;
  if (!prefs.begin(PREF_NS, false)) {
    errOut = "prefs";
    resultJson = "{\"ok\":false,\"err\":\"prefs\"}";
    return false;
  }

  auto resetOne = [&](ShellyDevice& device, const char* offsetKey) -> bool {
    if (device.ip.isEmpty()) return false;
    device.energyOffsetWh = 0.0f;
    const ShellyValues values = getShellyValues(device, 0, 80);
    const float rawEnergyWh = isnan(values.energyWh) ? 0.0f : values.energyWh;
    device.energyOffsetWh = rawEnergyWh;
    prefs.putFloat(offsetKey, rawEnergyWh);
    const bool resetSucceeded = shellyResetEnergyCounters(device, 0, 80);
    if (resetSucceeded) {
      device.energyOffsetWh = 0.0f;
      prefs.putFloat(offsetKey, 0.0f);
    }
    return resetSucceeded;
  };

  const bool mainOk = resetOne(settings.shelly.main, KEY_SHELLYMAINOFF);
  const bool lightOk = resetOne(settings.shelly.light, KEY_SHELLYLIGHTOFF);
  const bool humidifierOk = resetOne(settings.shelly.humidifier, KEY_SHELLYHUMOFF);
  const bool heaterOk = resetOne(settings.shelly.heater, KEY_SHELLYHEATEROFF);
  const bool fanOk = resetOne(settings.shelly.fan, KEY_SHELLYFANOFF);
  const bool exhaustOk = resetOne(settings.shelly.exhaust, KEY_SHELLYEXHAUSTOFF);
  const bool heatmatOk = resetOne(settings.shelly.heatmat, KEY_SHELLYHEATMATOFF);
  prefs.end();

  resultJson = "{\"main\":" + String(mainOk ? "true" : "false") +
               ",\"light\":" + String(lightOk ? "true" : "false") +
               ",\"humidifier\":" + String(humidifierOk ? "true" : "false") +
               ",\"heater\":" + String(heaterOk ? "true" : "false") +
               ",\"fan\":" + String(fanOk ? "true" : "false") +
               ",\"exhaust\":" + String(exhaustOk ? "true" : "false") +
               ",\"heatmat\":" + String(heatmatOk ? "true" : "false") + "}";
  return true;
}

static void processAsyncShellyActionApply() {
  if (!g_asyncShellyActionMutex) return;

  AsyncShellyActionPending action;
  bool hasAction = false;
  if (xSemaphoreTake(g_asyncShellyActionMutex, 0) == pdTRUE) {
    if (g_asyncShellyActionPending.pending && !g_asyncShellyActionPending.processing) {
      g_asyncShellyActionPending.pending = false;
      g_asyncShellyActionPending.processing = true;
      action = g_asyncShellyActionPending;
      hasAction = true;
    }
    xSemaphoreGive(g_asyncShellyActionMutex);
  }
  if (!hasAction) return;

  bool ok = false;
  String error;
  String result = "{}";

  if (action.dryRun) {
    ok = true;
    result = "{\"dryRun\":true,\"networkAccess\":false}";
    ++g_asyncShellyActionDryRuns;
  } else if (action.type == AsyncShellyActionType::Toggle) {
    ++g_asyncShellyActionNetworkOps;
    ShellyDevice* dev = asyncShellyRuntimeDeviceMutable(action.roleIndex);
    if (!dev || dev->ip.isEmpty()) {
      error = "shelly_not_configured";
    } else {
      ShellyValues before = getShellyValues(*dev, 0, 80);
      if (!before.ok) {
        error = "shelly_status_failed";
      } else {
        if (dev->gen >= 1 && dev->gen <= 4 &&
            !persistAutoDetectedShellyGeneration(*dev, dev->gen)) {
          logPrint("[ASYNC][SHELLY] warning: runtime generation could not be persisted for role=" +
                   String(asyncShellyRoleName(action.roleIndex)) +
                   " Gen" + String(dev->gen));
        }
        const bool target = !before.isOn;
        ok = shellySwitchSet(dev->ip, dev->gen, target, 0, 80);
        if (!ok) error = "shelly_switch_failed";
        result = String("{\"isOn\":") + (target ? "true" : "false") +
                 ",\"gen\":" + String(dev->gen) + "}";
      }
    }
  } else if (action.type == AsyncShellyActionType::RefreshSchedule) {
    ++g_asyncShellyActionNetworkOps;
    ShellyDevice* dev = asyncShellyRuntimeDeviceMutable(action.roleIndex);
    if (!dev || dev->ip.isEmpty()) {
      error = "shelly_not_configured";
    } else {
      if (action.roleIndex == 1) ok = refreshShellyLightSchedule(true);
      else ok = readShellyScheduleList(*dev, 0, 80);
      if (!ok) error = "shelly_schedule_refresh_failed";
      const DailySchedule& ds = dev->schedules.days[0];
      String on = (ds.onHour >= 0 && ds.onMinute >= 0) ? _fmtHHMM(ds.onHour, ds.onMinute) : String("");
      String off = (ds.offHour >= 0 && ds.offMinute >= 0) ? _fmtHHMM(ds.offHour, ds.offMinute) : String("");
      result = "{\"on\":\"" + asyncJsonEscape(on) + "\",\"off\":\"" + asyncJsonEscape(off) + "\"}";
    }
  } else if (action.type == AsyncShellyActionType::ResetEnergy) {
    ++g_asyncShellyActionNetworkOps;
    ok = performAsyncShellyEnergyReset(result, error);
  } else if (action.type == AsyncShellyActionType::ReloadConfig) {
    ok = reloadAsyncShellyRuntimeFromPreferences(result, error, action.detectGenerationMask);
    if (ok && action.programLightSchedule && !settings.shelly.light.ip.isEmpty()) {
      ++g_asyncShellyActionNetworkOps;
      const bool scheduleOk = applyGrowLightSchedule();
      if (result.endsWith("}")) result.remove(result.length() - 1);
      result += String(",\"lightScheduleApplied\":") + (scheduleOk ? "true" : "false") + "}";
      if (!scheduleOk) {
        logPrint("[ASYNC][SHELLY] config reload warning: light schedule programming failed");
      }
    }
    if (action.type == AsyncShellyActionType::ReloadConfig) {
      logPrint(String("[ASYNC][SHELLY-CONFIG] ") + (ok ? "applied" : "FAILED") +
               " id=" + String(action.requestId) +
               (error.length() ? " err=" + error : String("")));
    }
  } else {
    error = "invalid_shelly_action";
  }

  if (g_asyncShellyActionMutex && xSemaphoreTake(g_asyncShellyActionMutex, portMAX_DELAY) == pdTRUE) {
    g_asyncShellyActionPending.processing = false;
    g_asyncShellyActionLastType = action.type;
    g_asyncShellyActionLastRole = action.roleIndex;
    g_asyncShellyActionLastResultJson = result;
    g_asyncShellyActionLastError = error;
    if (ok) {
      ++g_asyncShellyActionApplied;
      g_asyncShellyActionLastAppliedId = action.requestId;
    } else {
      ++g_asyncShellyActionApplyFailures;
      g_asyncShellyActionLastFailedId = action.requestId;
    }
    xSemaphoreGive(g_asyncShellyActionMutex);
  }

  invalidateAsyncStateCacheAfterRelayAction();
  const char* role = action.roleIndex <= 6 ? asyncShellyRoleName(action.roleIndex) : nullptr;
  logPrint("[ASYNC][SHELLY] " + String(ok ? "applied" : "FAILED") +
           " id=" + String(action.requestId) +
           " action=" + String(asyncShellyActionTypeName(action.type)) +
           (role ? " role=" + String(role) : String("")) +
           " dryRun=" + String(action.dryRun ? "true" : "false") +
           (error.length() ? " err=" + error : String("")));
}

// -------------------- Phase 29: system settings restart handoff --------------------
static bool asyncSystemRestartDryRunRequested(AsyncWebServerRequest* request) {
  if (!request || !request->hasParam("dryRun")) return false;
  const String value = request->getParam("dryRun")->value();
  return value == "1" || value == "true" || value == "yes";
}

static void queueAsyncSystemRestart(AsyncWebServerRequest* request) {
  if (!request) return;
  ++g_asyncSystemRestartRequests;
  const bool dryRun = asyncSystemRestartDryRunRequested(request);

  if (!g_asyncSystemRestartMutex ||
      xSemaphoreTake(g_asyncSystemRestartMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncSystemRestartFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"restart_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (g_asyncSystemRestartPending.pending || g_asyncSystemRestartPending.processing) {
    xSemaphoreGive(g_asyncSystemRestartMutex);
    ++g_asyncSystemRestartFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"restart_pending\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const uint32_t requestId = ++g_asyncSystemRestartNextRequestId;
  g_asyncSystemRestartPending.pending = true;
  g_asyncSystemRestartPending.processing = false;
  g_asyncSystemRestartPending.requestId = requestId;
  g_asyncSystemRestartPending.dryRun = dryRun;
  ++g_asyncSystemRestartQueued;
  g_asyncSystemRestartLastQueuedId = requestId;
  xSemaphoreGive(g_asyncSystemRestartMutex);

  String json = "{\"ok\":true,\"queued\":true,\"requestId\":" + String(requestId);
  json += ",\"dryRun\":";
  json += dryRun ? "true" : "false";
  json += "}";
  AsyncWebServerResponse* response = request->beginResponse(202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  request->send(response);
}

static void handleAsyncSystemRestartStatus(AsyncWebServerRequest* request) {
  if (!request) return;
  uint32_t requestId = 0;
  if (request->hasParam("requestId")) {
    requestId = static_cast<uint32_t>(request->getParam("requestId")->value().toInt());
  }

  bool pending = false;
  bool processing = false;
  bool activeDryRun = false;
  uint32_t activeId = 0;
  if (g_asyncSystemRestartMutex &&
      xSemaphoreTake(g_asyncSystemRestartMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncSystemRestartPending.pending;
    processing = g_asyncSystemRestartPending.processing;
    activeDryRun = g_asyncSystemRestartPending.dryRun;
    activeId = g_asyncSystemRestartPending.requestId;
    xSemaphoreGive(g_asyncSystemRestartMutex);
  }

  String state = "unknown";
  bool dryRun = false;
  if (requestId != 0 && requestId == g_asyncSystemRestartLastAppliedId) {
    state = "applied";
    dryRun = g_asyncSystemRestartLastDryRun;
  } else if (requestId != 0 && requestId == activeId && (pending || processing)) {
    state = processing ? "processing" : "queued";
    dryRun = activeDryRun;
  }

  String json = "{\"ok\":true,\"requestId\":" + String(requestId) +
                ",\"state\":\"" + state + "\",\"dryRun\":";
  json += dryRun ? "true" : "false";
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncSystemRestartLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncSystemRestartLastAppliedId) + "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  request->send(response);
}

static void processAsyncSystemRestartApply() {
  if (!g_asyncSystemRestartMutex) return;

  AsyncSystemRestartPending action;
  bool hasAction = false;
  if (xSemaphoreTake(g_asyncSystemRestartMutex, portMAX_DELAY) == pdTRUE) {
    if (g_asyncSystemRestartPending.pending && !g_asyncSystemRestartPending.processing) {
      g_asyncSystemRestartPending.pending = false;
      g_asyncSystemRestartPending.processing = true;
      action = g_asyncSystemRestartPending;
      hasAction = true;
    }
    xSemaphoreGive(g_asyncSystemRestartMutex);
  }
  if (!hasAction) return;

  if (!action.dryRun) {
    requestSafeRestart(1500);
  } else {
    ++g_asyncSystemRestartDryRuns;
  }

  if (xSemaphoreTake(g_asyncSystemRestartMutex, portMAX_DELAY) == pdTRUE) {
    g_asyncSystemRestartPending.processing = false;
    ++g_asyncSystemRestartApplied;
    g_asyncSystemRestartLastAppliedId = action.requestId;
    g_asyncSystemRestartLastDryRun = action.dryRun;
    xSemaphoreGive(g_asyncSystemRestartMutex);
  }

  logPrint("[ASYNC][SYSTEM] restart " + String(action.dryRun ? "dry-run" : "scheduled") +
           " id=" + String(action.requestId));
}

// -------------------- Phase 30: factory reset main-loop handoff --------------------
static void queueAsyncFactoryReset(AsyncWebServerRequest* request) {
  if (!request) return;
  ++g_asyncFactoryResetRequests;

  String confirm;
  if (!asyncRequestParam(request, "confirm", confirm) || confirm != "1") {
    ++g_asyncFactoryResetFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"factory_reset_confirmation_required\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const bool dryRun = asyncSystemRestartDryRunRequested(request);
  if (!g_asyncFactoryResetMutex ||
      xSemaphoreTake(g_asyncFactoryResetMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncFactoryResetFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"factory_reset_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  if (g_asyncFactoryResetPending.pending || g_asyncFactoryResetPending.processing) {
    xSemaphoreGive(g_asyncFactoryResetMutex);
    ++g_asyncFactoryResetFailures;
    AsyncWebServerResponse* response = request->beginResponse(
      409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"factory_reset_pending\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
    return;
  }

  const uint32_t requestId = ++g_asyncFactoryResetNextRequestId;
  g_asyncFactoryResetPending.pending = true;
  g_asyncFactoryResetPending.processing = false;
  g_asyncFactoryResetPending.requestId = requestId;
  g_asyncFactoryResetPending.dryRun = dryRun;
  ++g_asyncFactoryResetQueued;
  g_asyncFactoryResetLastQueuedId = requestId;
  xSemaphoreGive(g_asyncFactoryResetMutex);

  if (dryRun) {
    String json = "{\"ok\":true,\"queued\":true,\"requestId\":" + String(requestId) +
                  ",\"dryRun\":true}";
    AsyncWebServerResponse* response = request->beginResponse(202, "application/json; charset=utf-8", json);
    addAsyncCommonHeaders(response, false);
    request->send(response);
  } else {
    String html = "<h1>Factory reset queued. Restarting...</h1>";
    AsyncWebServerResponse* response = request->beginResponse(202, "text/html; charset=utf-8", html);
    response->addHeader("X-GrowTent-Request-ID", String(requestId));
    addAsyncCommonHeaders(response, false);
    request->send(response);
  }
}

static void handleAsyncFactoryResetStatus(AsyncWebServerRequest* request) {
  if (!request) return;
  uint32_t requestId = 0;
  if (request->hasParam("requestId")) {
    requestId = static_cast<uint32_t>(request->getParam("requestId")->value().toInt());
  }

  bool pending = false;
  bool processing = false;
  bool activeDryRun = false;
  uint32_t activeId = 0;
  if (g_asyncFactoryResetMutex &&
      xSemaphoreTake(g_asyncFactoryResetMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    pending = g_asyncFactoryResetPending.pending;
    processing = g_asyncFactoryResetPending.processing;
    activeDryRun = g_asyncFactoryResetPending.dryRun;
    activeId = g_asyncFactoryResetPending.requestId;
    xSemaphoreGive(g_asyncFactoryResetMutex);
  }

  String state = "unknown";
  bool dryRun = false;
  if (requestId != 0 && requestId == g_asyncFactoryResetLastAppliedId) {
    state = "applied";
    dryRun = g_asyncFactoryResetLastDryRun;
  } else if (requestId != 0 && requestId == g_asyncFactoryResetLastFailedId) {
    state = "failed";
  } else if (requestId != 0 && requestId == activeId && (pending || processing)) {
    state = processing ? "processing" : "queued";
    dryRun = activeDryRun;
  }

  String json = "{\"ok\":true,\"requestId\":" + String(requestId) +
                ",\"state\":\"" + state + "\",\"dryRun\":";
  json += dryRun ? "true" : "false";
  json += ",\"lastQueuedId\":" + String((uint32_t)g_asyncFactoryResetLastQueuedId);
  json += ",\"lastAppliedId\":" + String((uint32_t)g_asyncFactoryResetLastAppliedId);
  json += ",\"lastFailedId\":" + String((uint32_t)g_asyncFactoryResetLastFailedId) + "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  request->send(response);
}

static void processAsyncFactoryResetApply() {
  if (!g_asyncFactoryResetMutex) return;

  AsyncFactoryResetPending action;
  bool hasAction = false;
  if (xSemaphoreTake(g_asyncFactoryResetMutex, portMAX_DELAY) == pdTRUE) {
    if (g_asyncFactoryResetPending.pending && !g_asyncFactoryResetPending.processing) {
      g_asyncFactoryResetPending.pending = false;
      g_asyncFactoryResetPending.processing = true;
      action = g_asyncFactoryResetPending;
      hasAction = true;
    }
    xSemaphoreGive(g_asyncFactoryResetMutex);
  }
  if (!hasAction) return;

  bool ok = true;
  if (action.dryRun) {
    ++g_asyncFactoryResetDryRuns;
  } else {
    preferences.begin(PREF_NS, false);
    ok = preferences.clear();
    preferences.end();
    if (ok) {
      // The GrowTent namespace contains our SSID/password. Additionally erase
      // any credentials cached by the ESP32 Wi-Fi stack immediately before the
      // scheduled restart, after the HTTP response had time to leave the device.
      g_eraseWifiBeforeRestart = true;
      requestSafeRestart(1500);
    }
  }

  if (xSemaphoreTake(g_asyncFactoryResetMutex, portMAX_DELAY) == pdTRUE) {
    g_asyncFactoryResetPending.processing = false;
    if (ok) {
      ++g_asyncFactoryResetApplied;
      g_asyncFactoryResetLastAppliedId = action.requestId;
      g_asyncFactoryResetLastDryRun = action.dryRun;
    } else {
      ++g_asyncFactoryResetApplyFailures;
      g_asyncFactoryResetLastFailedId = action.requestId;
    }
    xSemaphoreGive(g_asyncFactoryResetMutex);
  }

  logPrint("[ASYNC][FACTORY] " + String(ok ? (action.dryRun ? "dry-run" : "reset+restart scheduled") : "FAILED") +
           " id=" + String(action.requestId));
}


// -------------------- Phase 32: final STA legacy-route handoffs --------------------
static bool asyncCheckboxValue(AsyncWebServerRequest* request, const char* name) {
  String value;
  if (!asyncRequestParam(request, name, value)) return false;
  value.trim(); value.toLowerCase();
  return value == "on" || value == "1" || value == "true" || value == "yes";
}

static void handleAsyncMessageSettingsSave(AsyncWebServerRequest* request) {
  if (!request) return;
  ++g_asyncMessageSettingsRequests;
  if (!g_asyncMessageSettingsMutex ||
      xSemaphoreTake(g_asyncMessageSettingsMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncMessageSettingsFailures;
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"message_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (g_asyncMessageSettingsPending.pending || g_asyncMessageSettingsPending.processing) {
    xSemaphoreGive(g_asyncMessageSettingsMutex);
    ++g_asyncMessageSettingsFailures;
    AsyncWebServerResponse* response = request->beginResponse(409, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"message_save_pending\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }

  AsyncMessageSettingsPending pending;
  pending.pending = true;
  pending.requestId = ++g_asyncMessageSettingsNextRequestId;
  pending.dryRun = asyncSystemRestartDryRunRequested(request);
  pending.pushoverEnabled = asyncCheckboxValue(request, "webPushoverEnabled");
  pending.gotifyEnabled = asyncCheckboxValue(request, "webGotifyEnabled");
  asyncRequestParam(request, "webPushoverUserKey", pending.pushoverUser);
  asyncRequestParam(request, "webPushoverAppKey", pending.pushoverApp);
  asyncRequestParam(request, "webPushoverDevice", pending.pushoverDevice);
  asyncRequestParam(request, "webGotifyURL", pending.gotifyServer);
  asyncRequestParam(request, "webGotifyToken", pending.gotifyToken);
  pending.pushoverUser.trim(); pending.pushoverApp.trim(); pending.pushoverDevice.trim();
  pending.gotifyServer.trim(); pending.gotifyToken.trim();
  if (pending.pushoverUser.length() > 128 || pending.pushoverApp.length() > 128 ||
      pending.pushoverDevice.length() > 128 || pending.gotifyServer.length() > 256 ||
      pending.gotifyToken.length() > 128) {
    xSemaphoreGive(g_asyncMessageSettingsMutex);
    ++g_asyncMessageSettingsFailures;
    AsyncWebServerResponse* response = request->beginResponse(400, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"message_value_too_long\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  g_asyncMessageSettingsPending = pending;
  g_asyncMessageSettingsLastError = "";
  xSemaphoreGive(g_asyncMessageSettingsMutex);

  String json = "{\"ok\":true,\"queued\":true,\"requestId\":" + String(pending.requestId) +
                ",\"dryRun\":" + String(pending.dryRun ? "true" : "false") + "}";
  AsyncWebServerResponse* response = request->beginResponse(202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false); request->send(response);
}

static void handleAsyncMessageSettingsStatus(AsyncWebServerRequest* request) {
  if (!request) return;
  uint32_t id = 0; if (request->hasParam("requestId")) id = request->getParam("requestId")->value().toInt();
  String state = "unknown", error;
  if (g_asyncMessageSettingsMutex && xSemaphoreTake(g_asyncMessageSettingsMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    if (id && id == g_asyncMessageSettingsLastAppliedId) state = "applied";
    else if (id && id == g_asyncMessageSettingsLastFailedId) { state = "failed"; error = g_asyncMessageSettingsLastError; }
    else if (id && id == g_asyncMessageSettingsPending.requestId) {
      state = g_asyncMessageSettingsPending.processing ? "processing" : (g_asyncMessageSettingsPending.pending ? "queued" : "unknown");
    }
    xSemaphoreGive(g_asyncMessageSettingsMutex);
  }
  String json = "{\"ok\":true,\"requestId\":" + String(id) + ",\"state\":\"" + state + "\"";
  if (error.length()) { error.replace("\\","\\\\"); error.replace("\"","\\\""); json += ",\"error\":\"" + error + "\""; }
  json += "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false); request->send(response);
}

static void processAsyncMessageSettingsApply() {
  if (!g_asyncMessageSettingsMutex) return;
  AsyncMessageSettingsPending action; bool hasAction = false;
  if (xSemaphoreTake(g_asyncMessageSettingsMutex, portMAX_DELAY) == pdTRUE) {
    if (g_asyncMessageSettingsPending.pending && !g_asyncMessageSettingsPending.processing) {
      g_asyncMessageSettingsPending.pending = false; g_asyncMessageSettingsPending.processing = true;
      action = g_asyncMessageSettingsPending; hasAction = true;
    }
    xSemaphoreGive(g_asyncMessageSettingsMutex);
  }
  if (!hasAction) return;

  bool ok = true; String error;
  if (action.dryRun) {
    ++g_asyncMessageSettingsDryRuns;
  } else {
    if (!preferences.begin(PREF_NS, false)) {
      ok = false; error = "prefs_open_failed";
    } else {
      pushoverEnabled = action.pushoverEnabled ? "checked" : "";
      gotifyEnabled = action.gotifyEnabled ? "checked" : "";
      pushoverUserKey = action.pushoverUser;
      pushoverAppKey = action.pushoverApp;
      pushoverDevice = action.pushoverDevice;
      gotifyServer = action.gotifyServer;
      gotifyToken = action.gotifyToken;
      pushoverSent = action.pushoverEnabled;
      gotifySent = action.gotifyEnabled;
      preferences.putString(KEY_PUSHOVER, pushoverEnabled);
      preferences.putString(KEY_PUSHOVERUSER, pushoverUserKey);
      preferences.putString(KEY_PUSHOVERAPP, pushoverAppKey);
      preferences.putString(KEY_PUSHOVERDEVICE, pushoverDevice);
      preferences.putString(KEY_GOTIFY, gotifyEnabled);
      preferences.putString(KEY_GOTIFYSERVER, gotifyServer);
      preferences.putString(KEY_GOTIFYTOKEN, gotifyToken);
      preferences.end();

      if (action.pushoverEnabled) {
        if (language == "de") sendPushover("Testnachricht", "Die Pushover-Benachrichtigungen wurden erfolgreich eingerichtet.");
        else sendPushover("Test Message", "Pushover notifications have been set up successfully.");
      }
      if (action.gotifyEnabled) {
        if (language == "de") sendGotify("Testnachricht", "Die Gotify-Benachrichtigungen wurden erfolgreich eingerichtet.", 5);
        else sendGotify("Test Message", "Gotify notifications have been set up successfully.", 5);
      }
      requestSafeRestart(2500);
    }
  }

  if (xSemaphoreTake(g_asyncMessageSettingsMutex, portMAX_DELAY) == pdTRUE) {
    g_asyncMessageSettingsPending.processing = false;
    if (ok) { ++g_asyncMessageSettingsApplied; g_asyncMessageSettingsLastAppliedId = action.requestId; }
    else { ++g_asyncMessageSettingsFailures; g_asyncMessageSettingsLastFailedId = action.requestId; g_asyncMessageSettingsLastError = error; }
    xSemaphoreGive(g_asyncMessageSettingsMutex);
  }
  logPrint("[ASYNC][MESSAGE] " + String(ok ? (action.dryRun ? "dry-run" : "saved") : "FAILED") + " id=" + String(action.requestId));
}

static void handleAsyncNewGrow(AsyncWebServerRequest* request) {
  if (!request) return;
  ++g_asyncNewGrowRequests;
  if (!g_asyncNewGrowMutex || xSemaphoreTake(g_asyncNewGrowMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_asyncNewGrowFailures;
    AsyncWebServerResponse* response = request->beginResponse(503, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"new_grow_handoff_unavailable\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  if (g_asyncNewGrowPending.pending || g_asyncNewGrowPending.processing) {
    xSemaphoreGive(g_asyncNewGrowMutex); ++g_asyncNewGrowFailures;
    AsyncWebServerResponse* response = request->beginResponse(409, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"new_grow_pending\"}");
    addAsyncCommonHeaders(response, false); request->send(response); return;
  }
  const uint32_t id = ++g_asyncNewGrowNextRequestId;
  g_asyncNewGrowPending.pending = true; g_asyncNewGrowPending.processing = false;
  g_asyncNewGrowPending.requestId = id; g_asyncNewGrowPending.dryRun = asyncSystemRestartDryRunRequested(request);
  g_asyncNewGrowLastError = "";
  xSemaphoreGive(g_asyncNewGrowMutex);
  String json = "{\"ok\":true,\"queued\":true,\"requestId\":" + String(id) + ",\"dryRun\":" + String(g_asyncNewGrowPending.dryRun ? "true" : "false") + "}";
  AsyncWebServerResponse* response = request->beginResponse(202, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false); request->send(response);
}

static void handleAsyncNewGrowStatus(AsyncWebServerRequest* request) {
  if (!request) return;
  uint32_t id = 0; if (request->hasParam("requestId")) id = request->getParam("requestId")->value().toInt();
  String state = "unknown", error, date; bool mainReset=false, lightReset=false, humReset=false;
  if (g_asyncNewGrowMutex && xSemaphoreTake(g_asyncNewGrowMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    if (id && id == g_asyncNewGrowLastAppliedId) { state="applied"; date=g_asyncNewGrowLastStartDate; mainReset=g_asyncNewGrowResetMain; lightReset=g_asyncNewGrowResetLight; humReset=g_asyncNewGrowResetHumidifier; }
    else if (id && id == g_asyncNewGrowLastFailedId) { state="failed"; error=g_asyncNewGrowLastError; }
    else if (id && id == g_asyncNewGrowPending.requestId) state=g_asyncNewGrowPending.processing?"processing":(g_asyncNewGrowPending.pending?"queued":"unknown");
    xSemaphoreGive(g_asyncNewGrowMutex);
  }
  String json="{\"ok\":true,\"requestId\":"+String(id)+",\"state\":\""+state+"\"";
  if (state=="applied") {
    json += ",\"startDate\":\""+date+"\",\"reset\":{\"main\":"+String(mainReset?"true":"false")+",\"light\":"+String(lightReset?"true":"false")+",\"humidifier\":"+String(humReset?"true":"false")+"}";
  }
  if (error.length()) { error.replace("\\","\\\\"); error.replace("\"","\\\""); json += ",\"error\":\""+error+"\""; }
  json += "}";
  AsyncWebServerResponse* response=request->beginResponse(200,"application/json; charset=utf-8",json); addAsyncCommonHeaders(response,false); request->send(response);
}

static void processAsyncNewGrowApply() {
  if (!g_asyncNewGrowMutex) return;
  AsyncNewGrowPending action; bool hasAction=false;
  if (xSemaphoreTake(g_asyncNewGrowMutex,portMAX_DELAY)==pdTRUE) {
    if (g_asyncNewGrowPending.pending && !g_asyncNewGrowPending.processing) { g_asyncNewGrowPending.pending=false; g_asyncNewGrowPending.processing=true; action=g_asyncNewGrowPending; hasAction=true; }
    xSemaphoreGive(g_asyncNewGrowMutex);
  }
  if (!hasAction) return;
  bool ok=true; String error; String today; bool okMain=false,okLight=false,okHum=false;
  if (action.dryRun) {
    ++g_asyncNewGrowDryRuns;
    today = startDate;
  } else if (!preferences.begin(PREF_NS,false)) {
    ok=false; error="prefs_open_failed";
  } else {
    char dateBuf[11]={0}; time_t now=time(nullptr); struct tm tmNow{}; localtime_r(&now,&tmNow);
    snprintf(dateBuf,sizeof(dateBuf),"%04d-%02d-%02d",tmNow.tm_year+1900,tmNow.tm_mon+1,tmNow.tm_mday); today=dateBuf;
    startDate=today; startFlowering=""; startDrying=""; curPhase=1; settings.grow.currentPhase=1;
    preferences.putString(KEY_STARTDATE,startDate); preferences.putString(KEY_FLOWERDATE,startFlowering); preferences.putString(KEY_DRYINGDATE,startDrying); preferences.putInt(KEY_CURRENTPHASE,curPhase);
    auto resetOne=[&](ShellyDevice& d,const char* keyOff)->bool {
      if (d.ip.length()==0) return false;
      d.energyOffsetWh=0.0f; ShellyValues v=getShellyValues(d,0,80); const float rawWh=!isnan(v.energyWh)?v.energyWh:0.0f;
      d.energyOffsetWh=rawWh; preferences.putFloat(keyOff,d.energyOffsetWh);
      const bool resetOk=shellyResetEnergyCounters(d,0,80);
      if (resetOk) { d.energyOffsetWh=0.0f; preferences.putFloat(keyOff,0.0f); }
      return resetOk;
    };
    okMain=resetOne(settings.shelly.main,KEY_SHELLYMAINOFF); okLight=resetOne(settings.shelly.light,KEY_SHELLYLIGHTOFF); okHum=resetOne(settings.shelly.humidifier,KEY_SHELLYHUMOFF);
    preferences.end();
    invalidateAsyncStateCacheAfterRelayAction();
  }
  if (xSemaphoreTake(g_asyncNewGrowMutex,portMAX_DELAY)==pdTRUE) {
    g_asyncNewGrowPending.processing=false;
    if (ok) { ++g_asyncNewGrowApplied; g_asyncNewGrowLastAppliedId=action.requestId; g_asyncNewGrowLastStartDate=today; g_asyncNewGrowResetMain=okMain; g_asyncNewGrowResetLight=okLight; g_asyncNewGrowResetHumidifier=okHum; }
    else { ++g_asyncNewGrowFailures; g_asyncNewGrowLastFailedId=action.requestId; g_asyncNewGrowLastError=error; }
    xSemaphoreGive(g_asyncNewGrowMutex);
  }
  logPrint("[ASYNC][NEWGROW] "+String(ok?(action.dryRun?"dry-run":"applied"):"FAILED")+" id="+String(action.requestId));
}

static bool asyncDryRunParam(AsyncWebServerRequest* request) {
  return asyncSystemRestartDryRunRequested(request);
}

static void handleAsyncOtaCheckQueue(AsyncWebServerRequest* request) {
  if (!request) return;
  uint32_t id=0; String err; const bool dryRun=asyncDryRunParam(request);
  if (!otaQueueCheck(dryRun,id,err)) {
    String json="{\"ok\":false,\"error\":\""+err+"\"}";
    AsyncWebServerResponse* response=request->beginResponse(err=="ota_busy"?409:503,"application/json; charset=utf-8",json); addAsyncCommonHeaders(response,false); request->send(response); return;
  }
  String json="{\"ok\":true,\"queued\":true,\"requestId\":"+String(id)+",\"dryRun\":"+String(dryRun?"true":"false")+"}";
  AsyncWebServerResponse* response=request->beginResponse(202,"application/json; charset=utf-8",json); addAsyncCommonHeaders(response,false); request->send(response);
}

static void handleAsyncOtaCheckStatus(AsyncWebServerRequest* request) {
  if (!request) return; uint32_t id=0; if(request->hasParam("requestId")) id=request->getParam("requestId")->value().toInt();
  AsyncWebServerResponse* response=request->beginResponse(200,"application/json; charset=utf-8",otaCheckStatusJson(id)); addAsyncCommonHeaders(response,false); request->send(response);
}

static void handleAsyncOtaUpdateQueue(AsyncWebServerRequest* request) {
  if (!request) return; uint32_t id=0; String err; const bool dryRun=asyncDryRunParam(request);
  if (!otaQueueUpdate(dryRun,id,err)) {
    String json="{\"ok\":false,\"error\":\""+err+"\"}";
    AsyncWebServerResponse* response=request->beginResponse(err=="ota_busy"?409:503,"application/json; charset=utf-8",json); addAsyncCommonHeaders(response,false); request->send(response); return;
  }
  String json="{\"ok\":true,\"queued\":true,\"requestId\":"+String(id)+",\"dryRun\":"+String(dryRun?"true":"false")+"}";
  AsyncWebServerResponse* response=request->beginResponse(202,"application/json; charset=utf-8",json); addAsyncCommonHeaders(response,false); request->send(response);
}

static void handleAsyncOtaUpdateStatus(AsyncWebServerRequest* request) {
  if (!request) return; uint32_t id=0; if(request->hasParam("requestId")) id=request->getParam("requestId")->value().toInt();
  AsyncWebServerResponse* response=request->beginResponse(200,"application/json; charset=utf-8",otaUpdateStatusJson(id)); addAsyncCommonHeaders(response,false); request->send(response);
}

// -------------------- Phase 31 retained: Async captive portal / transactional Wi-Fi provisioning --------------------
static const char WIFI_PROVISION_HTML[] PROGMEM = R"GTWIFI(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>GrowTent Wi-Fi Setup</title><style>
:root{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;color-scheme:light dark}body{margin:0;background:#101418;color:#eef2f5}.wrap{max-width:680px;margin:0 auto;padding:24px}.card{background:#1b2228;border:1px solid #34414b;border-radius:16px;padding:20px;margin:14px 0;box-shadow:0 8px 28px #0005}h1{margin-top:0}label{display:block;margin:12px 0 6px}input,select,button,a.btn{box-sizing:border-box;width:100%;font:inherit;border-radius:10px;border:1px solid #51616d;padding:12px;background:#11171b;color:#fff}button,a.btn{cursor:pointer;background:#2d6f43;border-color:#3c9158;font-weight:700;text-align:center;text-decoration:none;display:block;margin-top:12px}button.secondary{background:#26323a;border-color:#51616d}.muted{color:#aebbc4;font-size:.92rem}.ok{color:#77dc92}.err{color:#ff8b8b}.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}.hidden{display:none}.langbar{display:flex;justify-content:flex-end;gap:6px;margin-bottom:12px}.langbar button{width:auto;margin:0;padding:6px 10px;background:#26323a;border-color:#51616d}.langbar button.active{background:#2d6f43;border-color:#3c9158}@media(max-width:540px){.grid{grid-template-columns:1fr}}</style></head>
<body><div class="wrap"><div class="card"><div class="langbar"><button type="button" id="langDe">DE</button><button type="button" id="langEn">EN</button></div><h1 data-i18n="title">GrowTent Wi-Fi Setup</h1><div class="muted"><span data-i18n="firmware">Firmware</span> %VERSION% · <span data-i18n="accessPoint">Access Point</span>: %APNAME%</div></div>
<div class="card" id="setupCard"><form id="wifiForm"><label for="networks" data-i18n="foundWifi">Available Wi-Fi networks</label><select id="networks"><option value="">Searching for Wi-Fi networks…</option></select><button class="secondary" type="button" id="scanBtn" data-i18n="scanAgain">Scan again</button><label for="ssid">SSID</label><input id="ssid" name="ssid" maxlength="32" required autocomplete="off"><label for="password" data-i18n="password">Wi-Fi password</label><input id="password" name="password" type="password" maxlength="64" autocomplete="new-password"><button type="submit" data-i18n="testConnection">Test connection</button></form><p class="muted" data-i18n="transactionalHint">Credentials are saved only after the connection succeeds.</p></div>
<div class="card hidden" id="statusCard"><h2 id="statusTitle" data-i18n="testing">Testing connection…</h2><p id="statusText" class="muted"></p><div id="success" class="hidden"><div class="grid"><div><strong>SSID</strong><br><span id="okSsid"></span></div><div><strong data-i18n="signal">Signal</strong><br><span id="okRssi"></span></div><div><strong data-i18n="ipAddress">IP address</strong><br><span id="okIp"></span></div><div><strong data-i18n="gateway">Gateway</strong><br><span id="okGateway"></span></div></div><a class="btn" id="openBtn" href="#" data-i18n="openGrowTent">Open GrowTent</a><button class="secondary" id="finishBtn" type="button" data-i18n="finishRestart">Finish setup & restart</button><p class="muted" data-i18n="rememberIp">You can note the IP address now; there is no need to look it up in your router.</p></div></div></div>
<script>
const q=s=>document.querySelector(s); let reqId=0,pollTimer=0;
const preferredLang='%PREFERRED_LANG%';
const T={
 de:{pageTitle:'GrowTent WLAN Setup',title:'GrowTent WLAN Setup',firmware:'Firmware',accessPoint:'Access Point',foundWifi:'Gefundene WLANs',scanAgain:'WLANs neu suchen',password:'WLAN-Passwort',testConnection:'Verbindung testen',transactionalHint:'Die Zugangsdaten werden erst gespeichert, wenn die Verbindung erfolgreich hergestellt wurde.',testing:'Verbindung wird getestet…',signal:'Signal',ipAddress:'IP-Adresse',gateway:'Gateway',openGrowTent:'GrowTent öffnen',finishRestart:'Setup abschließen & neu starten',rememberIp:'Die IP-Adresse kannst du dir jetzt merken; ein Nachschlagen im Router ist nicht nötig.',searching:'WLANs werden gesucht…',chooseWifi:'WLAN auswählen…',noneFound:'Keine WLANs gefunden – SSID manuell eingeben',openNetwork:'offen',preserveOld:'Die bisherigen Zugangsdaten bleiben erhalten, bis dieser Test erfolgreich ist.',startFailed:'Verbindung konnte nicht gestartet werden',queued:'Verbindungsauftrag wartet auf den Main Loop…',checking:'Verbindung mit {ssid} wird geprüft…',connected:'WLAN erfolgreich verbunden',savedAfterSuccess:'Die Zugangsdaten wurden erst nach erfolgreicher Verbindung gespeichert.',failed:'WLAN-Verbindung fehlgeschlagen',retry:'Die Zugangsdaten wurden nicht gespeichert. Bitte erneut versuchen.',restarting:'Neustart läuft…',err_missing_data:'SSID und Passwortdaten fehlen.',err_invalid_credentials:'SSID oder Passwort ist ungültig.',err_handoff_unavailable:'Der Verbindungsauftrag kann momentan nicht übernommen werden.',err_provisioning_busy:'Es wird bereits eine WLAN-Verbindung getestet.',err_provisioning_not_active:'Der WLAN-Einrichtungsmodus ist nicht aktiv.',err_persist_failed:'Die WLAN-Zugangsdaten konnten nicht gespeichert werden.',err_connect_timeout:'Innerhalb von 20 Sekunden konnte keine WLAN-Verbindung hergestellt werden. Die Zugangsdaten wurden nicht gespeichert.',err_wifi_not_connected:'Es besteht noch keine erfolgreiche WLAN-Verbindung.',err_request_failed:'Die Anfrage ist fehlgeschlagen.',wifiGeneric:'WLAN'},
 en:{pageTitle:'GrowTent Wi-Fi Setup',title:'GrowTent Wi-Fi Setup',firmware:'Firmware',accessPoint:'Access Point',foundWifi:'Available Wi-Fi networks',scanAgain:'Scan again',password:'Wi-Fi password',testConnection:'Test connection',transactionalHint:'Credentials are saved only after the connection succeeds.',testing:'Testing connection…',signal:'Signal',ipAddress:'IP address',gateway:'Gateway',openGrowTent:'Open GrowTent',finishRestart:'Finish setup & restart',rememberIp:'You can note the IP address now; there is no need to look it up in your router.',searching:'Searching for Wi-Fi networks…',chooseWifi:'Choose a Wi-Fi network…',noneFound:'No Wi-Fi networks found – enter the SSID manually',openNetwork:'open',preserveOld:'Existing credentials remain unchanged until this test succeeds.',startFailed:'Connection test could not be started',queued:'Connection request is waiting for the main loop…',checking:'Testing connection to {ssid}…',connected:'Wi-Fi connected successfully',savedAfterSuccess:'Credentials were saved only after the connection succeeded.',failed:'Wi-Fi connection failed',retry:'Credentials were not saved. Please try again.',restarting:'Restarting…',err_missing_data:'SSID or password data is missing.',err_invalid_credentials:'The SSID or password is invalid.',err_handoff_unavailable:'The connection request cannot be handed off right now.',err_provisioning_busy:'A Wi-Fi connection is already being tested.',err_provisioning_not_active:'Wi-Fi setup mode is not active.',err_persist_failed:'The Wi-Fi credentials could not be saved.',err_connect_timeout:'No Wi-Fi connection was established within 20 seconds. The credentials were not saved.',err_wifi_not_connected:'There is no successful Wi-Fi connection yet.',err_request_failed:'The request failed.',wifiGeneric:'Wi-Fi'}
};
let lang=(preferredLang==='de'||preferredLang==='en')?preferredLang:(((navigator.language||'en').toLowerCase().startsWith('de'))?'de':'en');
const tr=(k)=>T[lang][k]||T.en[k]||k;
function errorText(code){const k='err_'+String(code||'request_failed');return T[lang][k]||T.en[k]||tr('err_request_failed')}
function applyLang(next){lang=(next==='de')?'de':'en';document.documentElement.lang=lang;document.title=tr('pageTitle');document.querySelectorAll('[data-i18n]').forEach(el=>{el.textContent=tr(el.dataset.i18n)});q('#langDe').classList.toggle('active',lang==='de');q('#langEn').classList.toggle('active',lang==='en');if(q('#finishBtn').disabled)q('#finishBtn').textContent=tr('restarting');scan(false)}
q('#langDe').onclick=()=>applyLang('de');q('#langEn').onclick=()=>applyLang('en');
async function scan(refresh=false){try{const r=await fetch('/api/wifi/scan'+(refresh?'?refresh=1':''),{cache:'no-store'});const j=await r.json();const sel=q('#networks');sel.innerHTML='';if(j.scanning){sel.add(new Option(tr('searching'),''));return;}const list=j.networks||[];sel.add(new Option(list.length?tr('chooseWifi'):tr('noneFound'),''));for(const n of list){const label=n.ssid+' ('+n.rssi+' dBm'+(n.open?', '+tr('openNetwork'):'')+')';sel.add(new Option(label,n.ssid));}}catch(e){}}
q('#networks').addEventListener('change',e=>{if(e.target.value)q('#ssid').value=e.target.value});q('#scanBtn').onclick=()=>scan(true);
q('#wifiForm').addEventListener('submit',async e=>{e.preventDefault();q('#statusCard').classList.remove('hidden');q('#success').classList.add('hidden');q('#statusTitle').textContent=tr('testing');q('#statusTitle').className='';q('#statusText').textContent=tr('preserveOld');const b=new URLSearchParams();b.set('ssid',q('#ssid').value);b.set('password',q('#password').value);try{const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});const j=await r.json();if(!r.ok)throw new Error(j.err||'request_failed');reqId=j.requestId;clearInterval(pollTimer);pollTimer=setInterval(status,700);status();}catch(err){q('#statusTitle').textContent=tr('startFailed');q('#statusTitle').className='err';q('#statusText').textContent=errorText(err.message);}});
async function status(){if(!reqId)return;try{const r=await fetch('/api/wifi/status?requestId='+reqId,{cache:'no-store'});const j=await r.json();if(j.state==='queued'||j.state==='connecting'){q('#statusText').textContent=j.state==='queued'?tr('queued'):tr('checking').replace('{ssid}',j.ssid||tr('wifiGeneric'));return;}clearInterval(pollTimer);if(j.state==='connected'){q('#statusTitle').textContent=tr('connected');q('#statusTitle').className='ok';q('#statusText').textContent=tr('savedAfterSuccess');q('#okSsid').textContent=j.ssid||'';q('#okRssi').textContent=(j.rssi||0)+' dBm';q('#okIp').textContent=j.ip||'';q('#okGateway').textContent=j.gateway||'';q('#openBtn').href=j.dashboardUrl||'#';q('#success').classList.remove('hidden');}else{q('#statusTitle').textContent=tr('failed');q('#statusTitle').className='err';q('#statusText').textContent=errorText(j.error)||tr('retry');}}catch(e){}}
q('#finishBtn').onclick=async()=>{q('#finishBtn').disabled=true;try{const r=await fetch('/api/wifi/finish',{method:'POST'});const j=await r.json();if(!r.ok)throw new Error(j.err||'request_failed');q('#finishBtn').textContent=tr('restarting');}catch(e){q('#finishBtn').disabled=false;q('#statusText').textContent=errorText(e.message);}};
applyLang(lang);scan();setInterval(()=>scan(false),3000);
</script></body></html>
)GTWIFI";

static const char* wifiProvisionStateName(WifiProvisionState state) {
  switch (state) {
    case WifiProvisionState::Queued: return "queued";
    case WifiProvisionState::Connecting: return "connecting";
    case WifiProvisionState::Connected: return "connected";
    case WifiProvisionState::Failed: return "failed";
    default: return "idle";
  }
}

static String currentProvisionApName() {
  String chipId = String((uint32_t)ESP.getEfuseMac(), HEX);
  return String(KEY_APSSID) + "_" + chipId;
}

static void sendProvisioningPage(AsyncWebServerRequest* request) {
  if (!request) return;
  if (!g_wifiProvisioningActive) {
    AsyncWebServerResponse* response = request->beginResponse(410, "text/plain; charset=utf-8", "Gone");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
    return;
  }
  String html = FPSTR(WIFI_PROVISION_HTML);
  html.replace("%VERSION%", String(FW_VERSION));
  html.replace("%APNAME%", currentProvisionApName());
  String preferredLang = "";
  Preferences langPrefs;
  if (langPrefs.begin(PREF_NS, true)) {
    const String storedLang = langPrefs.getString(KEY_LANG, "");
    if (storedLang == "de" || storedLang == "en") preferredLang = storedLang;
    langPrefs.end();
  }
  html.replace("%PREFERRED_LANG%", preferredLang);
  AsyncWebServerResponse* response = request->beginResponse(200, "text/html; charset=utf-8", html);
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("X-GrowTent-Provisioning", "async-phase32");
  request->send(response);
}

static void redirectProvisioningRoot(AsyncWebServerRequest* request) {
  if (!request) return;
  if (!g_wifiProvisioningActive) {
    AsyncWebServerResponse* response = request->beginResponse(410, "text/plain; charset=utf-8", "Gone");
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
    return;
  }
  String target = String("http://") + WiFi.softAPIP().toString() + "/";
  AsyncWebServerResponse* response = request->beginResponse(302, "text/plain; charset=utf-8", "Redirecting");
  response->addHeader("Location", target);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

static void handleProvisioningSave(AsyncWebServerRequest* request) {
  if (!request) return;
  ++g_wifiProvisionRequests;
  if (!g_wifiProvisioningActive) {
    ++g_wifiProvisionFailures;
    request->send(410, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"provisioning_not_active\"}");
    return;
  }
  if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
    ++g_wifiProvisionFailures;
    request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"missing_data\"}");
    return;
  }
  String candidateSsid = request->getParam("ssid", true)->value();
  String candidatePassword = request->getParam("password", true)->value();
  candidateSsid.trim();
  if (candidateSsid.length() == 0 || candidateSsid.length() > 32 || candidatePassword.length() > 64) {
    ++g_wifiProvisionFailures;
    request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"invalid_credentials\"}");
    return;
  }
  if (!g_wifiProvisioningMutex || xSemaphoreTake(g_wifiProvisioningMutex, pdMS_TO_TICKS(25)) != pdTRUE) {
    ++g_wifiProvisionFailures;
    request->send(503, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"handoff_unavailable\"}");
    return;
  }
  if (g_wifiProvisionPending.pending || g_wifiProvisionPending.processing) {
    xSemaphoreGive(g_wifiProvisioningMutex);
    ++g_wifiProvisionFailures;
    request->send(409, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"provisioning_busy\"}");
    return;
  }
  const uint32_t requestId = ++g_wifiProvisionNextRequestId;
  g_wifiProvisionPending.pending = true;
  g_wifiProvisionPending.processing = false;
  g_wifiProvisionPending.requestId = requestId;
  g_wifiProvisionPending.ssid = candidateSsid;
  g_wifiProvisionPending.password = candidatePassword;
  g_wifiProvisionPending.startedMs = 0;
  g_wifiProvisionState = WifiProvisionState::Queued;
  g_wifiProvisionLastRequestId = requestId;
  g_wifiProvisionSsid = candidateSsid;
  g_wifiProvisionError = "";
  g_wifiProvisionIp = "";
  g_wifiProvisionGateway = "";
  g_wifiProvisionRssi = 0;
  ++g_wifiProvisionQueued;
  xSemaphoreGive(g_wifiProvisioningMutex);
  request->send(202, "application/json; charset=utf-8",
                String("{\"ok\":true,\"queued\":true,\"requestId\":") + String(requestId) + "}");
}

static void handleProvisioningStatus(AsyncWebServerRequest* request) {
  if (!request) return;
  uint32_t requestId = 0;
  if (request->hasParam("requestId")) requestId = (uint32_t)request->getParam("requestId")->value().toInt();
  WifiProvisionState state = WifiProvisionState::Idle;
  String ssid, error, ip, gateway;
  int rssi = 0;
  uint32_t lastId = 0;
  if (g_wifiProvisioningMutex && xSemaphoreTake(g_wifiProvisioningMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    state = g_wifiProvisionState;
    ssid = g_wifiProvisionSsid;
    error = g_wifiProvisionError;
    ip = g_wifiProvisionIp;
    gateway = g_wifiProvisionGateway;
    rssi = g_wifiProvisionRssi;
    lastId = g_wifiProvisionLastRequestId;
    xSemaphoreGive(g_wifiProvisioningMutex);
  }
  if (requestId != 0 && lastId != 0 && requestId != lastId) state = WifiProvisionState::Idle;
  String json = "{\"ok\":true,\"requestId\":" + String(requestId) + ",\"state\":\"" + wifiProvisionStateName(state) + "\"";
  json += ",\"ssid\":\"" + asyncJsonEscape(ssid) + "\"";
  json += ",\"error\":\"" + asyncJsonEscape(error) + "\"";
  json += ",\"ip\":\"" + asyncJsonEscape(ip) + "\"";
  json += ",\"gateway\":\"" + asyncJsonEscape(gateway) + "\"";
  json += ",\"rssi\":" + String(rssi);
  if (ip.length()) json += ",\"dashboardUrl\":\"http://" + asyncJsonEscape(ip) + ":8080/\"";
  json += "}";
  request->send(200, "application/json; charset=utf-8", json);
}

static void handleProvisioningScan(AsyncWebServerRequest* request) {
  if (!request) return;
  if (request->hasParam("refresh") && request->getParam("refresh")->value() == "1") g_wifiScanRequested = true;
  String networks = "[]";
  if (g_wifiProvisioningMutex && xSemaphoreTake(g_wifiProvisioningMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
    networks = g_wifiScanJson;
    xSemaphoreGive(g_wifiProvisioningMutex);
  }
  String json = String("{\"ok\":true,\"scanning\":") + (g_wifiScanInProgress ? "true" : "false") + ",\"networks\":" + networks + "}";
  request->send(200, "application/json; charset=utf-8", json);
}

static void handleProvisioningFinish(AsyncWebServerRequest* request) {
  if (!request) return;
  if (!g_wifiProvisioningActive || WiFi.status() != WL_CONNECTED) {
    request->send(409, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"wifi_not_connected\"}");
    return;
  }
  g_wifiProvisionFinishRequested = true;
  request->send(202, "application/json; charset=utf-8", "{\"ok\":true,\"restartQueued\":true}");
}

static void handleAsyncProvisioningInfo(AsyncWebServerRequest* request) {
  if (!request) return;
  String json = "{\"ok\":true,\"implementation\":\"async-captive-portal\",\"port\":80";
  json += ",\"active\":" + String(g_wifiProvisioningActive ? "true" : "false");
  json += ",\"transactional\":true,\"saveAfterConnect\":true,\"showsIpAfterConnect\":true";
  json += ",\"languages\":[\"de\",\"en\"],\"browserLanguageFallback\":true,\"manualLanguageSwitch\":true,\"localizedErrorCodes\":true";
  json += ",\"legacyWebServerUsedInApMode\":false,\"factoryResetErasesWifi\":true";
  json += "}";
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json; charset=utf-8", json);
  addAsyncCommonHeaders(response, false);
  request->send(response);
}

static void configureProvisioningServerRoutes() {
  provisioningServer.on("/", AsyncWebRequestMethod::HTTP_GET, sendProvisioningPage);
  provisioningServer.on("/save", AsyncWebRequestMethod::HTTP_POST, handleProvisioningSave);
  provisioningServer.on("/api/wifi/status", AsyncWebRequestMethod::HTTP_GET, handleProvisioningStatus);
  provisioningServer.on("/api/wifi/scan", AsyncWebRequestMethod::HTTP_GET, handleProvisioningScan);
  provisioningServer.on("/api/wifi/finish", AsyncWebRequestMethod::HTTP_POST, handleProvisioningFinish);
  provisioningServer.on("/generate_204", AsyncWebRequestMethod::HTTP_GET, redirectProvisioningRoot);
  provisioningServer.on("/gen_204", AsyncWebRequestMethod::HTTP_GET, redirectProvisioningRoot);
  provisioningServer.on("/hotspot-detect.html", AsyncWebRequestMethod::HTTP_GET, redirectProvisioningRoot);
  provisioningServer.on("/connecttest.txt", AsyncWebRequestMethod::HTTP_GET, redirectProvisioningRoot);
  provisioningServer.on("/ncsi.txt", AsyncWebRequestMethod::HTTP_GET, redirectProvisioningRoot);
  provisioningServer.onNotFound(redirectProvisioningRoot);
}

static void startProvisioningPortalServer() {
  if (!g_wifiProvisioningActive || g_wifiProvisioningServerStarted) return;
  provisioningDns.start(53, "*", WiFi.softAPIP());
  provisioningServer.begin();
  g_wifiProvisioningServerStarted = true;
  g_wifiScanRequested = true;
  logPrint("[WIFI][PROVISION] Async captive portal started on " + WiFi.softAPIP().toString() + ":80");
}

// v0.4.2: If boot fell back to AP+STA only because the stored STA connection
// did not come up within the boot timeout, tear the captive portal back down as
// soon as that same stored STA connection recovers. Manual/no-credential
// provisioning deliberately keeps the AP alive so the success page remains
// reachable until the user finishes setup.
static void stopFallbackProvisioningPortalIfStaRecovered() {
  if (!g_wifiProvisioningFallbackFromStoredCredentials ||
      !g_wifiProvisioningActive ||
      g_wifiProvisioningBusy ||
      WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (g_wifiScanInProgress) {
    WiFi.scanDelete();
    g_wifiScanInProgress = false;
  }
  g_wifiScanRequested = false;
  g_wifiLastScanMs = 0;

  provisioningDns.stop();
  if (g_wifiProvisioningServerStarted) {
    provisioningServer.end();
  }

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  g_wifiProvisioningServerStarted = false;
  g_wifiProvisioningActive = false;
  g_wifiProvisioningFallbackFromStoredCredentials = false;
  ++g_wifiProvisioningFallbackAutoStops;

  wifiReady = true;
  espMode = false;
  logPrint("[WIFI][PROVISION] Stored STA recovered; fallback AP/DNS/TCP80 stopped. IP=" +
           WiFi.localIP().toString());
}

static void processProvisioningScan() {
  if (!g_wifiProvisioningActive || g_wifiProvisioningBusy) return;
  const int scanState = WiFi.scanComplete();
  if (g_wifiScanInProgress) {
    if (scanState >= 0) {
      String arr = "[";
      for (int i = 0; i < scanState; ++i) {
        if (i) arr += ',';
        arr += "{\"ssid\":\"" + asyncJsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i));
        arr += ",\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
      }
      arr += "]";
      if (g_wifiProvisioningMutex && xSemaphoreTake(g_wifiProvisioningMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
        g_wifiScanJson = arr;
        xSemaphoreGive(g_wifiProvisioningMutex);
      }
      WiFi.scanDelete();
      g_wifiScanInProgress = false;
      g_wifiLastScanMs = millis();
    } else if (scanState == WIFI_SCAN_FAILED) {
      WiFi.scanDelete();
      g_wifiScanInProgress = false;
      g_wifiLastScanMs = millis();
    }
    return;
  }
  if (g_wifiScanRequested || g_wifiLastScanMs == 0 || (uint32_t)(millis() - g_wifiLastScanMs) >= 30000UL) {
    g_wifiScanRequested = false;
    if (WiFi.scanNetworks(true, true) == WIFI_SCAN_RUNNING) g_wifiScanInProgress = true;
  }
}

static void processWifiProvisioning() {
  if (!g_wifiProvisioningActive) return;
  processProvisioningScan();

  WifiProvisionPending action;
  bool startAction = false;
  if (g_wifiProvisioningMutex && xSemaphoreTake(g_wifiProvisioningMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (g_wifiProvisionPending.pending && !g_wifiProvisionPending.processing) {
      g_wifiProvisionPending.pending = false;
      g_wifiProvisionPending.processing = true;
      g_wifiProvisionPending.startedMs = millis();
      g_wifiProvisionState = WifiProvisionState::Connecting;
      action = g_wifiProvisionPending;
      startAction = true;
    }
    xSemaphoreGive(g_wifiProvisioningMutex);
  }

  if (startAction) {
    // An explicit portal submission turns the fallback portal into an
    // interactive provisioning session. Keep TCP/80 alive through the result
    // page instead of auto-stopping it when the candidate STA connects.
    g_wifiProvisioningFallbackFromStoredCredentials = false;
    g_wifiProvisioningBusy = true;
    if (g_wifiScanInProgress) {
      WiFi.scanDelete();
      g_wifiScanInProgress = false;
    }
    WiFi.mode(WIFI_AP_STA);
    WiFi.persistent(false);
    WiFi.disconnect(false, false);
    delay(50);
    WiFi.begin(action.ssid.c_str(), action.password.c_str());
    logPrint("[WIFI][PROVISION] Testing SSID '" + action.ssid + "' (password hidden)");
  }

  if (!g_wifiProvisioningBusy) {
    if (g_wifiProvisionFinishRequested) {
      g_wifiProvisionFinishRequested = false;
      requestSafeRestart(800);
    }
    return;
  }

  WifiProvisionPending current;
  bool haveCurrent = false;
  if (g_wifiProvisioningMutex && xSemaphoreTake(g_wifiProvisioningMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (g_wifiProvisionPending.processing) {
      current = g_wifiProvisionPending;
      haveCurrent = true;
    }
    xSemaphoreGive(g_wifiProvisioningMutex);
  }
  if (!haveCurrent) { g_wifiProvisioningBusy = false; return; }

  if (WiFi.status() == WL_CONNECTED) {
    const String newIp = WiFi.localIP().toString();
    const String newGateway = WiFi.gatewayIP().toString();
    const int newRssi = WiFi.RSSI();
    bool persisted = preferences.begin(PREF_NS, false);
    if (persisted) {
      preferences.putString(KEY_SSID, current.ssid);
      preferences.putString(KEY_PASS, current.password);
      const String verifySsid = preferences.getString(KEY_SSID, "");
      const String verifyPass = preferences.getString(KEY_PASS, "");
      persisted = (verifySsid == current.ssid && verifyPass == current.password);
      preferences.end();
    }
    if (persisted) {
      ssidName = current.ssid;
      ssidPassword = current.password;
      wifiReady = true;
      espMode = false;
      txPower = (WiFi.getTxPower() / 4);
      configTzTime(tzInfo.c_str(), ntpServer.c_str());
      ntpSyncPending = true;
      ntpStartMs = millis();
      if (g_wifiProvisioningMutex && xSemaphoreTake(g_wifiProvisioningMutex, portMAX_DELAY) == pdTRUE) {
        g_wifiProvisionPending.processing = false;
        g_wifiProvisionState = WifiProvisionState::Connected;
        g_wifiProvisionSsid = current.ssid;
        g_wifiProvisionIp = newIp;
        g_wifiProvisionGateway = newGateway;
        g_wifiProvisionRssi = newRssi;
        g_wifiProvisionError = "";
        xSemaphoreGive(g_wifiProvisioningMutex);
      }
      ++g_wifiProvisionConnected;
      logPrint("[WIFI][PROVISION] Connected; credentials committed. IP=" + newIp + " RSSI=" + String(newRssi));
    } else {
      if (g_wifiProvisioningMutex && xSemaphoreTake(g_wifiProvisioningMutex, portMAX_DELAY) == pdTRUE) {
        g_wifiProvisionPending.processing = false;
        g_wifiProvisionState = WifiProvisionState::Failed;
        g_wifiProvisionError = "persist_failed";
        xSemaphoreGive(g_wifiProvisioningMutex);
      }
      ++g_wifiProvisionFailures;
    }
    g_wifiProvisioningBusy = false;
    return;
  }

  if ((uint32_t)(millis() - current.startedMs) >= 20000UL) {
    WiFi.disconnect(false, false);
    if (ssidName.length()) WiFi.begin(ssidName.c_str(), ssidPassword.c_str());
    if (g_wifiProvisioningMutex && xSemaphoreTake(g_wifiProvisioningMutex, portMAX_DELAY) == pdTRUE) {
      g_wifiProvisionPending.processing = false;
      g_wifiProvisionState = WifiProvisionState::Failed;
      g_wifiProvisionError = "connect_timeout";
      xSemaphoreGive(g_wifiProvisioningMutex);
    }
    ++g_wifiProvisionFailures;
    g_wifiProvisioningBusy = false;
    g_wifiScanRequested = true;
    logPrint("[WIFI][PROVISION] Candidate connection failed; previous stored credentials preserved");
  }
}


static void handleAsyncHealth(AsyncWebServerRequest* request) {
  if (!request) return;

  if (!g_asyncHealthCacheMutex ||
      xSemaphoreTake(g_asyncHealthCacheMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    AsyncWebServerResponse* busyResponse = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"health_snapshot_busy\"}");
    addAsyncCommonHeaders(busyResponse, false);
    request->send(busyResponse);
    return;
  }

  String& json = g_asyncHealthCache;
  const bool rebuild = (asyncHealthActiveReaders() == 0 || json.length() == 0);
  if (rebuild) {
    json = "";
    json.reserve(8192);
  json += "{\"ok\":true";
  json += ",\"phase\":32";
  json += ",\"version\":\"" + String(FW_VERSION) + "\"";
  json += ",\"uptimeMs\":" + String(millis());
  json += ",\"wifiStatus\":" + String((int)WiFi.status());
  json += ",\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  json += ",\"freeHeap\":" + String(ESP.getFreeHeap());
  json += ",\"maxAlloc\":" + String(ESP.getMaxAllocHeap());
  json += ",\"stateCacheTtlMs\":" + String(ASYNC_STATE_CACHE_TTL_MS);
  json += ",\"stateCacheHits\":" + String((uint32_t)g_asyncStateCacheHits);
  json += ",\"stateCacheMisses\":" + String((uint32_t)g_asyncStateCacheMisses);
  json += ",\"stateCacheFallbacks\":" + String((uint32_t)g_asyncStateCacheFallbacks);
  json += ",\"stateResponseBufferFailures\":" + String((uint32_t)g_asyncStateResponseBufferFailures);
  json += ",\"stateActiveReaders\":" + String(asyncStateActiveReaders());
  json += ",\"stateDeferredRefreshes\":" + String((uint32_t)g_asyncStateDeferredRefreshes);
  json += ",\"stateGeneration\":" + String((uint32_t)g_asyncStateGeneration);
  json += ",\"stateCacheGeneration\":" + String((uint32_t)g_asyncStateCacheGeneration);
  json += ",\"rootRequests\":" + String((uint32_t)g_asyncRootRequests);
  json += ",\"logClearRequests\":" + String((uint32_t)g_asyncLogClearRequests);
  json += ",\"systemSettingsChangedLogs\":" + String((uint32_t)g_asyncSystemSettingsChangedLogs);
  json += ",\"themeWriteRequests\":" + String((uint32_t)g_asyncThemeWriteRequests);
  json += ",\"themeWriteFailures\":" + String((uint32_t)g_asyncThemeWriteFailures);
  json += ",\"languageWriteRequests\":" + String((uint32_t)g_asyncLanguageWriteRequests);
  json += ",\"languageWriteFailures\":" + String((uint32_t)g_asyncLanguageWriteFailures);
  json += ",\"unitWriteRequests\":" + String((uint32_t)g_asyncUnitWriteRequests);
  json += ",\"unitWriteFailures\":" + String((uint32_t)g_asyncUnitWriteFailures);
  json += ",\"timeFormatWriteRequests\":" + String((uint32_t)g_asyncTimeFormatWriteRequests);
  json += ",\"timeFormatWriteFailures\":" + String((uint32_t)g_asyncTimeFormatWriteFailures);
  json += ",\"boxNameWriteRequests\":" + String((uint32_t)g_asyncBoxNameWriteRequests);
  json += ",\"boxNameWriteFailures\":" + String((uint32_t)g_asyncBoxNameWriteFailures);
  json += ",\"debugWriteRequests\":" + String((uint32_t)g_asyncDebugWriteRequests);
  json += ",\"debugWriteFailures\":" + String((uint32_t)g_asyncDebugWriteFailures);
  json += ",\"ds18NameWriteRequests\":" + String((uint32_t)g_asyncDs18NameWriteRequests);
  json += ",\"ds18NameWriteFailures\":" + String((uint32_t)g_asyncDs18NameWriteFailures);
  json += ",\"powerPriceWriteRequests\":" + String((uint32_t)g_asyncPowerPriceWriteRequests);
  json += ",\"powerPriceWriteFailures\":" + String((uint32_t)g_asyncPowerPriceWriteFailures);
  json += ",\"relayNameWriteRequests\":" + String((uint32_t)g_asyncRelayNameWriteRequests);
  json += ",\"relayNameWriteFailures\":" + String((uint32_t)g_asyncRelayNameWriteFailures);
  json += ",\"ntpServerWriteRequests\":" + String((uint32_t)g_asyncNtpServerWriteRequests);
  json += ",\"ntpServerWriteFailures\":" + String((uint32_t)g_asyncNtpServerWriteFailures);
  json += ",\"timeZoneWriteRequests\":" + String((uint32_t)g_asyncTimeZoneWriteRequests);
  json += ",\"timeZoneWriteFailures\":" + String((uint32_t)g_asyncTimeZoneWriteFailures);
  json += ",\"ds18EnableWriteRequests\":" + String((uint32_t)g_asyncDs18EnableWriteRequests);
  json += ",\"ds18EnableWriteFailures\":" + String((uint32_t)g_asyncDs18EnableWriteFailures);
  json += ",\"relayCountWriteRequests\":" + String((uint32_t)g_asyncRelayCountWriteRequests);
  json += ",\"relayCountWriteFailures\":" + String((uint32_t)g_asyncRelayCountWriteFailures);
  json += ",\"shellyDeviceWriteRequests\":" + String((uint32_t)g_asyncShellyDeviceWriteRequests);
  json += ",\"shellyDeviceWriteFailures\":" + String((uint32_t)g_asyncShellyDeviceWriteFailures);
  json += ",\"shellyAuthWriteRequests\":" + String((uint32_t)g_asyncShellyAuthWriteRequests);
  json += ",\"shellyAuthWriteFailures\":" + String((uint32_t)g_asyncShellyAuthWriteFailures);
  json += ",\"growTargetsWriteRequests\":" + String((uint32_t)g_asyncGrowTargetsWriteRequests);
  json += ",\"growTargetsWriteFailures\":" + String((uint32_t)g_asyncGrowTargetsWriteFailures);
  json += ",\"growTargetsQueued\":" + String((uint32_t)g_asyncGrowTargetsQueued);
  json += ",\"growTargetsApplied\":" + String((uint32_t)g_asyncGrowTargetsApplied);
  json += ",\"growTargetsLastQueuedId\":" + String((uint32_t)g_asyncGrowTargetsLastQueuedId);
  json += ",\"growTargetsLastAppliedId\":" + String((uint32_t)g_asyncGrowTargetsLastAppliedId);
  json += ",\"heatingTuningWriteRequests\":" + String((uint32_t)g_asyncHeatingTuningWriteRequests);
  json += ",\"heatingTuningWriteFailures\":" + String((uint32_t)g_asyncHeatingTuningWriteFailures);
  json += ",\"heatingTuningQueued\":" + String((uint32_t)g_asyncHeatingTuningQueued);
  json += ",\"heatingTuningApplied\":" + String((uint32_t)g_asyncHeatingTuningApplied);
  json += ",\"heatingTuningLastQueuedId\":" + String((uint32_t)g_asyncHeatingTuningLastQueuedId);
  json += ",\"heatingTuningLastAppliedId\":" + String((uint32_t)g_asyncHeatingTuningLastAppliedId);
  json += ",\"heatingOutputWriteRequests\":" + String((uint32_t)g_asyncHeatingOutputWriteRequests);
  json += ",\"heatingOutputWriteFailures\":" + String((uint32_t)g_asyncHeatingOutputWriteFailures);
  json += ",\"heatingOutputQueued\":" + String((uint32_t)g_asyncHeatingOutputQueued);
  json += ",\"heatingOutputApplied\":" + String((uint32_t)g_asyncHeatingOutputApplied);
  json += ",\"heatingOutputApplyFailures\":" + String((uint32_t)g_asyncHeatingOutputApplyFailures);
  json += ",\"heatingOutputRollbackFailures\":" + String((uint32_t)g_asyncHeatingOutputRollbackFailures);
  json += ",\"heatingOutputLastQueuedId\":" + String((uint32_t)g_asyncHeatingOutputLastQueuedId);
  json += ",\"heatingOutputLastAppliedId\":" + String((uint32_t)g_asyncHeatingOutputLastAppliedId);
  json += ",\"heatingOutputLastFailedId\":" + String((uint32_t)g_asyncHeatingOutputLastFailedId);
  json += ",\"lightScheduleWriteRequests\":" + String((uint32_t)g_asyncLightScheduleWriteRequests);
  json += ",\"lightScheduleWriteFailures\":" + String((uint32_t)g_asyncLightScheduleWriteFailures);
  json += ",\"lightScheduleQueued\":" + String((uint32_t)g_asyncLightScheduleQueued);
  json += ",\"lightScheduleApplied\":" + String((uint32_t)g_asyncLightScheduleApplied);
  json += ",\"lightScheduleApplyFailures\":" + String((uint32_t)g_asyncLightScheduleApplyFailures);
  json += ",\"lightScheduleRollbackFailures\":" + String((uint32_t)g_asyncLightScheduleRollbackFailures);
  json += ",\"lightScheduleLastQueuedId\":" + String((uint32_t)g_asyncLightScheduleLastQueuedId);
  json += ",\"lightScheduleLastAppliedId\":" + String((uint32_t)g_asyncLightScheduleLastAppliedId);
  json += ",\"lightScheduleLastFailedId\":" + String((uint32_t)g_asyncLightScheduleLastFailedId);
  json += ",\"relayScheduleWriteRequests\":" + String((uint32_t)g_asyncRelayScheduleWriteRequests);
  json += ",\"relayScheduleWriteFailures\":" + String((uint32_t)g_asyncRelayScheduleWriteFailures);
  json += ",\"relayScheduleQueued\":" + String((uint32_t)g_asyncRelayScheduleQueued);
  json += ",\"relayScheduleApplied\":" + String((uint32_t)g_asyncRelayScheduleApplied);
  json += ",\"relayScheduleApplyFailures\":" + String((uint32_t)g_asyncRelayScheduleApplyFailures);
  json += ",\"relayScheduleRollbackFailures\":" + String((uint32_t)g_asyncRelayScheduleRollbackFailures);
  json += ",\"relayScheduleLastQueuedId\":" + String((uint32_t)g_asyncRelayScheduleLastQueuedId);
  json += ",\"relayScheduleLastAppliedId\":" + String((uint32_t)g_asyncRelayScheduleLastAppliedId);
  json += ",\"relayScheduleLastFailedId\":" + String((uint32_t)g_asyncRelayScheduleLastFailedId);
  json += ",\"relayActionRequests\":" + String((uint32_t)g_asyncRelayActionRequests);
  json += ",\"relayActionFailures\":" + String((uint32_t)g_asyncRelayActionFailures);
  json += ",\"relayActionQueued\":" + String((uint32_t)g_asyncRelayActionQueued);
  json += ",\"relayActionApplied\":" + String((uint32_t)g_asyncRelayActionApplied);
  json += ",\"relayActionApplyFailures\":" + String((uint32_t)g_asyncRelayActionApplyFailures);
  json += ",\"relayActionTransitions\":" + String((uint32_t)g_asyncRelayActionTransitions);
  json += ",\"relayActionNoops\":" + String((uint32_t)g_asyncRelayActionNoops);
  json += ",\"relayActionLastQueuedId\":" + String((uint32_t)g_asyncRelayActionLastQueuedId);
  json += ",\"relayActionLastAppliedId\":" + String((uint32_t)g_asyncRelayActionLastAppliedId);
  json += ",\"relayActionLastFailedId\":" + String((uint32_t)g_asyncRelayActionLastFailedId);
  json += ",\"wateringRequests\":" + String((uint32_t)g_asyncWateringRequests);
  json += ",\"wateringFailures\":" + String((uint32_t)g_asyncWateringFailures);
  json += ",\"wateringQueued\":" + String((uint32_t)g_asyncWateringQueued);
  json += ",\"wateringApplied\":" + String((uint32_t)g_asyncWateringApplied);
  json += ",\"wateringApplyFailures\":" + String((uint32_t)g_asyncWateringApplyFailures);
  json += ",\"wateringStarts\":" + String((uint32_t)g_asyncWateringStarts);
  json += ",\"wateringStops\":" + String((uint32_t)g_asyncWateringStops);
  json += ",\"wateringNoops\":" + String((uint32_t)g_asyncWateringNoops);
  json += ",\"wateringPumpPulses\":" + String((uint32_t)g_asyncWateringPumpPulses);
  json += ",\"wateringTankPings\":" + String((uint32_t)g_asyncWateringTankPings);
  json += ",\"wateringLastQueuedId\":" + String((uint32_t)g_asyncWateringLastQueuedId);
  json += ",\"wateringLastAppliedId\":" + String((uint32_t)g_asyncWateringLastAppliedId);
  json += ",\"wateringLastFailedId\":" + String((uint32_t)g_asyncWateringLastFailedId);
  json += ",\"floodEventRequests\":" + String((uint32_t)g_asyncFloodEventRequests);
  json += ",\"floodEventFailures\":" + String((uint32_t)g_asyncFloodEventFailures);
  json += ",\"floodEventQueued\":" + String((uint32_t)g_asyncFloodEventQueued);
  json += ",\"floodEventApplied\":" + String((uint32_t)g_asyncFloodEventApplied);
  json += ",\"floodEventApplyFailures\":" + String((uint32_t)g_asyncFloodEventApplyFailures);
  json += ",\"floodStops\":" + String((uint32_t)g_asyncFloodStops);
  json += ",\"floodEventLastQueuedId\":" + String((uint32_t)g_asyncFloodEventLastQueuedId);
  json += ",\"floodEventLastAppliedId\":" + String((uint32_t)g_asyncFloodEventLastAppliedId);
  json += ",\"floodEventLastFailedId\":" + String((uint32_t)g_asyncFloodEventLastFailedId);
  json += ",\"storageRequests\":" + String((uint32_t)g_asyncStorageRequests);
  json += ",\"storageFailures\":" + String((uint32_t)g_asyncStorageFailures);
  json += ",\"storageQueued\":" + String((uint32_t)g_asyncStorageQueued);
  json += ",\"storageApplied\":" + String((uint32_t)g_asyncStorageApplied);
  json += ",\"storageApplyFailures\":" + String((uint32_t)g_asyncStorageApplyFailures);
  json += ",\"storageNoops\":" + String((uint32_t)g_asyncStorageNoops);
  json += ",\"storageLastQueuedId\":" + String((uint32_t)g_asyncStorageLastQueuedId);
  json += ",\"storageLastAppliedId\":" + String((uint32_t)g_asyncStorageLastAppliedId);
  json += ",\"storageLastFailedId\":" + String((uint32_t)g_asyncStorageLastFailedId);
  json += ",\"runProfileWriteRequests\":" + String((uint32_t)g_asyncRunProfileWriteRequests);
  json += ",\"runProfileWriteFailures\":" + String((uint32_t)g_asyncRunProfileWriteFailures);
  json += ",\"runProfileQueued\":" + String((uint32_t)g_asyncRunProfileQueued);
  json += ",\"runProfileApplied\":" + String((uint32_t)g_asyncRunProfileApplied);
  json += ",\"runProfileApplyFailures\":" + String((uint32_t)g_asyncRunProfileApplyFailures);
  json += ",\"runProfileRollbackFailures\":" + String((uint32_t)g_asyncRunProfileRollbackFailures);
  json += ",\"runProfileLastQueuedId\":" + String((uint32_t)g_asyncRunProfileLastQueuedId);
  json += ",\"runProfileLastAppliedId\":" + String((uint32_t)g_asyncRunProfileLastAppliedId);
  json += ",\"runProfileLastFailedId\":" + String((uint32_t)g_asyncRunProfileLastFailedId);
  json += ",\"irrigationConfigWriteRequests\":" + String((uint32_t)g_asyncIrrigationConfigWriteRequests);
  json += ",\"irrigationConfigWriteFailures\":" + String((uint32_t)g_asyncIrrigationConfigWriteFailures);
  json += ",\"irrigationConfigQueued\":" + String((uint32_t)g_asyncIrrigationConfigQueued);
  json += ",\"irrigationConfigApplied\":" + String((uint32_t)g_asyncIrrigationConfigApplied);
  json += ",\"irrigationConfigApplyFailures\":" + String((uint32_t)g_asyncIrrigationConfigApplyFailures);
  json += ",\"irrigationConfigRollbackFailures\":" + String((uint32_t)g_asyncIrrigationConfigRollbackFailures);
  json += ",\"irrigationConfigLastQueuedId\":" + String((uint32_t)g_asyncIrrigationConfigLastQueuedId);
  json += ",\"irrigationConfigLastAppliedId\":" + String((uint32_t)g_asyncIrrigationConfigLastAppliedId);
  json += ",\"irrigationConfigLastFailedId\":" + String((uint32_t)g_asyncIrrigationConfigLastFailedId);
  json += ",\"shellyActionRequests\":" + String((uint32_t)g_asyncShellyActionRequests);
  json += ",\"shellyActionFailures\":" + String((uint32_t)g_asyncShellyActionFailures);
  json += ",\"shellyActionQueued\":" + String((uint32_t)g_asyncShellyActionQueued);
  json += ",\"shellyActionApplied\":" + String((uint32_t)g_asyncShellyActionApplied);
  json += ",\"shellyActionApplyFailures\":" + String((uint32_t)g_asyncShellyActionApplyFailures);
  json += ",\"shellyActionDryRuns\":" + String((uint32_t)g_asyncShellyActionDryRuns);
  json += ",\"shellyActionNetworkOps\":" + String((uint32_t)g_asyncShellyActionNetworkOps);
  json += ",\"shellyActionLastQueuedId\":" + String((uint32_t)g_asyncShellyActionLastQueuedId);
  json += ",\"shellyActionLastAppliedId\":" + String((uint32_t)g_asyncShellyActionLastAppliedId);
  json += ",\"shellyActionLastFailedId\":" + String((uint32_t)g_asyncShellyActionLastFailedId);
  json += ",\"shellyConfigWriteRequests\":" + String((uint32_t)g_asyncShellyConfigWriteRequests);
  json += ",\"shellyConfigWriteFailures\":" + String((uint32_t)g_asyncShellyConfigWriteFailures);
  json += ",\"shellyConfigRollbackFailures\":" + String((uint32_t)g_asyncShellyConfigRollbackFailures);
  json += ",\"systemRestartRequests\":" + String((uint32_t)g_asyncSystemRestartRequests);
  json += ",\"systemRestartFailures\":" + String((uint32_t)g_asyncSystemRestartFailures);
  json += ",\"systemRestartQueued\":" + String((uint32_t)g_asyncSystemRestartQueued);
  json += ",\"systemRestartApplied\":" + String((uint32_t)g_asyncSystemRestartApplied);
  json += ",\"systemRestartDryRuns\":" + String((uint32_t)g_asyncSystemRestartDryRuns);
  json += ",\"systemRestartLastQueuedId\":" + String((uint32_t)g_asyncSystemRestartLastQueuedId);
  json += ",\"systemRestartLastAppliedId\":" + String((uint32_t)g_asyncSystemRestartLastAppliedId);
  json += ",\"factoryResetRequests\":" + String((uint32_t)g_asyncFactoryResetRequests);
  json += ",\"factoryResetFailures\":" + String((uint32_t)g_asyncFactoryResetFailures);
  json += ",\"factoryResetQueued\":" + String((uint32_t)g_asyncFactoryResetQueued);
  json += ",\"factoryResetApplied\":" + String((uint32_t)g_asyncFactoryResetApplied);
  json += ",\"factoryResetApplyFailures\":" + String((uint32_t)g_asyncFactoryResetApplyFailures);
  json += ",\"factoryResetDryRuns\":" + String((uint32_t)g_asyncFactoryResetDryRuns);
  json += ",\"factoryResetLastQueuedId\":" + String((uint32_t)g_asyncFactoryResetLastQueuedId);
  json += ",\"factoryResetLastAppliedId\":" + String((uint32_t)g_asyncFactoryResetLastAppliedId);
  json += ",\"factoryResetLastFailedId\":" + String((uint32_t)g_asyncFactoryResetLastFailedId);
  json += ",\"apWifiSavePort\":80";
  json += ",\"apWifiSaveOnPermanentPort\":false";
  json += ",\"apProvisioningImplementation\":\"async-captive-portal\"";
  json += ",\"apProvisioningActive\":" + String(g_wifiProvisioningActive ? "true" : "false");
  json += ",\"apProvisioningServerStarted\":" + String(g_wifiProvisioningServerStarted ? "true" : "false");
  json += ",\"apProvisioningFallbackFromStoredCredentials\":" + String(g_wifiProvisioningFallbackFromStoredCredentials ? "true" : "false");
  json += ",\"apFallbackAutoStopEnabled\":true";
  json += ",\"apFallbackAutoStops\":" + String((uint32_t)g_wifiProvisioningFallbackAutoStops);
  json += ",\"legacyWebServerUsedInApMode\":false";
  json += ",\"wifiProvisionTransactional\":true";
  json += ",\"wifiProvisionShowsIp\":true";
  json += ",\"wifiProvisionBilingual\":true";
  json += ",\"wifiProvisionBrowserLanguageFallback\":true";
  json += ",\"wifiProvisionManualLanguageSwitch\":true";
  json += ",\"factoryResetErasesWifi\":true";
  json += ",\"wifiProvisionRequests\":" + String((uint32_t)g_wifiProvisionRequests);
  json += ",\"wifiProvisionFailures\":" + String((uint32_t)g_wifiProvisionFailures);
  json += ",\"wifiProvisionQueued\":" + String((uint32_t)g_wifiProvisionQueued);
  json += ",\"wifiProvisionConnected\":" + String((uint32_t)g_wifiProvisionConnected);
  json += ",\"legacyWebServerRemoved\":true";
  json += ",\"legacyStaCompatibilityServer\":false";
  json += ",\"messageSettingsAsync\":true";
  json += ",\"newGrowAsync\":true";
  json += ",\"otaAsyncWorker\":true";
  json += ",\"messageSettingsRequests\":" + String((uint32_t)g_asyncMessageSettingsRequests);
  json += ",\"messageSettingsFailures\":" + String((uint32_t)g_asyncMessageSettingsFailures);
  json += ",\"messageSettingsApplied\":" + String((uint32_t)g_asyncMessageSettingsApplied);
  json += ",\"messageSettingsDryRuns\":" + String((uint32_t)g_asyncMessageSettingsDryRuns);
  json += ",\"newGrowRequests\":" + String((uint32_t)g_asyncNewGrowRequests);
  json += ",\"newGrowFailures\":" + String((uint32_t)g_asyncNewGrowFailures);
  json += ",\"newGrowApplied\":" + String((uint32_t)g_asyncNewGrowApplied);
  json += ",\"newGrowDryRuns\":" + String((uint32_t)g_asyncNewGrowDryRuns);
  json += ",\"stateTransport\":\"shared-cache-direct-buffer-fixed-length\"";
  json += ",\"statePayloadFormat\":\"compact\"";
  json += ",\"stateChunkMax\":0";
  json += ",\"assetTransport\":\"direct-buffer-fixed-length\"";
  json += ",\"assetBlockMax\":0";
  json += "}";
  }

  const size_t sourceLength = json.length();
  const char* sourcePtr = sourceLength > 0 ? json.c_str() : nullptr;
  if (sourcePtr) retainAsyncHealthReader();
  xSemaphoreGive(g_asyncHealthCacheMutex);

  if (!sourcePtr || sourceLength == 0) {
    AsyncWebServerResponse* errorResponse = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"health_snapshot_unavailable\"}");
    addAsyncCommonHeaders(errorResponse, false);
    request->send(errorResponse);
    return;
  }

  AsyncWebServerResponse* response = request->beginResponse(
    200,
    "application/json; charset=utf-8",
    reinterpret_cast<const uint8_t*>(sourcePtr),
    sourceLength);

  if (!response) {
    releaseAsyncHealthReader();
    AsyncWebServerResponse* errorResponse = request->beginResponse(
      503, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"health_response_unavailable\"}");
    addAsyncCommonHeaders(errorResponse, false);
    request->send(errorResponse);
    return;
  }

  request->onDisconnect([]() { releaseAsyncHealthReader(); });
  addAsyncCommonHeaders(response, false);
  response->addHeader("Connection", "close");
  response->addHeader("X-GrowTent-Health-Transport", "direct-buffer-fixed-length");
  response->addHeader("X-GrowTent-Health-Source-Length", String(sourceLength));
  request->send(response);
}

static void registerAsyncPhase32Routes() {
  asyncServer.on("/", AsyncWebRequestMethod::HTTP_GET, handleAsyncRoot);

  // Phase 25 transport correction (v0.2.76): serve static assets through
  // ESPAsyncWebServer's native fixed-length byte-buffer response. v0.2.75
  // removed HTTP chunk framing but still used AsyncCallbackResponse; under a
  // mixed cold-dashboard load one callback response could still stall in the
  // final few bytes. The native buffer response removes the custom filler
  // callback entirely while preserving exact Content-Length semantics.
  auto sendProgmemAsset = [](AsyncWebServerRequest* request,
                             const char* contentType,
                             const char* data,
                             size_t dataLen) {
    if (!request || !data || dataLen == 0) return;
    AsyncWebServerResponse* response = request->beginResponse(
      200,
      contentType,
      reinterpret_cast<const uint8_t*>(data),
      dataLen);
    addAsyncCommonHeaders(response, true);
    response->addHeader("X-GrowTent-Asset-Transport", "direct-buffer-fixed-length");
    request->send(response);
  };

  asyncServer.on("/style.css", AsyncWebRequestMethod::HTTP_GET,
    [sendProgmemAsset](AsyncWebServerRequest* request) {
      sendProgmemAsset(request,
                       "text/css; charset=utf-8",
                       cssContent,
                       sizeof(cssContent) - 1);
    });

  asyncServer.on("/script.js", AsyncWebRequestMethod::HTTP_GET,
    [sendProgmemAsset](AsyncWebServerRequest* request) {
      sendProgmemAsset(request,
                       "application/javascript; charset=utf-8",
                       jsContent,
                       sizeof(jsContent) - 1);
    });

  asyncServer.on("/api/state", AsyncWebRequestMethod::HTTP_GET, handleAsyncState);

  // There is no distinct /api/stats implementation in the v0.2.46 controller
  // source. During the staged migration this endpoint is therefore an explicit compatibility
  // alias to the exact same read-only state snapshot, so it can be exercised by
  // monitoring clients on the permanent Async server.
  asyncServer.on("/api/stats", AsyncWebRequestMethod::HTTP_GET, handleAsyncStats);

  // Phase 2 routes retained: additional read-only SPA and diagnostic endpoints.
  asyncServer.on("/api/hint", AsyncWebRequestMethod::HTTP_GET, handleAsyncHint);
  asyncServer.on("/api/fs/status", AsyncWebRequestMethod::HTTP_GET, handleAsyncFsStatus);
  asyncServer.on("/api/logbuffer", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
    handleAsyncLogBuffer(request, false);
  });
  asyncServer.on("/log", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
    handleAsyncLogBuffer(request, true);
  });
  // The current dashboard link uses /download/log, while the legacy server
  // registers /log. Keep both aliases on the read-only async sidecar.
  asyncServer.on("/download/log", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
    handleAsyncLogBuffer(request, true);
  });
  // Phase 3 routes retained: read-only Grow Diary endpoints. These do not mount, migrate or
  // modify LittleFS; all diary mutations remain exclusively on port 80.
  asyncServer.on("/api/diary/list", AsyncWebRequestMethod::HTTP_GET, handleAsyncDiaryList);
  asyncServer.on("/api/diary.csv", AsyncWebRequestMethod::HTTP_GET, handleAsyncDiaryDownload);

  // Phase 5 retained: first controlled RAM-only write paths.
  // /api/logbuffer/clear is a real migrated route that changes only RAM state.
  asyncServer.on("/api/logbuffer/clear", AsyncWebRequestMethod::HTTP_POST, handleAsyncClearLog);

  // Phase 7 routes retained: safe persistent UI presentation settings. Every POST performs
  // strict allow-list validation plus NVS write/readback verification. Runtime
  // globals remain unchanged until restart, matching legacy settings semantics.
  asyncServer.on("/api/settings/theme", AsyncWebRequestMethod::HTTP_GET, handleAsyncThemeGet);
  asyncServer.on("/api/settings/theme", AsyncWebRequestMethod::HTTP_POST, handleAsyncThemePost, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/language", AsyncWebRequestMethod::HTTP_GET, handleAsyncLanguageGet);
  asyncServer.on("/api/settings/language", AsyncWebRequestMethod::HTTP_POST, handleAsyncLanguagePost, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/unit", AsyncWebRequestMethod::HTTP_GET, handleAsyncUnitGet);
  asyncServer.on("/api/settings/unit", AsyncWebRequestMethod::HTTP_POST, handleAsyncUnitPost, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/time-format", AsyncWebRequestMethod::HTTP_GET, handleAsyncTimeFormatGet);
  asyncServer.on("/api/settings/time-format", AsyncWebRequestMethod::HTTP_POST, handleAsyncTimeFormatPost, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/box-name", AsyncWebRequestMethod::HTTP_GET, handleAsyncBoxNameGet);
  asyncServer.on("/api/settings/box-name", AsyncWebRequestMethod::HTTP_POST, handleAsyncBoxNamePost, nullptr, handleAsyncJsonBody);

  // Phase 13: NTP/time-zone persistence only. These routes write Preferences
  // and verify readback, but deliberately do not call configTzTime() or mutate
  // ntpServer/tzInfo at runtime. The normal restart path applies them.
  asyncServer.on("/api/settings/ntp-server", AsyncWebRequestMethod::HTTP_GET, handleAsyncNtpServerGet);
  asyncServer.on("/api/settings/ntp-server", AsyncWebRequestMethod::HTTP_POST, handleAsyncNtpServerPost, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/time-zone", AsyncWebRequestMethod::HTTP_GET, handleAsyncTimeZoneGet);
  asyncServer.on("/api/settings/time-zone", AsyncWebRequestMethod::HTTP_POST, handleAsyncTimeZonePost, nullptr, handleAsyncJsonBody);

  // Phase 14: final restart-applied system settings. Persist-only means the
  // currently running DS18B20 task and relay hardware mapping are untouched.
  asyncServer.on("/api/settings/ds18b20-enabled", AsyncWebRequestMethod::HTTP_GET, sendAsyncDs18EnabledState);
  asyncServer.on("/api/settings/ds18b20-enabled", AsyncWebRequestMethod::HTTP_POST, handleAsyncDs18EnabledPost, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/active-relay-count", AsyncWebRequestMethod::HTTP_GET, sendAsyncRelayCountState);
  asyncServer.on("/api/settings/active-relay-count", AsyncWebRequestMethod::HTTP_POST, handleAsyncRelayCountPost, nullptr, handleAsyncJsonBody);

  // Phase 15: persistent Shelly device configuration only. These routes never
  // contact a Shelly and never mutate the running settings.shelly objects.
  asyncServer.on("/api/settings/shelly/main", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncShellyDeviceState(r, 0); });
  asyncServer.on("/api/settings/shelly/main", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncShellyDevicePost(r, 0); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/shelly/light", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncShellyDeviceState(r, 1); });
  asyncServer.on("/api/settings/shelly/light", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncShellyDevicePost(r, 1); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/shelly/humidifier", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncShellyDeviceState(r, 2); });
  asyncServer.on("/api/settings/shelly/humidifier", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncShellyDevicePost(r, 2); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/shelly/heater", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncShellyDeviceState(r, 3); });
  asyncServer.on("/api/settings/shelly/heater", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncShellyDevicePost(r, 3); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/shelly/fan", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncShellyDeviceState(r, 4); });
  asyncServer.on("/api/settings/shelly/fan", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncShellyDevicePost(r, 4); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/shelly/exhaust", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncShellyDeviceState(r, 5); });
  asyncServer.on("/api/settings/shelly/exhaust", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncShellyDevicePost(r, 5); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/shelly/heatmat", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncShellyDeviceState(r, 6); });
  asyncServer.on("/api/settings/shelly/heatmat", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncShellyDevicePost(r, 6); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/shelly/auth", AsyncWebRequestMethod::HTTP_GET, sendAsyncShellyAuthState);
  asyncServer.on("/api/settings/shelly/auth", AsyncWebRequestMethod::HTTP_POST, handleAsyncShellyAuthPost, nullptr, handleAsyncJsonBody);

  // Phase 12 retained: additional non-control system preferences. These affect only
  // logging verbosity, labels and displayed energy cost after restart.
  asyncServer.on("/api/settings/debug", AsyncWebRequestMethod::HTTP_GET, sendAsyncDebugState);
  asyncServer.on("/api/settings/debug", AsyncWebRequestMethod::HTTP_POST, handleAsyncDebugPost, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/ds18b20-name", AsyncWebRequestMethod::HTTP_GET, handleAsyncDs18NameGet);
  asyncServer.on("/api/settings/ds18b20-name", AsyncWebRequestMethod::HTTP_POST, handleAsyncDs18NamePost, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/power-price", AsyncWebRequestMethod::HTTP_GET, sendAsyncPowerPriceState);
  asyncServer.on("/api/settings/power-price", AsyncWebRequestMethod::HTTP_POST, handleAsyncPowerPricePost, nullptr, handleAsyncJsonBody);

  asyncServer.on("/api/settings/relay-name/1", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ handleAsyncRelayNameGet(r, 0); });
  asyncServer.on("/api/settings/relay-name/1", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelayNamePost(r, 0); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/relay-name/2", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ handleAsyncRelayNameGet(r, 1); });
  asyncServer.on("/api/settings/relay-name/2", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelayNamePost(r, 1); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/relay-name/3", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ handleAsyncRelayNameGet(r, 2); });
  asyncServer.on("/api/settings/relay-name/3", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelayNamePost(r, 2); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/relay-name/4", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ handleAsyncRelayNameGet(r, 3); });
  asyncServer.on("/api/settings/relay-name/4", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelayNamePost(r, 3); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/relay-name/5", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ handleAsyncRelayNameGet(r, 4); });
  asyncServer.on("/api/settings/relay-name/5", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelayNamePost(r, 4); }, nullptr, handleAsyncJsonBody);

  // Phase 17 retained: runtime-applied grow targets.
  asyncServer.on("/api/settings/grow-targets", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncGrowTargetsState(r); });
  asyncServer.on("/api/settings/grow-targets", AsyncWebRequestMethod::HTTP_POST, handleAsyncGrowTargetsPost, nullptr, handleAsyncJsonBody);

  // Phase 18 retained: heater tuning is persisted and verified in the Async callback,
  // then applied to the live controller only from the normal main loop.
  asyncServer.on("/api/settings/heating-tuning", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncHeatingTuningState(r); });
  asyncServer.on("/api/settings/heating-tuning", AsyncWebRequestMethod::HTTP_POST, handleAsyncHeatingTuningPost, nullptr, handleAsyncJsonBody);

  // Phase 20: heating source + ESP-heater relay configuration. No relay/Shelly
  // switch occurs in the Async callback; any required old-source shutdown and
  // the live settings mutation happen only from the normal main loop.
  asyncServer.on("/api/settings/heating-output", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncHeatingOutputState(r); });
  asyncServer.on("/api/settings/heating-output", AsyncWebRequestMethod::HTTP_POST, handleAsyncHeatingOutputPost, nullptr, handleAsyncJsonBody);

  // Phase 21: grow-light ON time + photoperiod. The callback never contacts the
  // Shelly; optional schedule programming happens only from the normal main loop.
  asyncServer.on("/api/settings/light-schedule", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncLightScheduleState(r); });
  asyncServer.on("/api/settings/light-schedule", AsyncWebRequestMethod::HTTP_POST, handleAsyncLightSchedulePost, nullptr, handleAsyncJsonBody);

  // Phase 22: ESP relay minute schedules. Async callbacks persist and queue only;
  // settings.relay.schedule[] is published from loop(), with enabled written last.
  asyncServer.on("/api/settings/relay-schedule/1", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncRelayScheduleState(r, 0); });
  asyncServer.on("/api/settings/relay-schedule/1", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelaySchedulePost(r, 0); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/relay-schedule/2", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncRelayScheduleState(r, 1); });
  asyncServer.on("/api/settings/relay-schedule/2", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelaySchedulePost(r, 1); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/relay-schedule/3", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncRelayScheduleState(r, 2); });
  asyncServer.on("/api/settings/relay-schedule/3", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelaySchedulePost(r, 2); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/relay-schedule/4", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncRelayScheduleState(r, 3); });
  asyncServer.on("/api/settings/relay-schedule/4", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelaySchedulePost(r, 3); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/relay-schedule/5", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncRelayScheduleState(r, 4); });
  asyncServer.on("/api/settings/relay-schedule/5", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelaySchedulePost(r, 4); }, nullptr, handleAsyncJsonBody);

  // Phase 23: dashboard-compatible manual relay actions. The legacy /relay/N/toggle
  // paths are intentionally reproduced on port 8080, but only enqueue work.
  // Explicit /api/relay/N/state POST is also provided for idempotent API control
  // and safe OFF->OFF automated regression coverage.
  asyncServer.on("/api/relay/1/state", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncRelayActionState(r, 0); });
  asyncServer.on("/api/relay/1/state", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelaySetPost(r, 0); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/relay/2/state", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncRelayActionState(r, 1); });
  asyncServer.on("/api/relay/2/state", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelaySetPost(r, 1); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/relay/3/state", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncRelayActionState(r, 2); });
  asyncServer.on("/api/relay/3/state", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelaySetPost(r, 2); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/relay/4/state", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncRelayActionState(r, 3); });
  asyncServer.on("/api/relay/4/state", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelaySetPost(r, 3); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/relay/5/state", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* r){ sendAsyncRelayActionState(r, 4); });
  asyncServer.on("/api/relay/5/state", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelaySetPost(r, 4); }, nullptr, handleAsyncJsonBody);

  asyncServer.on("/relay/1/toggle", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelayTogglePost(r, 0); });
  asyncServer.on("/relay/2/toggle", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelayTogglePost(r, 1); });
  asyncServer.on("/relay/3/toggle", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelayTogglePost(r, 2); });
  asyncServer.on("/relay/4/toggle", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelayTogglePost(r, 3); });
  asyncServer.on("/relay/5/toggle", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ handleAsyncRelayTogglePost(r, 4); });

  // Phase 24: watering, pump-test, tank and Flood routes on the permanent port.
  asyncServer.on("/api/watering/state", AsyncWebRequestMethod::HTTP_GET, sendAsyncWateringState);
  asyncServer.on("/api/watering/action", AsyncWebRequestMethod::HTTP_POST, handleAsyncWateringActionPost, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/watering/start", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncWateringAction(r, AsyncWateringActionType::StartApi); });
  asyncServer.on("/api/watering/stop", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncWateringAction(r, AsyncWateringActionType::Stop); });
  asyncServer.on("/startWatering", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncWateringAction(r, AsyncWateringActionType::StartApi); });
  asyncServer.on("/toggleWatering", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncWateringAction(r, AsyncWateringActionType::ToggleDashboard); });
  asyncServer.on("/pump/6/triggerPump10s", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncWateringAction(r, AsyncWateringActionType::PumpPulse, 0, 10000UL); });
  asyncServer.on("/pump/7/triggerPump10s", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncWateringAction(r, AsyncWateringActionType::PumpPulse, 1, 10000UL); });
  asyncServer.on("/pump/8/triggerPump10s", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncWateringAction(r, AsyncWateringActionType::PumpPulse, 2, 10000UL); });
  asyncServer.on("/pingTank", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncWateringAction(r, AsyncWateringActionType::TankPing); });
  asyncServer.on("/api/flood/event", AsyncWebRequestMethod::HTTP_GET, handleAsyncFloodEvent);
  asyncServer.on("/api/flood/event", AsyncWebRequestMethod::HTTP_POST, handleAsyncFloodEvent);


  // Phase 27: Shelly runtime actions on the permanent port. Callbacks enqueue
  // only; all Shelly HTTP/RPC and energy-reset Preferences writes execute in
  // processAsyncShellyActionApply() from loop(). dryRun=1 is a no-network,
  // no-mutation regression guard and is never used by the dashboard.
  asyncServer.on("/shelly/main/toggle", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncShellyAction(r, AsyncShellyActionType::Toggle, 0); });
  asyncServer.on("/shelly/light/toggle", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncShellyAction(r, AsyncShellyActionType::Toggle, 1); });
  asyncServer.on("/shelly/humidifier/toggle", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncShellyAction(r, AsyncShellyActionType::Toggle, 2); });
  asyncServer.on("/shelly/heater/toggle", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncShellyAction(r, AsyncShellyActionType::Toggle, 3); });
  asyncServer.on("/shelly/fan/toggle", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncShellyAction(r, AsyncShellyActionType::Toggle, 4); });
  asyncServer.on("/shelly/exhaust/toggle", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncShellyAction(r, AsyncShellyActionType::Toggle, 5); });
  asyncServer.on("/api/shelly/light/refresh-schedule", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncShellyAction(r, AsyncShellyActionType::RefreshSchedule, 1); });
  asyncServer.on("/api/shelly/humidifier/refresh-schedule", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncShellyAction(r, AsyncShellyActionType::RefreshSchedule, 2); });
  asyncServer.on("/api/shelly/heater/refresh-schedule", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncShellyAction(r, AsyncShellyActionType::RefreshSchedule, 3); });
  asyncServer.on("/api/shelly/fan/refresh-schedule", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncShellyAction(r, AsyncShellyActionType::RefreshSchedule, 4); });
  asyncServer.on("/api/shelly/exhaust/refresh-schedule", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncShellyAction(r, AsyncShellyActionType::RefreshSchedule, 5); });
  asyncServer.on("/api/shelly/reset-energy", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncShellyAction(r, AsyncShellyActionType::ResetEnergy); });
  asyncServer.on("/api/shelly/action-status", AsyncWebRequestMethod::HTTP_GET, handleAsyncShellyActionStatus);

  // Phase 28: dashboard Shelly configuration now persists on port 8080 and
  // queues a runtime reload. No Shelly network operation occurs in the Async callback.
  asyncServer.on("/api/settings/shelly-dashboard", AsyncWebRequestMethod::HTTP_GET, sendAsyncShellyDashboardState);
  asyncServer.on("/api/settings/shelly-dashboard", AsyncWebRequestMethod::HTTP_POST, handleAsyncShellyDashboardSave, nullptr, handleAsyncJsonBody);
  asyncServer.on("/saveshellysettings", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){
    logPrint("[ASYNC][SHELLY-CONFIG] rejected legacy POST /saveshellysettings");
    AsyncWebServerResponse* response = r->beginResponse(410, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"legacy_shellysettings_retired\"}");
    addAsyncCommonHeaders(response, false); r->send(response);
  });

  // Phase 29: the System Settings page now saves through the already proven
  // dedicated preference endpoints and queues a main-loop-owned restart. The
  // old broad /savesettings POST is retired on the permanent dashboard port.
  asyncServer.on("/api/system/restart", AsyncWebRequestMethod::HTTP_POST, queueAsyncSystemRestart);
  asyncServer.on("/api/system/restart-status", AsyncWebRequestMethod::HTTP_GET, handleAsyncSystemRestartStatus);
  asyncServer.on("/savesettings", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){
    logPrint("[ASYNC][SYSTEM] rejected legacy POST /savesettings");
    AsyncWebServerResponse* response = r->beginResponse(410, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"legacy_savesettings_retired\"}");
    addAsyncCommonHeaders(response, false); r->send(response);
  });

  // Phase 30: Factory reset is a permanent-port main-loop handoff. AP Wi-Fi
  // credential provisioning is the deliberate exception: /save stays on TCP/80.
  asyncServer.on("/factory-reset", AsyncWebRequestMethod::HTTP_POST, queueAsyncFactoryReset);
  asyncServer.on("/api/system/factory-reset-status", AsyncWebRequestMethod::HTTP_GET, handleAsyncFactoryResetStatus);
  asyncServer.on("/api/wifi/provisioning-info", AsyncWebRequestMethod::HTTP_GET, handleAsyncProvisioningInfo);
  asyncServer.on("/save", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){
    AsyncWebServerResponse* response = r->beginResponse(410, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"ap_wifi_save_port80_only\"}");
    addAsyncCommonHeaders(response, false); r->send(response);
  });

  // Phase 26: final Betriebseinstellungen decomposition. The dashboard save
  // button now targets only permanent-port endpoints; no broad port-80 save
  // route is required. Runtime mutation remains main-loop owned.
  asyncServer.on("/api/settings/run-profile", AsyncWebRequestMethod::HTTP_GET, sendAsyncRunProfileState);
  asyncServer.on("/api/settings/run-profile", AsyncWebRequestMethod::HTTP_POST, handleAsyncRunProfilePost, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/settings/irrigation", AsyncWebRequestMethod::HTTP_GET, sendAsyncIrrigationConfigState);
  asyncServer.on("/api/settings/irrigation", AsyncWebRequestMethod::HTTP_POST, handleAsyncIrrigationConfigPost, nullptr, handleAsyncJsonBody);
  asyncServer.on("/saverunsettings", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){
    AsyncWebServerResponse* response = r->beginResponse(410, "application/json; charset=utf-8",
      "{\"ok\":false,\"err\":\"legacy_runsettings_retired\"}");
    addAsyncCommonHeaders(response, false); r->send(response);
  });

  // Phase 25: diary mutations + explicit LittleFS recovery.
  asyncServer.on("/api/diary/add", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncStorageAction(r, AsyncStorageActionType::DiaryAdd); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/diary/update", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncStorageAction(r, AsyncStorageActionType::DiaryUpdate); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/diary/delete", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncStorageAction(r, AsyncStorageActionType::DiaryDelete); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/diary/clear", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncStorageAction(r, AsyncStorageActionType::DiaryClear); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/fs/format", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* r){ queueAsyncStorageAction(r, AsyncStorageActionType::FsFormat); }, nullptr, handleAsyncJsonBody);
  asyncServer.on("/api/storage/action-status", AsyncWebRequestMethod::HTTP_GET, handleAsyncStorageStatus);

  // Phase 32: final STA legacy routes migrated to Async/8080.
  asyncServer.on("/savemessagesettings", AsyncWebRequestMethod::HTTP_POST, handleAsyncMessageSettingsSave);
  asyncServer.on("/api/settings/message-status", AsyncWebRequestMethod::HTTP_GET, handleAsyncMessageSettingsStatus);
  asyncServer.on("/api/newgrow", AsyncWebRequestMethod::HTTP_POST, handleAsyncNewGrow);
  asyncServer.on("/api/newgrow/status", AsyncWebRequestMethod::HTTP_GET, handleAsyncNewGrowStatus);
  asyncServer.on("/api/ota/check", AsyncWebRequestMethod::HTTP_GET, handleAsyncOtaCheckQueue);
  asyncServer.on("/api/ota/check-status", AsyncWebRequestMethod::HTTP_GET, handleAsyncOtaCheckStatus);
  asyncServer.on("/api/ota/update", AsyncWebRequestMethod::HTTP_POST, handleAsyncOtaUpdateQueue);
  asyncServer.on("/api/ota/update-status", AsyncWebRequestMethod::HTTP_GET, handleAsyncOtaUpdateStatus);

  asyncServer.on("/api/async/health", AsyncWebRequestMethod::HTTP_GET, handleAsyncHealth);
  asyncServer.on("/favicon.ico", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(204, "text/plain; charset=utf-8", "");
    addAsyncCommonHeaders(response, true);
    request->send(response);
  });

  asyncServer.onNotFound([](AsyncWebServerRequest* request) {
    logPrint(String("[ASYNC][HTTP] not-found method=") + String((int)request->method()) +
             " url=" + request->url());
    AsyncWebServerResponse* response =
      request->beginResponse(404, "application/json; charset=utf-8",
                             "{\"ok\":false,\"error\":\"not_found_phase32\"}");
    addAsyncCommonHeaders(response, false);
    request->send(response);
  });

  asyncServer.begin();
  Serial.println("[ASYNC] phase 32 full Async application server listening on permanent port 8080");
}

// -------------------- State/Variables API (registry -> JSON) --------------------
// -------------------- Deferred init task (moves slow stuff out of setup) --------------------
static void taskDeferredInit(void* /*pv*/) {
  // Persistent settings are loaded synchronously in setup() before the web server starts.
  // Keep slow hardware/network initialization in this deferred task.

  // DS18B20 init (quick)
  sensors.begin();
  sensors.setWaitForConversion(false); // async mode; we will check for completion in the sensor task
  sensors.setResolution(12); // max precision, takes ~750ms per conversion, but we start it now so it can run in parallel with other init. Subsequent readings will be faster due to async mode.
  sensors.requestTemperatures(); // start first conversion ASAP (takes ~750ms), subsequent readings will be faster due to async mode

  // I2C + BME280 init
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000); // robust for longer wiring

  // I2C bus scan
  logPrint("[I2C] Scanning bus...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      logPrint("[I2C] Found device at 0x" + String(addr, HEX));
      delay(2);
    }
  }

  // Initialize BME280 sensor (retry up to 10s)
  bmeAvailable = false;
  uint8_t candidates[2] = { 0x76, 0x77 };
  bool bmeInit = false;

  unsigned long startTime = millis();
  while (!bmeInit && (millis() - startTime < 10000)) {
    for (uint8_t i = 0; i < 2 && !bmeInit; i++) {
      uint8_t a = candidates[i];
      logPrint("[SENSOR] Trying BME280 at 0x" + String(a, HEX) + " ...");

      if (bme.begin(a, &Wire)) { // wichtig: &Wire
        logPrint("[SENSOR] BME280 initialized at 0x" + String(a, HEX));
        bmeAvailable = true;

        // seed with one reading
        readSensorData();
        addReading(cur.temperatureC, cur.humidityPct, cur.vpdKpa);

        bmeInit = true;
      } else {
        delay(250);
      }
    }

    if (!bmeInit) {
      logPrint("[SENSOR] BME280 not found, retrying in 500 ms. Check wiring!");
      delay(500);
    }
  }

  if (!bmeAvailable) {
    logPrint("[SENSOR] BME280 not found (will run without it)");
  }

  // Start tasks (they should be written defensively: check wifiReady/bmeAvailable)
  xTaskCreatePinnedToCore(sensorTask, "sensor", 2048, nullptr, 1, &sensorTaskHandle, 1);

  if (bmeAvailable) {
    xTaskCreatePinnedToCore(taskCheckBMESensor, "BME", 4096, nullptr, 1, nullptr, 1);
  }

  // Shelly status task can run even if WiFi isn't ready yet; it should handle that.
  xTaskCreatePinnedToCore(taskShellyStatus, "Shelly", 4096, nullptr, 1, nullptr, 1);

  // Watering task (relay control) can also run without WiFi, but should check activeRelayCount and handle it gracefully.
  xTaskCreatePinnedToCore(taskWatering, "Watering", 4096, nullptr, 1, nullptr, 1);

  vTaskDelete(nullptr);
}

// -------------------- setup --------------------
void setup() {
  Serial.begin(115200);

  // Create mutex for relay state access (used by both HTTP handlers and watering task)
  relayMutex = xSemaphoreCreateMutex();
  if (relayMutex == nullptr) {
    logPrint("[BOOT] relayMutex create failed");
  }

  // Create mutex for log buffer access (used by multiple tasks)
  logBufferMutex = xSemaphoreCreateMutex();
  if (logBufferMutex == nullptr) {
    logPrint("[BOOT] logBufferMutex create failed");
  }

  g_wifiProvisioningMutex = xSemaphoreCreateMutex();
  if (g_wifiProvisioningMutex == nullptr) {
    logPrint("[BOOT] wifi provisioning mutex create failed");
  }
  configureProvisioningServerRoutes();

  // LittleFS is checked exactly once during boot.
  // On failure, the result is cached and no automatic retries are performed.
  ensureFsMounted();

  // Load WiFi credentials only (fast)
  preferences.begin(PREF_NS, true);
  loadPrefString(KEY_SSID, ssidName, "", true, "ssidName");
  loadPrefString(KEY_PASS, ssidPassword, "", false, "ssidPassword");
  loadPrefInt(KEY_RELAYCOUNT, activeRelayCount, 4, false, "activeRelayCount");
  activeRelayCount = (activeRelayCount == 8) ? 8 : 4;
  preferences.end();

  // Load ALL persistent runtime settings before any HTTP route can be served.
  // This prevents a boot-time race where the UI could render empty/default grow dates
  // and write them back before the deferred initialization task restored NVS values.
  readPreferences();
  logPrint("[BOOT] Persistent settings loaded before web server start");
  
  // Initialize relay outputs ASAP (prevent random toggles)
  for (int i = 0; i < activeRelayCount; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  // Decide WiFi mode:
  // - no credentials -> AP only
  // - credentials present -> try STA first
  // - if STA is not up in time, start AP as fallback but keep STA reconnect possible
  wifiReady = false;

  if (ssidName.length() == 0) {
    espMode = true;
    g_wifiProvisioningFallbackFromStoredCredentials = false;
    g_wifiProvisioningActive = startSoftAP();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssidName.c_str(), ssidPassword.c_str());
    txPower = (WiFi.getTxPower() / 4);
    //WiFi.setTxPower(WIFI_POWER_19_5dBm); // max power (range boost, but more current)

    Serial.println("[WIFI] Connecting to: " + ssidName);

    const uint32_t WIFI_BOOT_CONNECT_TIMEOUT_MS = 10000UL;
    const uint32_t t0 = millis();

    while (WiFi.status() != WL_CONNECTED &&
           (uint32_t)(millis() - t0) < WIFI_BOOT_CONNECT_TIMEOUT_MS) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiReady = true;
      espMode = false;
      g_wifiProvisioningFallbackFromStoredCredentials = false;

      Serial.println("[WIFI] Connected: " + WiFi.localIP().toString());
      Serial.println("[BOOT] FW build: " + String(__DATE__) + " " + String(__TIME__));

      logPrint("[BOOT] Starting initial NTP sync...");
      configTzTime(tzInfo.c_str(), ntpServer.c_str());
      ntpSyncPending = true;
      ntpStartMs = millis();
    } else {
      Serial.println("[WIFI] STA connect timeout -> start AP fallback, keep STA reconnect enabled");

      wifiReady = false;

      // Important:
      // We do NOT want to end up in AP-only mode if credentials exist.
      // Keep reconnect logic alive in loop().
      espMode = false;

      // Start fallback AP, but do not disable STA operation.
      WiFi.mode(WIFI_AP_STA);
      g_wifiProvisioningFallbackFromStoredCredentials = true;
      g_wifiProvisioningActive = startSoftAP();
    }
  }

  // Phase 32 port ownership: when the AP/captive portal is active, TCP/80 is
  // owned exclusively by ESPAsyncWebServer. The legacy synchronous server is
  // started only in normal STA mode for the few remaining rollback endpoints.
  if (g_wifiProvisioningActive) {
    startProvisioningPortalServer();
  }

  // Phase 32: the synchronous STA compatibility listener has been removed.
  // TCP/80 exists only while the Async captive portal owns it in provisioning
  // mode; normal GrowTent UI/API traffic is exclusively on TCP/8080.
  Serial.println("[WEB] Phase 32: legacy TCP/80 WebServer removed; dashboard/API = :8080");
  initAsyncPhase13ReadCache();
  registerAsyncPhase32Routes();
  pushHintKey("hint.systemStarted");
  logPrint("[APP] system started");

  // Do the slow init after the webserver is up.
  xTaskCreatePinnedToCore(taskDeferredInit, "deferredInit", 4096, nullptr, 1, nullptr, 0);
}

// -------------------- loop --------------------
void loop() {
  // Web/API responsiveness watchdog. Phase 32 has only Async web servers:
  // permanent application traffic on :8080 and provisioning-only :80.
  static uint32_t lastLoopEntryMs = 0;
  static uint32_t lastLoopGapLogMs = 0;
  const uint32_t loopNowMs = millis();
  if (lastLoopEntryMs != 0) {
    const uint32_t loopGapMs = (uint32_t)(loopNowMs - lastLoopEntryMs);
    if (loopGapMs >= 1000UL &&
        (lastLoopGapLogMs == 0 || (uint32_t)(loopNowMs - lastLoopGapLogMs) >= 5000UL)) {
      lastLoopGapLogMs = loopNowMs;
      logPrint("[WEB] main loop gap=" + String(loopGapMs) +
               "ms wifiStatus=" + String((int)WiFi.status()) +
               " freeHeap=" + String(ESP.getFreeHeap()) +
               " maxAlloc=" + String(ESP.getMaxAllocHeap()));
    }
  }
  lastLoopEntryMs = loopNowMs;

  // Runtime handoffs: Async callbacks persist and queue validated settings,
  // while the normal loop owns all live controller mutations.
  processAsyncFactoryResetApply();
  processAsyncSystemRestartApply();
  processAsyncMessageSettingsApply();
  processAsyncNewGrowApply();
  otaProcessQueue();
  processAsyncGrowTargetsApply();
  processAsyncHeatingTuningApply();
  processAsyncHeatingOutputApply();
  processAsyncLightScheduleApply();
  processAsyncRelayScheduleApply();
  processAsyncRelayActionApply();
  processAsyncRunProfileApply();
  processAsyncIrrigationConfigApply();
  processAsyncShellyActionApply();
  // Flood is applied before watering so an alarm wins a same-loop race.
  processAsyncFloodEventApply();
  processAsyncStorageActionApply();
  processAsyncWateringActionApply();

  // Observe WiFi link state once per second so even short disconnects become visible
  // in the system log. This is intentionally separate from the slower reconnect policy.
  static uint32_t lastWifiObserveMs = 0;
  static wl_status_t lastObservedWifiStatus = WL_NO_SHIELD;
  static uint32_t wifiLostAtMs = 0;
  static uint32_t wifiDisconnectCount = 0;

  if ((uint32_t)(loopNowMs - lastWifiObserveMs) >= 1000UL) {
    lastWifiObserveMs = loopNowMs;
    const wl_status_t observed = WiFi.status();

    if (lastObservedWifiStatus == WL_NO_SHIELD) {
      lastObservedWifiStatus = observed;
    } else if (observed != lastObservedWifiStatus) {
      if (lastObservedWifiStatus == WL_CONNECTED && observed != WL_CONNECTED) {
        wifiLostAtMs = loopNowMs;
        wifiDisconnectCount++;
        logPrint("[WIFI] link lost status=" + String((int)observed) +
                 " disconnects=" + String(wifiDisconnectCount));
      } else if (observed == WL_CONNECTED) {
        const uint32_t downtimeMs = wifiLostAtMs ? (uint32_t)(loopNowMs - wifiLostAtMs) : 0UL;
        logPrint("[WIFI] link restored ip=" + WiFi.localIP().toString() +
                 " rssi=" + String(WiFi.RSSI()) +
                 " downtimeMs=" + String(downtimeMs) +
                 " disconnects=" + String(wifiDisconnectCount));
        wifiLostAtMs = 0;
      }
      lastObservedWifiStatus = observed;
    }
  }

  // WiFi reconnect policy:
  // - check every 10s
  // - gentle reconnect only every 15s
  // - hard reset only after 60s offline
  // - hard reset retry only every 60s
  static uint32_t lastWifiCheck = 0;
  static uint32_t wifiDownSince = 0;
  static uint32_t lastWifiReconnectAttempt = 0;
  static uint32_t lastWifiHardResetAttempt = 0;

  const uint32_t WIFI_CHECK_MS = 10000UL;
  const uint32_t WIFI_RECONNECT_RETRY_MS = 15000UL;
  const uint32_t WIFI_HARD_RESET_AFTER_MS = 60000UL;
  const uint32_t WIFI_HARD_RESET_RETRY_MS = 60000UL;

  if ((uint32_t)(millis() - lastWifiCheck) >= WIFI_CHECK_MS) {
    lastWifiCheck = millis();

    // Only try STA reconnects when we actually have credentials and are not in AP mode
    if (!espMode && ssidName.length() > 0 && !g_wifiProvisioningBusy) {
      wl_status_t st = WiFi.status();

      if (st == WL_CONNECTED) {
        txPower = (WiFi.getTxPower() / 4);
        wifiReady = true;
        wifiDownSince = 0;
        lastWifiReconnectAttempt = 0;
        lastWifiHardResetAttempt = 0;
      } else {
        wifiReady = false;

        if (wifiDownSince == 0) {
          wifiDownSince = millis();
        }

        const bool longOffline =
          (uint32_t)(millis() - wifiDownSince) >= WIFI_HARD_RESET_AFTER_MS;

        // Avoid reconnect hammering while WiFi stack is already busy connecting
        const bool wifiBusyConnecting =
          (st == WL_IDLE_STATUS);

        if (!longOffline) {
          if (!wifiBusyConnecting &&
              (lastWifiReconnectAttempt == 0 ||
               (uint32_t)(millis() - lastWifiReconnectAttempt) >= WIFI_RECONNECT_RETRY_MS)) {

            lastWifiReconnectAttempt = millis();
            logPrint("[WIFI] reconnect attempt status=" + String((int)st));
            WiFi.reconnect();
          }
        } else {
          if (lastWifiHardResetAttempt == 0 ||
              (uint32_t)(millis() - lastWifiHardResetAttempt) >= WIFI_HARD_RESET_RETRY_MS) {

            lastWifiHardResetAttempt = millis();
            logPrint("[WIFI] hard reconnect after offlineMs=" +
                     String((uint32_t)(millis() - wifiDownSince)));

            // Important: do NOT erase saved WiFi/AP config
            WiFi.disconnect(false, false);
            delay(100);
            WiFi.begin(ssidName.c_str(), ssidPassword.c_str());
          }
        }
      }
    }
  }

  processPumpAutoOff();

  // v0.4.2: A boot-time fallback AP is temporary. Once the stored STA
  // connection has recovered, release TCP/80/DNS and return to STA-only mode
  // before any further provisioning scan work can consume radio/network time.
  stopFallbackProvisioningPortalIfStaRecovered();

  if (g_wifiProvisioningActive) {
    provisioningDns.processNextRequest();
    processWifiProvisioning();
  }
  delay(1);

  dailyNtpTrigger();
  ntpSyncTick();

  if (g_restartRequested && (int32_t)(millis() - g_restartAtMs) >= 0) {
    g_restartRequested = false;
    if (g_eraseWifiBeforeRestart) {
      g_eraseWifiBeforeRestart = false;
      logPrint("[WIFI][FACTORY] erasing ESP32 stored STA credentials before restart");
      WiFi.disconnect(true, true);
      delay(100);
    }
    ESP.restart();
  }
}
