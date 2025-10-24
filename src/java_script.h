// style_css.h
#pragma once

// Reines JavaScript – KEINE <script>-Tags, KEINE HTML-Kommentare
const char* jsContent = R"rawliteral(
/* === i18n JSON-Tags dynamisch in den <head> einfügen === */
(function injectI18N(){
  const addJSON = (id, obj) => {
    var s = document.createElement('script');
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
    "nav.factory": "Werkseinstellungen",

    "status.title": "Status",
    "status.ok": "System läuft normal ✅",

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

    "factory.title": "Werkseinstellungen",
    "factory.reset": "zrücksetzen / Neustart"
  });

  addJSON('i18n-en', {
    "a11y.menu": "Open/close menu",
    
    "settings.unsaved": "Changes pending – please save",

    "nav.status": "Status",
    "nav.runsetting": "Operating Settings",
    "nav.settings": "System Settings",
    "nav.factory": "Factory Reset",

    "status.title": "Status",
    "status.ok": "System is running ✅",

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

    "factory.title": "Factory Settings",
    "factory.reset": "Reset / Restart"
  });
})();

/* ===== Sidebar & SPA ===== */
const mqDesktop=window.matchMedia('(min-width:1024px)');
const sidebar=document.getElementById('sidebar');
const overlay=document.getElementById('overlay');
const burgerBtn=document.getElementById('hamburgerBtn');
const pages=document.querySelectorAll('.page');

function openSidebar(){ if(mqDesktop.matches) return; sidebar.classList.add('sidebar--open'); overlay.classList.add('overlay--show'); burgerBtn.setAttribute('aria-expanded','true'); }
function closeSidebar(){ sidebar.classList.remove('sidebar--open'); overlay.classList.remove('overlay--show'); burgerBtn.setAttribute('aria-expanded','false'); }
function toggleSidebar(){ if(mqDesktop.matches) return; const o=sidebar.classList.contains('sidebar--open'); o?closeSidebar():openSidebar(); }
function syncLayout(){ if(mqDesktop.matches){ closeSidebar(); } }
burgerBtn.addEventListener('click',toggleSidebar);
overlay.addEventListener('click',closeSidebar);
window.addEventListener('keydown',e=>{ if(e.key==='Escape') closeSidebar(); });
mqDesktop.addEventListener('change',syncLayout);

sidebar.addEventListener('click',e=>{
  const link=e.target.closest('.navlink'); if(!link) return;
  const id=link.getAttribute('data-page');
  pages.forEach(p=>p.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  sidebar.querySelectorAll('.navlink').forEach(a=>a.removeAttribute('aria-current'));
  link.setAttribute('aria-current','page');
  closeSidebar();
});
syncLayout();

/* ===== Theme ===== */
function applyTheme(theme){
  document.documentElement.setAttribute('data-theme',theme);
  localStorage.setItem('theme',theme);
  const sel=document.getElementById('theme'); if(sel && sel.value!==theme) sel.value=theme;
}
(function initTheme(){
  const saved=localStorage.getItem('theme');
  const prefersDark=window.matchMedia('(prefers-color-scheme: dark)').matches;
  applyTheme(saved || (prefersDark?'dark':'light'));
})();
document.getElementById('theme')?.addEventListener('change',e=>applyTheme(e.target.value));

/* ===== Date/Time helpers ===== */
function pad2(n){ return String(n).padStart(2,'0'); }
function getDefaultDateFormatFor(lang){ return lang==='de'?'DD.MM.YYYY':'YYYY-MM-DD'; }
function getDefaultTimeFormatFor(lang){ return lang==='de'?'HH:mm':'hh:mm A'; }
function formatDateWithPattern(d,pat){
  const y=d.getFullYear(), m=pad2(d.getMonth()+1), day=pad2(d.getDate());
  if(pat==='DD.MM.YYYY') return `${day}.${m}.${y}`;
  return `${y}-${m}-${day}`;
}
function formatTimeWithPattern(d,pat){
  let h=d.getHours(); const m=pad2(d.getMinutes()); const s=pad2(d.getSeconds());
  if(pat.includes('A')){ const ap=h>=12?'PM':'AM'; let hh=h%12; if(hh===0) hh=12; hh=pad2(hh); return `${hh}:${m}${pat.includes('ss')?':'+s:''} ${ap}`; }
  const HH=pad2(h); return `${HH}:${m}${pat.includes('ss')?':'+s:''}`;
}
function getCurDateFmt(){ return localStorage.getItem('dateFormat') || getDefaultDateFormatFor(currentLang||'de'); }
function getCurTimeFmt(){ return localStorage.getItem('timeFormat') || getDefaultTimeFormatFor(currentLang||'de'); }
function renderHeaderDateTime(){
  const d=new Date();
  const hd=document.getElementById('headerDate');
  const ht=document.getElementById('headerTime');
  if(!hd || !ht) return;
  hd.textContent=formatDateWithPattern(d,getCurDateFmt());
  ht.textContent=formatTimeWithPattern(d,getCurTimeFmt());
}
document.getElementById('dateFormat')?.addEventListener('change',e=>{ localStorage.setItem('dateFormat',e.target.value); renderHeaderDateTime(); });
document.getElementById('timeFormat')?.addEventListener('change',e=>{ localStorage.setItem('timeFormat',e.target.value); renderHeaderDateTime(); });
setInterval(renderHeaderDateTime,1000);

/* ===== Temperatur-Einheit & Umrechnung ===== */
function getTempUnit(){ return localStorage.getItem('tempUnit') || (currentLang==='en'?'F':'C'); }
function setTempUnit(unit){
  localStorage.setItem('tempUnit',unit);
  const sel=document.getElementById('tempUnit'); if(sel && sel.value!==unit) sel.value=unit;
  renderTemperatures();
}
function convertCelsiusString(strC,unit){
  if(!strC) return '';
  if(unit==='C'){
    return strC.replace(/°F/g,'°C').replace(/°\s*$/,' °C');
  }
  // Zahlen → °F (C * 9/5 + 32), Text & Zeichen bleiben
  const swapped=strC.replace(/°C/g,'°F');
  return swapped.replace(/(\d+(?:\.\d+)?)/g,(m)=>{
    const c=parseFloat(m); if(Number.isNaN(c)) return m;
    const f=c*9/5+32;
    return (Math.abs(f-Math.round(f))<0.05)? String(Math.round(f)) : f.toFixed(1);
  });
}
function renderTemperatures(){
  const unit=getTempUnit();
  document.querySelectorAll('[data-temp]').forEach(el=>{
    const src=el.getAttribute('data-temp');
    el.textContent=convertCelsiusString(src,unit);
  });
}
document.getElementById('tempUnit')?.addEventListener('change',e=>setTempUnit(e.target.value));

/* ===== i18n inline ===== */
let manifest=null, I18N={}, currentLang='de';
function readJsonTag(id){
  const el=document.getElementById(id); if(!el) throw new Error('Missing tag: '+id);
  return JSON.parse(el.textContent.trim());
}
function buildLanguageSelect(activeCode){
  const sel=document.getElementById('language'); if(!sel) return;
  sel.innerHTML='';
  (manifest.languages||[{code:'de',name:'Deutsch'},{code:'en',name:'English'}]).forEach(({code,name})=>{
    const opt=document.createElement('option'); opt.value=code; opt.textContent=name||code.toUpperCase(); sel.appendChild(opt);
  });
  if(activeCode) sel.value=activeCode;
  sel.onchange=e=>setLanguage(e.target.value);
}
function applyTranslations(){
  document.querySelectorAll('[data-i18n]').forEach(el=>{
    const key=el.getAttribute('data-i18n');
    const attr=el.getAttribute('data-i18n-attr');
    const val=I18N[key];
    if(val!==undefined){ if(attr){ el.setAttribute(attr,val); } else { el.textContent=val; } }
  });
  // Defaults für Formate nur setzen, wenn nichts gespeichert wurde
  const df=document.getElementById('dateFormat');
  if(df){ const saved=localStorage.getItem('dateFormat')||getDefaultDateFormatFor(currentLang); if(df.value!==saved) df.value=saved; }
  const tf=document.getElementById('timeFormat');
  if(tf){ const saved=localStorage.getItem('timeFormat')||getDefaultTimeFormatFor(currentLang); if(tf.value!==saved) tf.value=saved; }
  // TempUnit-Select aktualisieren
  const tu=document.getElementById('tempUnit'); if(tu){ const savedTU=getTempUnit(); if(tu.value!==savedTU) tu.value=savedTU; }
  // Header sofort
  renderHeaderDateTime();
  // Temperaturen rendern
  renderTemperatures();
}
function setLanguage(code){
  try{ I18N=readJsonTag('i18n-'+code); currentLang=code; }
  catch{ I18N=readJsonTag('i18n-de'); currentLang='de'; }
  localStorage.setItem('lang',currentLang);
  // Nur beim ersten Mal Defaults setzen
  if(!localStorage.getItem('dateFormat')) localStorage.setItem('dateFormat', getDefaultDateFormatFor(currentLang));
  if(!localStorage.getItem('timeFormat')) localStorage.setItem('timeFormat', getDefaultTimeFormatFor(currentLang));
  if(!localStorage.getItem('tempUnit'))   localStorage.setItem('tempUnit', currentLang==='en'?'F':'C');
  applyTranslations();
  const sel=document.getElementById('language'); if(sel && sel.value!==currentLang) sel.value=currentLang;
}
(function initI18n(){
  try{ manifest=readJsonTag('i18n-manifest'); }
  catch{ manifest={default:'de',languages:[{code:'de',name:'Deutsch'},{code:'en',name:'English'}]}; }
  const urlLang=new URLSearchParams(location.search).get('lang');
  const savedLang=localStorage.getItem('lang');
  const initial=urlLang || savedLang || (manifest.default||'de');
  buildLanguageSelect(initial);
  setLanguage(initial);
})();

// Initial render tick
renderHeaderDateTime();
renderTemperatures();
)rawliteral";