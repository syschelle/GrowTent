#pragma once

#include <Arduino.h>
#include <StreamString.h>
#include <WString.h>
#include <Preferences.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Include global definitions and functions
#include <globals.h>
#include <globals.h>
#include <function.h>

// external DallasTemperature sensor instance (defined in a single .cpp file)
extern DallasTemperature sensors;

extern WebServer server;

void loadPrefInt(
  const char* prefKey,
  int& targetVar,
  int defaultValue = 0,
  bool logValue = true,
  const char* logLabel = nullptr
);
void loadPrefFloat(
  const char* prefKey,
  float& targetVar,
  float defaultValue = NAN,
  bool logValue = true,
  const char* logLabel = nullptr,
  uint8_t decimals = 2
);
void loadPrefBool(
  const char* prefKey,
  bool& targetVar,
  bool defaultValue = false,
  bool logValue = true,
  const char* logLabel = nullptr
);
void loadPrefString(
  const char* prefKey,
  String& targetVar,
  const char* defaultValue = "",
  bool logValue = true,
  const char* logLabel = nullptr
);



extern Preferences preferences;
extern ShellySettings shelly;
extern SensorReadings cur;
extern Targets target;
extern String readSensorData();
extern void calculateTimeSince(const String& dateStr, int& daysOut, int& weeksOut);
extern void logPrint(const String& msg);
extern void appendLog(unsigned long timestamp, float temperature, float humidity, float vpd);
extern void compactLog();

// forward declaration for calcVPD (defined elsewhere)
float calcVPD(float temperatureC, float leafOffset, float humidityPct);

// Handle root path "/"
void handleRoot() {
  
  String html;
  if (espMode) {
    String sensorData = readSensorData();

    // Build HTML
    html = FPSTR(apPage);
    // Replace placeholders in index_html.h
    //html.replace("%DBG_CHECKED%", debugLog ? "checked" : "");
    html.replace("%CONTROLLERNAME%",  boxName);
    } else {
    html = FPSTR(htmlPage);

    if (startDate != "") {
      int daysSinceStartInt = 0;
      int weeksSinceStartInt = 0;
      calculateTimeSince(startDate, daysSinceStartInt, weeksSinceStartInt);
      String days = String(daysSinceStartInt);
      String weeks = String(weeksSinceStartInt);
      if (language == "de") {
        html.replace("%CURRENTGROW%", "Grow seit: Tag " + days + " / Woche " + weeks);
      } else {
        html.replace("%CURRENTGROW%", "Growing since: day " + days + " / week " + weeks);
      } 
    } else {
      html.replace("%CURRENTGROW%", "");
    }

    if (curPhase == 1) {
      int daysSinceStartInt = 0;
      int weeksSinceStartInt = 0;
      calculateTimeSince(startDate, daysSinceStartInt, weeksSinceStartInt);
      String days = String(daysSinceStartInt);
      String weeks = String(weeksSinceStartInt);
      if (language == "de") {
        html.replace("%CURRENTPHASE%", "<font color=\"lightgreen\">Wachstum: Tag " + days + " / Woche " + weeks + "</font>");
      } else {
        html.replace("%CURRENTPHASE%", "<font color=\"lightgreen\">Vegetative: day " + days + " / week " + weeks + "</font>");
      }
    } else if (curPhase == 2) {
      int daysSinceStartInt = 0;
      int weeksSinceStartInt = 0;
      calculateTimeSince(startFlowering, daysSinceStartInt, weeksSinceStartInt);
      String days = String(daysSinceStartInt);
      String weeks = String(weeksSinceStartInt);
      if (language == "de") {
        html.replace("%CURRENTPHASE%", "<font color=\"#ff9900\">Blüte: Tag " + days + " / Woche " + weeks + "</font>");
      } else {
        html.replace("%CURRENTPHASE%", "<font color=\"#ff9900\">Flowering: day " + days + " / week " + weeks + "</font>");
      }
    } else if (curPhase == 3) {
      int daysSinceStartInt = 0;
      int weeksSinceStartInt = 0;
      calculateTimeSince(startDrying, daysSinceStartInt, weeksSinceStartInt);
      String days = String(daysSinceStartInt);
      String weeks = String(weeksSinceStartInt);
      if (language == "de") {
        html.replace("%CURRENTPHASE%", "<font color=\"lightblue\">Trocknung: Tage " + days + " / Woche " + weeks + "</font>");
      } else {
        html.replace("%CURRENTPHASE%", "<font color=\"lightblue\">Drying: day " + days + " / week " + weeks + "</font>");
      }
    } else {
      html.replace("%CURRENTPHASE%", "");
    }

    // Replace placeholders in index_html.h
    html.replace("%TARGETTEMPERATURE%", String(targetTemperature, 1));
    html.replace("%LIGHTONTIME%", lightOnTime);
    html.replace("%LIGHTDAYHOURS%", String(lightDayHours));
    html.replace("%WATERTEMPERATURE%", String(DS18B20STemperature, 1));
    html.replace("%LEAFTEMPERATURE%", String(offsetLeafTemperature, 1));
    html.replace("%HUMIDITY%", String(cur.humidityPct, 0));
    html.replace("%TARGETVPD%",  String(target.targetVpdKpa, 1));

    html.replace("%RELAYNAMES1%", String(settings.relay.name[0]));
    html.replace("%ESPRELAY1_ENABLED_CHECKED%", settings.relay.schedule[0]. enabled ? "checked" : "");
    html.replace("%ESPRELAY1_IFLIGHTOFF_CHECKED%", settings.relay.schedule[0].ifLightOff ? "checked" : "");
    html.replace("%ESPRELAY1_ONMIN%", String(settings.relay.schedule[0].onMin));
    html.replace("%ESPRELAY1_OFFMIN%", String(settings.relay.schedule[0].offMin));
    html.replace("%RELAYNAMES2%", String(settings.relay.name[1]));
    html.replace("%ESPRELAY2_ENABLED_CHECKED%", settings.relay.schedule[1]. enabled ? "checked" : "");
    html.replace("%ESPRELAY2_IFLIGHTOFF_CHECKED%", settings.relay.schedule[1].ifLightOff ? "checked" : "");
    html.replace("%ESPRELAY2_ONMIN%", String(settings.relay.schedule[1].onMin));
    html.replace("%ESPRELAY2_OFFMIN%", String(settings.relay.schedule[1].offMin));
    html.replace("%RELAYNAMES3%", String(settings.relay.name[2]));
    html.replace("%ESPRELAY3_ENABLED_CHECKED%", settings.relay.schedule[2]. enabled ? "checked" : "");
    html.replace("%ESPRELAY3_IFLIGHTOFF_CHECKED%", settings.relay.schedule[2].ifLightOff ? "checked" : "");
    html.replace("%ESPRELAY3_ONMIN%", String(settings.relay.schedule[2].onMin));
    html.replace("%ESPRELAY3_OFFMIN%", String(settings.relay.schedule[2].offMin));
    html.replace("%RELAYNAMES4%", String(settings.relay.name[3]));
    html.replace("%ESPRELAY4_ENABLED_CHECKED%", settings.relay.schedule[3]. enabled ? "checked" : "");
    html.replace("%ESPRELAY4_IFLIGHTOFF_CHECKED%", settings.relay.schedule[3].ifLightOff ? "checked" : "");
    html.replace("%ESPRELAY4_ONMIN%", String(settings.relay.schedule[3].onMin));
    html.replace("%ESPRELAY4_OFFMIN%", String(settings.relay.schedule[3].offMin));

    html.replace("%CONTROLLERNAME%", boxName);
    html.replace("%GROWSTARTDATE%", String(startDate));
    html.replace("%GROWFLOWERDATE%", String(startFlowering));
    html.replace("%GROWDRAYINGDATE%", String(startDrying));

    html.replace("%PHASE1_SEL%", curPhase == 1 ? "selected" : "");
    html.replace("%PHASE2_SEL%", curPhase == 2 ? "selected" : "");
    html.replace("%PHASE3_SEL%", curPhase == 3 ? "selected" : "");

    html.replace("%TARGETVPD%", String(targetVPD, 1));

    html.replace("%HEATRELAY1_SEL%", settings.heating.Relay == 1 ? "selected" : "");
    html.replace("%HEATRELAY2_SEL%", settings.heating.Relay == 2 ? "selected" : "");
    html.replace("%HEATRELAY3_SEL%", settings.heating.Relay == 3 ? "selected" : "");
    html.replace("%HEATRELAY4_SEL%", settings.heating.Relay == 4 ? "selected" : "");

    html.replace("%SHELLYMAINIP%", settings.shelly.main.ip);
    html.replace("%SHMAINSWKIND1%", settings.shelly.main.gen == 1 ? "selected" : "");
    html.replace("%SHMAINSWKIND2%", settings.shelly.main.gen == 2 ? "selected" : "");
    html.replace("%SHMAINSWKIND3%", settings.shelly.main.gen == 3 ? "selected" : "");

    html.replace("%SHELLYLIGHTIP%", settings.shelly.light.ip);
    html.replace("%SHLIGHTKIND1%", settings.shelly.light.gen == 1 ? "selected" : "");
    html.replace("%SHLIGHTKIND2%", settings.shelly.light.gen == 2 ? "selected" : "");
    html.replace("%SHLIGHTKIND3%", settings.shelly.light.gen == 3 ? "selected" : "");

    html.replace("%POWERPRICEKWH%", String(powerPriceKwhEur, 2));

    html.replace("%SHUSER%", settings.shelly.username);
    html.replace("%SHPASSWORD%", settings.shelly.password);

    html.replace("%NTPSERVER%", ntpServer);
    html.replace("%TZINFO%", tzInfo);
    html.replace("%THEME%", theme);
    html.replace("%LANGUAGE%", language);
    html.replace("%TIMEFORMAT%", timeFormat);
    html.replace("%UNIT%", unit);
    html.replace("%DS18B20ENABLE%", DS18B20Enable);
    html.replace("%DS18B20NAME%", DS18B20Name);

    html.replace("%PUSHOVERENABLED%", pushoverEnabled);
    html.replace("%PUSHOVERAPPKEY%", pushoverAppKey);
    html.replace("%PUSHOVERUSERKEY%", pushoverUserKey);
    html.replace("%PUSHOVERDEVICE%", pushoverDevice);
    html.replace("%GOTIFYENABLED%", gotifyEnabled);
    html.replace("%GOTIFYURL%", gotifyServer);
    html.replace("%GOTIFYTOKEN%", gotifyToken);
  }

  server.send(200, "text/html", html);
}

// Read stored preferences
void readPreferences() {
  preferences.begin(PREF_NS, true);

  // relays
  settings.relay.name[0] = preferences.isKey(KEY_RELAY_1) ? strdup(preferences.getString(KEY_RELAY_1).c_str()) : strdup("relay 1");
  settings.relay.name[1] = preferences.isKey(KEY_RELAY_2) ? strdup(preferences.getString(KEY_RELAY_2).c_str()) : strdup("relay 2");
  settings.relay.name[2] = preferences.isKey(KEY_RELAY_3) ? strdup(preferences.getString(KEY_RELAY_3).c_str()) : strdup("relay 3");
  settings.relay.name[3] = preferences.isKey(KEY_RELAY_4) ? strdup(preferences.getString(KEY_RELAY_4).c_str()) : strdup("relay 4");

  // running settings
  loadPrefString(KEY_STARTDATE, startDate, "", true, "startDate");
  loadPrefString(KEY_FLOWERDATE, startFlowering, "", true, "startFlowering");
  loadPrefString(KEY_DRYINGDATE, startDrying, "", true, "startDrying");
  loadPrefInt(KEY_CURRENTPHASE, curPhase, 1, true, "curPhase");
  loadPrefFloat(KEY_TARGETTEMP, targetTemperature, 22.0f, true, "targets.tempC");
  loadPrefFloat(KEY_LEAFTEMP, offsetLeafTemperature, -1.5f, true, "offsetLeafTemperature");
  loadPrefFloat(KEY_TARGETVPD, target.targetVpdKpa, 1.0f, true, "targetVPD");
  loadPrefInt(KEY_HEATING_RELAY, settings.heating.Relay, 0, true, "heatingRelay");
  settings.grow.lightOnTime = lightOnTime;
  settings.grow.lightDayHours = lightDayHours;

  // relay schedules
  // Load relay schedules into settings.relay.schedule[0..3]
  for (int i = 0; i < NUM_RELAYS; i++) {
    int relay = i + 1;

    String keyEn = "relay_enable_" + String(relay);
    String keyILO = "relay_iflightoff_" + String(relay);

    const char* keyOn =
    (relay == 1) ? KEY_RELAY_START_1 :
    (relay == 2) ? KEY_RELAY_START_2 :
    (relay == 3) ? KEY_RELAY_START_3 :
    KEY_RELAY_START_4;

    const char* keyOff =
    (relay == 1) ? KEY_RELAY_END_1 :
    (relay == 2) ? KEY_RELAY_END_2 :
    (relay == 3) ? KEY_RELAY_END_3 :
    KEY_RELAY_END_4;

    settings.relay.schedule[i].enabled = preferences.getBool(keyEn.c_str(), false);
    settings.relay.schedule[i].ifLightOff = preferences.getBool(keyILO.c_str(), false);
    settings.relay.schedule[i].onMin = preferences.getInt(keyOn, 0);
    settings.relay.schedule[i].offMin = preferences.getInt(keyOff, 0);
  }

  // Shelly devices
  loadPrefString(KEY_SHELLYMAINIP, settings.shelly.main.ip, "", true, "Shelly Main IP");
  loadPrefInt(KEY_SHELLYMAINGEN, settings.shelly.main.gen, 0, true, "Shelly Main Generation");
  loadPrefString(KEY_SHELLYLIGHTIP, settings.shelly.light.ip, "", true, "Shelly Light IP");
  loadPrefInt(KEY_SHELLYLIGHTGEN, settings.shelly.light.gen, 0, true, "Shelly Light Generation");
  loadPrefFloat(KEY_SHELLYMAINOFF, settings.shelly.main.energyOffsetWh, 0.0f, true, "Shelly Main Energy Offset");
  loadPrefFloat(KEY_SHELLYLIGHTOFF, settings.shelly.light.energyOffsetWh, 0.0f, true, "Shelly Light Energy Offset");
  // Shelly credentials (optional Basic Auth)
  loadPrefString(KEY_SHELLYUSERNAME, settings.shelly.username, "", true, "Shelly Username");
  loadPrefString(KEY_SHELLYPASSWORD, settings.shelly.password, "", false, "Shelly Password");

  // Power price
  loadPrefFloat(KEY_POWER_PRICE_KWH, powerPriceKwhEur, 0.30f, true, "powerPriceKwhEur", 3);

  // settings
  loadPrefString(KEY_NAME, boxName, "newGrowTent", true, "boxName");
  loadPrefString(KEY_NTPSRV, ntpServer, DEFAULT_NTP_SERVER, true, "ntpServer");
  loadPrefString(KEY_TZINFO, tzInfo, DEFAULT_TZ_INFO, true, "tzInfo");
  loadPrefString(KEY_LANG, language, "de", true, "language");
  loadPrefString(KEY_THEME, theme, "light", true, "theme");
  loadPrefString(KEY_UNIT, unit, "metric", true, "unit");
  loadPrefString(KEY_TFMT, timeFormat, "24h", true, "timeFormat");
  loadPrefBool(KEY_DS18B20ENABLE, DS18B20, false, true, "DS18B20Enable");
  DS18B20Enable = DS18B20 ? "checked" : "";
  loadPrefString(KEY_DS18NAME, DS18B20Name, "", true, "DS18B20Name");

  // notification settings
  loadPrefString(KEY_PUSHOVER, pushoverEnabled, "", true, "pushoverEnabled");
  if (pushoverEnabled ) pushoverSent = true;
  loadPrefString(KEY_PUSHOVERAPP, pushoverAppKey, "", true, "pushoverAppKey");
  loadPrefString(KEY_PUSHOVERUSER, pushoverUserKey, "", true, "pushoverUserKey");
  loadPrefString(KEY_PUSHOVERDEVICE, pushoverDevice, "", true, "pushoverDevice");
  loadPrefString(KEY_GOTIFY, gotifyEnabled, "", true, "gotifyEnabled");
  if (gotifyEnabled ) gotifySent = true;
  loadPrefString(KEY_GOTIFYSERVER, gotifyServer, "", true, "gotifyServer");
  loadPrefString(KEY_GOTIFYTOKEN, gotifyToken, "", true, "gotifyToken");

  preferences.end();
}

// Forward declaration so this header can call the function defined later
String readSensorData();
void calculateTimeSince(const String& startDate, int& daysSinceStartInt, int& weeksSinceStartInt);

// Forward-declare notification functions used before their definitions
bool sendPushover(const String& message, const String& title);
bool sendGotify(const String& msg, const String& title, int priority = 5);
String calculateEndtimeWatering();

// ---- ESP32 system stats ----
// Some Arduino-ESP32 / PlatformIO builds do not link the FreeRTOS run-time stats
// functions even if the config macros are defined. Declare the symbol as weak so
// the project still links; if it's unavailable we fall back to NAN.
// Read sensor temperature, humidity and vpd and DS18B20 water temperature
String readSensorData() {

  // read DS18B20 water temperature if enabled
  if (DS18B20) {
    sensors.requestTemperatures();
    float dsTemp = sensors.getTempCByIndex(0);
    // only update global water temp if valid
    if (dsTemp != DEVICE_DISCONNECTED_C && dsTemp > -100.0) {
      DS18B20STemperature = dsTemp;
      cur.extTempC = dsTemp;
      logPrint("[SENSOR] DS18B20 water temperature: " + String(cur.extTempC, 1) + " °C");
    } else {
      logPrint("[SENSOR] DS18B20 sensor error or disconnected. Please check wiring.");
    }
  }
  
  // we will ALWAYS return valid JSON, even if BME not available or not time yet
  unsigned long now = millis();
  struct tm timeinfo;
  char timeStr[32] = "";
  if (getLocalTime(&timeinfo)) {
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
  }

  // toggle status LED as before
  if (bmeAvailable) {
    if (now - previousMillis >= blinkInterval) {
      previousMillis = now;
      ledState = !ledState;
      digitalWrite(STATUS_LED_PIN, ledState);
    }

    // time to read fresh BME values?
    if (now - lastRead >= READ_INTERVAL_MS) {
      lastRead = now;

      cur.temperatureC = bme.readTemperature();
      cur.humidityPct    = bme.readHumidity();
      cur.vpdKpa         = calcVPD(cur.temperatureC, offsetLeafTemperature, cur.humidityPct);

      // log every 60s if valid
      if ((now - lastLog >= LOG_INTERVAL_MS) && !isnan(cur.temperatureC) && !isnan(cur.humidityPct) && !isnan(cur.vpdKpa)) {
        appendLog(now, cur.temperatureC, cur.humidityPct, cur.vpdKpa);
        lastLog = now;
        logPrint("[LITTLEFS] Logged data to " + String(LOG_PATH));
      }

      // compact hourly
      static unsigned long lastCompact = 0;
      if (now - lastCompact >= COMPACT_EVERY_MS) {
        compactLog();  // keeps only the last 48 hours
        lastCompact = now;
        logPrint("[LITTLEFS] Compacted log file " + String(LOG_PATH));
      }
    }
  }

  // === JSON BUILDING (always) ===
  String json = "{\n";
  // ---- current readings ----
  if (!isnan(curPhase)) {
    json += "\"curGrowPhase\":" + String(curPhase) + ",\n";
  } else {
    json += "\"curGrowPhase\":null,\n";
  } 
  if (!isnan(cur.temperatureC)) {
    json += "\"curTemperature\":" + String(cur.temperatureC, 1) + ",\n";
  } else {
    json += "\"curTemperature\":null,\n";
  } 
  if (!isnan(cur.extTempC)) {
    json += "\"curDS18B20Se1\":" + String(cur.extTempC, 1) + ",\n";
  } else {
    json += "\"curDS18B20Se1\":null,\n";
  }
  if (!isnan(cur.humidityPct)) {
    json += "\"curHumidity\":" + String(cur.humidityPct, 0) + ",\n";
  } else {
    json += "\"curHumidity\":null,\n";
  }
  if (!isnan(cur.vpdKpa)) {
    json += "\"curVpd\":" + String(cur.vpdKpa, 1) + ",\n";
  } else {
    json += "\"curVpd\":null,\n";
  }

  // ---- relays ----
  // returns e.g. "relays":[true,false,true,false]
  json += "\"relays\":[";
  for (int i = 0; i < NUM_RELAYS; i++) {
    int state = digitalRead(relayPins[i]); // depends on your wiring (LOW=on or HIGH=on)
    // here we assume HIGH=on
    bool on = (state == HIGH);
    json += (on ? "true" : "false");
    if (i < NUM_RELAYS - 1) json += ",";
  }
  json += "],\n";
  
  // ---- Shelly Main Switch ----
  if (!shelly.main.values.ok) {
    logPrint("[API] MAIN SWITCH request not ok");
    json += "\"shellyMainSwitchStatus\":false,\n";
    json += "\"shellyMainSwitchPower\":null,\n";
    json += "\"shellyMainSwitchTotalWh\":null,\n";
  } else {
    json += "\"shellyMainSwitchStatus\":" + String(shelly.main.values.isOn ? "true" : "false") + ",\n";
    if (!isnan(shelly.main.values.powerW) && !isinf(shelly.main.values.powerW)) {
      json += "\"shellyMainSwitchPower\":" + String(shelly.main.values.powerW, 2) + ",\n";
    } else {
      json += "\"shellyMainSwitchPower\":null,\n";
    }
    if (!isnan(shelly.main.values.energyWh) && !isinf(shelly.main.values.energyWh)) {
      json += "\"shellyMainSwitchTotalWh\":" + String(shelly.main.values.energyWh, 2) + ",\n";
    } else {
      json += "\"shellyMainSwitchTotalWh\":null,\n";
    }
  }

  // Main cost in EUR (energyWh -> kWh)
  if (!isnan(shelly.main.values.energyWh) && !isinf(shelly.main.values.energyWh)) {
    const float mainCost = (shelly.main.values.energyWh / 1000.0f) * powerPriceKwhEur;
    json += "\"shellyMainSwitchCostEur\":" + String(mainCost, 2) + ",\n";
  } else {
    json += "\"shellyMainSwitchCostEur\":null,\n";
  } 

  // ---- Shelly Light ----
  if (!shelly.light.values.ok) {
    logPrint("[API] LIGHT request not ok");
    json += "\"shellyLightStatus\":false,\n";
    json += "\"shellyLightPower\":null,\n";
    json += "\"shellyLightTotalWh\":null,\n";
  } else {
    json += "\"shellyLightStatus\":" + String(shelly.light.values.isOn ? "true" : "false") + ",\n";
    if (!isnan(shelly.light.values.powerW) && !isinf(shelly.light.values.powerW)) {
      json += "\"shellyLightPower\":" + String(shelly.light.values.powerW, 2) + ",\n";
    } else {
      json += "\"shellyLightPower\":null,\n";
    }
    if (!isnan(shelly.light.values.energyWh) && !isinf(shelly.light.values.energyWh)) {
      json += "\"shellyLightTotalWh\":" + String(shelly.light.values.energyWh, 2) + ",\n";
    } else {
      json += "\"shellyLightTotalWh\":null,\n";
    }
  }

  // Light cost in EUR
  if (!isnan(shelly.light.values.energyWh) && !isinf(shelly.light.values.energyWh)) {
    const float lightCost = (shelly.light.values.energyWh / 1000.0f) * powerPriceKwhEur;
    json += "\"shellyLightCostEur\":" + String(lightCost, 2) + ",\n";
  } else {
    json += "\"shellyLightCostEur\":null,\n";
  }

  // ---- ESP32 stats ----
  json += "\"espFreeHeap\":" + String(ESP.getFreeHeap()) + ",\n";
  json += "\"espMinFreeHeap\":" + String(ESP.getMinFreeHeap()) + ",\n";
  json += "\"espCpuMhz\":" + String(ESP.getCpuFreqMHz()) + ",\n";
  json += "\"espUptimeS\":" + String((uint32_t)(millis() / 1000UL)) + ",\n";
  // captured time
  json += "\"captured\":\"" + String(timeStr)  + "\"\n";

  json += "}";

  return json;
}