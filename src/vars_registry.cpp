#include "vars_registry.h"
#include "globals.h"

// These are defined in main.cpp (kept there intentionally)
extern SensorReadings cur;
extern Targets target;
extern ShellySettings shelly;

// Average helpers are defined in your runtime module.
extern float avgTemp();
extern float avgHum();
extern float avgVPD();
extern float avgWaterTemp();

// -------------- small JSON helpers --------------
static String jBool(bool v) { return v ? "true" : "false"; }

static String jNumOrNull(float v, uint8_t dec = 2) {
  if (isnan(v) || isinf(v)) return "null";
  return String((double)v, (unsigned int)dec);
}

static String jInt(int v) { return String(v); }
static String jUInt(uint32_t v) { return String(v); }

static String jStr(const String& s) {
  String out = "\"";
  out.reserve(s.length() + 2);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  out += "\"";
  return out;
}

static String jMasked() { return jStr("********"); }

static String g_buildTag() {
    String tag = "\"BUILD_";
    tag += __DATE__;  // z.B. Jan 27 2026
    tag += "_";
    tag += __TIME__;  // z.B. 14:52:10
    tag += "\"";
    return tag;
}

static String jTimeOrNull(int h, int m) {
  if (h < 0 || m < 0) return "null";
  String s = "\"";
  s += String(h);
  s += ":";
  if (m < 10) s += "0";
  s += String(m);
  s += "\"";
  return s;
}

static String jShellyLine(const ShellyDevice& d) {
  // Uses day 0 because your schedule is "same for every day"
  const DailySchedule& ds = d.schedules.days[0];

  String line;
  line.reserve(48);

  line += d.ip;
  line += " | Gen ";
  line += String(d.gen);
  line += " | ON ";

  if (ds.onHour >= 0 && ds.onMinute >= 0) {
    line += String(ds.onHour);
    line += ":";
    if (ds.onMinute < 10) line += "0";
    line += String(ds.onMinute);
  } else {
    line += "--:--";
  }

  line += " | OFF ";

  if (ds.offHour >= 0 && ds.offMinute >= 0) {
    line += String(ds.offHour);
    line += ":";
    if (ds.offMinute < 10) line += "0";
    line += String(ds.offMinute);
  } else {
    line += "--:--";
  }

  return jStr(line);
}

// -------------- getters --------------

static String g_uptime() { return jUInt((uint32_t)(millis() / 1000UL)); }
static String g_heap() { return jUInt((uint32_t)ESP.getFreeHeap()); }
static String g_minheap() { return jUInt((uint32_t)ESP.getMinFreeHeap()); }
static String g_cpumhz() { return jUInt((uint32_t)ESP.getCpuFreqMHz()); }

static String g_wifiReady() { return jBool(wifiReady); }
static String g_espMode() { return jBool(espMode); }
static String g_ssid() { return jStr(ssidName); }

static String g_temp() { return jNumOrNull(cur.temperatureC, 1); }
static String g_hum() { return jNumOrNull(cur.humidityPct, 1); }
static String g_extTemp() { return jNumOrNull(cur.extTempC, 1); }
static String g_vpd() { return jNumOrNull(cur.vpdKpa, 2); }

static String g_tgtTemp() { return jNumOrNull(target.targetTempC, 1); }
static String g_tgtHum() { return jNumOrNull(target.targetHumPct, 1); }
static String g_tgtVpd() { return jNumOrNull(target.targetVpdKpa, 2); }

static String g_avgTemp() { return jNumOrNull(avgTemp(), 1); }
static String g_avgHum() { return jNumOrNull(avgHum(), 1); }
static String g_avgVpd() { return jNumOrNull(avgVPD(), 2); }
static String g_avgWater() { return jNumOrNull(avgWaterTemp(), 1); }

// Settings shortcuts
static String g_ui_box() { return jStr(settings.ui.boxName); }
static String g_ui_lang() { return jStr(settings.ui.language); }
static String g_ui_theme() { return jStr(settings.ui.theme); }
static String g_ui_unit() { return jStr(settings.ui.unit); }
static String g_ui_timeFmt() { return jStr(settings.ui.timeFormat); }
static String g_ui_ntp() { return jStr(settings.ui.ntpServer); }
static String g_ui_tz() { return jStr(settings.ui.tzInfo); }

static String g_grow_phase() { return jInt(settings.grow.currentPhase); }
static String g_grow_tTemp() { return jNumOrNull(settings.grow.targetTemperature, 1); }
static String g_grow_leafOff() { return jNumOrNull(settings.grow.offsetLeafTemperature, 1); }
static String g_grow_tVpd() { return jNumOrNull(settings.grow.targetVPD, 2); }

// Notifications (masked tokens)
static String g_notify_pushoverEnabled() { return jBool(settings.notify.pushoverEnabled); }
static String g_notify_pushoverAppKey() { return jMasked(); }
static String g_notify_pushoverUserKey() { return jMasked(); }
static String g_notify_pushoverDevice() { return jStr(settings.notify.pushoverDevice); }
static String g_notify_gotifyEnabled() { return jBool(settings.notify.gotifyEnabled); }
static String g_notify_gotifyServer() { return jStr(settings.notify.gotifyServer); }
static String g_notify_gotifyToken() { return jMasked(); }

// Shelly settings
// For the main device we also show schedule & generation in one line
static String g_sh_main_ip() { return jStr(settings.shelly.main.ip); }
static String g_sh_main_gen() { return jInt(settings.shelly.main.gen); }
static String g_sh_main_on()  { return jTimeOrNull(settings.shelly.main.schedules.days[0].onHour, settings.shelly.main.schedules.days[0].onMinute); }
static String g_sh_main_off() { return jTimeOrNull(settings.shelly.main.schedules.days[0].offHour, settings.shelly.main.schedules.days[0].offMinute); }
static String g_sh_main_line(){ return jShellyLine(settings.shelly.main); }
// For the light devices we show schedule & generation in one line
static String g_sh_light_ip()  { return jStr(settings.shelly.light.ip); }
static String g_sh_light_gen() { return jInt(settings.shelly.light.gen); }
static String g_sh_light_on()  { return jTimeOrNull(settings.shelly.light.schedules.days[0].onHour, settings.shelly.light.schedules.days[0].onMinute); }
static String g_sh_light_off() { return jTimeOrNull(settings.shelly.light.schedules.days[0].offHour, settings.shelly.light.schedules.days[0].offMinute); }
static String g_sh_light_line(){ return jShellyLine(settings.shelly.light); }
// For username/password we always return masked value
static String g_sh_user() { return jStr(settings.shelly.username); }
static String g_sh_pass() { return jMasked(); }

static String g_heating_relay() { return jInt(settings.heating.Relay); }

// Relay getters (state + name)
#define RELAY_GETTERS(i) \
  static String g_relay_state_##i() { return jBool(relayStates[i]); } \
  static String g_relay_name_##i() { return jStr(settings.relay.name[i]); } \
  static String g_relay_schen_##i() { return jBool(settings.relay.schedule[i].enabled); } \
  static String g_relay_schst_##i() { return jInt(settings.relay.schedule[i].onMin); } \
  static String g_relay_schen_##i##_end() { return jInt(settings.relay.schedule[i].offMin); }

RELAY_GETTERS(0)
RELAY_GETTERS(1)
RELAY_GETTERS(2)
RELAY_GETTERS(3)

const VarItem VARS[] = {
  {"debug.buildTag", g_buildTag, false, "debug"},
  // --- system ---
  {"sys.uptimeS", g_uptime, false, "system"},
  {"sys.freeHeap", g_heap, false, "system"},
  {"sys.minFreeHeap", g_minheap, false, "system"},
  {"sys.cpuMhz", g_cpumhz, false, "system"},

  // --- wifi ---
  {"wifi.ready", g_wifiReady, false, "wifi"},
  {"wifi.apMode", g_espMode, false, "wifi"},
  {"wifi.ssid", g_ssid, false, "wifi"},

  // --- sensors ---
  {"sensors.cur.temperatureC", g_temp, false, "sensors"},
  {"sensors.cur.humidityPct", g_hum, false, "sensors"},
  {"sensors.cur.extTempC", g_extTemp, false, "sensors"},
  {"sensors.cur.vpdKpa", g_vpd, false, "sensors"},

  // --- targets ---
  {"targets.tempC", g_tgtTemp, false, "targets"},
  {"targets.humPct", g_tgtHum, false, "targets"},
  {"targets.vpdKpa", g_tgtVpd, false, "targets"},

  // --- settings.ui ---
  {"settings.ui.boxName", g_ui_box, false, "settings.ui"},
  {"settings.ui.language", g_ui_lang, false, "settings.ui"},
  {"settings.ui.theme", g_ui_theme, false, "settings.ui"},
  {"settings.ui.unit", g_ui_unit, false, "settings.ui"},
  {"settings.ui.timeFormat", g_ui_timeFmt, false, "settings.ui"},
  {"settings.ui.ntpServer", g_ui_ntp, false, "settings.ui"},
  {"settings.ui.tzInfo", g_ui_tz, false, "settings.ui"},

  // --- settings.grow ---
  {"settings.grow.currentPhase", g_grow_phase, false, "settings.grow"},
  {"settings.grow.targetTemperature", g_grow_tTemp, false, "settings.grow"},
  {"settings.grow.offsetLeafTemperature", g_grow_leafOff, false, "settings.grow"},
  {"settings.grow.targetVPD", g_grow_tVpd, false, "settings.grow"},

  // --- notifications ---
  {"settings.notify.pushoverEnabled", g_notify_pushoverEnabled, false, "settings.notify"},
  {"settings.notify.pushoverAppKey", g_notify_pushoverAppKey, true, "settings.notify"},
  {"settings.notify.pushoverUserKey", g_notify_pushoverUserKey, true, "settings.notify"},
  {"settings.notify.pushoverDevice", g_notify_pushoverDevice, false, "settings.notify"},
  {"settings.notify.gotifyEnabled", g_notify_gotifyEnabled, false, "settings.notify"},
  {"settings.notify.gotifyServer", g_notify_gotifyServer, false, "settings.notify"},
  {"settings.notify.gotifyToken", g_notify_gotifyToken, true, "settings.notify"},

  // --- shelly ---
  {"settings.shelly.main.ip", g_sh_main_ip, false, "settings.shelly"},
  {"settings.shelly.main.gen", g_sh_main_gen, false, "settings.shelly"},
  {"settings.shelly.main.on",  g_sh_main_on,  false, "settings.shelly"},
  {"settings.shelly.main.off", g_sh_main_off, false, "settings.shelly"},
  {"settings.shelly.main.line", g_sh_main_line, false, "settings.shelly"},
  {"settings.shelly.light.ip", g_sh_light_ip, false, "settings.shelly"},
  {"settings.shelly.light.gen", g_sh_light_gen, false, "settings.shelly"},
  {"settings.shelly.light.on",  g_sh_light_on,  false, "settings.shelly"},
  {"settings.shelly.light.off", g_sh_light_off, false, "settings.shelly"},
  {"settings.shelly.light.line", g_sh_light_line, false, "settings.shelly"},
  {"settings.shelly.username", g_sh_user, false, "settings.shelly"},
  {"settings.shelly.password", g_sh_pass, true, "settings.shelly"},

  // --- heating relay ---
  {"settings.heatingrelay", g_heating_relay, false, "settingsheating"},

  // --- relays (names + states + schedule) ---
  {"relays[0].name", g_relay_name_0, false, "relays"},
  {"relays[0].state", g_relay_state_0, false, "relays"},
  {"relays[0].schedule.enabled", g_relay_schen_0, false, "relays"},
  {"relays[0].schedule.start", g_relay_schst_0, false, "relays"},
  {"relays[0].schedule.end", g_relay_schen_0_end, false, "relays"},

  {"relays[1].name", g_relay_name_1, false, "relays"},
  {"relays[1].state", g_relay_state_1, false, "relays"},
  {"relays[1].schedule.enabled", g_relay_schen_1, false, "relays"},
  {"relays[1].schedule.start", g_relay_schst_1, false, "relays"},
  {"relays[1].schedule.end", g_relay_schen_1_end, false, "relays"},

  {"relays[2].name", g_relay_name_2, false, "relays"},
  {"relays[2].state", g_relay_state_2, false, "relays"},
  {"relays[2].schedule.enabled", g_relay_schen_2, false, "relays"},
  {"relays[2].schedule.start", g_relay_schst_2, false, "relays"},
  {"relays[2].schedule.end", g_relay_schen_2_end, false, "relays"},

  {"relays[3].name", g_relay_name_3, false, "relays"},
  {"relays[3].state", g_relay_state_3, false, "relays"},
  {"relays[3].schedule.enabled", g_relay_schen_3, false, "relays"},
  {"relays[3].schedule.start", g_relay_schst_3, false, "relays"},
  {"relays[3].schedule.end", g_relay_schen_3_end, false, "relays"},

};

const size_t VARS_COUNT = sizeof(VARS) / sizeof(VARS[0]);
