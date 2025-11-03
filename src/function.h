#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <index_html.h>
#include <time.h>
#include <deque>
#include <OneWire.h>
#include <DallasTemperature.h>

// If no pin is defined elsewhere, default to GPIO4
#ifndef DS18B20_PIN
#define DS18B20_PIN 4
#endif

// Create OneWire + DallasTemperature objects (internal linkage so header can be included safely)
static OneWire oneWire(DS18B20_PIN);
static DallasTemperature sensors(&oneWire);

// declare the global WebServer instance defined elsewhere
extern WebServer server;
extern Preferences preferences;
extern const char* htmlPage;
extern std::deque<String> logBuffer;
extern volatile float DS18B20STemperature;

// log buffer to store recent log lines
void logPrint(const String& msg) {
  // Output serially
  Serial.println(msg);

  // Write to the weblog buffer
  logBuffer.push_back(msg);
  if (logBuffer.size() > LOG_MAX_LINES) {
    logBuffer.pop_front();  // Remove old rows if exceeding max lines logBuffer.size()
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
  Serial.println("[SoftAP] Start SoftAP mode...");
  
  // Disconnect previous connections
  WiFi.disconnect(true, true);  
  delay(100);
  
  WiFi.mode(WIFI_AP_STA);

  //last parameter 'false' = do not hide SSID
  String chipId = String((uint32_t)ESP.getEfuseMac(), HEX);
  String apName = String(KEY_APSSID) + "_" + chipId;
  bool ok = WiFi.softAP(apName.c_str(), KEY_APPASSWORD, /*channel*/ 1, /*hidden*/ false);
  if (!ok) {
    Serial.println("[SoftAP] Error starting the SoftAP!");
    return;
  }
}

// forward declaration so handleRoot can call toIsoDate which is defined later
void calculateTimeSince(String startDate, int &days, int &weeks);

// Handle root path "/"
void handleRoot() {
  
  String html;
  if (espMode) {
    // Build HTML
    html = FPSTR(apPage);
    // Replace placeholders in index_html.h
    html.replace("%CONTROLLERNAME%",  boxName);
    } else {
    html = FPSTR(htmlPage);

    if (startDate != "") {
      int daysSinceStartInt = 0;
      int weeksSinceStartInt = 0;
      calculateTimeSince(startDate, daysSinceStartInt, weeksSinceStartInt);
      String days = String(daysSinceStartInt);
      String weeks = String(weeksSinceStartInt);
      html.replace("%CURRENTGROW%", "Grow seit: " + days + " Tage | " + weeks + " Wochen");
    } else {
      html.replace("%CURRENTGROW%", "");
    }

    // Replace placeholders in index_html.h
    html.replace("%TARGETTEMPERATURE%", String(targetTemperature, 1));
    html.replace("%WATERTEMPERATURE%", String(DS18B20STemperature, 1));
    html.replace("%LEAFTEMPERATURE%", String(offsetLeafTemperature, 1));
    html.replace("%HUMIDITY%", String(lastHumidity, 0));
    html.replace("%TARGETVPD%",  String(targetVPD, 1));

    html.replace("%CONTROLLERNAME%", boxName);
    html.replace("%GROWSTARTDATE%", String(startDate));
    html.replace("%GROWFLOWERDATE%", String(startFlowering));
    html.replace("%GROWDRAYINGDATE%", String(startDrying));

    html.replace("%TARGETVPD%", String(targetVPD, 1));

    html.replace("%NTPSERVER%", ntpServer);
    html.replace("%TZINFO%", tzInfo);
    html.replace("%THEME%", theme);
    html.replace("%LANGUAGE%", language);
    html.replace("%TIMEFORMAT%", timeFormat);
    html.replace("%UNIT%", unit);
  }

  server.send(200, "text/html", html);
}

// Read stored preferences
void readPreferences() {
  preferences.begin(PREF_NS, true);
  preferences.begin(PREF_NS, false);
  //WIFI
  ssidName = preferences.isKey(KEY_SSID) ? preferences.getString(KEY_SSID) : String();
  ssidPassword = preferences.isKey(KEY_PASS) ? preferences.getString(KEY_PASS) : String();
  // running settings
  startDate = preferences.isKey(KEY_STARTDATE) ? preferences.getString(KEY_STARTDATE) : String();
  startFlowering = preferences.isKey(KEY_FLOWERDATE) ? preferences.getString(KEY_FLOWERDATE) : String();
  startDrying = preferences.isKey(KEY_DRYINGDATE) ? preferences.getString(KEY_DRYINGDATE) : String();
  curPhase = preferences.isKey(KEY_CURRENTPHASE) ? preferences.getInt(KEY_CURRENTPHASE) : 3;
  targetTemperature = preferences.isKey(KEY_TARGETTEMP) ? preferences.getFloat(KEY_TARGETTEMP) : 22.0;
  offsetLeafTemperature = preferences.isKey(KEY_LEAFTEMP) ? preferences.getFloat(KEY_LEAFTEMP) : -1.5;
  targetVPD = preferences.isKey(KEY_TARGETVPD) ? preferences.getFloat(KEY_TARGETVPD) : 1.0;
  // settings
  boxName = preferences.isKey(KEY_NAME) ? preferences.getString(KEY_NAME) : String("newGrowTent");
  ntpServer = preferences.isKey(KEY_NTPSRV) ? preferences.getString(KEY_NTPSRV) : String(DEFAULT_NTP_SERVER);
  tzInfo = preferences.isKey(KEY_TZINFO) ? preferences.getString(KEY_TZINFO) : String(DEFAULT_TZ_INFO);
  language = preferences.isKey(KEY_LANG) ? preferences.getString(KEY_LANG) : String("de");
  theme = preferences.isKey(KEY_THEME) ? preferences.getString(KEY_THEME) : String("light");
  unit = preferences.isKey(KEY_UNIT) ? preferences.getString(KEY_UNIT) : String("metric");
  timeFormat = preferences.isKey(KEY_TFMT) ? preferences.getString(KEY_TFMT) : String("24h");
  preferences.end();
  logPrint("[PREF] loading - ssid:" + ssidName + " boxName:" + boxName + " language:" + language + " theme:" + theme +
           " unit:" + unit + " timeFormat:" + timeFormat + " ntpServer:" + ntpServer + " tzInfo:" + tzInfo +
           " startDate:" + startDate + " floweringStart:" + startFlowering + " dryingStart:" + startDrying +
           " targetTemperature:" + targetTemperature + " offsetLeafTemperature:" + offsetLeafTemperature + 
           " targetVPD:" + targetVPD + " curPhase:" + String(curPhase));
}

void handleSaveRunsettings() {
  // 1) Open the Preferences namespace with write access (readOnly = false)
  // Only call begin() once — calling it twice can cause writes to fail!
  if (!preferences.begin(PREF_NS, false)) {
    logPrint("[PREF][ERROR] preferences.begin() failed. "
             "Check that PREF_NS length <= 15 characters.");
    server.send(500, "text/plain", "Failed to open Preferences");
    return;
  }

  // 2) Save grow start date if provided
  if (server.hasArg("webGrowStart")) {
    String v = server.arg("webGrowStart");
    preferences.putString(KEY_STARTDATE, v) > 0;
    startDate = v; // also update RAM variable
  }

  // 3) Save flowering start date if provided
  if (server.hasArg("webFloweringStart")) {
    String v = server.arg("webFloweringStart");
    preferences.putString(KEY_FLOWERDATE, v) > 0;
    startFlowering = v;
  }

  // 4) Save drying start date if provided
  if (server.hasArg("webDryingStart")) {
    String v = server.arg("webDryingStart");
    preferences.putString(KEY_DRYINGDATE, v) > 0;
    startDrying = v;
  }

  // 5) Save current phase if provided
  if (server.hasArg("webCurrentPhase")) {
    curPhase = server.arg("webCurrentPhase").toInt();
    preferences.putInt(KEY_CURRENTPHASE, curPhase) > 0;
  }

  // 6) Save target temperature if provided
  if (server.hasArg("webTargetTemp")) {
    targetTemperature = server.arg("webTargetTemp").toFloat();
    preferences.putFloat(KEY_TARGETTEMP, targetTemperature);
  }

  // 7) Save target VPD if provided
  if (server.hasArg("webTargetVPD")) {
    targetVPD = server.arg("webTargetVPD").toFloat();
    preferences.putFloat(KEY_TARGETVPD, targetVPD);
  }

  // 8) Save leaf temperature offset if provided
  if (server.hasArg("webOffsetLeafTemp")) {
    offsetLeafTemperature = server.arg("webOffsetLeafTemp").toFloat();
    preferences.putFloat(KEY_LEAFTEMP, offsetLeafTemperature);
  }

  // 9) Optionally read the values back to confirm they were written
  String  chkStartDate       = preferences.getString(KEY_STARTDATE, "");
  String  chkFloweringStart  = preferences.getString(KEY_FLOWERDATE, "");
  String  chkDryingStart     = preferences.getString(KEY_DRYINGDATE, "");
  int     chkPhase           = preferences.getInt(KEY_CURRENTPHASE, -1);
  float   chkTargetTemp      = preferences.getFloat(KEY_TARGETTEMP, NAN);
  float   chkTargetVPD       = preferences.getFloat(KEY_TARGETVPD, NAN);
  float   chkOffsetLeafTemp  = preferences.getFloat(KEY_LEAFTEMP, NAN);

  preferences.end(); // always close Preferences handle

  // 10) Log everything for debugging
  logPrint(String("[PREF] runssetings saved startDate:") + chkStartDate +
           " floweringStart:" + chkFloweringStart +
           " dryingStart:" + chkDryingStart +
           " curPhase:" + String(chkPhase) +
           " targetTemp:" + String(chkTargetTemp) +
           " offsetLeafTemperature:" + String(chkOffsetLeafTemp) +
           " targetVPD:" + String(chkTargetVPD));

  // 11) Send redirect response and restart the ESP
  server.sendHeader("Location", "/");
  server.send(303);  // HTTP redirect to status page
  delay(250);
  ESP.restart();
}

// Handle general settings save
void handleSaveSettings() {
  // 1) Open the Preferences namespace with write access (readOnly = false)
  // Only call begin() once — calling it twice can cause writes to fail!
  if (!preferences.begin(PREF_NS, false)) {
    logPrint("[PREF][ERROR] preferences.begin() failed. "
             "Check that PREF_NS length <= 15 characters.");
    server.send(500, "text/plain", "Failed to open Preferences");
    return;
  }

  // 2) Save controller name if provided
  if (server.hasArg("webControllerName")) {
    String v = server.arg("webControllerName");
    preferences.putString(KEY_NAME, v) > 0;
    boxName = v; // also update RAM variable
  }
  // 3) Save NTP server if provided
  if (server.hasArg("webNTPServer")) {
    String v = server.arg("webNTPServer");
    preferences.putString(KEY_NTPSRV, v) > 0;
    ntpServer = v;
  }
  // 4) Save timezone info if provided
  if (server.hasArg("webTimeZoneInfo")) {
    String v = server.arg("webTimeZoneInfo");
    preferences.putString(KEY_TZINFO, v) > 0;
    tzInfo = v;
  } 
  // 5) Save language if provided
  if (server.hasArg("webLanguage")) {
    String v = server.arg("webLanguage");
    preferences.putString(KEY_LANG, v) > 0;
    language = v;
  }
  // 6) Save theme if provided
  if (server.hasArg("webTheme")) {
    String v = server.arg("webTheme");
    preferences.putString(KEY_THEME, v) > 0;
    theme = v;
  }
  // 7) Save time format if provided
  if (server.hasArg("webTimeFormat")) {
    String v = server.arg("webTimeFormat");
    preferences.putString(KEY_TFMT, v) > 0;
    timeFormat = v;
  } 
  // 8) Save unit if provided
  if (server.hasArg("webTempUnit")) { 
    String v = server.arg("webTempUnit");
    preferences.putString(KEY_UNIT, v) > 0;
    unit = v;
  }
  preferences.end(); // always close Preferences handle

  // 9) Log everything for debugging
  logPrint(String("[PREF] settings saved boxName:") + boxName +
           " ntpServer:" + ntpServer +
           " tzInfo:" + tzInfo +
           " lang:" + language +
           " theme:" + theme +
           " timeFormat:" + timeFormat +
           " unit:" + unit);

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

// Handle factory reset
void handleFactoryReset() {
  preferences.begin(PREF_NS, false);
  preferences.clear();  // Deletes all keys in the namespace"
  preferences.end();

  server.send(200, "text/html", "<h1>Factory reset performed. Restarting...</h1>");
  delay(2000);
  ESP.restart();
}

// NTP sync
void syncDateTime() {
  // syncing NTP time
  logPrint("[DATETIME] syncing NTP time to server: " + ntpServer + " TZ: " + tzInfo);
  configTzTime(tzInfo.c_str(), ntpServer.c_str());  // Synchronizing ESP32 system time with NTP
  if (getLocalTime(&local, 10000)) { // Try to synchronize up to 10s
    // set actual date in global variable actualDate
    char readDate[11]; // YYYY-MM-DD + null
    strftime(readDate, sizeof(readDate), "%Y-%m-%d", &local);
    lastSyncDay = local.tm_mday;
    char buf[64];
    strftime(buf, sizeof(buf), "now: %d.%m.%y  Zeit: %H:%M:%S", &local);
    logPrint(String("[DATETIME] ") + buf);  // Format date print output
  } else {
    logPrint("[DATETIME] Failed to obtain time");
  }
}

// calculate elapsed days and weeks from defined date
void calculateTimeSince(String startDate, int &days, int &weeks) {
  struct tm tmStart = { 0 };
  int y, m, d;
  sscanf(startDate.c_str(), "%d-%d-%d", &y, &m, &d);
  tmStart.tm_mday = d;
  tmStart.tm_mon = m - 1;
  tmStart.tm_year = y - 1900;
  tmStart.tm_hour = 0;
  tmStart.tm_min = 1;
  time_t startEpoch = mktime(&tmStart);
  time_t nowEpoch = time(nullptr);
  long diffSec = nowEpoch - startEpoch;
  days = (diffSec / 86400) + 1;
  weeks = (days / 7) + 1;

  //logPrint(String("Running since ") + String(days) + String(" days (") + String(weeks) + String(" weeks + ")  + String(" days)\n"));
}

// CSV: ts_ms,tempC,hum,vpd\n
static void appendLog(uint32_t ts, float t, float h, float v) {
  File f = LittleFS.open(LOG_PATH, FILE_APPEND);
  if (!f) { logPrint("appendLog: open failed"); return; }
  logPrint("[LITTLEFS] Loging data: " + String(ts) + "," + String(t,1) + "," + String(h,0) + "," + String(v,1));
  // Zahlen schlank formatieren
  String line;
  line.reserve(40);
  line += String(ts); line += ',';
  line += String(t, 2); line += ',';
  line += String(h, 0); line += ',';
  line += String(v, 3); line += '\n';
  if(f.print(line)){
        logPrint("[LITTLEFS] " + String(LOG_PATH) + " file written");
    } else {
        logPrint("[LITTLEFS] " + String(LOG_PATH) + " write failed");
    }
    f.close();
}

// Compaction: discard everything < (now-RETAIN_MS)
static void compactLog() {
  const uint32_t now = millis();
  const uint32_t cutoff = (now > RETAIN_MS) ? (now - RETAIN_MS) : 0;

  File in = LittleFS.open(LOG_PATH, FILE_READ);
  if (!in) return; // nichts zu tun

  File out = LittleFS.open("/envlog.tmp", FILE_WRITE);
  if (!out) { in.close(); return; }

  // Zeilenweise kopieren
  String line;
  while (in.available()) {
    line = in.readStringUntil('\n');
    if (line.length() < 5) continue;
    // ts am Anfang extrahieren
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

// Read sensor temperatur, humidity and vpd and DS18B20 water temperature
String readSensorData() {

  sensors.requestTemperatures();
  float dsTemp = sensors.getTempCByIndex(0);
  // nur übernehmen, wenn gültig
  if (dsTemp != DEVICE_DISCONNECTED_C && dsTemp > -100.0) DS18B20STemperature = dsTemp;

  // Read sensor temperatur, humidity and vpd
  if (bmeAvailable) {
      unsigned long now = millis();
      struct tm timeinfo;
      char timeStr[32] = "";
      if (getLocalTime(&timeinfo)) {
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
        // keep timeStr for later JSON output instead of returning here
      }

      if (now - previousMillis >= blinkInterval) {
        previousMillis = now;

        ledState = !ledState;
        digitalWrite(STATUS_LED_PIN, ledState);
      }

      if (now - lastRead >= READ_INTERVAL_MS) {
        lastRead = now;
        lastTemperature = bme.readTemperature();
        lastHumidity = bme.readHumidity();
        // Calculate VPD
        lastVPD = calcVPD(lastTemperature, offsetLeafTemperature, lastHumidity);
        //logPrint("[SENSOR] Last Sensorupdate Temperature: " + String(lastTemperature, 1) + " °C, Humidity: " + String(lastHumidity, 0) + " %, VPD: " + String(lastVPD, 1) + " kPa");

        // 60s: Only write to the log (if values are valid)
        if ((now - lastLog >= LOG_INTERVAL_MS) && !isnan(lastTemperature) && !isnan(lastHumidity) && !isnan(lastVPD)) {
          appendLog(now, lastTemperature, lastHumidity, lastVPD);
          lastLog = now;
          logPrint("[LITTLEFS] Logged data to " + String(LOG_PATH));
        }

        // continue compressing hourly
        static unsigned long lastCompact = 0;
        if (now - lastCompact >= COMPACT_EVERY_MS) {
          compactLog();                 // keeps only the last 48 hours
          lastCompact = now;
          logPrint("[LITTLEFS] Compacted log file " + String(LOG_PATH));
        }

        // JSON building: { "temperature": 21.5, "humidity": 45.3, "vpd": 1.23, waterTemperature: 19.5, "captured": "12:34:56"  }
        String json = "{";
        if (!isnan(lastTemperature) && !isnan(lastHumidity) && !isnan(lastVPD)) {
          json += "\"temperature\":" + String(lastTemperature, 1);
          json += ",\"humidity\":"  + String(lastHumidity, 0);
          json += ",\"vpd\":"  + String(lastVPD, 1);
          json += ",\"waterTemperature\":" + String(DS18B20STemperature, 1);
          json += ",\"captured\":\"" + String(timeStr) +"\"";
        } else {
          // Always send valid JSON, even if sensor is not ready
          json += "\"ok\":false";
          json += ",\"temperature\":null,\"humidity\":null,\"vpd\":null";
        }
        json += "}";
        
        return json;
        
      } else {
        // Not time to read yet
        return String("");
      }
  }
  // BME not available
  return String("");
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
    int c2 = line.indexOf(',', c1 + 1);
    int c3 = line.indexOf(',', c2 + 1);
    if (c2 < 0 || c3 < 0) continue;

    float t = atof(line.substring(c1 + 1, c2).c_str());
    float h = atof(line.substring(c2 + 1, c3).c_str());
    float v = atof(line.substring(c3 + 1).c_str());

    if (!first) server.sendContent(",");
    first = false;

    chunk = "{\"ts\":";
    chunk += String(ts);
    chunk += ",\"tempC\":";
    chunk += String(t, 2);
    chunk += ",\"hum\":";
    chunk += String(h, 0);
    chunk += ",\"vpd\":";
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
