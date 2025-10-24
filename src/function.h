#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <index_html.h>

// declare the global WebServer instance defined elsewhere
extern WebServer server;
extern Preferences preferences;
extern const char* htmlPage;

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