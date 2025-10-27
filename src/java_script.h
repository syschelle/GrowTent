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
    "settings.unsaved": "Änderungen – bitte speichern",
    "nav.status": "Status",
    "nav.runsetting": "Betriebseinstellungen",
    "nav.settings": "Systemeinstellungen",
    "nav.logging": "Systemprotokoll",
    "nav.factory": "Werkseinstellungen",
    "status.title": "Status",
    "status.updated": "Letztes Update:",
    "status.download": "History herunterladen",
    "status.lastTemperature": "akt. Temperatur",
    "status.lasthumidity": "akt. Luftfeuchte",
    "status.lastvpd": "akt. VPD",
    "runsetting.title": "Betriebseinstellungen",
    "settings.title": "Systemeinstellungen",
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
    "settings.unsaved": "Changes pending – please save",
    "nav.status": "Status",
    "nav.runsetting": "Operating Settings",
    "nav.settings": "System Settings",
    "nav.logging": "System Log",
    "nav.factory": "Factory Reset",
    "status.title": "Status",
    "status.updated": "Last update:",
    "status.download": "Download History",
    "status.lastTemperature": "current Temperature",
    "status.lasthumidity": "current Humidity",
    "status.lastvpd": "current VPD",
    "runsetting.title": "Operating settings",
    "settings.title": "Settings",
    "settings.language": "Language:",
    "settings.theme": "Theme:",
    "settings.themeLight": "Light",
    "settings.themeDark": "Dark",
    "settings.dateFormat": "Date format:",
    "settings.df_ymd": "YYYY-MM-DD",
    "settings.df_dmy": "DD.MM.YYYY",
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

  // SPA navigation + call onPageChanged
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

  // ---------- Temperature unit (optional text conversions) ----------
  function getTempUnit(){ return localStorage.getItem('tempUnit') || (currentLang === 'en' ? 'F' : 'C'); }
  function setTempUnit(unit){
    localStorage.setItem('tempUnit', unit);
    const sel=$('tempUnit'); if(sel && sel.value !== unit) sel.value = unit;
  }
  $('tempUnit')?.addEventListener('change', e => setTempUnit(e.target.value));

  // ---------- i18n (inline scripts) ----------
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

      if (isNum(data.temperature)) { setText('tempSpan', data.temperature.toFixed(1)); }
      else                         { setText('tempSpan', 'N/A'); }

      if (isNum(data.humidity))    { setText('humSpan',  data.humidity.toFixed(0)); }
      else                         { setText('humSpan',  'N/A'); }

      if (isNum(data.vpd))         { setText('vpdSpan',  data.vpd.toFixed(1)); }
      else                         { setText('vpdSpan',  'N/A'); }

      const cap =
        (typeof data.captured === 'string' && data.captured.length) ? data.captured :
        (isNum(data.ts) ? new Date(data.ts).toLocaleString() : 'N/A');
      setText('capturedSpan', cap);

    } catch (error) {
      console.error('Exception in updateSensorValues():', error);
      setNA();
    }
  }
  function setNA(){
    setText('tempSpan', 'N/A');
    setText('humSpan',  'N/A');
    setText('vpdSpan',  'N/A');
    setText('capturedSpan', 'N/A');
  }
  // Poll every 10s and once immediately
  setInterval(updateSensorValues, 10000);
  updateSensorValues();

  // ---------- Embedded Web-Log ----------
  let logTimer = null;

  async function fetchWebLog() {
    try {
      const r = await fetch('/api/logbuffer', { cache: 'no-store' });
      if (!r.ok) return;
      const t = await r.text();
      const pre = $('weblog');
      if (pre) {
        pre.textContent = t || '—';
        pre.scrollTop = pre.scrollHeight; // auto-scroll to bottom
      }
    } catch (e) {
      console.warn('weblog fetch failed', e);
    }
  }
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
  $('clearLogBtn')?.addEventListener('click', async () => {
    try {
      await fetch('/api/logbuffer/clear', { method: 'POST' });
      fetchWebLog();
    } catch {}
  });

  // Fire on page changes (status => start log polling)
  function onPageChanged(activeId) {
    if (activeId === 'status') startWebLog(); else stopWebLog();
  }
  // Initial: assume status is active on load
  onPageChanged('status');

}); // end DOMContentLoaded
)rawliteral";