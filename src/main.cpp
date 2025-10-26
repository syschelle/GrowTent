// main.cpp
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WebServer.h>
#include <Preferences.h>
#include <config.h>
#include <function.h>
#include <index_html.h>
#include <style_css.h>
#include <java_script.h>
#include <time.h>

// tasks
#include <task_Check_Sensor.h>

Preferences preferences;
WebServer server(80);

//function prototypes
void handleSave();
void startSoftAP();
void handleSave();

// setup function
void setup() {
  Serial.begin(115200);

  preferences.begin(PREF_NS, false);
  String ssid = preferences.isKey(KEY_SSID) ? preferences.getString(KEY_SSID) : String();
  String password = preferences.isKey(KEY_PASS) ? preferences.getString(KEY_PASS) : String();
  preferences.end();
  Serial.println();

  // If no SSID is stored, start SoftAP mode
  if (ssid == "") {
      espMode = true;
      startSoftAP();
  }

  // Try to connect to stored WiFi credentials
  if (ssid != "") {
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.print("[WIFI] Connecting to: ");
    Serial.print(ssid + " ");

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(500);
      Serial.print(".");
      Serial.println();
    }

    // Check connection status
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WIFI] Connected! ");
      Serial.print("[WIFI] IP-Address: ");
      Serial.println(WiFi.localIP());

      // Initialize relay outputs (LOW = OFF)
      for (int i = 0; i < NUM_RELAYS; i++) {
      pinMode(relayPins[i], OUTPUT);
      digitalWrite(relayPins[i], LOW);
      }
      
      // Initialize BME280 sensor
      unsigned long startTime = millis();

      while (millis() - startTime < 10000) {
        if (bme.begin(BME_ADDR)) {
          Serial.println("[SENSOR] BME280 successfully initialized!");
          bmeAvailable = true;
          break;
        } else {
          Serial.println("[SENSOR] BME280 not found, retrying in 500 ms");
          delay(500);
        }
      }

      xTaskCreatePinnedToCore(
        taskCheckBMESensor,                // Task function
        "Read Valuse of BME280 every 10s",      // Task name
        8192,                                   // Stack size
        NULL,                                   // Task input parameters
        1,                                      // Task priority, be carefull when changing this
        NULL,                                   // Task handle, add one if you want control over the task (resume or suspend the task)
        1                                       // Core to run the task on
      );
      
    } else {
      // if not connected, start SoftAP mode
      Serial.println("[WIFI] Failed to connect. Starting SoftAP mode...");
      startSoftAP();
    }
  }

  // funtion handlers
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/style.css", []() {
    server.send(200, "text/css", cssContent);
  });
  server.on("/script.js", []() {
    server.send(200, "application/javascript", jsContent);
  });
   // route for pure sensor data. for update on the fly in the web page.
  server.on("/sensordata", HTTP_GET, []() {
    // JSON building: { "temperature": 21.5, "humidity": 45.3, "vpd": 1.23 }
    JsonDocument doc; doc;

    if (bmeAvailable) {
      float t = bme.readTemperature();
      float h = bme.readHumidity();
      float svp = 0.6108f * exp((17.27f * t) / (t + 237.3f));
      float vpd = svp - (h / 100.0f) * svp;

      String json = "{";
      if (!isnan(lastTemperature) && !isnan(lastHumidity) && !isnan(lastVPD)) {

        json += "\"temperature\":" + String(lastTemperature, 1);
        json += ",\"humidity\":"  + String(lastHumidity, 0);
        json += ",\"vpd\":"  + String(lastVPD, 1);
      } else {
        // Always send valid JSON, even if sensor is not ready
        json += "\"ok\":false";
        json += ",\"temperature\":null,\"humidity\":null,\"vpd\":null";
      }
      json += "}";

      server.sendHeader("Cache-Control", "no-store");
      server.send(200, "application/json", json);

      String payload;
      serializeJson(doc, payload);
      server.send(200, "application/json", payload);
    }
  });
  server.on("/factory-reset", handleFactoryReset);
  server.onNotFound([](){
    Serial.printf("404 Not Found: %s (method %d)\n", server.uri().c_str(), (int)server.method());
  });
  server.on("/favicon.ico", HTTP_GET, []() {
    String data = FAVICON_ICO_BASE64;
    server.send(200, "image/x-icon;base64", data);
  });

  // start webserver
  server.begin();
  Serial.println("[APP] Web server started");
}

// loop function
void loop() {
  // handle client requests
  server.handleClient();

  // Load NTP server and timezone info from Preferences
  preferences.begin(PREF_NS, true);
  tzInfo = preferences.isKey(KEY_TFMT) ? preferences.getString(KEY_TFMT) : String(DEFAULT_TZ_INFO);
  ntpServer = preferences.isKey(KEY_NTPSRV) ? preferences.getString(KEY_NTPSRV) : String(DEFAULT_NTP_SERVER);
  preferences.end();
  

  // Daily NTP sync at 01:00 AM
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (timeinfo.tm_hour == 1 && timeinfo.tm_min == 0 && timeinfo.tm_mday != lastSyncDay) {
      Serial.println("Performing daily NTP sync...");
      configTzTime(tzInfo.c_str(), ntpServer.c_str());
      lastSyncDay = timeinfo.tm_mday;
    }
  }
}