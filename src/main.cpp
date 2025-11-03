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
#include <LittleFS.h>
#include <deque>
#include <OneWire.h>
#include <DallasTemperature.h>

// tasks
#include <task_Check_Sensor.h>

Preferences preferences;
WebServer server(80);

// define weblog buffer variable
std::deque<String> logBuffer;

//function prototypes
void handleSave();
void startSoftAP();
void handleSave();

// setup function
void setup() {
  Serial.begin(115200);
  
  if (!LittleFS.begin(true)) {
    Serial.println(F("[LITTLEFS] LittleFS mount failed"));
  } else {
    Serial.println(F("[LITTLEFS] LittleFS mounted"));
  }

  // read stored preferences
  readPreferences();

  // If no SSID is stored, start SoftAP mode
  if (ssidName == "") {
      espMode = true;
      startSoftAP();
  }

  // Try to connect to stored WiFi credentials
  if (ssidName != "") {
    WiFi.begin(ssidName.c_str(), ssidPassword.c_str());
    logPrint("[WIFI] Connecting to: ");
    logPrint(ssidName + " ");

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(500);
      logPrint(".");
    }

    // Check connection status
    if (WiFi.status() == WL_CONNECTED) {
      logPrint("[WIFI] Connected! ");
      logPrint("[WIFI] IP-Address: " + WiFi.localIP().toString());

      // Sync NTP time
      syncDateTime();

      // Initialize relay outputs (LOW = OFF)
      for (int i = 0; i < NUM_RELAYS; i++) {
      pinMode(relayPins[i], OUTPUT);
      digitalWrite(relayPins[i], LOW);
      }
      
      // Initialize BME280 sensor
      unsigned long startTime = millis();

      while (millis() - startTime < 10000) {
        if (bme.begin(BME_ADDR)) {
          logPrint("[SENSOR] BME280 successfully initialized!");
          bmeAvailable = true;
          break;
        } else {
          logPrint("[SENSOR] BME280 not found, retrying in 500 ms");
          delay(500);
        }
      }

      OneWire oneWire(DS18B20_PIN);
      DallasTemperature sensors(&oneWire);
      sensors.begin();

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
      logPrint("[WIFI] Failed to connect. Starting SoftAP mode...");
      startSoftAP();
    }
  }

  // funtion handlers
  server.on("/", handleRoot);
  // route for saving WiFi credentials
  server.on("/save", HTTP_POST, handleSaveWiFi);
  // route for saving runsettings
  server.on("/saverunsettings", HTTP_POST, handleSaveRunsettings);
  // route for saving general settings
  server.on("/savesettings", HTTP_POST, handleSaveSettings);
  // route for CSS
  server.on("/style.css", []() {
    server.send(200, "text/css", cssContent);
  });
  // route for JS
  server.on("/script.js", []() {
    server.send(200, "application/javascript", jsContent);
  });
   // route for pure sensor data. for update on the fly in the web page.
  server.on("/sensordata", HTTP_GET, []() {
    // Read sensor data and send as JSON
    String jsonSensorData = readSensorData();
    server.send(200, "application/json; charset=utf-8", jsonSensorData);
  });
  // route for factory reset
  server.on("/factory-reset", handleFactoryReset);
  // route for getting the history log as JSON
  server.on("/history", HTTP_GET, handleHistory);
  server.onNotFound([](){
    Serial.printf("404 Not Found: %s (method %d)\n", server.uri().c_str(), (int)server.method());
  });
  // route for downloading the history log as CSV file
  server.on("/download/history", HTTP_GET, handleDownloadHistory);
  // route for favicon.ico
  server.on("/favicon.ico", HTTP_GET, []() {
    String data = FAVICON_ICO_BASE64;
    server.send(200, "image/x-icon;base64", data);
  });
  // API endpoints for log buffer
  server.on("/api/logbuffer", HTTP_GET, handleApiLogBuffer);
  // API endpoint to clear log buffer
  server.on("/api/logbuffer/clear", HTTP_POST, handleClearLog);
  // API endpoint to download log buffer
  server.on("/log", HTTP_GET, handleDownloadLog);


  // start webserver
  server.begin();
  logPrint("[APP] Web server started");
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
      logPrint("Performing daily NTP sync...");
      configTzTime(tzInfo.c_str(), ntpServer.c_str());
      lastSyncDay = timeinfo.tm_mday;
    }
  }
}