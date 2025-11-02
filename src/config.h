#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// max log lines to weblog
const size_t LOG_MAX_LINES = 50;

// WiFi Access Point credentials
const char* KEY_APSSID = "new growtent";
const char* KEY_APPASSWORD = "12345678";
// WiFi credentials storage
String ssidName = "";
String ssidPassword = "";

// Namespace for Preferences
// WIFI keys
static const char* KEY_SSID    = "ssid";
static const char* KEY_PASS    = "password";
// namespace for preferences
static const char* PREF_NS     = "growtent";
//runningsettings
static const char* KEY_STARTDATE = "startDate";
static const char* KEY_FLOWERDATE = "startFlowering";
static const char* KEY_DRYINGDATE = "startDrying";
static const char* KEY_CURRENTPHASE = "curPhase";
static const char* KEY_TARGETTEMP = "targetTemp";
static const char* KEY_LEAFTEMP = "offsetLeaf";
static const char* KEY_TARGETVPD = "targetVPD";
// settings
static const char* KEY_NAME    = "boxName";
static const char* KEY_LANG    = "lang";
static const char* KEY_THEME   = "theme";
static const char* KEY_UNIT    = "unit";
static const char* KEY_TFMT    = "timeFmt";
static const char* KEY_NTPSRV  = "ntpSrv";
static const char* KEY_TZINFO  = "tzInfo";

// Include sensor libraries
#define BME_ADDR 0x76
Adafruit_BME280 bme;
bool bmeAvailable = false;

// Relay Configuration
#define NUM_RELAYS 4
const int relayPins[NUM_RELAYS] = { 32, 33, 25, 26 };
#define STATUS_LED_PIN 23
unsigned long previousMillis = 0;
const unsigned long blinkInterval = 500; // Blinkrate in Millisekunden
bool ledState = false;

// human designations for the relays
static const char* relayNames[NUM_RELAYS] = {
	"left fan",
	"right fan",
	"pod fan",
	"humidifier fan"
};

// Global configuration variables
String boxName;
String language;
String theme; // light or dark
String unit = "metric"; // metric or imperial
String timeFormat = "24h"; // 12h or 24h
bool espMode = false; // false = Station mode, true = Access Point mode

// Measurement interval in milliseconds
static constexpr uint32_t MEASUREMENT_INTERVAL_MS = 30000; // 30s

// default-NTP-Server
const char* DEFAULT_NTP_SERVER = "de.pool.ntp.org";
String ntpServer;

// default-timezone (POSIX-String)
const char* DEFAULT_TZ_INFO    = "WEST-1DWEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00";  // Western European Time
String tzInfo;

// day of month for last NTP sync
RTC_DATA_ATTR int lastSyncDay = -1;

// structure to hold time info
struct tm local;

// Last published sensor values
volatile float lastTemperature = NAN;
volatile float lastHumidity = NAN;
volatile float lastVPD = NAN;
// Timestamp of last sensor read
const uint32_t READ_INTERVAL_MS = 1000;
uint32_t lastRead = 0;

// Default growlight setting
char actualDate[10];
String startDate;
String startFlowering;
String startDrying;
int daysSinceStart = 0;
int weeksSinceStart = 0;
int daysSinceFlowering = 0;
int weeksSinceFlowering = 0;
int daysSinceDrying = 0;
int weeksSinceDrying = 0;

// Growth phase configuration
const char* phaseNames[3] = { "Vegetative", "Flowering", "Drying"};
int curPhase;
float targetTemperature;
float offsetLeafTemperature;
// Default VPD targets per phase
float targetVPD;

// Logfile-Settings
static const char* LOG_PATH = "/envlog.csv"; // CSV: ts_ms,tempC,hum,vpd
static const uint32_t RETAIN_MS = 48UL * 3600UL * 1000UL; // 48h
static uint32_t lastCompact = 0;
static const uint32_t COMPACT_EVERY_MS = 3600UL * 1000UL; // hourly
// Logging nur 1x pro Minute
const unsigned long LOG_INTERVAL_MS  = 60000; // 60 s
static unsigned long lastLog = 0;