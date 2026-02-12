#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// -------------------- Konstanten / Defines --------------------
#define NUM_RELAYS 4
#define DS18B20_PIN 4
#define STATUS_LED_PIN 23
#define BME_ADDR 0x76

#ifndef I2C_SDA
  #define I2C_SDA 21
#endif
#ifndef I2C_SCL
  #define I2C_SCL 22
#endif

// -------------------- Structs (erst Typen, dann Settings) --------------------
struct DailySchedule {
    int onHour;
    int onMinute;
    int offHour;
    int offMinute;
};

struct ShellySchedules {
    DailySchedule days[7];  // 0 = Montag, 6 = Sonntag
};


// Shelly device values
struct ShellyValues {
  bool  ok = false;
  bool  isOn = false;
  float powerW = 0.0f;
  float energyWh = 0.0f;
};

// Shelly device info
struct ShellyDevice {
  String ip;
  int    gen  = 0;
  float  energyOffsetWh = 0.0f; // baseline to show 0 at "new grow"
  ShellyValues values;
  ShellySchedules schedules;
};

// Shelly overall settings
struct ShellySettings {
  ShellyDevice main;
  ShellyDevice light;

  String username;
  String password;
};

// Relay schedule
struct RelaySchedule {
  bool enabled = false;
  bool ifLightOff = false;
  int onMin = 0;
  int offMin   = 0;
};

// Relay settings
struct RelayGroup {
  String name[NUM_RELAYS];
  RelaySchedule schedule[NUM_RELAYS];
};

struct GrowGroup {
  String startDate;
  String startFlowering;
  String startDrying;

  int currentPhase = 1;

  float targetTemperature = 22.0f;
  float offsetLeafTemperature = -1.5f;
  float targetVPD = 1.0f;

  String lightOnTime = "06:00";   // HH:MM
  int lightDayHours = 18;          // 1..20
};

struct UIGroup {
  String boxName = "newGrowTent";
  String ntpServer = "pool.ntp.org";
  String tzInfo = "Europe/Berlin";

  String language = "de";
  String theme = "light";
  String unit = "metric";
  String timeFormat = "24h";

  bool ds18b20Enabled = false;
  String ds18b20Name;
};

struct NotifyGroup {
  bool pushoverEnabled = false;
  String pushoverAppKey;
  String pushoverUserKey;
  String pushoverDevice;

  bool gotifyEnabled = false;
  String gotifyServer;
  String gotifyToken;
};

struct DebugGroup {
  bool enabled = false;
};

struct HeatingGroup {
  int Relay = 0; // 0 = none, 1-4 = relay index + 1
};

struct Settings {
  DebugGroup debug;
  RelayGroup relay;
  HeatingGroup heating;
  GrowGroup grow;
  ShellySettings shelly;
  UIGroup ui;
  NotifyGroup notify;
};

extern Settings settings;

// Runtime structs (ok im Header)
struct SensorReadings {
  float temperatureC = NAN;
  float humidityPct  = NAN;
  float extTempC     = NAN;
  float vpdKpa       = NAN;
  uint32_t lastUpdateMs = 0;

  bool ok() const { 
    return !isnan(temperatureC) && !isnan(humidityPct); 
  }
};

struct Targets {
  float targetVpdKpa = NAN;
  float targetTempC  = NAN;
  float targetHumPct = NAN;
};

struct AvgAccumulator {
  float sum = 0.0f;
  uint32_t count = 0;
  inline void add(float v) { sum += v; count++; }
  inline float avg() const { return (count == 0) ? 0.0f : (sum / count); }
  inline void reset() { sum = 0.0f; count = 0; }
};

// -------------------- Globale Variablen: nur extern! --------------------

// Debug
extern bool debugLog;

// Logging
extern const size_t LOG_MAX_LINES;

// WiFi/AP
extern const char* KEY_APSSID;
extern const char* KEY_APPASSWORD;
extern String ssidName;
extern String ssidPassword;
extern bool wifiReady;

// Preferences namespace + keys
extern const char* PREF_NS;
extern const char* KEY_SSID;
extern const char* KEY_PASS;

// Running settings keys
extern const char* KEY_STARTDATE;
extern const char* KEY_FLOWERDATE;
extern const char* KEY_DRYINGDATE;
extern const char* KEY_CURRENTPHASE;
extern const char* KEY_TARGETTEMP;
extern const char* KEY_LEAFTEMP;
extern const char* KEY_TARGETVPD;
extern const char* KEY_LIGHT_ON_TIME;
extern const char* KEY_LIGHT_DAY_HOURS;
extern const char* KEY_HEATING_RELAY;

// Relay schedule keys (start/end as stored in Preferences)
extern const char* KEY_RELAY_START_1;
extern const char* KEY_RELAY_END_1;
extern const char* KEY_RELAY_START_2;
extern const char* KEY_RELAY_END_2;
extern const char* KEY_RELAY_START_3;
extern const char* KEY_RELAY_END_3;
extern const char* KEY_RELAY_START_4;
extern const char* KEY_RELAY_END_4;

// Shelly device keys
extern const char* KEY_SHELLYMAINIP;
extern const char* KEY_SHELLYMAINGEN;
extern const char* KEY_SHELLYLIGHTIP;
extern const char* KEY_SHELLYLIGHTGEN;
extern const char* KEY_SHELLYMAINOFF;
extern const char* KEY_SHELLYLIGHTOFF;
extern const char* KEY_SHELLYHEATOFF;
extern const char* KEY_SHELLYHUMOFF;
extern const char* KEY_SHELLYFANOFF;
extern const char* KEY_SHELLYUSERNAME;
extern const char* KEY_SHELLYPASSWORD;

// UI/settings keys
extern const char* KEY_NAME;
extern const char* KEY_LANG;
extern const char* KEY_THEME;
extern const char* KEY_UNIT;
extern const char* KEY_TFMT;
extern const char* KEY_NTPSRV;
extern const char* KEY_TZINFO;
extern const char* KEY_DS18B20ENABLE;
extern const char* KEY_DS18NAME;
extern const char* KEY_RELAY_1;
extern const char* KEY_RELAY_2;
extern const char* KEY_RELAY_3;
extern const char* KEY_RELAY_4;

// Notification keys
extern const char* KEY_PUSHOVER;
extern const char* KEY_PUSHOVERAPP;
extern const char* KEY_PUSHOVERUSER;
extern const char* KEY_PUSHOVERDEVICE;
extern const char* KEY_GOTIFY;
extern const char* KEY_GOTIFYSERVER;
extern const char* KEY_GOTIFYTOKEN;

// Legacy/global UI vars (used throughout runtime.h/function.h)
extern String boxName;
extern String language;
extern String theme;
extern String unit;
extern String timeFormat;
extern bool espMode;

extern int heatingRelay;

// Relay scheduling arrays + relay names (legacy)
extern bool relaySchedulesEnabled[NUM_RELAYS];
extern int  relaySchedulesStart[NUM_RELAYS];
extern int  relaySchedulesEnd[NUM_RELAYS];
extern String relayNames[NUM_RELAYS];

// Grow / phase globals (legacy)
extern String startDate;
extern String startFlowering;
extern String startDrying;
extern int curPhase;
extern float targetTemperature;
extern float offsetLeafTemperature;
extern float targetVPD;
extern String lightOnTime;
extern int lightDayHours;

// Notifications (legacy)
extern bool pushoverSent;
extern String pushoverEnabled;
extern String pushoverAppKey;
extern String pushoverUserKey;
extern String pushoverDevice;

extern bool gotifySent;
extern String gotifyEnabled;
extern String gotifyServer;
extern String gotifyToken;

// Relays
extern const int relayPins[NUM_RELAYS];
extern bool relayStates[NUM_RELAYS];
extern const int PUMP_COUNT;
extern unsigned long relayOffTime[NUM_RELAYS];
extern bool relayActive[NUM_RELAYS];

// Sensors
extern Adafruit_BME280 bme;
extern bool bmeAvailable;

extern volatile float DS18B20STemperature;
extern bool DS18B20;
extern String DS18B20Enable;
extern String DS18B20Name;

// LED
extern unsigned long previousMillis;
extern const unsigned long blinkInterval;
extern bool ledState;

// Averages instances
extern AvgAccumulator tempAvg;
extern AvgAccumulator humAvg;
extern AvgAccumulator vpdAvg;
extern AvgAccumulator waterTempAvg;

// Time/NTP
extern const char* DEFAULT_NTP_SERVER;
extern String ntpServer;
extern const char* DEFAULT_TZ_INFO;
extern String tzInfo;
extern struct tm local;
extern int lastSyncDay;
extern bool ntpSyncPending;
extern unsigned long ntpStartMs;

// Hint-State
extern String hintKey;
extern String hintDetailsJson;
extern uint32_t hintId;

// Buffers / circular logs
extern const uint32_t READ_INTERVAL_MS;
extern uint32_t lastRead;
extern const uint32_t MEASUREMENT_INTERVAL_MS;

#define HISTORY_INTERVAL_SEC 300  // record one point every 5 minutes
#define HISTORY_WINDOW_HOURS 12
#define NUM_VALUES (HISTORY_WINDOW_HOURS * 3600 / HISTORY_INTERVAL_SEC)  // store up to 12 hours of history
extern float temps[NUM_VALUES];
extern float hums[NUM_VALUES];
extern float vpds[NUM_VALUES];
extern float waterTemps[NUM_VALUES];
extern float sumTemp, sumHum, sumVPD, sumWaterTemp;
extern int index_pos;
extern int count;
extern int head;

// Grow runtime
extern char actualDate[10];
extern int daysSinceStart, weeksSinceStart;
extern int daysSinceFlowering, weeksSinceFlowering;
extern int daysSinceDrying, weeksSinceDrying;
extern const char* phaseNames[3];

// Log file
extern const char* LOG_PATH;
extern const uint32_t RETAIN_MS;
extern uint32_t lastCompact;
extern const uint32_t COMPACT_EVERY_MS;
extern const unsigned long LOG_INTERVAL_MS;
extern unsigned long lastLog;

String csvFieldToString(const String& s);

// Web UI payloads (PROGMEM)
extern const char jsContent[] PROGMEM;
extern const char cssContent[] PROGMEM;
extern const char htmlContent[] PROGMEM;
