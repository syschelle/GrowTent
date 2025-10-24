#pragma once
#include <Arduino.h>

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

// Relay pin definitions
static constexpr uint8_t RELAY1_PIN = 32;
static constexpr uint8_t RELAY2_PIN = 33;
static constexpr uint8_t RELAY3_PIN = 25;
static constexpr uint8_t RELAY4_PIN = 26;

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
