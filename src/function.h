#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <index_html.h>
#include <time.h>
#include <deque>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiClientSecure.h>
#include <IPAddress.h>
#include <HTTPClient.h>
#include <mbedtls/md.h>
// Base64 (for Basic auth) via mbedTLS (available in ESP32 Arduino core)
#include <mbedtls/base64.h>

// global, functions, html code, js code and css code includes
#include "globals.h"
#include <runtime.h>

// Include platform-specific headers for inet_ntoa
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
#include <lwip/inet.h>

// Forward declarations (used before definitions)
struct ShellyDevice;
struct ShellyValues;
ShellyValues getShellyValues(ShellyDevice& dev, int switchId, int port = 80);
static bool shellyResetEnergyCounters(ShellyDevice &dev, uint8_t switchId, uint16_t port);

// Called early (e.g. from handleNewGrow) but implemented further below.
static bool applyGrowLightSchedule();

#endif


// If no pin is defined elsewhere, default to GPIO4
#ifndef DS18B20_PIN
#define DS18B20_PIN 4
#endif

// Declare OneWire + DallasTemperature objects (defined in function.cpp to avoid multiple/conflicting definitions)
extern OneWire oneWire;
extern DallasTemperature sensors;

// declare the global WebServer instance defined elsewhere
extern WebServer server;
extern Preferences preferences;
extern const char* htmlPage;
extern std::deque<String> logBuffer;
extern volatile float DS18B20STemperature;
extern unsigned long relayOffTime[];
extern bool relayActive[];

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "--:--:--";
  }

  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
           timeinfo.tm_hour,
           timeinfo.tm_min,
           timeinfo.tm_sec);
  return String(buf);
}

// log buffer to store recent log lines
void logPrint(const String& msg) {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);

  char ts[20];
  strftime(ts, sizeof(ts), "%H:%M:%S", &t);

  String line = String(ts) + " - " + msg;

  Serial.println(line);

  logBuffer.push_back(line);
  if (logBuffer.size() > LOG_MAX_LINES) {
    logBuffer.pop_front();
  }
}

bool checkFS() {
  // Info abfragen (geht nur wenn gemountet)
  size_t total = LittleFS.totalBytes();
  size_t used  = LittleFS.usedBytes();

  if (total == 0) {
    logPrint("[LITTLEFS][ERROR] totalBytes=0 (not mounted?)");
    return false;
  }

  // Test: Root existiert / Datei kann geöffnet werden
  File f = LittleFS.open("/.health", FILE_WRITE);
  if (!f) {
    logPrint("[LITTLEFS][ERROR] cannot open /.health");
    return false;
  }
  f.println("ok");
  f.close();

  logPrint("[LITTLEFS] OK total=" + String(total) + " used=" + String(used));
  return true;
}

void sensorTask(void* pvParameters) {
  for (;;) {
    // ... dein Task-Code ...

    UBaseType_t freeWords = uxTaskGetStackHighWaterMark(NULL);
    logPrint("[TASK][sensor] free stack: " + String(freeWords) + " words (" + String(freeWords * 4) + " bytes)");

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

// Helper function: Send JSON from PROGMEM
void sendJSON_P(const char* jsonP) {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json");
  // stream the content:
  WiFiClient client = server.client();
  // Attention: PROGMEM -> Read in pieces
  PGM_P p = reinterpret_cast<PGM_P>(jsonP);
  char c;
  while ((c = pgm_read_byte(p++)) != 0) {
    client.write(c);
  }
}

// soft ap configuration
void startSoftAP() {
  IPAddress ip = WiFi.softAPIP();
  Serial.println("[SoftAP] Start SoftAP mode. IP address: " + ip.toString());

  
  // Disconnect previous connections
  WiFi.disconnect(true, true);  
  delay(100);
  
  WiFi.mode(WIFI_AP_STA);

  //last parameter 'false' = do not hide SSID
  String chipId = String((uint32_t)ESP.getEfuseMac(), HEX);
  String apName = String(KEY_APSSID) + "_" + chipId;

  // Harden weak/default AP password at runtime.
  String apPass = String(KEY_APPASSWORD);
  if (apPass.length() < 8 || apPass == "12345678") {
    String suffix = chipId;
    suffix.toUpperCase();
    if (suffix.length() > 6) suffix = suffix.substring(suffix.length() - 6);
    apPass = "GT-" + suffix + "!";
    Serial.println("[SoftAP] Weak default password detected. Using device-specific password: " + apPass);
  }

  bool ok = WiFi.softAP(apName.c_str(), apPass.c_str(), /*channel*/ 1, /*hidden*/ false);
  if (!ok) {
    Serial.println("[SoftAP] Error starting the SoftAP!");
    return;
  }
}

// Helper function to save a string preference if the corresponding argument is present
void savePrefString(
  const char* argName,
  const char* prefKey,
  String& targetVar,
  bool logValue = true,
  const char* logLabel = nullptr
) {
  if (!server.hasArg(argName)) return;

  targetVar = server.arg(argName);
  preferences.putString(prefKey, targetVar);

  if (logLabel == nullptr) logLabel = prefKey;

  if (logValue) {
    logPrint("[PREFERENCES SAVE] " + String(logLabel) + " written = " + targetVar);
  } else {
    logPrint("[PREFERENCES SAVE] " + String(logLabel) + " updated (hidden)");
  }
}

// Helper function to save an integer preference if the corresponding argument is present
void savePrefInt(
  const char* argName,
  const char* prefKey,
  int& targetVar,
  bool logValue = true,
  const char* logLabel = nullptr
) {
  if (!server.hasArg(argName)) return;

  targetVar = server.arg(argName).toInt();
  preferences.putInt(prefKey, targetVar);

  if (logLabel == nullptr) logLabel = prefKey;

  if (logValue) {
    logPrint("[PREFERENCES SAVE] " + String(logLabel) + " written = " + String(targetVar));
  } else {
    logPrint("[PREFERENCES SAVE] " + String(logLabel) + " updated (hidden)");
  }
}

void savePrefFloat(
  const char* argName,
  const char* prefKey,
  float& targetVar,
  bool logValue = true,
  const char* logLabel = nullptr
) {
  if (!server.hasArg(argName)) return;

  String v = server.arg(argName);
  targetVar = v.toFloat();

  preferences.putFloat(prefKey, targetVar);

  if (logLabel == nullptr) logLabel = prefKey;

  if (logValue) {
    logPrint("[PREFERENCES SAVE] " + String(logLabel) +
             " = " + String(targetVar, 2));
  } else {
    logPrint("[PREFERENCES SAVE] " + String(logLabel) + " updated (hidden)");
  }
}

// Helper function to save a boolean preference if the corresponding argument is present
void savePrefBool(
  const char* argName,
  const char* prefKey,
  bool& targetVar,
  bool logValue = true,
  const char* logLabel = nullptr
) {
  if (!server.hasArg(argName)) return;

  String val = server.arg(argName);

  // HTML Checkbox: "on", "1", "true"
  targetVar = (val == "1" || val == "on" || val == "true");

  preferences.putBool(prefKey, targetVar);

  if (logLabel == nullptr) logLabel = prefKey;

  if (logValue) {
    logPrint("[PREFERENCES SAVE] " + String(logLabel) +
             " = " + String(targetVar ? "true" : "false"));
  } else {
    logPrint("[PREFERENCES SAVE] " + String(logLabel) + " updated (hidden)");
  }
}

void loadPrefInt(
  const char* prefKey,
  int& targetVar,
  int defaultValue,
  bool logValue,
  const char* logLabel
) {
  targetVar = preferences.getInt(prefKey, defaultValue);

  if (logLabel == nullptr) logLabel = prefKey;

  if (logValue) {
    logPrint("[PREFERENCES LOAD] " + String(logLabel) + " read = " + String(targetVar));
  } else {
    logPrint("[PREFERENCES LOAD] " + String(logLabel) + " read (hidden)");
  }
}

void loadPrefFloat(
  const char* prefKey,
  float& targetVar,
  float defaultValue,
  bool logValue,
  const char* logLabel,
  uint8_t decimals
) {
  targetVar = preferences.getFloat(prefKey, defaultValue);

  if (logLabel == nullptr) logLabel = prefKey;

  if (logValue) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, targetVar);
    logPrint("[PREFERENCES LOAD] " + String(logLabel) + " read = " + String(buf));
  } else {
    logPrint("[PREFERENCES LOAD] " + String(logLabel) + " read (hidden)");
  }
}

void loadPrefBool(
  const char* prefKey,
  bool& targetVar,
  bool defaultValue,
  bool logValue,
  const char* logLabel
) {
  targetVar = preferences.getBool(prefKey, defaultValue);

  if (logLabel == nullptr) logLabel = prefKey;

  if (logValue) {
    logPrint("[PREFERENCES LOAD] " + String(logLabel) + " read = " + String(targetVar ? "true" : "false"));
  } else {
    logPrint("[PREFERENCES LOAD] " + String(logLabel) + " read (hidden)");
  }
}

void loadPrefString(
  const char* prefKey,
  String& targetVar,
  const char* defaultValue,
  bool logValue,
  const char* logLabel
) {
  targetVar = preferences.getString(prefKey, defaultValue);

  if (logLabel == nullptr) logLabel = prefKey;

  if (logValue) {
    logPrint("[PREFERENCES LOAD] " + String(logLabel) + " read = " + targetVar);
  } else {
    logPrint("[PREFERENCES LOAD] " + String(logLabel) + " read (hidden)");
  }
}

void handleSaveRunsettings() {
  // Open the Preferences namespace with write access (readOnly = false)
  // Only call begin() once — calling it twice can cause writes to fail!
  if (!preferences.begin(PREF_NS, false)) {
    logPrint("[PREF][ERROR] preferences.begin() failed. "
             "Check that PREF_NS length <= 15 characters.");
    server.send(500, "text/plain", "Failed to open Preferences");
    return;
  }

  // Save all run settings
  savePrefString("webStartDate", KEY_STARTDATE, startDate, "Grow Start Date");
  savePrefString("webFloweringStart", KEY_FLOWERDATE, startFlowering, "Flowering Start Date");
  savePrefString("webDryingStart", KEY_DRYINGDATE, startDrying, "Drying Start Date");
  savePrefInt("webCurrentPhase", KEY_CURRENTPHASE, curPhase, "Current Phase");
  savePrefFloat("webTargetTemp", KEY_TARGETTEMP, targetTemperature, "Target Temperature");
  savePrefFloat("webTargetVPD", KEY_TARGETVPD, targetVPD, "Target VPD");
  savePrefFloat("webOffsetLeafTemp", KEY_LEAFTEMP, offsetLeafTemperature, "Leaf Temperature Offset");
  savePrefString("webLightOnTime", KEY_LIGHT_ON_TIME, lightOnTime, "Grow Light On Time");
  savePrefInt("webLightDayHours", KEY_LIGHT_DAY_HOURS, lightDayHours, "Grow Light Day Hours");
  savePrefInt("webHeatingRelay", KEY_HEATING_RELAY, heatingRelay, "Heating Relay Pin");

  preferences.end(); // always close Preferences handle

  // Send redirect response and restart the ESP
  server.sendHeader("Location", "/");
  server.send(303);  // HTTP redirect to status page
  delay(250);
  ESP.restart();
}

// Handle Shelly settings save

void handleNewGrow() {
  if (!preferences.begin(PREF_NS, false)) {
    logPrint("[PREF][ERROR] preferences.begin() failed (newGrow)");
    server.send(500, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"prefs\"}");
    return;
  }

  // Today in local time: YYYY-MM-DD
  char dateBuf[11] = {0};
  time_t now = time(nullptr);
  struct tm tmNow {};
  localtime_r(&now, &tmNow);
  snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
           tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);
  const String today(dateBuf);

  // Reset grow dates/phases
  startDate = today;
  startFlowering = "";
  startDrying = "";
  curPhase = 1; // Vegetativ

  preferences.putString(KEY_STARTDATE, startDate);
  preferences.putString(KEY_FLOWERDATE, startFlowering);
  preferences.putString(KEY_DRYINGDATE, startDrying);
  preferences.putInt(KEY_CURRENTPHASE, curPhase);

  // Reset energy display: store current totals as offset (so UI shows 0)
  // Then try to reset device counters (Gen2), if successful -> offset can be 0.
  auto resetOne = [&](ShellyDevice &d, const char* keyOff) -> bool {
    if (d.ip.length() == 0) return false;

    // read current raw total (offset currently applied inside getShellyValues, so temporarily disable)
    const float prevOff = d.energyOffsetWh;
    d.energyOffsetWh = 0.0f;
    ShellyValues v = getShellyValues(d, 0, 80);
    float rawWh = (!isnan(v.energyWh) ? v.energyWh : 0.0f);

    // baseline offset to show 0
    d.energyOffsetWh = rawWh;
    preferences.putFloat(keyOff, d.energyOffsetWh);

    // try to reset counters on device; if succeeds -> keep offset 0
    bool resetOk = shellyResetEnergyCounters(d, 0, 80);
    if (resetOk) {
      d.energyOffsetWh = 0.0f;
      preferences.putFloat(keyOff, 0.0f);
    }

    // refresh values
    return resetOk;
  };

  bool okMain = resetOne(settings.shelly.main, KEY_SHELLYMAINOFF);
  bool okLight= resetOne(settings.shelly.light,KEY_SHELLYLIGHTOFF);

  // Response
  String resp = "{";
  resp += "\"ok\":true,";
  resp += "\"startDate\":\"" + startDate + "\",";
  resp += "\"reset\":{";
  resp += "\"main\":" + String(okMain ? "true" : "false") + ",";
  resp += "\"light\":" + String(okLight ? "true" : "false") + ",";
  resp += "}}";

  server.send(200, "application/json; charset=utf-8", resp);
}

void handleSaveShellySettings() {

  if (!preferences.begin(PREF_NS, false)) {
    server.send(500, "text/plain", "Failed to open Preferences");
    return;
  }

  // --- MAIN ---
  // IPv4-only: normalize/validate IP strings before saving
  auto normalizeIPv4 = [](String& ip) {
    ip.trim();
    if (ip.length() == 0) return;
    IPAddress tmp;
    if (!tmp.fromString(ip)) {
      // invalid -> clear to avoid saving DNS/IPv6/garbage
      ip = "";
      return;
    }
    ip = tmp.toString();
  };

  // keep Settings as source of truth
  normalizeIPv4(settings.shelly.main.ip);
  savePrefString("webShellyMainIP",   KEY_SHELLYMAINIP,   settings.shelly.main.ip,   "Main IP");
  savePrefInt   ("webShellyMainGen",  KEY_SHELLYMAINGEN,  settings.shelly.main.gen,  "Main Gen");

  // --- LIGHT ---
  normalizeIPv4(settings.shelly.light.ip);
  savePrefString("webShellyLightIP",   KEY_SHELLYLIGHTIP,   settings.shelly.light.ip,   "Light IP");
  savePrefInt   ("webShellyLightGen",  KEY_SHELLYLIGHTGEN,  settings.shelly.light.gen,  "Light Gen");

  // --- AUTH ---
  savePrefString("webShellyUsername", KEY_SHELLYUSERNAME, settings.shelly.username, "User");
  savePrefString("webShellyPassword", KEY_SHELLYPASSWORD, settings.shelly.password, "Pass");

  preferences.end();

  settings.grow.lightOnTime = lightOnTime;
  settings.grow.lightDayHours = lightDayHours;
  applyGrowLightSchedule();

  server.sendHeader("Location", "/");
  server.send(303);
  delay(250);
  ESP.restart();
}

// Helper function to save a string preference to a C-style string if the corresponding argument is present
void savePrefStringToCString(
  const char* argName,
  const char* prefKey,
  char*& targetPtr,
  bool logValue = true,
  const char* logLabel = nullptr
) {
  if (!server.hasArg(argName)) return;

  String v = server.arg(argName);
  preferences.putString(prefKey, v);

  // 🔥 alten Speicher freigeben
  if (targetPtr != nullptr) {
    free(targetPtr);
    targetPtr = nullptr;
  }

  targetPtr = strdup(v.c_str());

  if (logLabel == nullptr) logLabel = prefKey;

  if (logValue) {
    logPrint("[PREFERENCES] " + String(logLabel) + " = " + v);
  } else {
    logPrint("[PREFERENCES] " + String(logLabel) + " updated");
  }
}

// Handle general settings save
void handleSaveSettings() {
  // Open the Preferences namespace with write access (readOnly = false)
  // Only call begin() once — calling it twice can cause writes to fail!
  if (!preferences.begin(PREF_NS)) {
    logPrint("[PREFERENCES][ERROR] preferences.begin() failed. "
             "Check that PREF_NS length <= 15 characters.");
    server.send(500, "text/plain", "Failed to open Preferences");
    return;
  }

  savePrefString("webBoxName", KEY_NAME, boxName, "Boxname");
  savePrefString("webNTPServer", KEY_NTPSRV, ntpServer);
  savePrefString("webTimeZoneInfo", KEY_TZINFO, tzInfo);
  savePrefString("webLanguage", KEY_LANG, language);
  savePrefString("webTheme", KEY_THEME, theme);
  savePrefString("webTimeFormat", KEY_TFMT, timeFormat);
  savePrefString("webTempUnit", KEY_UNIT, unit);
  savePrefString("webDS18B20Name", KEY_DS18NAME, DS18B20Name);
  savePrefBool("webDS18B20Enable", KEY_DS18B20ENABLE, DS18B20, "DS18B20 Enable");
  savePrefString("webDS18B20Name", KEY_DS18NAME, DS18B20Name);
  savePrefString("webRelayName1", KEY_RELAY_1, relayNames[0], "Relay 1 Name");
  savePrefString("webRelayName2", KEY_RELAY_2, relayNames[1], "Relay 2 Name");
  savePrefString("webRelayName3", KEY_RELAY_3, relayNames[2], "Relay 3 Name");
  savePrefString("webRelayName4", KEY_RELAY_4, relayNames[3], "Relay 4 Name");

  preferences.end(); // always close Preferences handle

  // 11) Send redirect response and restart the ESP
  server.sendHeader("Location", "/");
  server.send(303);  // HTTP redirect to status page
  delay(250);
  ESP.restart();
}

void handleSaveMessageSettings() {
  if (!preferences.begin(PREF_NS)) {
    logPrint("[PREFERENCES][ERROR] preferences.begin() failed. "
             "Check that PREF_NS length <= 15 characters.");
    server.send(500, "text/plain", "Failed to open Preferences");
    return;
  }

  if (server.arg("webPushoverEnabled") == "on") {
    pushoverEnabled = "checked";
  } else {
    pushoverEnabled = "";
  }

  savePrefString("webPushoverUserKey", KEY_PUSHOVERUSER, pushoverUserKey, "Pushover User Key");
  savePrefString("webPushoverAppKey", KEY_PUSHOVERAPP, pushoverAppKey, "Pushover App Key");
  savePrefString("webPushoverDevice", KEY_PUSHOVERDEVICE, pushoverDevice, "Pushover Device");
  
  if (server.arg("webGotifyEnabled") == "on") {
    gotifyEnabled = "checked";
  } else {
    gotifyEnabled = "";
  }
  logPrint("[PREFERENCES] Gotify " + String(gotifyEnabled));
  preferences.putString(KEY_GOTIFY, gotifyEnabled);

  savePrefString("webGotifyURL", KEY_GOTIFYSERVER, gotifyServer, "Gotify Server URL");
  savePrefString("webGotifyToken", KEY_GOTIFYTOKEN, gotifyToken, "Gotify Token");

  preferences.end(); // always close Preferences handle

  if (pushoverEnabled == "checked") {
    if (language == "de") {
      sendPushover("Testnachricht", "Die Pushover-Benachrichtigungen wurden erfolgreich eingerichtet.");
    } else {
      sendPushover("Test Message", "Pushover notifications have been set up successfully.");
    }
  }

  if (gotifyEnabled == "checked") {
    if (language == "de") {
      sendGotify("Testnachricht", "Die Gotify-Benachrichtigungen wurden erfolgreich eingerichtet.", 5);
    } else {
      sendGotify("Test Message", "Gotify notifications have been set up successfully.", 5);
    }
  }
  
  // 11) Send redirect response and restart the ESP
  server.sendHeader("Location", "/");
  server.send(303);  // HTTP redirect to status page
  delay(250);
  ESP.restart();
}

// Handle form submission save WIFI credentials
void handleSaveWiFi() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    preferences.begin(PREF_NS, false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();

    server.send(200, "text/html", "<h1>Saved! Restarting...</h1>");
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing data");
  }
}

// key, detailsJson = "{}"
void pushHintKey(const String& key, const String& detailsJson = "{}") {
  hintKey = key;
  hintDetailsJson = detailsJson;
  hintId++;
}

// API: /get hint
void handleHint() {
  // {"id":1,"key":"hint.saved","details":{"percent":12}}
  String json = "{\"id\":" + String(hintId) + ",\"key\":\"";

  // escape quotes/backslashes in key
  String escapedKey = hintKey;
  escapedKey.replace("\\", "\\\\");
  escapedKey.replace("\"", "\\\"");
  json += escapedKey + "\",\"details\":" + hintDetailsJson + "}";

  server.send(200, "application/json", json);
}

// Handle factory reset
void handleFactoryReset() {
  preferences.begin(PREF_NS, false);
  preferences.clear();  // Deletes all keys in the namespace"
  preferences.end();

  server.send(200, "text/html", "<h1>Factory reset performed. Restarting...</h1>");
  delay(2000);
  ESP.restart();
}

// initial NTP sync (called at boot)
void initialSyncBlocking() {
  logPrint("[BOOT] Initial NTP sync...");

  // Start Sync
  configTzTime(tzInfo.c_str(), ntpServer.c_str());

  struct tm local;
  unsigned long start = millis();

  // Wait up to 2 seconds for time
  while (millis() - start < 2000) {
    if (getLocalTime(&local, 50)) {
      char buf[64];
      strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &local);
      logPrint(String("[BOOT] Time initialized: ") + buf);
      return;
    }
    delay(50); // do not block completely
  }

  logPrint("[BOOT] Failed initial NTP sync");
}

// Daily NTP trigger at 01:00
void dailyNtpTrigger() {
  if (!wifiReady) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 50)) return;  // try to get time (non-blocking, max 50ms)

  if (timeinfo.tm_hour == 1 && timeinfo.tm_min == 0 && timeinfo.tm_mday != lastSyncDay) {
    logPrint("Performing daily NTP sync...");
    configTzTime(tzInfo.c_str(), ntpServer.c_str());

    ntpSyncPending = true;
    ntpStartMs = millis();

    // so it really only fires once per day:
    lastSyncDay = timeinfo.tm_mday;
  }
}

// NTP sync tick (non-blocking)
void ntpSyncTick() {
  if (!ntpSyncPending) return;

  struct tm local;
  // try to get time (non-blocking, max 50ms)
  if (getLocalTime(&local, 50)) {
    char readDate[11];
    strftime(readDate, sizeof(readDate), "%Y-%m-%d", &local);

    char buf[64];
    strftime(buf, sizeof(buf), "now: %d.%m.%y  Zeit: %H:%M:%S", &local);
    logPrint(String("[DATETIME] ") + buf);

    ntpSyncPending = false;
    return;
  }

  // timeout after 15 seconds
  if (millis() - ntpStartMs > 15000) {
    logPrint("[DATETIME] NTP sync timeout (non-blocking)");
    ntpSyncPending = false;
  }
}

// calculate elapsed days and weeks from defined date
void calculateTimeSince(const String& startDate, int &days, int &weeks) {
  // Compute "day N / week M" since a given YYYY-MM-DD (local time).
  // NOTE: weeks must be computed as ((days-1)/7)+1 to avoid an off-by-one at day 7, 14, ...
  days = 0;
  weeks = 0;

  int y = 0, m = 0, d = 0;
  if (sscanf(startDate.c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
    return;
  }

  struct tm tmStart {};
  tmStart.tm_mday = d;
  tmStart.tm_mon  = m - 1;
  tmStart.tm_year = y - 1900;
  tmStart.tm_hour = 0;
  tmStart.tm_min  = 0;
  tmStart.tm_sec  = 0;
  tmStart.tm_isdst = -1;

  const time_t startEpoch = mktime(&tmStart);
  const time_t nowEpoch   = time(nullptr);
  if (startEpoch <= 0 || nowEpoch <= 0) return;

  long diffSec = (long)(nowEpoch - startEpoch);
  if (diffSec < 0) diffSec = 0;

  days = (int)(diffSec / 86400L) + 1;
  weeks = (days > 0) ? ((days - 1) / 7 + 1) : 0;
}


// Convert minutes to milliseconds (int return type)
int minutesToMilliseconds(int minutes) {
    return minutes * 60 * 1000;
}

// Convert seconds to milliseconds (int return type)
int secondsToMilliseconds(int seconds) {
    return seconds * 1000;
}

// CSV: ts_ms,tempC,hum,vpd\n
// Append one CSV line: ts_ms,tempC,humPct,vpdKpa
void appendLog(unsigned long timestamp, float temperature, float humidity, float vpd) {
  File f = LittleFS.open(LOG_PATH, FILE_APPEND);
  if (!f) {
    logPrint("[LITTLEFS][ERROR] Failed to open log for append: " + String(LOG_PATH));
    return;
  }

  f.print(String(timestamp));
  f.print(',');
  f.print(String(temperature, 2));
  f.print(',');
  f.print(String(humidity, 2));
  f.print(',');
  f.print(String(vpd, 3));
  f.print('\n');
  f.close();
}

// Compaction: discard everything < (now-RETAIN_MS)
void compactLog() {
  const uint32_t now = millis();
  const uint32_t cutoff = (now > RETAIN_MS) ? (now - RETAIN_MS) : 0;

  File in = LittleFS.open(LOG_PATH, FILE_READ);
  if (!in) return; // nothing to do

  File out = LittleFS.open("/envlog.tmp", FILE_WRITE);
  if (!out) { in.close(); return; }

  // Copy line by line
  String line;
  while (in.available()) {
    line = in.readStringUntil('\n');
    if (line.length() < 5) continue;
    // extract ts at beginning
    int c1 = line.indexOf(',');
    if (c1 <= 0) continue;
    uint32_t ts = strtoul(line.substring(0, c1).c_str(), nullptr, 10);
    if (ts >= cutoff) {
      out.print(line); out.print('\n');
    }
  }
  in.close();
  out.close();

  LittleFS.remove(LOG_PATH);
  LittleFS.rename("/envlog.tmp", LOG_PATH);
}

inline float avgValue(float sum, uint32_t count) {
  return (count == 0) ? 0.0f : (sum / count);
}

// calculate elapsed days and weeks from defined unix timestamp
float calcVPD(float valLastTemperature,float valOffsetLeafTemperature , float valLastHumidity) {
      float FT = valLastTemperature;
      float FARH = valLastHumidity;
      float FLTO = valOffsetLeafTemperature;
      float FLT = FT + FLTO;
      float VPLEAF = (610.7 * pow(10, (7.5 * FLT) / (237.3 + FLT)) / 1000);
      float ASVPF = (610.7 * pow(10, (7.5 * FT) / (237.3 + FT)) / 1000);
      float VPAIR = (FARH / 100) * ASVPF;
      float VPD = VPLEAF - VPAIR;
      return VPD;
}

// Store new reading and update running sums
void addReading(float temp, float hum, float vpd) {
  // Remove old values from sums
  sumTemp -= temps[index_pos];
  sumHum  -= hums[index_pos];
  sumVPD  -= vpds[index_pos];
  sumWaterTemp -= waterTemps[index_pos];

  // Add new values
  temps[index_pos] = temp;
  hums[index_pos]  = hum;
  vpds[index_pos]  = vpd;
  waterTemps[index_pos] = DS18B20STemperature;

  sumTemp += temp;
  sumHum  += hum;
  sumVPD  += vpd;
  sumWaterTemp += waterTemps[index_pos];

  // Move index (circular buffer)
  index_pos = (index_pos + 1) % NUM_VALUES;

  // Count how many values are valid (up to NUM_VALUES)
  if (count < NUM_VALUES) {
    count++;
  }
}

// check HCSR04 sensor
float pingTankLevel(uint8_t trigPin, uint8_t echoPin,
                     uint32_t timeout_us = 30000,   // ~5m max, praktisch weniger
                     uint8_t samples = 3) {         // Mittelwert über N Samples
  // Pins konfigurieren (idempotent, stört nicht wenn öfter aufgerufen)
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  float sum = 0.0f;
  uint8_t ok = 0;

  for (uint8_t i = 0; i < samples; i++) {
    // Trigger-Puls (10 µs HIGH)
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    // Echo-Pulsdauer messen
    unsigned long duration = pulseIn(echoPin, HIGH, timeout_us);
    if (duration == 0) {
      // Timeout -> ungültig
      continue;
    }

    // Schallgeschwindigkeit ~343 m/s => 0.0343 cm/µs
    // Hin- und Rückweg => /2
    float cm = (duration * 0.0343f) / 2.0f;

    // Plausibilitätsfilter (HC-SR04 typ. 2..400cm)
    if (cm >= 2.0f && cm <= 400.0f) {
      sum += cm;
      ok++;
    }

    delay(10); // kleine Pause zwischen Messungen
  }

  if (ok == 0) return -1.0f;
  return sum / ok;
}

static void handleHistory() {
  // Query-Parameter
  uint32_t hours = 48;
  uint32_t maxPts = 1500;
  if (server.hasArg("hours"))  hours  = std::max<uint32_t>(1, server.arg("hours").toInt());
  if (server.hasArg("max"))    maxPts = std::max<uint32_t>(50, server.arg("max").toInt());

  uint32_t nowms = millis();
  uint32_t from  = (hours >= 596523) ? 0 : (nowms - hours * 3600UL * 1000UL); // guard overflow

  // 1. Datei öffnen und erst mal zählen (wie viele im Zeitraum?)
  auto f = LittleFS.open(LOG_PATH, FILE_READ);
  if (!f) { server.send(200, "application/json", "[]"); 
    logPrint(String("[LITTLEFS]: ") + LOG_PATH + " open failed!");
    return; 
  }

  // Zählen
  size_t count = 0;
  {
    String line;
    while (f.available()) {
      line = f.readStringUntil('\n');
      int c1 = line.indexOf(',');
      if (c1 <= 0) continue;
      uint32_t ts = strtoul(line.substring(0, c1).c_str(), nullptr, 10);
      if (ts >= from) count++;
    }
  }

  // stride berechnen
  size_t stride = (count > maxPts) ? (count / maxPts + ((count % maxPts) ? 1 : 0)) : 1;
  f.seek(0);

  // 2. JSON streamen
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "[");
  String chunk; chunk.reserve(64);

  size_t i = 0, kept = 0;
  bool first = true;

  String line;
  while (f.available()) {
    line = f.readStringUntil('\n');
    int c1 = line.indexOf(',');
    if (c1 <= 0) continue;
    uint32_t ts = strtoul(line.substring(0, c1).c_str(), nullptr, 10);
    if (ts < from) continue;

    if ((i++ % stride) != 0) continue; // ausdünnen

    // Felder parsen
    int c2 = line.indexOf(';', c1 + 1);
    int c3 = line.indexOf(';', c2 + 1);
    if (c2 < 0 || c3 < 0) continue;

    float t = atof(line.substring(c1 + 1, c2).c_str());
    float h = atof(line.substring(c2 + 1, c3).c_str());
    float v = atof(line.substring(c3 + 1).c_str());

    if (!first) server.sendContent(";");
    first = false;

    chunk = "{\"ts\":";
    chunk += String(ts);
    chunk += ";\"tempC\":";
    chunk += String(t, 2);
    chunk += ";\"hum\":";
    chunk += String(h, 0);
    chunk += ";\"vpd\":";
    chunk += String(v, 3);
    chunk += "}";
    server.sendContent(chunk);
    kept++;
  }
  f.close();
  server.sendContent("]");
}

static void handleDownloadHistory() {
  if (!LittleFS.exists(LOG_PATH)) { server.send(404, "text/plain", "No log file"); return; }
  File f = LittleFS.open(LOG_PATH, FILE_READ);
  if (!f) { server.send(500, "text/plain", "Open failed"); return; }

  server.sendHeader("Content-Type", "text/csv");
  server.sendHeader("Content-Disposition", "attachment; filename=envlog.csv");
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "text/csv");
  f.close();
}

static void handleDeleteLog() {
  if (LittleFS.exists(LOG_PATH)) {
    LittleFS.remove(LOG_PATH);
    server.send(200, "text/html", "<html><body>Gel&ouml;scht <a href=\"/\">Back</a></body></html>");
    logPrint("[WEB] CSV deleted: " + String(LOG_PATH));
  } else {
    server.send(404, "text/html", "<html><body>No CSV found. <a href=\"/\">Back</a></body></html>");
  }
}

void handleApiLogBuffer() {
  String txt; txt.reserve(4096);
  for (const auto& line : logBuffer) {
    txt += line; txt += '\n';
  }
  server.send(200, "text/plain; charset=utf-8", txt);
}

void handleClearLog() {
  logBuffer.clear();
  server.send(204); // No Content
}

void handleDownloadLog() {
  String txt; txt.reserve(8192);
  for (const auto& line : logBuffer) { txt += line; txt += '\n'; }
  server.sendHeader("Content-Type", "text/plain; charset=utf-8");
  server.sendHeader("Content-Disposition", "attachment; filename=weblog.txt");
  server.send(200, "text/plain; charset=utf-8", txt);
}

// helper: read actual relay pin and convert to bool  
bool isRelayOn(int idx) {
  return digitalRead(relayPins[idx]) == HIGH;
}

// set relay state by index
void setRelay(int idx, bool on) {
  if (idx < 0 || idx >= NUM_RELAYS) return;
  digitalWrite(relayPins[idx], on ? HIGH : LOW); // <-- so!
  relayStates[idx] = on;
}

// toggle relay state by index and return new state as JSON
void handleRelayToggleIdx(int idx) {
  bool cur = isRelayOn(idx);
  bool next = !cur;
  setRelay(idx, next);

  String res = "{";
  res += "\"id\":" + String(idx + 1);
  res += ",\"state\":" + String(next ? "true" : "false");
  res += "}";
  server.send(200, "application/json", res);
}

void handleRelayIrrigationIdx(int idx) {
  setRelay(idx, true);
  relayOffTime[idx] = millis() + 10000;
  relayActive[idx] = true;

  String res = "{";
  res += "\"id\":" + String(idx + 1);
  res += ",\"state\":true";
  res += "}";
  server.send(200, "application/json", res);
}

// Send notification via Pushover
bool sendPushover(const String& message, const String& title) {
  if (pushoverSent) {
    WiFiClientSecure client;
    client.setInsecure(); // simpel & schnell (ohne Zertifikatsprüfung)

    HTTPClient https;
    if (!https.begin(client, "https://api.pushover.net/1/messages.json")) {
      return false;
    }

    https.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String body =
      "token=" + String(pushoverAppKey) +
      "&user="  + String(pushoverUserKey) +
      "&device=" + String(pushoverDevice) +
      "&title=" + title +
      "&priority=1" +
      "&message=" + message;

    int code = https.POST(body);
    String resp = https.getString();
    https.end();

    logPrint("[MESSAGE] Pushover HTTP send code: " + String(code));
    logPrint("[MESSAGE] " + resp);

    return (code == 200);
  } else {
    logPrint("[MESSAGE] Pushover notification not sent: disabled");
    return false;
  }
}

// Send notification via Gotify
bool sendGotify(const String& msg, const String& title, int priority) {
  if (gotifySent) {
    String GOTIFY_URL = String("https://") + gotifyServer + "/message?token=" + gotifyToken;
  
    // priority: -2 (lowest) to 10 (highest)
    WiFiClientSecure client;
    client.setInsecure(); // schnell & unkompliziert (ohne Zertifikatsprüfung)

    HTTPClient http;
    if (!http.begin(client, GOTIFY_URL.c_str())) return false;

    http.addHeader("Content-Type", "application/json");

    // JSON Body
    String body = "{";
    body += "\"title\":\"" + title + "\",";
    body += "\"message\":\"" + msg + "\",";
    body += "\"priority\":" + String(priority);
    body += "}";

    int code = http.POST(body);
    String resp = http.getString();
    http.end();

    logPrint("[MESSAGE] Gotify HTTP send code: " + String(code));
    logPrint("[MESSAGE] " + resp);
    return (code >= 200 && code < 300);
  } else {
    logPrint("[MESSAGE] Gotify notification not sent: disabled");
    return false;
  }
  
}

// =======================
// URL HELPERS
// =======================

// Build base URL without credentials (safe for logging)
static String makeBaseUrl(const String& host, uint16_t port) {
  String u = "http://" + host;
  if (port != 80) u += ":" + String(port);
  return u;
}

// =======================
// HASH HELPERS (Digest)
// =======================

// Compute hash (MD5/SHA-256) and return lowercase hex
static String hashHex(mbedtls_md_type_t mdType, const String& s) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(mdType);
  if (!info) return "";

  unsigned char out[64];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, info, 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char*)s.c_str(), s.length());
  mbedtls_md_finish(&ctx, out);
  mbedtls_md_free(&ctx);

  const size_t outLen = mbedtls_md_get_size(info);
  static const char* hex = "0123456789abcdef";
  String res;
  res.reserve(outLen * 2);
  for (size_t i = 0; i < outLen; i++) {
    res += hex[(out[i] >> 4) & 0xF];
    res += hex[out[i] & 0xF];
  }
  return res;
}

static String toLowerCopy(String s) { s.toLowerCase(); return s; }

// Extract parameter value from Digest header, supports:
// key="value"  OR  key=value
static String digestParam(const String& header, const String& key) {
  String h = header;
  h.trim();
  String hl = toLowerCopy(h);

  String kl = key;
  kl.trim();
  kl.toLowerCase();

  int p = hl.indexOf(kl + "=");
  if (p < 0) return "";

  p += kl.length() + 1; // skip "key="
  if (p >= h.length()) return "";

  // quoted value
  if (h[p] == '"') {
    p++;
    int e = h.indexOf('"', p);
    if (e < 0) return "";
    return h.substring(p, e);
  }

  // unquoted value until comma
  int e = h.indexOf(',', p);
  if (e < 0) e = h.length();
  String v = h.substring(p, e);
  v.trim();
  return v;
}

static String makeCnonce() {
  // Simple pseudo-random cnonce, good enough for LAN usage
  uint32_t r1 = (uint32_t)esp_random();
  uint32_t r2 = (uint32_t)esp_random();
  char buf[17];
  snprintf(buf, sizeof(buf), "%08lx%08lx", (unsigned long)r1, (unsigned long)r2);
  return String(buf);
}

// Build Digest Authorization header for RFC7616 (SHA-256) and MD5 (fallback)
// Supports "-sess" variants as well.
static String buildDigestAuth(const String& wwwAuth,
                              const String& user,
                              const String& pass,
                              const String& method,
                              const String& uri) {
  String realm  = digestParam(wwwAuth, "realm");
  String nonce  = digestParam(wwwAuth, "nonce");
  String qop    = digestParam(wwwAuth, "qop");
  String opaque = digestParam(wwwAuth, "opaque");
  String alg    = digestParam(wwwAuth, "algorithm"); // e.g. SHA-256, SHA-256-sess, MD5

  if (realm.length() == 0 || nonce.length() == 0) return "";

  bool sess = false;
  String algLower = toLowerCopy(alg);
  if (algLower.endsWith("-sess")) {
    sess = true;
    algLower.replace("-sess", "");
  }

  mbedtls_md_type_t mdType = MBEDTLS_MD_MD5;
  if (algLower == "sha-256") mdType = MBEDTLS_MD_SHA256;
  else if (algLower == "md5" || algLower.length() == 0) mdType = MBEDTLS_MD_MD5;

  // Prefer qop=auth if offered
  String qopUse = (toLowerCopy(qop).indexOf("auth") >= 0) ? "auth" : "";

  String nc = "00000001";
  String cnonce = makeCnonce();

  // HA1 = H(user:realm:pass)
  String ha1 = hashHex(mdType, user + ":" + realm + ":" + pass);
  // HA1-sess = H(HA1:nonce:cnonce)
  if (sess) {
    ha1 = hashHex(mdType, ha1 + ":" + nonce + ":" + cnonce);
  }

  // HA2 = H(method:uri)
  String ha2 = hashHex(mdType, method + ":" + uri);

  // response
  String resp;
  if (qopUse.length()) {
    resp = hashHex(mdType, ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qopUse + ":" + ha2);
  } else {
    resp = hashHex(mdType, ha1 + ":" + nonce + ":" + ha2);
  }

  // Build header line
  String h = "Authorization: Digest ";
  h += "username=\"" + user + "\", ";
  h += "realm=\"" + realm + "\", ";
  h += "nonce=\"" + nonce + "\", ";
  h += "uri=\"" + uri + "\", ";
  if (alg.length()) h += "algorithm=" + alg + ", ";

  if (qopUse.length()) {
    h += "response=\"" + resp + "\", ";
    h += "qop=" + qopUse + ", ";
    h += "nc=" + nc + ", ";
    h += "cnonce=\"" + cnonce + "\", ";
  } else {
    h += "response=\"" + resp + "\", ";
  }

  if (opaque.length()) h += "opaque=\"" + opaque + "\", ";

  if (h.endsWith(", ")) h.remove(h.length() - 2);
  h += "\r\n";
  return h;
}

// =======================
// HTTP HELPERS
// =======================

// Fetch WWW-Authenticate header using HTTPClient (no auth).
// This is used to discover whether the device requires Digest or Basic.
static bool fetchWwwAuthenticate(const String& url, String& outWwwAuth, int& outCode) {
  outWwwAuth = "";
  outCode = -1;

  HTTPClient http;
  http.setTimeout(4000);
  http.useHTTP10(true);

  const char* keys[] = {"WWW-Authenticate"};
  http.collectHeaders(keys, 1);

  if (!http.begin(url)) return false;

  int code = http.GET();
  outCode = code;

  if (code == 401) {
    outWwwAuth = http.header("WWW-Authenticate");
  }

  http.end();
  return true;
}

// Read the complete HTTP response (status line + headers + body)
static bool readAllFromClient(WiFiClient& client, String& outRaw) {
  outRaw = "";
  unsigned long start = millis();

  // Wait for first bytes (up to 2s)
  while (!client.available() && client.connected() && (millis() - start) < 2000) {
    delay(5);
  }

  // Read until connection closes or timeout (4s max)
  start = millis();
  while ((client.connected() || client.available()) && (millis() - start) < 4000) {
    while (client.available()) {
      outRaw += client.readString();
      start = millis(); // reset timeout while data arrives
    }
    delay(5);
  }

  return outRaw.length() > 0;
}

// Parse HTTP status code and body from a raw response string
static bool parseHttpResponse(const String& raw, int& outCode, String& outBody) {
  outCode = -1;
  outBody = "";

  int sep = raw.indexOf("\r\n\r\n");
  if (sep < 0) sep = raw.indexOf("\n\n");
  if (sep < 0) {
    // No headers found, treat everything as body
    outBody = raw;
    return true;
  }

  String head = raw.substring(0, sep);
  outBody = raw.substring(sep + ((raw[sep] == '\r') ? 4 : 2));

  // Status line is first header line
  int lineEnd = head.indexOf("\r\n");
  if (lineEnd < 0) lineEnd = head.indexOf('\n');
  String statusLine = (lineEnd >= 0) ? head.substring(0, lineEnd) : head;
  statusLine.trim();

  int sp1 = statusLine.indexOf(' ');
  int sp2 = statusLine.indexOf(' ', sp1 + 1);
  if (sp1 > 0 && sp2 > sp1) {
    outCode = statusLine.substring(sp1 + 1, sp2).toInt();
  }

  return true;
}

// Low-level GET using WiFiClient, with optional Authorization header line.
static bool rawHttpGet(const String& host, uint16_t port, const String& path,
                       const String& authHeaderLine,
                       int& outCode, String& outBody) {
  outCode = -1;
  outBody = "";

  WiFiClient client;
  client.setTimeout(4); // seconds

  if (!client.connect(host.c_str(), port)) {
    return false;
  }

  String p = path;
  if (!p.startsWith("/")) p = "/" + p;

  // Use HTTP/1.0 to avoid chunked transfer encoding
  String req =
    "GET " + p + " HTTP/1.0\r\n"
    "Host: " + host + "\r\n" +
    authHeaderLine +
    "Connection: close\r\n\r\n";

  client.print(req);

  String raw;
  bool ok = readAllFromClient(client, raw);
  client.stop();
  if (!ok) return false;

  parseHttpResponse(raw, outCode, outBody);
  return true;
}



// Low-level HTTP request using WiFiClient, with optional Authorization header line.
static bool rawHttpRequest(const String& method,
                           const String& host, uint16_t port,
                           const String& path,
                           const String& authHeaderLine,
                           const String& contentType,
                           const String& body,
                           int& outCode, String& outBody) {
  outCode = -1;
  outBody = "";

  WiFiClient client;
  client.setTimeout(6); // seconds

  if (!client.connect(host.c_str(), port)) {
    return false;
  }

  String p = path;
  if (!p.startsWith("/")) p = "/" + p;

  String req = method + " " + p + " HTTP/1.0\r\n"
               "Host: " + host + "\r\n" +
               authHeaderLine;

  if (method == "POST" || method == "PUT" || method == "PATCH") {
    const String ct = contentType.length() ? contentType : String("application/json");
    req += "Content-Type: " + ct + "\r\n";
    req += "Content-Length: " + String(body.length()) + "\r\n";
  }

  req += "Connection: close\r\n\r\n";

  client.print(req);
  if (method == "POST" || method == "PUT" || method == "PATCH") {
    client.print(body);
  }

  String raw;
  bool ok = readAllFromClient(client, raw);
  client.stop();
  if (!ok) return false;

  parseHttpResponse(raw, outCode, outBody);
  return true;
}

// Auto-auth request (GET/POST): first fetch WWW-Authenticate, then perform Digest/BASIC accordingly
static bool parseHttpUrl(const String& url, String& host, uint16_t& port, String& path) {
  String u = url;
  u.trim();
  if (u.startsWith("http://")) {
    u.remove(0, 7);
    port = 80;
  } else if (u.startsWith("https://")) {
    // NOTE: raw WiFiClient (no TLS) can't handle https. Use http on Shelly LAN.
    u.remove(0, 8);
    port = 443;
  } else {
    port = 80;
  }

  int slash = u.indexOf('/');
  String hostPort = (slash >= 0) ? u.substring(0, slash) : u;
  path = (slash >= 0) ? u.substring(slash) : "/";

  int colon = hostPort.indexOf(':');
  if (colon >= 0) {
    host = hostPort.substring(0, colon);
    port = (uint16_t) hostPort.substring(colon + 1).toInt();
  } else {
    host = hostPort;
  }

  host.trim();
  if (host.length() == 0) return false;
  if (path.length() == 0) path = "/";
  return true;
}

static String buildBasicAuthHeaderLine(const String& user, const String& pass) {
  // mbedTLS base64 encoder (avoid dependency on non-standard Arduino base64 libs)
  const String token = user + ":" + pass;

  // Base64 length: 4 * ceil(n/3) + 1 for NUL
  const size_t inLen  = (size_t)token.length();
  const size_t outCap = 4 * ((inLen + 2) / 3) + 1;
  unsigned char* out = (unsigned char*)malloc(outCap);
  if (!out) return "";

  size_t outLen = 0;
  const int rc = mbedtls_base64_encode(out, outCap, &outLen,
                                      (const unsigned char*)token.c_str(), inLen);
  if (rc != 0) {
    free(out);
    return "";
  }
  out[outLen] = 0;

  String line = "Authorization: Basic " + String((const char*)out) + "\r\n";
  free(out);
  return line;
}

static bool httpRequestWithDigestAutoAuth(const String& method,
                                          const String& host, uint16_t port,
                                          const String& path,
                                          const String& user, const String& pass,
                                          const String& contentType,
                                          const String& body,
                                          int& outCode,
                                          String& outBody,
                                          uint32_t timeoutMs = 4000) {
  // 1) Try without auth first
  if (rawHttpRequest(method, host, port, path, "", contentType, body, outCode, outBody)) {
    if (outCode != 401) return (outCode >= 200 && outCode < 300);
  }

  // 2) Ask for WWW-Authenticate via lightweight GET
  String www;
  const String url = String("http://") + host + ":" + String(port) + path;
  if (!fetchWwwAuthenticate(url, www, outCode)) return false;

  if (outCode != 401 || www.length() == 0) {
    // Not an auth challenge (or server didn't include header)
    return (outCode >= 200 && outCode < 300);
  }

  // 3) Build auth header line
  String authLine;
  if (www.startsWith("Digest")) {
    authLine = buildDigestAuth(www, user, pass, method, path); // already includes \r\n
  } else if (www.startsWith("Basic")) {
    authLine = buildBasicAuthHeaderLine(user, pass);
  } else {
    Serial.println("[SHELLY][AUTH] Unsupported auth scheme: " + www);
    return false;
  }

  // 4) Retry with auth header
  return rawHttpRequest(method, host, port, path, authLine, contentType, body, outCode, outBody);
}

// Convenience wrapper used by schedule code (URL-based POST)
static bool httpPostWithDigestAutoAuth(const String& host, uint16_t port,
                                      const String& path,
                                      const String& payload,
                                      const String& user,
                                      const String& pass,
                                      int& outCode,
                                      String& outBody,
                                      uint32_t timeoutMs = 4000) {
  return httpRequestWithDigestAutoAuth("POST", host, port, path, user, pass,
                                      "application/json", payload,
                                      outCode, outBody, timeoutMs);
}

static bool httpPostWithDigestAutoAuth(const String& url,
                                      const String& payload,
                                      const String& contentType,
                                      String& outResp,
                                      uint32_t timeoutMs,
                                      const String& user,
                                      const String& pass) {
  String host, path;
  uint16_t port = 80;
  if (!parseHttpUrl(url, host, port, path)) return false;

  int code = 0;
  String body;
  const bool ok = httpRequestWithDigestAutoAuth("POST", host, port, path, user, pass,
                                               contentType, payload, code, body, timeoutMs);
  outResp = body;
  return ok;
}

// Auto-auth GET: first fetch WWW-Authenticate, then perform Digest/BASIC accordingly
static bool httpGetWithDigestAutoAuth(const String& host, uint16_t port, const String& path,
                                      const String& user, const String& pass,
                                      int& outCode, String& outBody) {
  outCode = -1;
  outBody = "";

  // Discover auth scheme (no credentials sent here)
  String url = makeBaseUrl(host, port) + path;
  String www;
  int firstCode = -1;

  if (!fetchWwwAuthenticate(url, www, firstCode)) {
    return false;
  }

  // If endpoint is open (200), just request without auth
  if (firstCode == 200) {
    return rawHttpGet(host, port, path, "", outCode, outBody);
  }

  // If unauthorized, decide scheme
  if (firstCode != 401) {
    // Unexpected (404, 500, etc.). Still try without auth to get body.
    return rawHttpGet(host, port, path, "", outCode, outBody);
  }

  String wwwLower = toLowerCopy(www);

  if (wwwLower.startsWith("digest")) {
    // Build Digest Authorization header
    String digestLine = buildDigestAuth(www, user, pass, "GET", path);
    if (digestLine.length() == 0) return false;
    return rawHttpGet(host, port, path, digestLine, outCode, outBody);
  }

  // Basic auth (rare with Shelly Gen2/3 but supported sometimes)
  if (wwwLower.startsWith("basic")) {
    // We'll avoid putting credentials in logs; we only build the header
    // NOTE: If you need Basic, you can implement base64 header here.
    // Many Shelly Gen2/3 use Digest, so this branch is typically not needed.
    return false;
  }

  return false;
}

// =======================
// SIMPLE IP VALIDATION
// =======================
static bool hasValidIPv4(const String& ip) {
  IPAddress tmp;
  return ip.length() > 0 && tmp.fromString(ip);
}

// =======================
// SIMPLE HTTP GET FOR GEN1 DEVICES (BASIC AUTH)
// =======================
static bool httpGetGen1(
  const String& host, int port, const String& path,
  const String& user, const String& pass,
  int& code, String& body
) {
  WiFiClient client;
  HTTPClient http;

  http.setTimeout(2000); // Gen1 braucht manchmal etwas länger
  http.setReuse(false);

  String url = "http://" + host + ":" + String(port) + path;

  if (!http.begin(client, url)) {
    code = -1;
    return false;
  }

  // Nur wenn user gesetzt ist (sonst schickst du leere Auth-Header)
  if (user.length() > 0) {
    http.setAuthorization(user.c_str(), pass.c_str());  // Basic Auth
  }

  code = http.GET();
  if (code > 0) body = http.getString();

  http.end();
  return (code >= 200 && code < 300);
}

// =======================
// MAIN: GET VALUES
// =======================
ShellyValues getShellyValues(ShellyDevice& dev, int switchId, int port) {
  ShellyValues v; // default ok=false

  // Validate IP
  if (!hasValidIPv4(dev.ip)) {
    logPrint("[SHELLY] Invalid IP: '" + dev.ip + "'");
    return v;
  }

  String path = (dev.gen == 1)
    ? "/status"
    : ("/rpc/Switch.GetStatus?id=" + String(switchId));

  int code = 0;
  String body;
  bool ok = false;

  if (dev.gen == 1) {
    ok = httpGetGen1(dev.ip, port, path,
                     settings.shelly.username, settings.shelly.password,
                     code, body);
  } else {
    ok = httpGetWithDigestAutoAuth(dev.ip, port, path,
                                   settings.shelly.username, settings.shelly.password,
                                   code, body);
  }

  if (!ok) {
    logPrint("[SHELLY] request failed gen=" + String(dev.gen) +
             " HTTP=" + String(code) + " " + dev.ip + ":" + String(port) + path);
    if (body.length()) logPrint("[SHELLY] body(first200): " + body.substring(0, 200));
    return v;
  }

  // Parse JSON (Gen1 /status can be bigger)
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    logPrint("[SHELLY] JSON parse error: " + String(err.c_str()));
    return v;
  }

  if (dev.gen == 1) {
    // Gen1: relays[x].ison, meters[x].power, meters[x].total
    v.isOn     = doc["relays"][switchId]["ison"] | false;
    v.powerW   = doc["meters"][switchId]["power"] | NAN;
    v.energyWh = doc["meters"][switchId]["total"] | NAN;
  } else {
    // Gen2+: Switch.GetStatus
    v.isOn     = doc["output"] | false;
    v.powerW   = doc["apower"] | NAN;
    v.energyWh = doc["aenergy"]["total"] | NAN;
  }

  v.ok = true;
  dev.values = v;

  // ------------------------------------------------------------
  // EXTRA: Read exactly one ON and one OFF schedule (Gen2+ only).
  // Assumption: One ON time + one OFF time applies to every day.
  // Fills dev.schedules.days[0..6] with identical times.
  // Unset values are -1.
  // ------------------------------------------------------------
  if (dev.gen >= 2) {

    // Initialize schedules to "unset"
    for (int i = 0; i < 7; i++) {
      dev.schedules.days[i].onHour = -1;
      dev.schedules.days[i].onMinute = -1;
      dev.schedules.days[i].offHour = -1;
      dev.schedules.days[i].offMinute = -1;
    }

    int scode = 0;
    String sbody;

    // Use existing digest auth helper and existing credentials
    bool sok = httpGetWithDigestAutoAuth(dev.ip, port, "/rpc/Schedule.List",
                                         settings.shelly.username, settings.shelly.password,
                                         scode, sbody);

    if (!sok) {
      logPrint("[SHELLY] Schedule.List failed gen=" + String(dev.gen) +
               " HTTP=" + String(scode) + " " + dev.ip + ":" + String(port));
      if (sbody.length()) logPrint("[SHELLY] Schedule body(first200): " + sbody.substring(0, 200));
      return v; // device values are fine; schedules stay unset
    }

    JsonDocument sdoc;
    DeserializationError serr = deserializeJson(sdoc, sbody);
    if (serr) {
      logPrint("[SHELLY] Schedule JSON parse error: " + String(serr.c_str()));
      return v; // device values are fine; schedules stay unset
    }

    bool haveOn = false;
    bool haveOff = false;
    int onHour = -1, onMinute = -1;
    int offHour = -1, offMinute = -1;

    JsonArray jobs = sdoc["jobs"].as<JsonArray>();
    for (JsonObject job : jobs) {
      if (!(job["enable"] | false)) continue;

      String timespec = job["timespec"] | "";
      if (timespec.length() == 0) continue;

      // Expected cron-like format: "SEC MIN HOUR * * ..."
      // We only extract MIN and HOUR.
      int sp1 = timespec.indexOf(' ');
      if (sp1 < 0) continue;
      int sp2 = timespec.indexOf(' ', sp1 + 1);
      if (sp2 < 0) continue;
      int sp3 = timespec.indexOf(' ', sp2 + 1);
      if (sp3 < 0) continue;

      int minute = timespec.substring(sp1 + 1, sp2).toInt();
      int hour   = timespec.substring(sp2 + 1, sp3).toInt();

      // Check calls: we only care about switch.set for this switchId
      JsonArray calls = job["calls"].as<JsonArray>();
      for (JsonObject c : calls) {
        String method = c["method"] | "";
        String methodLower = method;
        methodLower.toLowerCase();

        // Shelly returns "switch.set" (lowercase) in Schedule.List
        if (methodLower != "switch.set") continue;

        int cid = c["params"]["id"] | -1;
        if (cid != switchId) continue;

        bool on = c["params"]["on"] | false;

        // First ON and first OFF win
        if (on && !haveOn) {
          haveOn = true;
          onHour = hour;
          onMinute = minute;
        } else if (!on && !haveOff) {
          haveOff = true;
          offHour = hour;
          offMinute = minute;
        }
      }

      // Stop early once both are found
      if (haveOn && haveOff) break;
    }

    // Log if schedules are missing
    if (!haveOn && !haveOff) {
      logPrint("[SHELLY] Schedule: no ON/OFF time found for switchId=" + String(switchId) +
               " on " + dev.ip + ":" + String(port));
    } else if (!haveOn) {
      logPrint("[SHELLY] Schedule: missing ON time for switchId=" + String(switchId) +
               " on " + dev.ip + ":" + String(port));
    } else if (!haveOff) {
      logPrint("[SHELLY] Schedule: missing OFF time for switchId=" + String(switchId) +
               " on " + dev.ip + ":" + String(port));
    }

    // Apply the same schedule to all days (and log what was applied)
    if (haveOn || haveOff) {
      for (int i = 0; i < 7; i++) {
        if (haveOn) {
          dev.schedules.days[i].onHour = onHour;
          dev.schedules.days[i].onMinute = onMinute;
        }
        if (haveOff) {
          dev.schedules.days[i].offHour = offHour;
          dev.schedules.days[i].offMinute = offMinute;
        }
      }

      // Build time strings with leading zeros for minutes
      String onStr  = haveOn
        ? (String(onHour)  + ":" + (onMinute  < 10 ? "0" : "") + String(onMinute))
        : "unset";

      String offStr = haveOff
        ? (String(offHour) + ":" + (offMinute < 10 ? "0" : "") + String(offMinute))
        : "unset";

      logPrint("[SHELLY] Schedule applied for switchId=" + String(switchId) +
               " on " + dev.ip + ":" + String(port) +
               " ON=" + onStr + " OFF=" + offStr + " (all days)");
    }
  }

  return v;
}

// =======================
// Reset Shelly energy counters (best-effort).
// Gen2: Switch.ResetCounters / PM1.ResetCounters
// Gen1: there is no universal reset for Plug/1PM; we keep a local offset instead.
// =======================
static bool shellyResetEnergyCounters(ShellyDevice &dev, uint8_t switchId = 0, uint16_t port = 80) {
  if (WiFi.status() != WL_CONNECTED) return false;

  int code = 0;
  String body;

  auto callGen2 = [&](const String& path) -> bool {
    bool ok = httpGetWithDigestAutoAuth(dev.ip, port, path,
                                       settings.shelly.username, settings.shelly.password,
                                       code, body);
    return ok && code == 200;
  };

  auto callGen1 = [&](const String& path) -> bool {
    bool ok = httpGetGen1(dev.ip, port, path,
                         settings.shelly.username, settings.shelly.password,
                         code, body);
    return ok && code == 200;
  };

  if (dev.gen >= 2) {
    // Try Switch.ResetCounters first (works on many Plus/Pro switch devices)
    if (callGen2("/rpc/Switch.ResetCounters?id=" + String(switchId))) return true;
    // Some devices expose metering via PM1 component
    if (callGen2("/rpc/PM1.ResetCounters?id=" + String(switchId))) return true;
    return false;
  }

  // Gen1 best-effort for EM/3EM style devices (if present)
  // (Plug/1PM typically has no supported reset endpoint; offset will handle display reset)
  if (callGen1("/reset_data")) return true;
  return false;
}

// =======================
// Uses the same Digest auto-auth mechanism.
// =======================
static bool shellySwitchSet(const String& host, uint8_t gen, bool on, uint8_t switchId = 0, uint16_t port = 80) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String path;
  if (gen == 1) {
    path = "/relay/" + String(switchId) + "?turn=" + String(on ? "on" : "off");
  } else {
    path = "/rpc/Switch.Set?id=" + String(switchId) + "&on=" + String(on ? "true" : "false");
  }

  logPrint("[SHELLY] SET " + host + ":" + String(port) + " " + path);

  int code = -1;
  String body;

  bool ok = httpGetWithDigestAutoAuth(host, port, path,
                                     settings.shelly.username,
                                     settings.shelly.password,
                                     code, body);
  logPrint("[SHELLY] HTTP=" + String(code) + " bodyLen=" + String(body.length()));
  return ok && (code == 200);
}

static bool shellySwitchOn(const String& host, uint8_t gen, uint8_t switchId = 0, uint16_t port = 80) {
  return shellySwitchSet(host, gen, true, switchId, port);
}

static bool shellySwitchOff(const String& host, uint8_t gen, uint8_t switchId = 0, uint16_t port = 80) {
  return shellySwitchSet(host, gen, false, switchId, port);
}

// --- GrowLight schedule support -------------------------------------------------
// Encodes query string parts (for Gen1 schedule_rules)
static String _urlEncodeQS(const String& s){
  String o; o.reserve(s.length()*3);
  const char* hex = "0123456789ABCDEF";
  for (size_t i=0; i<s.length(); i++) {
    const uint8_t c = (uint8_t)s[i];
    const bool ok = (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.' || c=='~';
    if (ok) {
      o += (char)c;
    } else {
      o += '%';
      o += hex[(c >> 4) & 0xF];
      o += hex[c & 0xF];
    }
  }
  return o;
}

static bool _parseHHMM(const String& hhmm, int& h, int& m){
  if (hhmm.length() < 4) return false;
  int colon = hhmm.indexOf(':');
  if (colon > 0) {
    h = hhmm.substring(0, colon).toInt();
    m = hhmm.substring(colon + 1).toInt();
  } else {
    h = hhmm.substring(0, 2).toInt();
    m = hhmm.substring(2, 4).toInt();
  }
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  return true;
}

static String _fmtHHMM(int h, int m){
  char b[6];
  snprintf(b, sizeof(b), "%02d:%02d", h, m);
  return String(b);
}

static String _fmtRuleTime4(int h, int m){
  char b[5];
  snprintf(b, sizeof(b), "%02d%02d", h, m);
  return String(b);
}

// Applies a daily ON/OFF schedule directly to the Shelly (so it can run autonomously).
// Gen1: /settings/relay/0?schedule=true&schedule_rules=....
// Gen2+: /rpc/Schedule.DeleteAll + /rpc/Schedule.Create (2 jobs)
static bool applyShellyLightSchedule(const String& onTimeHHMM, int dayHours){
  const String ip = settings.shelly.light.ip;
  if (!ip.length()) {
    Serial.println("[SHELLY][LIGHT][SCHEDULE] No IP configured");
    return false;
  }

  int h = 0, mi = 0;
  if (!_parseHHMM(onTimeHHMM, h, mi)) {
    Serial.println("[SHELLY][LIGHT][SCHEDULE] Invalid onTime");
    return false;
  }

  int dh = dayHours;
  if (dh < 1) dh = 1;
  if (dh > 20) dh = 20;

  const int offH = (h + dh) % 24;
  const int offM = mi;
  const String offTime = _fmtHHMM(offH, offM);

  const uint8_t gen = settings.shelly.light.gen ? settings.shelly.light.gen : 1;
  const String user = settings.shelly.username;
  const String pass = settings.shelly.password;

  int status = 0;
  String body;

  if (gen <= 1) {
    const String on4  = _fmtRuleTime4(h, mi);
    const String off4 = _fmtRuleTime4(offH, offM);
    const String rules = on4 + "-0123456-on," + off4 + "-0123456-off";
    const String path = String("/settings/relay/0?schedule=true&schedule_rules=") + _urlEncodeQS(rules);

    const bool ok = httpGetWithDigestAutoAuth(ip, 80, path, user, pass, status, body);
    Serial.printf("[SHELLY][LIGHT][SCHEDULE] Gen1 set rules=%s status=%d ok=%d\n", rules.c_str(), status, (int)ok);
    return ok && (status >= 200 && status < 300);
  }

  // Gen2+: clear + create two schedule jobs
  const bool okDel = httpPostWithDigestAutoAuth(ip, 80, "/rpc/Schedule.DeleteAll", "{}", user, pass, status, body);
  Serial.printf("[SHELLY][LIGHT][SCHEDULE] Gen2 DeleteAll status=%d ok=%d\n", status, (int)okDel);

  auto mkCreate = [&](bool turnOn, const String& t)->String {
    const String timespec = t + " * * SUN,MON,TUE,WED,THU,FRI,SAT";
    const String payload = String("{\"enable\":true,\"timespec\":\"") + timespec + "\",\"calls\":[{\"method\":\"Switch.Set\",\"params\":{\"id\":0,\"on\":" + (turnOn ? "true" : "false") + "}}]}";
    return payload;
  };

  const String createOn  = mkCreate(true, onTimeHHMM);
  const String createOff = mkCreate(false, offTime);

  const bool ok1 = httpPostWithDigestAutoAuth(ip, 80, "/rpc/Schedule.Create", createOn, user, pass, status, body);
  Serial.printf("[SHELLY][LIGHT][SCHEDULE] Gen2 Create ON %s status=%d ok=%d\n", onTimeHHMM.c_str(), status, (int)ok1);

  const bool ok2 = httpPostWithDigestAutoAuth(ip, 80, "/rpc/Schedule.Create", createOff, user, pass, status, body);
  Serial.printf("[SHELLY][LIGHT][SCHEDULE] Gen2 Create OFF %s status=%d ok=%d\n", offTime.c_str(), status, (int)ok2);

  return okDel && ok1 && ok2;
}

// Apply the configured growlight schedule to the Shelly "light" device.
// Uses:
//   - settings.grow.lightOnTime  ("HH:MM")
//   - settings.grow.lightDayHours (1..20)
// Derives OFF time = ON + dayHours (wrap around midnight).
static bool applyGrowLightSchedule() {
  // must have device configured
  if (settings.shelly.light.ip.length() == 0) {
    Serial.println("[SHELLY][LIGHT][SCHEDULE] No IP configured -> skip");
    return false;
  }

  // basic validation of on-time
  String on = settings.grow.lightOnTime;
  on.trim();
  int h = -1, m = -1;
  if (on.length() >= 4) {
    const int colon = on.indexOf(':');
    if (colon > 0) {
      h = on.substring(0, colon).toInt();
      m = on.substring(colon + 1).toInt();
    }
  }
  if (h < 0 || h > 23 || m < 0 || m > 59) {
    Serial.printf("[SHELLY][LIGHT][SCHEDULE] Invalid onTime '%s'\n", on.c_str());
    return false;
  }

  int dayHours = (int)settings.grow.lightDayHours;
  if (dayHours < 1) dayHours = 1;
  if (dayHours > 20) dayHours = 20;

  const int totalOnMin  = h * 60 + m;
  const int totalOffMin = (totalOnMin + dayHours * 60) % (24 * 60);
  const int offH = totalOffMin / 60;
  const int offM = totalOffMin % 60;
  char offBuf[6];
  snprintf(offBuf, sizeof(offBuf), "%02d:%02d", offH, offM);
  String off = String(offBuf);

  Serial.printf("[SHELLY][LIGHT][SCHEDULE] Apply %s -> %s (%dh)\n", on.c_str(), off.c_str(), dayHours);
  return applyShellyLightSchedule(on, dayHours);
}