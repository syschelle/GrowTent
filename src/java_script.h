// java_script.h 
#pragma once

// Pure JavaScript payload (no <script> tags, no HTML comments)
const char* jsContent = R"rawliteral(
// === Inject i18n JSON <script type="application/json"> tags ===
(function injectI18N(){
  const addJSON = (id, obj) => {
    const s = document.createElement('script');
    s.type = 'application/json';
    s.id   = id;
    s.textContent = JSON.stringify(obj);
    let currentLang = localStorage.getItem('lang') || 'de';
    document.head.appendChild(s);
  };

  addJSON('i18n-manifest', {
    "default": "de",
    "languages": [
      { "code": "de", "name": "Deutsch", "dir": "ltr" },
      { "code": "en", "name": "English", "dir": "ltr" }
    ]
  });

  addJSON('i18n-de', {
    "a11y.menu": "Menü öffnen/schließen",
    "info.Grow": "Aktueller Grow",
    "info.Days": "Tage",
    "info.Weeks": "Wochen",
    "settings.unsaved": "Änderungen – bitte speichern",
    "nav.status": "Status",
    "nav.runsetting": "Betriebseinstellungen",
    "nav.settings": "Systemeinstellungen",
    "nav.logging": "Systemprotokoll",
    "nav.factory": "Werkseinstellungen",
    "status.title": "Status",
    "status.updated": "Letztes Update:",
    "status.download": "History herunterladen",
    "status.delete": "History löschen",
    "status.currentValues": "Aktuelle Werte",
    "status.lastTemperature": "akt. Temperatur",
    "status.targetTemp": "Soll-Temperatur",
    "status.lastWaterTemperature": "akt. Wassertemperatur",
    "status.lasthumidity": "akt. Luftfeuchte",
    "status.lastvpd": "akt. VPD",
    "status.targetVpd": "Soll-VPD:",
    "status.averagesLastHour": "Durchschnittswerte der letzten Stunde",
    "status.avgTemperature": "Ø Temperatur",
    "status.avgWaterTemperature": "Ø Wassertemperatur",
    "status.avgHumidity": "Ø rel. Feuchte",
    "status.avgVpd": "Ø VPD",
    "status.relayControl": "Relais Steuerung",
    "status.toggleRelay": "umschalten",
    "runsetting.title": "Betriebseinstellungen",
    "runsetting.startGrow": "Startdatum:",
    "runsetting.startFlower": "Startdatum Blüte:",
    "runsetting.startDry": "Startdatum Trocknung:",
    "runsetting.phase": "aktuelle Phase:",
    "runsetting.phase.grow": "VEGETATIV",
    "runsetting.phase.flower": "BLÜTE",
    "runsetting.phase.dry": "TROCKNUNG",
    "runsetting.targetTemp": "Soll-Temperatur",
    "runsetting.offsetLeafTemperature": "Offset Blatttemperatur:",
    "runsetting.targetVPD": "Soll-VPD",
    "settings.title": "Systemeinstellungen",
    "settings.boxName": "Boxname:",
    "settings.boxName.ph": "z. B. Growtent-1",
    "settings.ntpserver": "NTP-Server:",
    "settings.ntpserver.ph": "z. B. de.pool.ntp.org",
    "settings.timeZoneInfo": "Zeitzone:",
    "settings.timeZoneInfo.ph": "z.B. WEST-1D/WEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00",
    "settings.language": "Sprache:",
    "settings.theme": "Theme:",
    "settings.themeLight": "Hell",
    "settings.themeDark": "Dunkel",
    "settings.dateFormat": "Datumsformat:",
    "settings.df_ymd": "YYYY-MM-DD",
    "settings.df_dmy": "DD.MM.YYYY",
    "settings.timeFormat": "Zeitformat:",
    "settings.tf_HHmm": "24h",
    "settings.tf_hhmma": "12h AM/PM",
    "settings.save": "Speichern",
    "settings.tempUnit": "Temperatur-Einheit:",
    "settings.celsius": "°C (Celsius)",
    "settings.fahrenheit": "°F (Fahrenheit)",
    "logging.title": "Systemprotokoll",
    "factory.title": "Werkseinstellungen",
    "factory.reset": "Zurücksetzen / Neustart"
  });

  addJSON('i18n-en', {
    "a11y.menu": "Open/close menu",
    "info.Grow": "Current Grow",
    "info.Days": "Days",
    "info.Weeks": "Weeks",
    "settings.unsaved": "Changes pending – please save",
    "nav.status": "Status",
    "nav.runsetting": "Operating Settings",
    "nav.settings": "System Settings",
    "nav.logging": "System Log",
    "nav.factory": "Factory Reset",
    "status.title": "Status",
    "status.updated": "Last update:",
    "status.download": "Download History",
    "status.delete": "Delete History",
    "status.currentValues": "current Values",
    "status.lastTemperature": "current Temperature",
    "status.targetTemp": "target Temperature",
    "status.lastWaterTemperature": "current Water Temperature",
    "status.lasthumidity": "current Humidity",
    "status.lastvpd": "current VPD",
    "status.targetVpd": "target VPD",
    "status.averagesLastHour": "Averages last hour",
    "status.avgTemperature": "avg. Temperature",
    "status.avgWaterTemperature": "avg. Water Temperature",
    "status.avgHumidity": "avg. Humidity",
    "status.avgVpd": "avg. VPD",
    "status.relayControl": "Relay Control",
    "status.toggleRelay": "toggle",
    "runsetting.title": "Operating settings",
    "runsetting.startGrow": "Start Date:",
    "runsetting.startFlower": "Start Flowering Date:",
    "runsetting.startDry": "Start Drying Date:",
    "runsetting.phase": "Current Phase:",
    "runsetting.phase.grow": "VEGETATIVE",
    "runsetting.phase.flower": "FLOWERING",
    "runsetting.phase.dry": "DRYING",
    "runsetting.targetTemp": "Target temperature",
    "runsetting.offsetLeafTemperature": "Offset leaf temperature",
    "runsetting.targetVPD": "Target VPD",
    "settings.title": "Settings",
    "settings.boxName": "Box name:",
    "settings.ntpserver": "NTP server:",
    "settings.ntpserver.ph": "e.g. pool.ntp.org",
    "settings.timeZoneInfo": "Time zone:",
    "settings.timeZoneInfo.ph": "e.g. WEST-1D/WEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00",
    "settings.boxName.ph": "e.g. Growtent-1",
    "settings.language": "Language:",
    "settings.theme": "Theme:",
    "settings.themeLight": "Light",
    "settings.themeDark": "Dark",
    "settings.dateFormat": "Date format:",
    "settings.df_ymd": "YYYY-MM-DD",
    "settings.timeFormat": "Time format:",
    "settings.tf_HHmm": "24h",
    "settings.tf_hhmma": "12h AM/PM",
    "settings.save": "Save",
    "settings.tempUnit": "Temperature unit:",
    "settings.celsius": "°C (Celsius)",
    "settings.fahrenheit": "°F (Fahrenheit)",
    "logging.title": "Logging Settings",
    "factory.title": "Factory Settings",
    "factory.reset": "Reset / Restart"
  });
})();

// ---- relay state (4 relays) ----
let relayStates = [false, false, false, false];

// Aktualisiert die Relay-Buttons im UI
window.updateRelayButtons = function() {
  for (let i = 0; i < relayStates.length; i++) {
    const btn = document.getElementById(`relayStatus${i + 1}`);
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

// Run after DOM is ready
window.addEventListener('DOMContentLoaded', () => {

  // ---------- Small DOM helpers ----------
  const $  = (id) => document.getElementById(id);
  const setText = (id, val) => { const el = $(id); if (el) el.textContent = val; };
  const isNum = x => typeof x === 'number' && !Number.isNaN(x);

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
  }
  (function initTheme(){
    const saved = localStorage.getItem('theme');
    const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
    applyTheme(saved || (prefersDark ? 'dark' : 'light'));
  })();
  $('theme')?.addEventListener('change', e => applyTheme(e.target.value));

  // ---------- Date / Time helpers ----------
  function pad2(n){ return String(n).padStart(2,'0'); }
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
  let manifest=null, I18N={}, currentLang='de';
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
      rerenderDatePreviews();
    });
    const df=$('dateFormat');
    if(df){ const saved=localStorage.getItem('dateFormat')||getDefaultDateFormatFor(currentLang); if(df.value!==saved) df.value=saved; }
    const tf=$('timeFormat');
    if(tf){ const saved=localStorage.getItem('timeFormat')||getDefaultTimeFormatFor(currentLang); if(tf.value!==saved) tf.value=saved; }
    const tu=$('tempUnit'); if(tu){ const savedTU=getTempUnit(); if(tu.value!==savedTU) tu.value=savedTU; }
    renderHeaderDateTime();
  }
  function setLanguage(code){
    try{ I18N = readJsonTag('i18n-'+code); currentLang = code; }
    catch{ I18N = readJsonTag('i18n-de');  currentLang = 'de'; }
    localStorage.setItem('lang', currentLang);
    if(!localStorage.getItem('dateFormat')) localStorage.setItem('dateFormat', getDefaultDateFormatFor(currentLang));
    if(!localStorage.getItem('timeFormat')) localStorage.setItem('timeFormat', getDefaultTimeFormatFor(currentLang));
    if(!localStorage.getItem('tempUnit'))   localStorage.setItem('tempUnit', currentLang === 'en' ? 'F' : 'C');
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
  })();

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

      // current
      if (isNum(data.curTemperature)) { setText('tempSpan', data.curTemperature.toFixed(1)); }
      if (isNum(data.curWaterTemperature)) { setText('waterTempSpan', data.curWaterTemperature.toFixed(1)); }
      if (isNum(data.curHumidity))    { setText('humSpan',  data.curHumidity.toFixed(0)); }
      if (isNum(data.curVpd))         { setText('vpdSpan',  data.curVpd.toFixed(1)); }

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
      console.error('Exception in updateSensorValues():', error);
      setNA();
      updateRelayButtons();
    }
  }
  function setNA(){
    setText('tempSpan', 'N/A');
    setText('waterTempSpan', 'N/A');
    setText('humSpan',  'N/A');
    setText('vpdSpan',  'N/A');
    setText('capturedSpan', 'N/A');
    // averages optional
  }
  // Poll every 10s and once immediately
  setInterval(updateSensorValues, 10000);
  updateSensorValues();

  // ---------- Relay helpers ----------
  function toggleRelay(nr) {
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
  }

  function updateRelayButtons() {
    for (let i = 0; i < relayStates.length; i++) {
      const btn = document.getElementById(`relay${i+1}`);
      if (!btn) continue;
      if (relayStates[i]) {
        btn.classList.add('on');
        btn.classList.remove('off');
      } else {
        btn.classList.add('off');
        btn.classList.remove('on');
      }
    }
  }

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

  document.addEventListener('DOMContentLoaded', () => {
    const pre = document.getElementById('weblog');
    if (!pre) return;

    pre.addEventListener('scroll', () => {
      const nearBottom = Math.abs(pre.scrollTop + pre.clientHeight - pre.scrollHeight) < 10;
      autoScroll = nearBottom;
    });
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

  // SPA-Seitenwechsel-Callback
  function onPageChanged(activeId) {
    if (activeId === 'logging') startWebLog(); else stopWebLog();
  }

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
  
  document.getElementById('toggleScrollBtn')?.addEventListener('click', () => {
    autoScroll = !autoScroll;
    document.getElementById('toggleScrollBtn').textContent = `AutoScroll: ${autoScroll ? 'ON' : 'OFF'}`;
  });

  // Initial call to set the correct page on load
  const initiallyActive = document.querySelector('.page.active')?.id || 'status';
  onPageChanged(initiallyActive);
  updateRelayButtons();

}); // end DOMContentLoaded

updateRelayButtons();

)rawliteral";