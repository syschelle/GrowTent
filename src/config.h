#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// WiFi Access Point credentials
const char* KEY_APSSID = "new growtent";
const char* KEY_APPASSWORD = "12345678";

// Namespace for Preferences
static const char* KEY_SSID    = "ssid";
static const char* KEY_PASS    = "password";
static const char* PREF_NS     = "growtent";
static const char* KEY_NAME    = "boxName";
static const char* KEY_LANG    = "lang";
static const char* KEY_THEME   = "theme";
static const char* KEY_UNIT    = "unit";
static const char* KEY_TFMT    = "timeFmt";
static const char* KEY_NTPSRV  = "ntpSrv";

// Include sensor libraries
#define BME_ADDR 0x76
Adafruit_BME280 bme;
bool bmeAvailable = false;

// Relay Configuration
#define NUM_RELAYS 4
const int relayPins[NUM_RELAYS] = { 32, 33, 25, 26 };

// human designations for the relays
static const char* relayNames[NUM_RELAYS] = {
	"left fan",
	"right fan",
	"pod fan",
	"humidifier fan"
};

// Global configuration variables
String boxName = "GrowTent";
String language = "de";
String theme = "light"; // light or dark
String unit = "metric"; // metric or imperial
String timeFormat = "24h"; // 12h or 24h
bool espMode = false; // false = Station mode, true = Access Point mode

// Measurement interval in milliseconds
static constexpr uint32_t MEASUREMENT_INTERVAL_MS = 30000; // 30s

// default-NTP-Server
const char* DEFAULT_NTP_SERVER = "de.pool.ntp.org";
String ntpServer = "";

// default-timezone (POSIX-String)
const char* DEFAULT_TZ_INFO    = "WEST-1DWEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00";  // Western European Time
String tzInfo = "";

// day of month for last NTP sync
RTC_DATA_ATTR int lastSyncDay = -1;

// Last published sensor values
volatile float lastTemperature = NAN;
volatile float lastHumidity = NAN;
volatile float lastVPD = NAN;
// Timestamp of last sensor read
const uint32_t READ_INTERVAL_MS = 1000;
uint32_t lastRead = 0;

// Growth phase configuration
const char* phaseNames[5] = { "", "Seedling/Clone", "Vegetative", "Flowering", "Drying"};
int curPhase;
// Default VPD targets per phase
const float defaultVPDs[5] = { 0.0f, 0.8f, 1.2f, 1.4f, 1.0f };