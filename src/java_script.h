// java_script.h 
#pragma once

// Pure JavaScript payload (no <script> tags, no HTML comments)
const char jsContent[] PROGMEM = R"rawliteral(
// === Inject i18n JSON <script type="application/json"> tags ===
(function injectI18N(){
  const addJSON = (id, obj) => {
    const s = document.createElement('script');
    s.type = 'application/json';
    s.id   = id;
    s.textContent = JSON.stringify(obj);
    document.head.appendChild(s);
  };

  addJSON('i18n-manifest', {
    "default": "de",
    "languages": [
      { "code": "de", "name": "Deutsch", "dir": "ltr" },
      { "code": "en", "name": "English", "dir": "ltr" }
    ]
  });

  addJSON('i18n', {

  /* -------------------- a11y / info -------------------- */
  "a11y.menu": { de: "Menü öffnen/schließen", en: "Open/close menu" },

  "info.Grow": { de: "Aktueller Grow", en: "Current Grow" },
  "info.Days": { de: "Tage", en: "Days" },
  "info.Weeks": { de: "Wochen", en: "Weeks" },
  "info.growLine": {
    de: "Grow seit: {days} {daysLabel} | {weeks} {weeksLabel}",
    en: "Growing since: {days} {daysLabel} | {weeks} {weeksLabel}"
  },

  "settings.unsaved": {
    de: "Änderungen – bitte speichern",
    en: "Changes pending – please save"
  },

  /* -------------------- Navigation -------------------- */
  "nav.status":      { de: "Status", en: "Status" },
  "nav.runsetting":  { de: "Betriebseinstellungen", en: "Operating Settings" },
  "nav.shelly":      { de: "Shelly Einstellungen", en: "Shelly Settings" },
  "nav.settings":    { de: "Systemeinstellungen", en: "System Settings" },
  "nav.message":     { de: "Push-Einstellungen", en: "Push Settings" },
  "nav.logging":     { de: "Systemprotokoll", en: "System Log" },
  "nav.factory":     { de: "Werkseinstellungen", en: "Factory Reset" },
  "nav.diary":      { de: "Grow Tagebuch", en: "Grow Diary" },

  /* -------------------- Status -------------------- */
  "status.title": { de: "Status", en: "Status" },
  "status.updated": { de: "Letztes Update:", en: "Last update:" },
  "status.download": { de: "History herunterladen", en: "Download History" },
  "status.delete": { de: "History löschen", en: "Delete History" },
  "status.currentValues": { de: "Aktuelle Werte", en: "Current values" },

  "status.temperature": { de: "Temperatur", en: "Temperature" },
  "status.targetTemp": { de: "Soll-Temperatur", en: "Target temperature" },
  "status.last": { de: "akt. ", en: "current " },

  "status.lastWaterTemperature": {
    de: "akt. Wassertemperatur",
    en: "current Water Temperature"
  },

  "status.humidity": { de: "Luftfeuchte", en: "Humidity" },
  "status.lastvpd": { de: "akt. VPD", en: "current VPD" },
  "status.targetVpd": { de: "Soll-VPD:", en: "Target VPD" },

  "status.averagesLastHour": {
    de: "Durchschnittswerte der letzten Stunde",
    en: "Averages last hour"
  },
  "status.history": { de: "Verlauf (Aufzeichnung alle 10 Minuten)", en: "History (recorded every 10 minutes)" },
  "status.refresh": { de: "Aktualisieren", en: "Refresh" },

  "status.avgTemperature": { de: "Ø Temperatur", en: "Ø Temperature" },
  "status.avgHumidity": { de: "Ø Luftfeuchte", en: "Ø Humidity" },
  "status.avgVpd": { de: "Ø VPD", en: "Ø VPD" },
  "status.avg": { de: "Ø ", en: "Ø " },
  "status.avgWaterTemperature": {
    de: "Ø Wassertemperatur",
    en: "Ø Water temperature"
  },

  /* Relay / Irrigation */
  "status.relayControl": { de: "Relais Steuerung", en: "Relay Control" },
  "status.toggleRelay": { de: "umschalten", en: "toggle" },
  "status.toggleRelayLabel": { de: "umschalten", en: "toggle" },

  /* Shelly */
  "status.shellyControl": { de: "Shelly Steuerung", en: "Shelly Control" },
  "status.shellyMainSw": { de: "Hauptschalter", en: "Main Switch" },
  "status.shellyHeater": { de: "Heizung", en: "Heater" },
  "status.shellyHumidifier": { de: "Luftbefeuchter", en: "Humidifier" },
  "status.shellyFan": { de: "Ventilator", en: "Fan" },
  "status.shellyLight": { de: "Pflanzlicht", en: "Grow Light" },
  "status.shellyAuth": { de: "Authentifizierung", en: "Authentication" },
  "status.shellyDevices": { de: "Shelly Geräte", en: "Shelly Devices" },

  "status.relayOn": { de: "einschalten (10s)", en: "turn on (10s)" },

  /* -------------------- runsetting.* -------------------- */
  "runsetting.title": { de: "Betriebseinstellungen", en: "Operating settings" },
  "runsetting.newGrowError": {"de": "Fehler: Neuer Grow konnte nicht gesetzt werden.", "en": "Error: could not set new grow."},
  "runsetting.newGrowDone": {"de": "Neuer Grow wurde gesetzt.", "en": "New grow has been set."},
  "runsetting.newGrowWorking": {"de": "Setze neuen Grow…", "en": "Setting new grow…"},
  "runsetting.newGrowConfirm": {"de": "Neuen Grow starten?\n\n• Startdatum = heute\n• Blüte/Trocknung wird geleert\n• Energieanzeige wird zurückgesetzt", "en": "Start a new grow?\n\n• Start date = today\n• Flowering/Drying cleared\n• Energy display reset"},
  "runsetting.newGrowBtn": {"de": "Neuer Grow", "en": "New grow"},
  "runsetting.startGrow": { de: "Startdatum:", en: "Start Date:" },
  "runsetting.startFlower": { de: "Startdatum Blüte:", en: "Start Flowering Date:" },
  "runsetting.startDry": { de: "Startdatum Trocknung:", en: "Start Drying Date:" },

  "runsetting.phase": { de: "aktuelle Phase:", en: "Current Phase:" },
  "runsetting.phase.grow": { de: "VEGETATIV", en: "VEGETATIVE" },
  "runsetting.phase.flower": { de: "BLÜTE", en: "FLOWERING" },
  "runsetting.phase.dry": { de: "TROCKNUNG", en: "DRYING" },

  "runsetting.targetTemp": { de: "Soll-Temperatur", en: "Target temperature" },
  "runsetting.offsetLeafTemperature": {
    de: "Offset Blatttemperatur:",
    en: "Offset leaf temperature"
  },
  "runsetting.targetVPD": { de: "Soll-VPD", en: "Target VPD" },

  "runsetting.relayScheduling": {
    de: "Relais Zeitsteuerung",
    en: "Relay Scheduling"
  },
  "runsetting.relay.enable": { de: "Aktivieren:", en: "Enable:" },
  "runsetting.relay.start": { de: "Start:", en: "Start:" },
  "runsetting.relay.stop": { de: "Stopp:", en: "Stop:" },

  "runsetting.relay.minutesHint": {
  de: "Minutenformat: 0–59 (Minute innerhalb der Stunde).",
  en: "Minute format: 0–59 (minute within the hour)."
  },
  "runsetting.relay.ifLightOff": { de: "auch wenn Licht aus", en: "also if light off" },
  "runsetting.relay.onMinute": { de: "Einschaltminute", en: "On minute" },
  "runsetting.relay.offMinute": { de: "Ausschaltminute", en: "Off minute" },
  "runsetting.relay.enabledShort": { de: "Aktiv", en: "Enabled" },

  /* -------------------- shelly.* -------------------- */
  "shelly.title": { de: "Shelly Einstellungen", en: "Shelly Settings" },
  "shelly.shellyIP": { de: "Shelly IP Adresse:", en: "Shelly IP address:" },

  "shelly.shellyUsername": {
    de: "Shelly Benutzername (optional):",
    en: "Username (optional):"
  },
  "shelly.shellyPassword": {
    de: "Shelly Passwort (optional):",
    en: "Password (optional):"
  },

  "shelly.shellyIPNames": { de: "Shelly Gerätenamen:", en: "Shelly device names:" },
  "shelly.shellyIPNamesHeater": { de: "Heizung:", en: "Heater:" },

  /* -------------------- settings.* -------------------- */
  "settings.title": { de: "Systemeinstellungen", en: "Settings" },
  "settings.debugEnabled": { de: "Debug aktivieren", en: "Enable debug" },

  "settings.boxName": { de: "Boxname:", en: "Box name:" },
  "settings.boxName.ph": { de: "z. B. Growtent-1", en: "e.g. Growtent-1" },

  "settings.ntpserver": { de: "NTP-Server:", en: "NTP server:" },
  "settings.ntpserver.ph": { de: "z. B. de.pool.ntp.org", en: "e.g. pool.ntp.org" },

  "settings.timeZoneInfo": { de: "Zeitzone:", en: "Time zone:" },
  "settings.timeZoneInfo.ph": {
    de: "z.B. WEST-1D/WEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00",
    en: "e.g. WEST-1D/WEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00"
  },

  "settings.language": { de: "Sprache:", en: "Language:" },
  "settings.theme": { de: "Theme:", en: "Theme:" },
  "settings.themeLight": { de: "Hell", en: "Light" },
  "settings.themeDark": { de: "Dunkel", en: "Dark" },

  "settings.dateFormat": { de: "Datumsformat:", en: "Date format:" },
  "settings.df_ymd": { de: "YYYY-MM-DD", en: "YYYY-MM-DD" },
  "settings.df_dmy": { de: "DD.MM.YYYY", en: "DD.MM.YYYY" },

  "settings.timeFormat": { de: "Zeitformat:", en: "Time format:" },
  "settings.tf_HHmm": { de: "24h", en: "24h" },
  "settings.tf_hhmma": { de: "12h AM/PM", en: "12h AM/PM" },

  "settings.save": { de: "Speichern", en: "Save" },

  "settings.tempUnit": { de: "Temperatur-Einheit:", en: "Temperature unit:" },
  "settings.celsius": { de: "°C (Celsius)", en: "°C (Celsius)" },
  "settings.fahrenheit": { de: "°F (Fahrenheit)", en: "°F (Fahrenheit)" },

  "settings.DS18B20": { de: "DS18B20 Sensor", en: "DS18B20 Sensor" },
  "settings.DS18B20Address": { de: "DS18B20 Name:", en: "DS18B20 Name:" },

  "settings.relaySettings": {
    de: "Relais Einstellungen",
    en: "Relay Settings"
  },
  "settings.relay1": { de: "Relaisname 1:", en: "Relay name 1:" },
  "settings.relay2": { de: "Relaisname 2:", en: "Relay name 2:" },
  "settings.relay3": { de: "Relaisname 3:", en: "Relay name 3:" },
  "settings.relay4": { de: "Relaisname 4:", en: "Relay name 4:" },

  /* -------------------- message.* -------------------- */
  "message.title": { de: "Nachrichteneinstellungen", en: "Message Settings" },
  "message.enabled": { de: "aktivieren", en: "Enable" },

  "message.pushoverSettings": { de: "Pushover Einstellungen", en: "Pushover settings" },
  "message.pushoverUserKey": { de: "Pushover Benutzer:", en: "Pushover User:" },
  "message.pushoverUserKey.ph": { de: "Your User Key", en: "Your User Key" },

  "message.pushoverAppKey": { de: "Pushover App Token:", en: "Pushover App Token:" },
  "message.pushoverAppKey.ph": { de: "API Token/Key", en: "API Token/Key" },

  "message.pushoverDevice": {
    de: "Pushover Gerät (optional):",
    en: "Pushover Device (optional):"
  },
  "message.pushoverDevice.ph": { de: "z. B. mein-telefon", en: "e.g. my-phone" },

  "message.gotifySettings": { de: "Gotify Einstellungen", en: "Gotify settings" },
  "message.gotifyServer": { de: "Gotify Server URL:", en: "Gotify Server URL:" },
  "message.gotifyUrl.ph": {
    de: "z. B. https://gotify.me/message",
    en: "e.g. https://gotify.me/message"
  },

  "message.gotifyToken": { de: "Gotify App Token:", en: "Gotify App Token:" },
  "message.gotifyToken.ph": { de: "z. B. a1b2c3", en: "e.g. a1b2c3" },

  /* -------------------- Vars / Logging / Factory -------------------- */
  "vars.variables": { de: "Variablen", en: "Variables" },
  "vars.hint": {
    de: "Debug-Ansicht: alle registrierten Werte (automatisch aus /api/state). Tokens/Passwörter werden maskiert.",
    en: "Debug view: all registered values (automatically from /api/state). Tokens/passwords are masked."
  },

  "logging.title": { de: "Systemprotokoll", en: "Logging Settings" },

  "factory.title": { de: "Werkseinstellungen", en: "Factory Settings" },
  "factory.reset": { de: "Zurücksetzen / Neustart", en: "Reset / Restart" },

  /* -------------------- diary.* -------------------- */
  "diary.title": { de: "Grow Tagebuch", en: "Grow Diary" },
  "diary.total": { de: "Gesamtgrow", en: "Total grow" },
  "diary.phase": { de: "Phase", en: "Phase" },
  "diary.day": { de: "Tag", en: "Day" },
  "diary.week": { de: "Woche", en: "Week" },
  "diary.note": { de: "Notiz (max. 400 Zeichen)", en: "Note (max 400 characters)" },
  "diary.note.ph": { de: "z. B. Gießen, Dünger, Beobachtungen…", en: "e.g. watering, nutrients, observations…" },
  "diary.save": { de: "Eintrag speichern", en: "Save entry" },
  "diary.download": { de: "CSV herunterladen", en: "Download CSV" },
  "diary.clear": { de: "Tagebuch löschen", en: "Clear diary" },
  "diary.saved": { de: "Gespeichert ✓", en: "Saved ✓" },
  "diary.error": { de: "Fehler beim Speichern", en: "Save failed" },
  "diary.confirmClear": { de: "Wirklich alle Tagebuch-Einträge löschen?", en: "Really clear all diary entries?" },

  /* -------------------- Hints -------------------- */
  "hint.systemStarted": { de: "System gestartet", en: "System started" },
  "hint.saved": { "de": "Gespeichert ✓", "en": "Saved ✓" },
  "hint.unsaved": { "de": "Änderungen – bitte speichern", "en": "Changes pending – please save" },

  });

  
})();

// ---------- Sensor polling ----------
window.sensorTimer = null;
// Sensor poll interval (ms). We'll dynamically slow down when Status page is not visible.
window._sensorPollMs = 10000;

// ---- relay state (NUM_RELAYS = 8 on firmware side) ----
const RELAY_COUNT = 8;
let relayStates = Array(RELAY_COUNT).fill(false);

// Funktion zum Aktualisieren der Sensorwerte
window._stopSensorPoll = function () {
  if (window.sensorTimer) {
    clearInterval(window.sensorTimer);
    window.sensorTimer = null;
  }
};

window._startSensorPoll = function () {
  if (!window.sensorTimer && typeof window.updateSensorValues === 'function') {
    window.sensorTimer = setInterval(window.updateSensorValues, window._sensorPollMs || 10000);
  }
};

// Change poll interval without duplicating timers
window._setSensorPollInterval = function(ms){
  const next = Math.max(2000, Number(ms) || 10000);
  if (next === window._sensorPollMs) return;
  window._sensorPollMs = next;
  if (window.sensorTimer) {
    clearInterval(window.sensorTimer);
    window.sensorTimer = null;
    window._startSensorPoll();
  }
};

// Aktualisiert die Relay-Buttons im UI
window.updateRelayButtons = function() {
  // cache relay elements (avoid repeated DOM lookups + layout thrash)
  if (!window._relayEls || window._relayEls.length !== relayStates.length) {
    window._relayEls = Array.from({ length: relayStates.length }, (_, i) =>
      document.getElementById(`relay-Status${i + 1}`)
    );
  }
  for (let i = 0; i < relayStates.length; i++) {
    const btn = window._relayEls[i];
    if (!btn) continue;
    if (relayStates[i]) {
      btn.classList.add('on');
      btn.classList.remove('off');
    } else {
      btn.classList.add('off');
      btn.classList.remove('on');
    }
  }
};

// Relay umschalten
window.toggleRelay = function(nr) {
  const idx = nr - 1;
  fetch(`/relay/${nr}/toggle`, { method: 'POST' })
    .then(r => r.json())
    .then(data => {
      if (typeof data.state !== 'undefined') {
        relayStates[idx] = !!data.state;
      } else {
        relayStates[idx] = !relayStates[idx];
      }
      updateRelayButtons();
    })
    .catch(err => {
      console.error('toggle relay failed:', err);
    });
};

// ---------- Shelly status update ----------
function setShellyStateClass(el, isOn) {
  if (!el) return;

  el.classList.remove('shelly-on', 'shelly-off');
  el.classList.add(isOn ? 'shelly-on' : 'shelly-off');
}

window.toggleShellyRelay = async function(device) {
  const url = (device === 'heater')
    ? '/shelly/heater/toggle'
    : (device === 'humidifier')
      ? '/shelly/humidifier/toggle'
      : (device === 'light')
        ? '/shelly/light/toggle'
        : null;

  if (!url) {
    console.error('[SHELLY][JS] Unknown device:', device);
    return;
  }

  // stop polling while toggling
  window._stopSensorPoll?.();

  try {
    const res = await fetch(url, { method: 'POST', cache: 'no-store' });

    if (!res.ok) {
      console.error('[SHELLY][JS] Toggle failed:', res.status);
      return;
    }

    console.log('[SHELLY][JS] Toggle OK:', device);

    // refresh once AFTER toggle finished
    if (typeof window.updateSensorValues === 'function') {
      setTimeout(() => window.updateSensorValues(), 1200);
    }
  } catch (err) {
    console.error('[SHELLY][JS] Toggle exception:', err);
  } finally {
    // restart polling after everything settled
    setTimeout(() => window._startSensorPoll?.(), 2000);
  }
};

// Run after DOM is ready
window.addEventListener('DOMContentLoaded', () => {

  // ---------- Chart style cache (MUST be before applyTheme) ----------
  let chartStrokeCache = null;

  function getChartStroke(){
    if (chartStrokeCache) return chartStrokeCache;
    const css = getComputedStyle(document.documentElement);
    chartStrokeCache =
      (css.getPropertyValue('--link') ||
       css.getPropertyValue('--text') ||
       '#888').trim();
    return chartStrokeCache;
  }

  function invalidateChartStyleCache(){
    chartStrokeCache = null;
  }  

  // ---------- Small DOM helpers ----------
  const $  = (id) => document.getElementById(id);
  const setText = (id, val) => {
    const el = $(id);
    if (!el) return;
    const next = (val == null) ? '' : String(val);
    // avoid pointless DOM writes (each write can trigger style/layout)
    if (el.textContent !== next) el.textContent = next;
  };
  const isNum = x => typeof x === 'number' && !Number.isNaN(x);
  const escapeHtml = (s) => {
    const str = String(s);
    return str.replace(/[&<>"']/g, ch => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
    }[ch]));
  };


  const escAttr = (s) => escapeHtml(String(s));
  // ===================================================================
  //  Unsaved/Status hint (8s, resets on new message)
  //  Target element: <span id="unsavedHint" class="dirty-hint" hidden ...>
  // ===================================================================
  const unsavedHintEl = document.getElementById('unsavedHint');
  let _unsavedHintTimer = null;

  /**
   * Show a status hint for a limited time (default 8s). If called again within
   * that time, the previous timer is cleared and the new message stays visible
   * for the full duration.
   */
  function showStatusHint(message, { durationMs = 8000 } = {}) {
    if (!unsavedHintEl) return;

    if (_unsavedHintTimer) {
      clearTimeout(_unsavedHintTimer);
      _unsavedHintTimer = null;
    }

    const msg = (message == null) ? '' : String(message);
    if (!msg.trim()) {
      unsavedHintEl.hidden = true;
      unsavedHintEl.classList.remove('is-visible');
      return;
    }

    unsavedHintEl.textContent = msg;
    unsavedHintEl.hidden = false;
    unsavedHintEl.classList.add('is-visible');

    _unsavedHintTimer = setTimeout(() => {
      unsavedHintEl.hidden = true;
      unsavedHintEl.classList.remove('is-visible');
      _unsavedHintTimer = null;
    }, durationMs);
  }

  /**
   * Convenience helper: shows the localized "settings.unsaved" text plus optional details.
   * details can be a string or an object like { temp: 23.1, vpd: 1.2 }.
   */
  function formatI18n(key, details = {}) {
    const tpl = (typeof I18N === 'object' && I18N && I18N[key]) ? I18N[key] : key;
    return String(tpl).replace(/\{(\w+)\}/g, (_, k) => {
      const v = details?.[k];
      return (v === null || typeof v === 'undefined') ? '' : String(v);
    });
  }


  // ===================================================================
  //  Hint polling from ESP32 WebServer endpoint (/api/hint)
  //  ESP should return: {"id":123,"message":"..."}
  //  - When id changes, message is shown for 8s (and resets if new comes)
  // ===================================================================
  (function pollHint(){
     let lastId = -1;

     async function tick(){
       try {
         const r = await fetch('/api/hint', { cache: 'no-store' });
         if (!r.ok) return;
         const d = await r.json();

         if (typeof d.id === 'number' && d.id !== lastId) {
           lastId = d.id;

           const msg = formatI18n(d.key, d.details || {});
           if (msg && msg.trim()) window.showStatusHint(msg); // 8s + reset
         }
       } catch(e) {}
     }

     tick();
     setInterval(tick, 1000);
  })();

  // ---------- Sidebar & SPA ----------
  const mqDesktop = window.matchMedia('(min-width:1024px)');
  const sidebar   = $('sidebar');
  const overlay   = $('overlay');
  const burgerBtn = $('hamburgerBtn');
  const pages     = document.querySelectorAll('.page');

  function openSidebar(){ if(mqDesktop.matches) return; sidebar?.classList.add('sidebar--open'); overlay?.classList.add('overlay--show'); burgerBtn?.setAttribute('aria-expanded','true'); }
  function closeSidebar(){ sidebar?.classList.remove('sidebar--open'); overlay?.classList.remove('overlay--show'); burgerBtn?.setAttribute('aria-expanded','false'); }
  function toggleSidebar(){ if(mqDesktop.matches) return; const o = sidebar?.classList.contains('sidebar--open'); o ? closeSidebar() : openSidebar(); }
  function syncLayout(){ if(mqDesktop.matches){ closeSidebar(); } }

  burgerBtn?.addEventListener('click', toggleSidebar);
  overlay?.addEventListener('click', closeSidebar);
  window.addEventListener('keydown', e => { if(e.key === 'Escape') closeSidebar(); });
  mqDesktop.addEventListener('change', syncLayout);

  // ---------- SPA navigation ----------
  function activatePage(id){
    pages.forEach(p => p.classList.remove('active'));
    document.querySelectorAll('.navlink').forEach(a => a.removeAttribute('aria-current'));
    const pageEl = $(id);
    if (pageEl) pageEl.classList.add('active');
    const currentLink = sidebar?.querySelector(`.navlink[data-page="${id}"]`);
    currentLink?.setAttribute('aria-current', 'page');
    onPageChanged(id);
  }

  sidebar?.addEventListener('click', e => {
    const link = e.target.closest('.navlink'); if(!link) return;
    const id = link.getAttribute('data-page');
    activatePage(id);
    closeSidebar();
  });
  syncLayout();

  // ---------- Theme ----------
  function applyTheme(theme){
    document.documentElement.setAttribute('data-theme', theme);
    localStorage.setItem('theme', theme);
    const sel = $('theme');
    if (sel && sel.value !== theme) sel.value = theme;

    // charts: avoid repeated getComputedStyle cost after theme changes
    try { invalidateChartStyleCache(); } catch(e) {}
    window.updateHistoryCharts?.(true);
  }
  (function initTheme(){
    const saved = localStorage.getItem('theme');
    const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
    applyTheme(saved || (prefersDark ? 'dark' : 'light'));
  })();
  $('theme')?.addEventListener('change', e => applyTheme(e.target.value));

  // ---------- Date / Time helpers ----------
  function pad2(n){ return String(n).padStart(2,'0'); }


// ---------- Light schedule helpers (ON time + day hours -> OFF time) ----------
function parseHHMM(str){
  // Accepts "H:MM" or "HH:MM"
  const s = String(str || '').trim();
  const m = s.match(/^(\d{1,2}):(\d{2})$/);
  if (!m) return null;
  const hh = Number(m[1]), mm = Number(m[2]);
  if (!Number.isFinite(hh) || !Number.isFinite(mm)) return null;
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return null;
  return hh * 60 + mm;
}

function fmtHHMM(totalMin){
  const t = ((totalMin % 1440) + 1440) % 1440;
  const hh = Math.floor(t / 60);
  const mm = t % 60;
  return `${hh}:${pad2(mm)}`;
}

function clampInt(x, lo, hi){
  const n = Number(x);
  if (!Number.isFinite(n)) return lo;
  return Math.min(hi, Math.max(lo, Math.round(n)));
}
  function getDefaultDateFormatFor(lang){ return lang === 'de' ? 'DD.MM.YYYY' : 'YYYY-MM-DD'; }
  function getDefaultTimeFormatFor(lang){ return lang === 'de' ? 'HH:mm' : 'hh:mm A'; }
  function formatDateWithPattern(d, pat){
    const y=d.getFullYear(), m=pad2(d.getMonth()+1), day=pad2(d.getDate());
    return pat === 'DD.MM.YYYY' ? `${day}.${m}.${y}` : `${y}-${m}-${day}`;
  }
  function formatTimeWithPattern(d, pat){
    let h=d.getHours(); const m=pad2(d.getMinutes()); const s=pad2(d.getSeconds());
    if(pat.includes('A')){ const ap=h>=12?'PM':'AM'; let hh=h%12; if(hh===0) hh=12; hh=pad2(hh); return `${hh}:${m}${pat.includes('ss')?':'+s:''} ${ap}`; }
    const HH=pad2(h); return `${HH}:${m}${pat.includes('ss')?':'+s:''}`;
  }
  function getCurDateFmt(){ return localStorage.getItem('dateFormat') || getDefaultDateFormatFor(currentLang || 'de'); }
  function getCurTimeFmt(){ return localStorage.getItem('timeFormat') || getDefaultTimeFormatFor(currentLang || 'de'); }
  function renderHeaderDateTime(){
    const d=new Date();
    const hd=$('headerDate');
    const ht=$('headerTime');
    if(!hd || !ht) return;
    hd.textContent = formatDateWithPattern(d, getCurDateFmt());
    ht.textContent = formatTimeWithPattern(d, getCurTimeFmt());
  }
  $('dateFormat')?.addEventListener('change', e => { localStorage.setItem('dateFormat', e.target.value); renderHeaderDateTime(); });
  $('timeFormat')?.addEventListener('change', e => { localStorage.setItem('timeFormat', e.target.value); renderHeaderDateTime(); });
  setInterval(renderHeaderDateTime, 1000);

  // ---------- Date input localization ----------
  const DATE_INPUT_IDS = ['webGrowStart','webFloweringStart','webDryingStart'];

  function setDateInputsLang(lang) {
    DATE_INPUT_IDS.forEach(id => {
      const el = document.getElementById(id);
      if (el) el.setAttribute('lang', lang || 'de');
    });
  }

  function updateDatePreview(inputId, previewId) {
    const inp = document.getElementById(inputId);
    const prev = document.getElementById(previewId);
    if (!inp || !prev) return;
    const val = inp.value;
    if (!val) { prev.textContent = '—'; return; }
    const dt = new Date(val + 'T12:00:00');
    prev.textContent = formatDateWithPattern(dt, getCurDateFmt());
  }

  function rerenderDatePreviews() {
    updateDatePreview('webGrowStart', 'prevGrowStart');
    updateDatePreview('webFloweringStart', 'prevFloweringStart');
    updateDatePreview('webDryingStart', 'prevDryingStart');
  }

  // Initial setup
  rerenderDatePreviews();

  // Event listeners for date inputs
  document.getElementById('dateFormat')?.addEventListener('change', e => {
    localStorage.setItem('dateFormat', e.target.value);
    renderHeaderDateTime();
    setDateInputsLang(currentLang);
    rerenderDatePreviews();
  });

  // beim Sprachwechsel -> applyTranslations patchen
  (function patchApplyTranslations(){
    const _origApply = applyTranslations;
    applyTranslations = function(){
      _origApply();
      setDateInputsLang(currentLang);
      rerenderDatePreviews();
    };
  })();

  // ---------- Temperature unit ----------
  function getTempUnit(){ return localStorage.getItem('tempUnit') || (currentLang === 'en' ? 'F' : 'C'); }
  function setTempUnit(unit){
    localStorage.setItem('tempUnit', unit);
    const sel=$('tempUnit'); if(sel && sel.value !== unit) sel.value = unit;
  }
  $('tempUnit')?.addEventListener('change', e => setTempUnit(e.target.value));

  // ---------- i18n ----------
  let manifest=null, I18N_RAW={}, I18N={}, currentLang='de';
  function readJsonTag(id){
    const el=$(id); if(!el) throw new Error('Missing tag: '+id);
    return JSON.parse(el.textContent.trim());
  }
  function buildLanguageSelect(activeCode){
    const sel=$('language'); if(!sel) return;
    sel.innerHTML='';
    (manifest.languages || [{code:'de',name:'Deutsch'},{code:'en',name:'English'}]).forEach(({code,name})=>{
      const opt=document.createElement('option'); opt.value=code; opt.textContent=name||code.toUpperCase(); sel.appendChild(opt);
    });
    if(activeCode) sel.value=activeCode;
    sel.onchange = e => setLanguage(e.target.value);
  }
  function applyTranslations(){
    document.querySelectorAll('[data-i18n]').forEach(el=>{
      const key=el.getAttribute('data-i18n');
      const attr=el.getAttribute('data-i18n-attr');
      const val=I18N[key];
      if(val!==undefined){ if(attr){ el.setAttribute(attr,val); } else { el.textContent=val; } }
    });
    rerenderDatePreviews();
    const df=$('dateFormat');
    if(df){ const saved=localStorage.getItem('dateFormat')||getDefaultDateFormatFor(currentLang); if(df.value!==saved) df.value=saved; }
    const tf=$('timeFormat');
    if(tf){ const saved=localStorage.getItem('timeFormat')||getDefaultTimeFormatFor(currentLang); if(tf.value!==saved) tf.value=saved; }
    const tu=$('tempUnit'); if(tu){ const savedTU=getTempUnit(); if(tu.value!==savedTU) tu.value=savedTU; }
    renderHeaderDateTime();
  }
  function setLanguage(code){
    try { I18N_RAW = readJsonTag('i18n'); }
    catch { I18N_RAW = {}; }

    currentLang = code || 'de';

    // flatten: I18N[key] = string for currentLang (with fallbacks)
    I18N = {};
    for (const [k, v] of Object.entries(I18N_RAW)) {
      if (v && typeof v === 'object') {
        I18N[k] = v[currentLang] ?? v.de ?? v.en;
      } else {
        // allow legacy plain strings if any slipped in
        I18N[k] = v;
      }
    }
    
    localStorage.setItem('lang', currentLang);
    if(!localStorage.getItem('dateFormat')) localStorage.setItem('dateFormat', getDefaultDateFormatFor(currentLang));
    if(!localStorage.getItem('timeFormat')) localStorage.setItem('timeFormat', getDefaultTimeFormatFor(currentLang));
    if(!localStorage.getItem('tempUnit'))   localStorage.setItem('tempUnit', currentLang === 'en' ? 'F' : 'C');

    validateI18n();
    applyTranslations();

    const sel=$('language'); if(sel && sel.value !== currentLang) sel.value = currentLang;
  }
  (function initI18n(){
    try{ manifest = readJsonTag('i18n-manifest'); }
    catch{ manifest = { default:'de', languages:[{code:'de',name:'Deutsch'},{code:'en',name:'English'}] }; }
    const urlLang   = new URLSearchParams(location.search).get('lang');
    const savedLang = localStorage.getItem('lang');
    const initial   = urlLang || savedLang || (manifest.default || 'de');
    buildLanguageSelect(initial);
    setLanguage(initial);
  

async function startNewGrow(){
  const msgObj = (typeof I18N_RAW==='object' && I18N_RAW) ? I18N_RAW['runsetting.newGrowConfirm'] : null;
  const msg = (msgObj && (msgObj[currentLang] || msgObj['de'] || msgObj['en'])) ||
              'Neuer Grow starten? (setzt Startdatum auf heute, löscht Blüte/Trocknung, setzt Energie zurück)';
  if (!confirm(msg)) return;

  try{
    showStatusHint((I18N_RAW['runsetting.newGrowWorking']?.[currentLang] || I18N_RAW['runsetting.newGrowWorking']?.de || 'Setze neuen Grow...'), {durationMs: 8000});
    const r = await fetch('/api/newgrow', { method:'POST' });
    if(!r.ok) throw new Error('HTTP '+r.status);
    const d = await r.json();
    if(!d.ok) throw new Error('API');
    // Update UI fields immediately
    const gs = document.getElementById('webGrowStart'); if(gs) gs.value = d.startDate || '';
    const fs = document.getElementById('webFloweringStart'); if(fs) fs.value = '';
    const ds = document.getElementById('webDryingStart'); if(ds) ds.value = '';
    const ps = document.getElementById('phaseSelect');
    if(ps){ ps.value = '1'; ps.dispatchEvent(new Event('change')); }

    showStatusHint((I18N_RAW['runsetting.newGrowDone']?.[currentLang] || I18N_RAW['runsetting.newGrowDone']?.de || 'Neuer Grow gesetzt.'), {durationMs: 6000});
  }catch(e){
    console.warn(e);
    showStatusHint((I18N_RAW['runsetting.newGrowError']?.[currentLang] || I18N_RAW['runsetting.newGrowError']?.de || 'Fehler beim Setzen des neuen Grows.'), {durationMs: 9000});
  }
}



  // expose for onclick handlers
  window.startNewGrow = startNewGrow;
})();

  // ---------- i18n validation ----------
  function validateI18n(){
    // nur prüfen, wenn wir das RAW-Objekt haben
    if (!I18N_RAW || typeof I18N_RAW !== 'object') return;

    const missingKey = new Set();
    const missingLang = new Set();

    document.querySelectorAll('[data-i18n]').forEach(el => {
      const key = el.getAttribute('data-i18n');
      if (!key) return;

      const entry = I18N_RAW[key];

      // Key fehlt komplett
      if (typeof entry === 'undefined') {
        missingKey.add(key);
        return;
      }

      // Entry ist {de,en} aber aktuelle Sprache fehlt
      if (entry && typeof entry === 'object') {
        const val = entry[currentLang];
        if (typeof val === 'undefined' || val === null || String(val).trim() === '') {
          missingLang.add(`${key} (missing "${currentLang}")`);
        }
      }
    });

    if (missingKey.size) {
      console.warn(`[i18n] Missing keys (${missingKey.size}):`, Array.from(missingKey).sort());
    }
    if (missingLang.size) {
      console.warn(`[i18n] Missing translations (${missingLang.size}):`, Array.from(missingLang).sort());
    }
  }

  // ---------- Sensor fetch (/sensordata) ----------
  async function updateSensorValues() {
    try {
      const response = await fetch('/sensordata', { cache: 'no-store' });
      if (!response.ok) {
        console.error('Error retrieving sensor data:', response.status);
        setNA();
        return;
      }
      const data = await response.json();

      // Only do heavy DOM work when the status page (with grids) is visible.
      // This dramatically reduces layout/reflow cost on other pages.
      const statusActive = (getActivePageId() === 'status');
      // If we're not on the Status page, stop here.
if (!statusActive) return;

      // current
      if (isNum(data.curTemperature))        { setText('tempSpan', data.curTemperature.toFixed(1)); }
      if (isNum(data.curWaterTemperature))   { setText('waterTempSpan', data.curWaterTemperature.toFixed(1)); }
      if (isNum(data.curHumidity))           { setText('humSpan',  data.curHumidity.toFixed(0)); }
      if (isNum(data.curVpd))                { setText('vpdSpan',  data.curVpd.toFixed(1)); }
      
      const isText = x => typeof x === 'string' && x.trim() !== '';

      if (isText(data.curTimeLeftIrrigation)) {
        setText('irTimeLeftSpan', data.curTimeLeftIrrigation);
      } else {
        setText('irTimeLeftSpan', '00:00');
      }

      // ---- Shelly Main Switch ----
      const mainSwitchEl = document.getElementById('shelly-main-switch-state');
      if (mainSwitchEl) {
        const rawStatus = data.shellyMainSwitchStatus;
        const rawPower  = data.shellyMainSwitchPower;
        const rawTotalWh = data.shellyMainSwitchTotalWh;

        const isOn = (rawStatus === true) || (rawStatus === 'true') || (rawStatus === 1) || (rawStatus === '1');
        const powerW = (typeof rawPower === 'number') ? rawPower : 0;
        const totalWh = (typeof rawTotalWh === 'number') ? rawTotalWh : 0;
        const totalKWh = totalWh / 1000;

        setShellyStateClass(mainSwitchEl, isOn);

        // Create child nodes once (avoid innerHTML churn + reflow every 10s)
        if (!mainSwitchEl._pwr) {
          mainSwitchEl.textContent = '';
          const p = document.createElement('div');
          const s = document.createElement('div');
          s.className = 'sub';
          mainSwitchEl.appendChild(p);
          mainSwitchEl.appendChild(s);
          mainSwitchEl._pwr = p;
          mainSwitchEl._sum = s;
        }
        mainSwitchEl._pwr.textContent = `${powerW.toFixed(1)} W`;
        mainSwitchEl._sum.textContent = `${totalKWh.toFixed(2)} kWh`;
      }

      // ---------- Heater ----------
      const heaterStateEl = document.getElementById('shelly-heater-state');
      if (heaterStateEl) {
        const rawStatus = data.shellyHeaterStatus;
        const rawPower  = data.shellyHeaterPower;
        const rawTotalWh = data.shellyHeaterTotalWh;

        const isOn = (rawStatus === true) || (rawStatus === 'true') || (rawStatus === 1) || (rawStatus === '1');
        const powerW = (typeof rawPower === 'number') ? rawPower : 0;
        const totalWh = (typeof rawTotalWh === 'number') ? rawTotalWh : 0;
        const totalKWh = totalWh / 1000;

        setShellyStateClass(heaterStateEl, isOn);

        if (!heaterStateEl._pwr) {
          heaterStateEl.textContent = '';
          const p = document.createElement('div');
          const s = document.createElement('div');
          s.className = 'sub';
          heaterStateEl.appendChild(p);
          heaterStateEl.appendChild(s);
          heaterStateEl._pwr = p;
          heaterStateEl._sum = s;
        }
        heaterStateEl._pwr.textContent = `${powerW.toFixed(1)} W`;
        heaterStateEl._sum.textContent = `${totalKWh.toFixed(2)} kWh`;
      }

      // ---------- Humidifier ----------
      const humidifierStateEl = document.getElementById('shelly-humidifier-state');
      if (humidifierStateEl) {
        const rawStatus = data.shellyHumidifierStatus;
        const rawPower  = data.shellyHumidifierPower;
        const rawTotalWh = data.shellyHumidifierTotalWh;

        const isOn = (rawStatus === true) || (rawStatus === 'true') || (rawStatus === 1) || (rawStatus === '1');
        const powerW = (typeof rawPower === 'number') ? rawPower : 0;
        const totalWh = (typeof rawTotalWh === 'number') ? rawTotalWh : 0;
        const totalKWh = totalWh / 1000;

        setShellyStateClass(humidifierStateEl, isOn);

        if (!humidifierStateEl._pwr) {
          humidifierStateEl.textContent = '';
          const p = document.createElement('div');
          const s = document.createElement('div');
          s.className = 'sub';
          humidifierStateEl.appendChild(p);
          humidifierStateEl.appendChild(s);
          humidifierStateEl._pwr = p;
          humidifierStateEl._sum = s;
        }
        humidifierStateEl._pwr.textContent = `${powerW.toFixed(1)} W`;
        humidifierStateEl._sum.textContent = `${totalKWh.toFixed(2)} kWh`;
      }

      // ---------- Fan ----------
      const fanStateEl = document.getElementById('shelly-fan-state');
      if (fanStateEl) {
        const rawStatus = data.shellyFanStatus;
        const rawPower  = data.shellyFanPower;
        const rawTotalWh = data.shellyFanTotalWh;

        const isOn = (rawStatus === true) || (rawStatus === 'true') || (rawStatus === 1) || (rawStatus === '1');
        const powerW = (typeof rawPower === 'number') ? rawPower : 0;
        const totalWh = (typeof rawTotalWh === 'number') ? rawTotalWh : 0;
        const totalKWh = totalWh / 1000;

        setShellyStateClass(fanStateEl, isOn);

        if (!fanStateEl._pwr) {
          fanStateEl.textContent = '';
          const p = document.createElement('div');
          const s = document.createElement('div');
          s.className = 'sub';
          fanStateEl.appendChild(p);
          fanStateEl.appendChild(s);
          fanStateEl._pwr = p;
          fanStateEl._sum = s;
        }
        fanStateEl._pwr.textContent = `${powerW.toFixed(1)} W`;
        fanStateEl._sum.textContent = `${totalKWh.toFixed(2)} kWh`;
      }


      // ---------- Grow Light ----------
      const lightStateEl = (document.getElementById('shelly-light-switch-state') || document.getElementById('shelly-light-state'));
      if (lightStateEl) {
        // support both flat fields and nested shelly.light.values
        const v = (data && data.shelly && data.shelly.light && data.shelly.light.values) ? data.shelly.light.values : null;

        const rawStatus = (typeof data.shellyLightStatus !== 'undefined') ? data.shellyLightStatus : (v ? v.status : undefined);
        const rawPower  = (typeof data.shellyLightPower  !== 'undefined') ? data.shellyLightPower  : (v ? v.power  : undefined);
        const rawTotalWh= (typeof data.shellyLightTotalWh!== 'undefined') ? data.shellyLightTotalWh: (v ? v.totalWh: undefined);

        const isOn = (rawStatus === true) || (rawStatus === 'true') || (rawStatus === 1) || (rawStatus === '1');
        const powerW = (typeof rawPower === 'number') ? rawPower : (Number(rawPower) || 0);
        const totalWh = (typeof rawTotalWh === 'number') ? rawTotalWh : (Number(rawTotalWh) || 0);
        const totalKWh = totalWh / 1000;

        setShellyStateClass(lightStateEl, isOn);

        if (!lightStateEl._pwr) {
          lightStateEl.textContent = '';
          const p = document.createElement('div');
          const s = document.createElement('div');
          s.className = 'sub';
          lightStateEl.appendChild(p);
          lightStateEl.appendChild(s);
          lightStateEl._pwr = p;
          lightStateEl._sum = s;
        }
        lightStateEl._pwr.textContent = `${powerW.toFixed(1)} W`;
        lightStateEl._sum.textContent = `${totalKWh.toFixed(2)} kWh`;
      }

      // ---------- Averages ----------
      // averages (support both naming styles from backend)
      const avgTemp = isNum(data.avgTemperature) ? data.avgTemperature : (isNum(data.avgTemp) ? data.avgTemp : null);
      if (avgTemp !== null) setText('avgTempSpan', avgTemp.toFixed(1));

      const avgWater = isNum(data.avgWaterTemperature) ? data.avgWaterTemperature : (isNum(data.avgWaterTemp) ? data.avgWaterTemp : null);
      if (avgWater !== null) setText('avgWaterTempSpan', avgWater.toFixed(1));

      const avgHum = isNum(data.avgHumidity) ? data.avgHumidity : null;
      if (avgHum !== null) setText('avgHumSpan', avgHum.toFixed(1));

      const avgVpd = isNum(data.avgVpd) ? data.avgVpd : null;
      if (avgVpd !== null) setText('avgVpdSpan', avgVpd.toFixed(1));

      // captured / timestamp
      const cap =
        (typeof data.captured === 'string' && data.captured.length) ? data.captured :
        (isNum(data.ts) ? new Date(data.ts).toLocaleString() : 'N/A');
      setText('capturedSpan', cap);

      if (Array.isArray(data.relays)) {
      relayStates = data.relays.map(v => !!v);
      updateRelayButtons();
      }

    } catch (error) {
      console.error('Exception in updateSensorValues():', error?.message || error, error);
      setNA();
    }
  }

  window.updateSensorValues = updateSensorValues;

  // ---------- History charts (/api/history) ----------
  let historyTimer = null;

  // ---------- Shelly light schedule poll (only active on Shelly page) ----------
  window._shellyLightTimer = null;

  function getActivePageId(){
    const p = document.querySelector('.page.active');
    return p ? p.id : '';
  }

  function canvasHiDPI(canvas){
    const dpr = window.devicePixelRatio || 1;
    const cssW = canvas.clientWidth || canvas.width;
    const cssH = canvas.clientHeight || canvas.height;
    const w = Math.max(1, Math.floor(cssW * dpr));
    const h = Math.max(1, Math.floor(cssH * dpr));
    if (canvas.width !== w) canvas.width = w;
    if (canvas.height !== h) canvas.height = h;
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return ctx;
  }

  // ---------- chart perf helpers ----------
  // Downsample to at most ~1 point per CSS pixel (huge speedup),
  // and smooth only the DRAW line (stats stay raw/real).
  const CHART_CFG = {
    pointsPerPixel: 1.0,   // 1.0 = max ~1 point per pixel
    smoothWindow: 5        // 1 = no smoothing, 3..9 typical
  };

  function movingAverage(arr, window){
    const w = Math.max(1, window|0);
    if (w <= 1 || arr.length < 3) return arr;
    const half = Math.floor(w / 2);
    const out = new Array(arr.length).fill(null);

    for (let i = 0; i < arr.length; i++) {
      let sum = 0, ok = 0;
      const a = Math.max(0, i - half);
      const b = Math.min(arr.length - 1, i + half);
      for (let j = a; j <= b; j++) {
        const v = arr[j];
        if (v === null || typeof v !== 'number' || !isFinite(v)) continue;
        sum += v; ok++;
      }
      out[i] = ok ? (sum / ok) : null;
    }
    return out;
  }

  function prepareSeries(arr, maxPoints, smoothWindow){
    const n = Array.isArray(arr) ? arr.length : 0;
    if (!n) return { draw: [], stats: { min: null, max: null, avg: null } };

    const mp = Math.max(20, maxPoints|0);

    let min = Infinity, max = -Infinity, sum = 0, ok = 0;

    // already small enough: compute stats on raw; draw = raw (then smooth)
    if (n <= mp) {
      const draw = new Array(n);
      for (let i = 0; i < n; i++) {
        const v = arr[i];
        draw[i] = v;
        if (v === null || typeof v !== 'number' || !isFinite(v)) continue;
        ok++; sum += v;
        if (v < min) min = v;
        if (v > max) max = v;
      }
      const stats = ok ? { min, max, avg: (sum/ok) } : { min: null, max: null, avg: null };
      return { draw: movingAverage(draw, smoothWindow), stats };
    }

    // bucket downsampling: each bucket -> avg for draw, stats from raw values
    const bucketSize = Math.ceil(n / mp);
    const draw = [];

    for (let i = 0; i < n; i += bucketSize) {
      const end = Math.min(n, i + bucketSize);
      let bSum = 0, bOk = 0;

      for (let j = i; j < end; j++) {
        const v = arr[j];
        if (v === null || typeof v !== 'number' || !isFinite(v)) continue;

        // stats from RAW
        ok++; sum += v;
        if (v < min) min = v;
        if (v > max) max = v;

        // draw avg
        bOk++; bSum += v;
      }
      draw.push(bOk ? (bSum / bOk) : null);
    }

    const stats = ok ? { min, max, avg: (sum/ok) } : { min: null, max: null, avg: null };
    return { draw: movingAverage(draw, smoothWindow), stats };
  }

  // Draws a series of data into a canvas element
  function drawSeries(canvasId, arr, minSpanId, avgSpanId, maxSpanId, decimals, targetValue, intervalSec){
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;

  const cssW = (canvas.clientWidth || canvas.width || 300);
  const maxPoints = Math.max(60, Math.floor(cssW * CHART_CFG.pointsPerPixel));
  const prep = prepareSeries(arr, maxPoints, CHART_CFG.smoothWindow);
  const drawArr = prep.draw;
  const { min, max, avg } = prep.stats;

  // --- optional: Sollwert in Skala einbeziehen, damit Linie nicht "außerhalb" liegt ---
  let min2 = min, max2 = max;
  const tOk = (typeof targetValue === 'number' && isFinite(targetValue));
  if (tOk && min2 !== null && max2 !== null) {
    if (targetValue < min2) min2 = targetValue;
    if (targetValue > max2) max2 = targetValue;
    if (min2 === max2) { min2 -= 0.5; max2 += 0.5; }
  }

  // If the series is flat (often happens when only the first value exists),
  // expand the range a bit so the grid and the first red marker can be drawn.
  if (min2 !== null && max2 !== null && min2 === max2) {
    const eps = (decimals >= 2) ? 0.05 : (decimals === 1 ? 0.5 : 1);
    min2 -= eps;
    max2 += eps;
  }

  const minEl = document.getElementById(minSpanId);
  const avgEl = document.getElementById(avgSpanId);
  const maxEl = document.getElementById(maxSpanId);
  if (minEl) minEl.textContent = (min2 === null) ? '–' : min2.toFixed(decimals);
  if (avgEl) avgEl.textContent = (avg === null) ? '–' : avg.toFixed(decimals);
  if (maxEl) maxEl.textContent = (max2 === null) ? '–' : max2.toFixed(decimals);

  const ctx = canvasHiDPI(canvas);
  const stroke = getChartStroke();
  ctx.strokeStyle = stroke;

  const w = canvas.clientWidth || canvas.width;
  const h = canvas.clientHeight || canvas.height;
  ctx.clearRect(0, 0, w, h);

  // --- Layout: Platz links für Skala ---
  const padTop = 6, padRight = 6, padBottom = 22; // extra space for X-axis labels
  const padLeft = 44; // Platz für Zahlen links (bei Bedarf 50 machen)

  const innerW = w - padLeft - padRight;
  const innerH = h - padTop - padBottom;

  // kleine Hilfsfunktion für "schöne" Tick-Abstände
  function niceStep(range, ticks){
    if (!(range > 0) || !(ticks > 0)) return 1;
    const rough = range / ticks;
    const pow10 = Math.pow(10, Math.floor(Math.log10(rough)));
    const r = rough / pow10;
    let nice = 1;
    if (r >= 5) nice = 5;
    else if (r >= 2) nice = 2;
    else nice = 1;
    return nice * pow10;
  }

  // background grid + Y-Achsen-Labels
  ctx.save();
  const ticks = 4; // 4 Zwischenlinien
  const range = (max2 - min2);
  const step = niceStep(range, ticks);

  // Ticks so wählen, dass sie "rund" sind
  const yMinTick = Math.floor(min2 / step) * step;
  const yMaxTick = Math.ceil(max2 / step) * step;

  ctx.font = '12px system-ui, -apple-system, Segoe UI, Roboto, Arial';
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';

  // Grid-Linien + Labels
  for (let v = yMinTick; v <= yMaxTick + 0.000001; v += step) {
    const t = (v - min2) / (max2 - min2);         // 0..1
    const y = padTop + innerH * (1 - t);          // invertiert (oben max)

    // Linie
    ctx.globalAlpha = 0.20;
    ctx.beginPath();
    ctx.moveTo(padLeft, y);
    ctx.lineTo(w - padRight, y);
    ctx.stroke();

    // Label links
    ctx.globalAlpha = 0.85;
    ctx.fillStyle = stroke;
    ctx.fillText(v.toFixed(decimals), padLeft - 6, y);
  }

  // Y-Achse (links)
  ctx.globalAlpha = 0.35;
  ctx.beginPath();
  ctx.moveTo(padLeft, padTop);
  ctx.lineTo(padLeft, h - padBottom);
  ctx.stroke();
  ctx.restore();
  if (min2 === null || max2 === null) return;

  // Count finite values; if we only have one, draw a small start marker instead of a line.
  let firstValid = -1;
  let validCount = 0;
  for (let i = 0; i < drawArr.length; i++) {
    const v = drawArr[i];
    if (typeof v === 'number' && isFinite(v)) {
      if (firstValid < 0) firstValid = i;
      validCount++;
    }
  }
  if (validCount === 0) return;

  const n = drawArr.length;

  const xStep = innerW / Math.max(1, n - 1);
  const yScale = innerH / (max2 - min2);

  // ---- X-axis (relative time) ----
  // Uses /api/history intervalSec; shows minutes ago -> now.
  if (intervalSec && isFinite(intervalSec) && intervalSec > 0) {
    ctx.save();
    ctx.font = '12px system-ui, -apple-system, Segoe UI, Roboto, Arial';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    ctx.fillStyle = stroke;

    const axisY = h - padBottom;

    // bottom axis line
    ctx.globalAlpha = 0.35;
    ctx.beginPath();
    ctx.moveTo(padLeft, axisY);
    ctx.lineTo(w - padRight, axisY);
    ctx.stroke();

    // labels
    ctx.globalAlpha = 0.85;
    const ticksX = 4; // start, 1/3, 2/3, end
    for (let t = 0; t < ticksX; t++) {
      const frac = t / (ticksX - 1);
      const i = Math.round(frac * (n - 1));
      const x = padLeft + xStep * i;

      const secondsAgo = (n - 1 - i) * intervalSec;
      const minutesAgo = Math.round(secondsAgo / 60);
      const labelNow = (currentLang === 'de') ? 'jetzt' : 'now';
      const txt = (i === n - 1) ? labelNow : `-${minutesAgo}m`;

      ctx.fillText(txt, x, axisY + 4);
    }

    ctx.restore();
  }


  // ---- Soll-Linie (gestrichelt) ----
  if (tOk) {
    const yT = padTop + (max2 - targetValue) * yScale;
    ctx.save();
    ctx.globalAlpha = 0.75;
    ctx.setLineDash([6, 4]);
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(padLeft, yT);
    ctx.lineTo(w - padRight, yT);
    ctx.stroke();
    ctx.restore();
  }

  // Datenlinie (IST-Wert) -> ROT
  ctx.save();
  ctx.strokeStyle = 'red';
  ctx.lineWidth = 2;

  if (validCount >= 2) {
    ctx.beginPath();
    let started = false;
    for (let i = 0; i < n; i++) {
      const v = drawArr[i];
      if (v === null || typeof v !== 'number' || !isFinite(v)) {
        started = false;
        continue;
      }
      const x = padLeft + xStep * i;
      const y = padTop + (max2 - v) * yScale;
      if (!started) {
        ctx.moveTo(x, y);
        started = true;
      } else {
        ctx.lineTo(x, y);
      }
    }
    ctx.stroke();
  } else if (firstValid >= 0) {
    // Only one value so far: draw a small "start bar" so the user sees something immediately.
    const v = drawArr[firstValid];
    const x = padLeft + xStep * firstValid;
    const y = padTop + (max2 - v) * yScale;

    // vertical mini bar + dot
    ctx.beginPath();
    ctx.moveTo(x, y - 6);
    ctx.lineTo(x, y + 6);
    ctx.stroke();

    ctx.fillStyle = 'red';
    ctx.beginPath();
    ctx.arc(x, y, 3, 0, Math.PI * 2);
    ctx.fill();
  }

  ctx.restore();

 }

  // Only redraw when history actually changes
  let _histSig = "";
  function _sig(arr){
    const n = arr?.length || 0;
    const a = n ? arr[0] : '';
    const b = n ? arr[n-1] : '';
    return `${n}|${a}|${b}`;
  }

  window.updateHistoryCharts = async function(force){
    try {
      if (!force && getActivePageId() !== 'status') return;
      const r = await fetch('/api/history', { cache: 'no-store' });
      if (!r.ok) return;
      const d = await r.json();
      if (!d || !Array.isArray(d.temp)) return;

      drawSeries('chartTemp',  d.temp,  'chartTempMin',  'chartTempAvg',  'chartTempMax',  1, d.targetTempC,  d.intervalSec);
      drawSeries('chartHum',   d.hum,   'chartHumMin',   'chartHumAvg',   'chartHumMax',   1, null,           d.intervalSec);
      drawSeries('chartVpd',   d.vpd,   'chartVpdMin',   'chartVpdAvg',   'chartVpdMax',   2, d.targetVpdKpa, d.intervalSec);
      drawSeries('chartWater', d.water, 'chartWaterMin', 'chartWaterAvg', 'chartWaterMax', 1, null,           d.intervalSec);
    } catch (e) {
      console.warn('history fetch failed', e);
    }
  };

  function startHistoryPoll(){
    if (historyTimer) return;
    historyTimer = setInterval(() => window.updateHistoryCharts(false), 30000);
  }
  startHistoryPoll();

  function setNA(){
    setText('tempSpan', 'N/A');
    setText('waterTempSpan', 'N/A');
    setText('humSpan',  'N/A');
    setText('vpdSpan',  'N/A');
    setText('capturedSpan', 'N/A');
    // averages optional
  }
    window._startSensorPoll();     // intervall start
  window.updateSensorValues();
  window.updateHistoryCharts(true);

  
  // ---------- Embedded Web-Log ----------
  let logTimer = null;  // (nur EINMAL deklarieren)
  let autoScroll = true;

  async function fetchWebLog() {
    try {
      const r = await fetch('/api/logbuffer', { cache: 'no-store' });
      if (!r.ok) return;
      const t = await r.text();
      const pre = document.getElementById('weblog');
      if (pre) {
        const atBottom = Math.abs(pre.scrollTop + pre.clientHeight - pre.scrollHeight) < 5;
        pre.textContent = t || '—';
        if (autoScroll && atBottom) {
          pre.scrollTop = pre.scrollHeight;
        }
      }
    } catch (e) {
      console.warn('weblog fetch failed', e);
    }
  }
  const pre = document.getElementById('weblog');
  if (!pre) return;

  pre.addEventListener('scroll', () => {
    const nearBottom = Math.abs(pre.scrollTop + pre.clientHeight - pre.scrollHeight) < 10;
    autoScroll = nearBottom;
  });

  function startWebLog() {
    if (logTimer) return;
    fetchWebLog();
    logTimer = setInterval(fetchWebLog, 2000);
  }
  function stopWebLog() {
    if (!logTimer) return;
    clearInterval(logTimer);
    logTimer = null;
  }
 
  // Sichtbarkeit
  document.addEventListener('visibilitychange', () => {
    const loggingActive = document.getElementById('logging')?.classList.contains('active');
    if (document.visibilityState === 'visible' && loggingActive) startWebLog();
    else stopWebLog();
  });


  // ---------- Shelly settings summary line ----------
  // Reads schedule summary lines from /api/state and shows them on the Shelly settings page.
  async function loadShellyLines() {
    const map = {
      main:  'shellyMainLine',
      light: 'shellyLightLine',
    };

    try {
      const res = await fetch('/api/state', { cache: 'no-store' });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const s = await res.json();

      // Update Shelly info lines (if elements exist)
      updateShellyInfoLinesFromState(s);

      const setLine = (key, elId) => {
        const el = document.getElementById(elId);
        if (!el) return;
        const v = s[key];
        el.textContent = (v === null || typeof v === 'undefined') ? '—' : String(v);
      };

      setLine('settings.shelly.main.line',  map.main);
      setLine('settings.shelly.light.line', map.light);
    } catch (e) {
      console.warn('[SHELLY][JS] loadShellyLines failed:', e);
    }
  }

  // SPA-Seitenwechsel-Callback
  function onPageChanged(activeId) {
    // Status page has lots of grid cards; keep sensor polling fast there,
    // slow down elsewhere to reduce layout work.
    if (activeId === 'status') {
      window._setSensorPollInterval?.(10000);
      // refresh immediately when returning to status
      window.updateSensorValues?.();
    } else {
      window._setSensorPollInterval?.(60000);
    }

    if (activeId === 'logging') startWebLog(); else stopWebLog();

    // SHELLY light schedule poll: keep UI in sync (fixes sporadic updates)
    if (activeId === 'shelly') {
      try { initLightScheduleControls(); } catch(e) {}
      // Immediate update (no waiting for timer)
      try { fetchStateAndUpdateShellyUI(); } catch(e) {}
      if (!window._shellyLightTimer) {
        window._shellyLightTimer = setInterval(() => {
          // Immediate update (no waiting for timer)
      try { fetchStateAndUpdateShellyUI(); } catch(e) {}
        }, 15000);
      }
    } else {
      if (window._shellyLightTimer) {
        clearInterval(window._shellyLightTimer);
        window._shellyLightTimer = null;
      }
    }

    if (activeId === 'shelly') {
      // Load Shelly summary lines (IP | Gen | ON | OFF) from /api/state
      loadShellyLines();
    }

    if (activeId === 'vars') {
      // Load once when opening the page (and on refresh)
      loadVars();
    }

    if (activeId === 'diary') {
      updateDiaryUI();
      loadDiaryList();
    }
  }

  // ---------- Diary list (load + render) ----------
  async function loadDiaryList() {
    const listEl = document.getElementById('diaryList');
    if (!listEl) return;

    // optional: kleine Ladeanzeige
    listEl.innerHTML = `<div class="muted">Loading…</div>`;

    try {
      // <- HIER ggf. Endpoint anpassen:
      const res = await fetch('/api/diary/list', { cache: 'no-store' });
      if (!res.ok) throw new Error('HTTP ' + res.status);

      const data = await res.json();
      const items = Array.isArray(data?.items) ? data.items : [];

      if (!items.length) {
        listEl.innerHTML = `<div class="muted">—</div>`;
        return;
      }

      // Sort: newest first (wenn ts vorhanden)
      items.sort((a,b) => (b.ts || 0) - (a.ts || 0));

      const rows = items.map(it => {
        // Unterstützt ts (unix sec), id als ISO, oder date string
        let dt = null;
        if (typeof it.ts === 'number' && isFinite(it.ts)) dt = new Date(it.ts * 1000);
        else if (typeof it.id === 'string') dt = new Date(it.id);
        else if (typeof it.date === 'string') dt = new Date(it.date);

        const dtStr = dt && !isNaN(dt.getTime()) ? dt.toLocaleString() : (it.id || it.date || '—');
        const phase = it.phase ? escapeHtml(String(it.phase)) : '';
        const note  = it.note ? escapeHtml(String(it.note)) : '';
        const prev  = it.preview ? escapeHtml(String(it.preview)) : note;

        return `
          <div class="diary-row">
            <div class="diary-row-left">
              <div class="diary-date">${escapeHtml(dtStr)}${phase ? ` • <span class="diary-phase">${phase}</span>` : ''}</div>
              <div class="diary-preview">${prev || ''}</div>
            </div>
            <div class="diary-row-actions">
              <button class="mini-btn" onclick="diaryEdit('${escAttr(it.id||'')}', '${escAttr(it.note||'')}', '${escAttr(it.phase||'')}')">Edit</button>
              <button class="mini-btn danger" onclick="diaryDelete('${escAttr(it.id||'')}')">Del</button>
            </div>
          </div>
        `;
      });

      listEl.innerHTML = rows.join('');
    } catch (e) {
      console.warn('[DIARY] list load failed', e);
      listEl.innerHTML = `<div class="muted">Error loading diary</div>`;
    }
  }

  // optional: CSV Download (Button kann einfach window.location nutzen)
  function downloadDiaryCsv(){
    window.location.href = '/api/diary.csv'; // ggf. anpassen
  }



  async function diaryDelete(id) {
    if (!id) return;
    if (!confirm('Eintrag wirklich löschen?')) return;
    try {
      const res = await fetch('/api/diary/delete', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id: String(id) })
      });
      const data = await res.json().catch(() => null);
      if (!res.ok || !data || !data.ok) throw new Error('delete');
      await loadDiaryList();
    } catch (e) {
      alert('Löschen fehlgeschlagen');
    }
  }

  async function diaryEdit(id, curNote, curPhase) {
    if (!id) return;
    const note = prompt('Notiz bearbeiten:', curNote || '');
    if (note === null) return; // cancelled
    const phase = prompt('Phase (grow|flower|dry) – leer lassen um unverändert zu lassen:', curPhase || '');
    try {
      const res = await fetch('/api/diary/update', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id: String(id), note: String(note), phase: String(phase || '') })
      });
      const data = await res.json().catch(() => null);
      if (!res.ok || !data || !data.ok) throw new Error('update');
      await loadDiaryList();
    } catch (e) {
      alert('Ändern fehlgeschlagen');
    }
  }

  window.diaryDelete = diaryDelete;
  window.diaryEdit = diaryEdit;

  // expose if you want to call from HTML onclick
  window.loadDiaryList = loadDiaryList;
  window.downloadDiaryCsv = downloadDiaryCsv;

  // Phase-Init
  function initPhaseSelect(){
    const currentPhase = '%PHASE%';
    const sel = document.getElementById('phaseSelect');

    if (!sel) return;

    const validPhases = ['grow', 'flower', 'dry'];
    const phase = validPhases.includes(currentPhase) ? currentPhase : 'grow';

    sel.value = phase;

    const phaseLabel = document.getElementById('currentPhase');
    if (phaseLabel) {
      const phaseNames = {
        grow:   'Wuchs (Grow)',
        flower: 'Blüte (Flower)',
        dry:    'Trocknung (Dry)',
      };
      phaseLabel.textContent = phaseNames[phase] || phase;
    }
  }
  
  

  // ---------- Grow Diary (client-side helpers) ----------
  function parseYmd(str){
    if (!str || str.length < 10) return null;
    const y = Number(str.slice(0,4));
    const m = Number(str.slice(5,7));
    const d = Number(str.slice(8,10));
    if (!y || !m || !d) return null;
    // local midnight
    return new Date(y, m-1, d, 0, 0, 0, 0);
  }

  function daysBetween(start, end){
    if (!start || !end) return null;
    const ms = end.getTime() - start.getTime();
    if (!isFinite(ms)) return null;
    return Math.floor(ms / 86400000);
  }

  function getPhaseCode(){
    const sel = document.getElementById('phaseSelect');
    let v = sel ? sel.value : null;
    // Fallback to the current runtime phase provided by the backend template
    if (!v) v = '%PHASE%';
    if (v === 'flower') return 'flower';
    if (v === 'dry') return 'dry';
    return 'grow';
  }

  function phaseNameFor(code){
    // Use i18n phase strings if present
    const key = (code === 'flower') ? 'runsetting.phase.flower' : (code === 'dry') ? 'runsetting.phase.dry' : 'runsetting.phase.grow';
    return I18N[key] || code;
  }

  function updateDiaryCounter(){
    const ta = document.getElementById('diaryNote');
    const out = document.getElementById('diaryCount');
    if (!ta || !out) return;
    out.textContent = `${ta.value.length}/400`;
  }

  function updateDiaryUI(){
    // Prefer the header grow-info because it is already computed correctly (phase + total grow) and localized.
    const gi = document.querySelector('.grow-info');
    if (gi) {
      const raw = (gi.innerText || gi.textContent || '').trim();
      const lines = raw.split(/\r?\n/).map(s => s.trim()).filter(Boolean);

      const parseLine = (line) => {
        // Supports DE: "... Tag 63 / Woche 9" and EN: "... day 63 / week 9"
        const m = line.match(/(?:Tag|Tage|day|days)\s*(\d+)\s*\/\s*(?:Woche|week)\s*(\d+)/i);
        const day = m ? m[1] : null;
        const week = m ? m[2] : null;
        // label before ":" if present
        const lm = line.match(/^\s*([^:]+):/);
        const label = lm ? lm[1].trim() : '';
        return { day, week, label };
      };

      if (lines.length >= 1) {
        const g = parseLine(lines[0]);
        if (g.day)  setText('diaryGrowDay', g.day);
        if (g.week) setText('diaryGrowWeek', g.week);
      }
      if (lines.length >= 2) {
        const p = parseLine(lines[1]);
        if (p.label) setText('diaryPhaseName', p.label.toUpperCase());
        if (p.day)  setText('diaryPhaseDay', p.day);
        if (p.week) setText('diaryPhaseWeek', p.week);
      }

      // Also keep meta inside the note field in sync
      applyDiaryMetaToTextarea();
      return;
    }

    // compute "total grow" from grow start date input (if present)
    const now = new Date();
    const nowMid = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 0,0,0,0);

    const growStartEl = document.getElementById('webGrowStart');
    const flowerStartEl = document.getElementById('webFloweringStart');
    const dryStartEl = document.getElementById('webDryingStart');

    const growStart = parseYmd(growStartEl ? growStartEl.value : '');
    const flowerStart = parseYmd(flowerStartEl ? flowerStartEl.value : '');
    const dryStart = parseYmd(dryStartEl ? dryStartEl.value : '');

    const gDiff = daysBetween(growStart, nowMid);
    const gDay  = (gDiff === null || gDiff < 0) ? '–' : String(gDiff + 1);
    const gWeek = (gDiff === null || gDiff < 0) ? '–' : String(Math.floor(gDiff / 7) + 1);

    setText('diaryGrowDay', gDay);
    setText('diaryGrowWeek', gWeek);

    const phaseCode = getPhaseCode();
    setText('diaryPhaseName', phaseNameFor(phaseCode));

    let phaseStart = growStart;
    if (phaseCode === 'flower' && flowerStart) phaseStart = flowerStart;
    if (phaseCode === 'dry' && dryStart) phaseStart = dryStart;

    const pDiff = daysBetween(phaseStart, nowMid);
    const pDay  = (pDiff === null || pDiff < 0) ? '–' : String(pDiff + 1);
    const pWeek = (pDiff === null || pDiff < 0) ? '–' : String(Math.floor(pDiff / 7) + 1);

    setText('diaryPhaseDay', pDay);
    setText('diaryPhaseWeek', pWeek);

    // Show the current grow/phase status directly inside the note field
    applyDiaryMetaToTextarea();
    updateDiaryCounter();
    loadDiaryList();
  }

  
  function computeDiaryMetaText(){
    // Prefer the header info because it already reflects the correct start dates and is localized.
    const gi = document.querySelector('.grow-info');
    if (gi) {
      const raw = (gi.innerText || gi.textContent || '').trim();
      const lines = raw.split(/\r?\n/).map(s => s.trim()).filter(Boolean);
      if (lines.length) return lines.join(' | ');
    }

    // Fallback: compute from diary UI fields
    const gW = (document.getElementById('diaryGrowWeek')?.textContent || '–').trim();
    const gD = (document.getElementById('diaryGrowDay')?.textContent  || '–').trim();
    const pW = (document.getElementById('diaryPhaseWeek')?.textContent || '–').trim();
    const pD = (document.getElementById('diaryPhaseDay')?.textContent  || '–').trim();

    const phaseCode = getPhaseCode();
    const phaseName = phaseNameFor(phaseCode);

    const lang = (typeof currentLang === 'string' ? currentLang : 'de');
    const wLabel = (lang === 'en') ? 'Week' : 'Woche';
    const dLabel = (lang === 'en') ? 'Day'  : 'Tag';
    const growLabel = (lang === 'en') ? 'Grow' : 'Grow';

    return `${growLabel}: ${wLabel} ${gW} ${dLabel} ${gD} | ${phaseName}: ${wLabel} ${pW} ${dLabel} ${pD}`;
  }

  function stripDiaryMeta(raw){
    const s = String(raw || '');
    // Remove a leading "(meta ...)" block that contains grow/phase timing hints (localized).
    return s.replace(/^\s*\((?=[^)]+(?:Woche|Week|Grow|Blüte|Flower|Trock|Dry))[^)]*\)\s*/i, '').trimStart();
  }

  function applyDiaryMetaToTextarea(){
    const ta = document.getElementById('diaryNote');
    if (!ta) return;

    const base = stripDiaryMeta(ta.value);
    const meta = computeDiaryMetaText();

    // Only append meta if we have numeric values
    const hasNumbers = /\b\d+\b/.test(meta);
    const nextVal = hasNumbers ? (base.length ? `(${meta})  ${base}` : `(${meta})`) : base;

    // Avoid cursor jumps if nothing changes
    if (ta.value !== nextVal) {
      const wasAtEnd = (ta.selectionStart === ta.value.length && ta.selectionEnd === ta.value.length);
      ta.value = nextVal;
      if (wasAtEnd) {
        ta.selectionStart = ta.selectionEnd = ta.value.length;
      }
      updateDiaryCounter();
    }
  }

// diary events
  document.getElementById('diaryNote')?.addEventListener('input', () => { updateDiaryCounter(); applyDiaryMetaToTextarea(); });

  document.getElementById('diarySaveBtn')?.addEventListener('click', async () => {
    const ta = document.getElementById('diaryNote');
    const status = document.getElementById('diaryStatus');
    if (!ta) return;

    const note = stripDiaryMeta((ta.value || '')).trim();
    const phase = getPhaseCode();

    if (status) status.textContent = '';

    try {
      const res = await fetch('/api/diary/add', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ note, phase, lang: (typeof currentLang === "string" ? currentLang : "de"), meta: computeDiaryMetaText() }),
        cache: 'no-store'
      });

      if (!res.ok) throw new Error('HTTP ' + res.status);

      ta.value = '';
      updateDiaryCounter();

      // refresh list so the new entry appears immediately under the form
      loadDiaryList();

      if (status) status.textContent = I18N['diary.saved'] || 'Saved ✓';
      setTimeout(() => { if (status) status.textContent = ''; }, 2500);
    } catch (e) {
      console.warn('[DIARY] save failed', e);
      if (status) status.textContent = I18N['diary.error'] || 'Save failed';
    }
  });

  document.getElementById('diaryClearBtn')?.addEventListener('click', async () => {
    const msg = I18N['diary.confirmClear'] || 'Really clear all diary entries?';
    if (!confirm(msg)) return;

    const status = document.getElementById('diaryStatus');
    if (status) status.textContent = '';

    try {
      const res = await fetch('/api/diary/clear', { method: 'POST', cache: 'no-store' });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      if (status) status.textContent = (I18N['diary.saved'] || 'Saved ✓');
      setTimeout(() => { if (status) status.textContent = ''; }, 2500);
      loadDiaryList();
    } catch (e) {
      console.warn('[DIARY] clear failed', e);
      if (status) status.textContent = I18N['diary.error'] || 'Error';
    }
  });

  // update diary KPIs when dates / phase change
  document.getElementById('webGrowStart')?.addEventListener('change', updateDiaryUI);
  document.getElementById('webFloweringStart')?.addEventListener('change', updateDiaryUI);
  document.getElementById('webDryingStart')?.addEventListener('change', updateDiaryUI);
  document.getElementById('phaseSelect')?.addEventListener('change', updateDiaryUI);

document.getElementById('toggleScrollBtn')?.addEventListener('click', () => {
    autoScroll = !autoScroll;
    document.getElementById('toggleScrollBtn').textContent = `AutoScroll: ${autoScroll ? 'ON' : 'OFF'}`;
  });

// ---------- Shelly info line updater (optional DOM elements) ----------
// If these elements exist in index_html.h, they will be filled from /api/state:
//   <div id="shellyMainInfo"></div>
//   <div id="shellyLightInfo"></div>


// Fetch /api/state and update Shelly info lines (and light schedule controls if present).
async function fetchStateAndUpdateShellyUI(){
  try{
    const res = await fetch('/api/state', { cache: 'no-store' });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const s = await res.json();

    // Always update info lines (these are simple <div> elements)
    updateShellyInfoLinesFromState(s);

    // If the Light schedule controls exist, also prefill them
    try{
      const onSel  = document.getElementById('shellyLightOnTime');
      const daySel = document.getElementById('shellyLightDayHours');
      const offInp = document.getElementById('shellyLightOffTime');
      if (onSel && daySel && offInp) {
        let onStr  = s['settings.shelly.light.on'];
        let offStr = s['settings.shelly.light.off'];

        // Fallback: parse from "settings.shelly.light.line"
        if ((!onStr || !offStr) && typeof s['settings.shelly.light.line'] === 'string') {
          const line = s['settings.shelly.light.line'];
          const mOn  = line.match(/\bON\s+(\d{1,2}:\d{2})\b/i);
          const mOff = line.match(/\bOFF\s+(\d{1,2}:\d{2})\b/i);
          if (!onStr  && mOn)  onStr  = mOn[1];
          if (!offStr && mOff) offStr = mOff[1];
        }

        // Apply ON
        if (typeof onStr === 'string' && onStr.includes(':')) {
          onSel.value = onStr;
        }

        // Derive day hours from ON/OFF if possible (12..23)
        if (typeof onStr === 'string' && typeof offStr === 'string') {
          const onMin = parseHHMM(onStr);
          const offMin = parseHHMM(offStr);
          if (onMin != null && offMin != null) {
            let diff = offMin - onMin;
            if (diff <= 0) diff += 1440;
            const hours = Math.round(diff / 60);
            daySel.value = String(clampInt(hours, 12, 23));
          }
        }

        // Compute OFF from UI values
        const onMin2 = parseHHMM(onSel.value);
        const dayHours2 = clampInt(daySel.value, 12, 23);
        offInp.value = (onMin2 != null) ? fmtHHMM((onMin2 + dayHours2 * 60) % 1440) : '—';
      }
    } catch(e) {}

    return s;
  } catch(e) {
    // No console error by default; uncomment if needed:
    // console.warn('[SHELLY] fetchStateAndUpdateShellyUI failed', e);
    return null;
  }
}
function updateShellyInfoLinesFromState(s){
  try{
    const map = [
      { ids: ['shellyMainInfo'],                         key: 'main'  },
      { ids: ['shellyLightInfo'],                        key: 'light' },
    ];

    for (const it of map) {
      const els = (it.ids || []).map(id => document.getElementById(id)).filter(Boolean);
      if (!els.length) continue;

      const base = `settings.shelly.${it.key}`;
      const line = s[`${base}.line`];

      if (typeof line === 'string' && line.length) {
        for (const el of els) el.textContent = line;
        continue;
      }

      const ip  = s[`${base}.ip`];
      const gen = s[`${base}.gen`];
      const on  = s[`${base}.on`];
      const off = s[`${base}.off`];

      const hasAny = (ip != null || gen != null || on != null || off != null);
      if (!hasAny) {
        for (const el of els) el.textContent = '—';
        continue;
      }

      const genTxt = (gen === null || typeof gen === 'undefined') ? '—' : String(gen);
      const onTxt  = (on  === null || typeof on  === 'undefined') ? '—' : String(on);
      const offTxt = (off === null || typeof off === 'undefined') ? '—' : String(off);
      const txt = `${ip || '—'} | Gen ${genTxt} | ON ${onTxt} | OFF ${offTxt}`;
      for (const el of els) el.textContent = txt;
    }
  } catch(e) {}
}

// ---------- Light schedule UI (Shelly settings page) ----------
// Requires these elements in index_html.h (Shelly page):
//   <select id="shellyLightOnTime"></select>
//   <select id="shellyLightDayHours"></select>
//   <input  id="shellyLightOffTime" readonly>
function initLightScheduleControls(){
  const onSel  = document.getElementById('shellyLightOnTime');
  const daySel = document.getElementById('shellyLightDayHours');
  const offInp = document.getElementById('shellyLightOffTime');
  if (!onSel || !daySel || !offInp) return;

  // Fill ON select (15-minute steps)
  if (!onSel._filled) {
    onSel.innerHTML = '';
    for (let h = 0; h < 24; h++) {
      for (let m = 0; m < 60; m += 15) {
        const opt = document.createElement('option');
        const v = `${h}:${pad2(m)}`;
        opt.value = v;
        opt.textContent = v;
        onSel.appendChild(opt);
      }
    }
    onSel._filled = true;
  }

  // Fill Day length select (12..23 hours)
  if (!daySel._filled) {
    daySel.innerHTML = '';
    for (let h = 12; h <= 23; h++) {
      const opt = document.createElement('option');
      opt.value = String(h);
      opt.textContent = String(h);
      daySel.appendChild(opt);
    }
    daySel._filled = true;
  }

  function recalcOff(){
    const onMin = parseHHMM(onSel.value);
    const dayHours = clampInt(daySel.value, 12, 23);
    if (onMin == null) { offInp.value = '—'; return; }
    const offMin = (onMin + dayHours * 60) % 1440;
    offInp.value = fmtHHMM(offMin);
  }

  const markDirty = () => {
    try {
      window.showStatusHint?.(I18N?.['settings.unsaved'] || 'Changes pending – please save');
    } catch(e) {}
  };

  // Recompute OFF time live
  onSel.addEventListener('change', () => { recalcOff(); markDirty(); });
  daySel.addEventListener('change', () => { recalcOff(); markDirty(); });

  // Initial calc
  recalcOff();
}

// Prefill ON/OFF from /api/state (read-only device schedule).
// This uses /api/state values:
//   settings.shelly.light.on  ("H:MM" or "HH:MM")
//   settings.shelly.light.off ("H:MM" or "HH:MM")
async function loadLightScheduleFromState(){
  try{
    // This already updates Shelly info lines + Light schedule controls (if present).
    await fetchStateAndUpdateShellyUI();
  } catch (e) {
    console.warn('[SHELLY] loadLightScheduleFromState failed', e);
  }
}

// ---------- Variables / State page ----------
  let _lastStateObj = null;

  function renderVars(stateObj, filterText) {
    const tbody = document.getElementById('varsTbody');
    if (!tbody) return;
    const f = (filterText || '').trim().toLowerCase();

    // flatten keys and sort for stable UX
    const entries = Object.entries(stateObj || {}).
      sort((a,b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0)
      .filter(([k,v]) => !f || k.toLowerCase().includes(f) || String(v).toLowerCase().includes(f));

    if (entries.length === 0) {
      tbody.innerHTML = '<tr><td colspan="2">—</td></tr>';
      return;
    }

    const rows = entries.map(([k, v]) => {
      // JSON values can be numbers, booleans, strings, null
      const val = (v === null || typeof v === 'undefined') ? 'null' : String(v);
      return `<tr><td>${escapeHtml(k)}</td><td>${escapeHtml(val)}</td></tr>`;
    });
    tbody.innerHTML = rows.join('');
  }

  async function loadVars() {
    const meta = document.getElementById('varsMeta');
    const search = document.getElementById('varsSearch');
    if (meta) meta.textContent = 'Loading…';
    try {
      const res = await fetch('/api/state', { cache: 'no-store' });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const obj = await res.json();
      _lastStateObj = obj;
      const n = obj ? Object.keys(obj).length : 0;
      if (meta) meta.textContent = `${n} values • updated ${new Date().toLocaleTimeString()}`;
      renderVars(obj, search ? search.value : '');
    } catch (e) {
      console.error('[VARS] load failed', e);
      if (meta) meta.textContent = 'Error loading /api/state';
    }
  }

  document.getElementById('varsRefreshBtn')?.addEventListener('click', loadVars);
  document.getElementById('varsSearch')?.addEventListener('input', (e) => renderVars(_lastStateObj, e.target.value));

  // Initial call to set the correct page on load
  const initiallyActive = document.querySelector('.page.active')?.id || 'status';
  onPageChanged(initiallyActive);
  updateRelayButtons();


  // Initialize Light schedule controls once on boot as well.
  // Even if the Shelly page isn't active yet, the elements exist in the DOM (hidden),
  // so we can safely prepare them here.
  try {
    initLightScheduleControls();
    loadLightScheduleFromState();
  } catch(e) {
    console.warn('[SHELLY] light schedule init failed', e);
  }


}); // end DOMContentLoaded

)rawliteral";