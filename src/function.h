#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <index_html.h>
#include <time.h>
#include <deque>

// declare the global WebServer instance defined elsewhere
extern WebServer server;
extern Preferences preferences;
extern const char* htmlPage;
extern std::deque<String> logBuffer;

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

// Handle root path "/"
void handleRoot() {
  
  String html;
  if (espMode) {
    // Build HTML
    html = FPSTR(apPage);
    // Replace placeholders in index_html.h
    html.replace("%CONTENTCONTROLLERNAME%",  boxName);
  } else {
    html = FPSTR(htmlPage);
    // Replace placeholders in index_html.h
    html.replace("%CONTENTCONTROLLERNAME%",  boxName);
  }

  server.send(200, "text/html", html);
}

// Read stored preferences
void readPreferenes() {
  preferences.begin(PREF_NS, true);
  preferences.begin(PREF_NS, false);
  ssidName = preferences.isKey(KEY_SSID) ? preferences.getString(KEY_SSID) : String();
  ssidPassword = preferences.isKey(KEY_PASS) ? preferences.getString(KEY_PASS) : String();
  boxName = preferences.isKey(KEY_NAME) ? preferences.getString(KEY_NAME) : String("GrowTent");
  language = preferences.isKey(KEY_LANG) ? preferences.getString(KEY_LANG) : String("de");
  theme = preferences.isKey(KEY_THEME) ? preferences.getString(KEY_THEME) : String("light");
  unit = preferences.isKey(KEY_UNIT) ? preferences.getString(KEY_UNIT) : String("metric");
  timeFormat = preferences.isKey(KEY_TFMT) ? preferences.getString(KEY_TFMT) : String("24h");
  ntpServer = preferences.isKey(KEY_NTPSRV) ? preferences.getString(KEY_NTPSRV) : String(DEFAULT_NTP_SERVER);
  tzInfo = preferences.isKey(KEY_TFMT) ? preferences.getString(KEY_TFMT) : String(DEFAULT_TZ_INFO);
  preferences.end();
  Serial.println("[PREF] Preferences loaded:");
}

// Handle form submission save WIFI credentials
void handleSave() {
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
  logPrint("[DATETIME] syncing NTP time");
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

// Sensor data reading
String readSensorData() {
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
        float svp = 0.6108f * exp((17.27f * lastTemperature) / (lastTemperature + 237.3f));
        lastVPD = svp - (lastHumidity / 100.0f) * svp;
        logPrint("[SENSOR] Last Sensorupdate Temperature: " + String(lastTemperature, 1) + " °C, Humidity: " + String(lastHumidity, 0) + " %, VPD: " + String(lastVPD, 1) + " kPa");

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

        // JSON building: { "temperature": 21.5, "humidity": 45.3, "vpd": 1.23 }
        String json = "{";
        if (!isnan(lastTemperature) && !isnan(lastHumidity) && !isnan(lastVPD)) {
          json += "\"temperature\":" + String(lastTemperature, 1);
          json += ",\"humidity\":"  + String(lastHumidity, 0);
          json += ",\"vpd\":"  + String(lastVPD, 1);
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