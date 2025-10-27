// index_html.h
#pragma once

// Favicon (base64-encoded)
const char FAVICON_ICO_BASE64[] PROGMEM = 
"AAABAAEAEBAAAAEAIABoBAAAFgAAACgAAAAQAAAAIAAAAAEAIAAAAAAAAAQAAAAAAAAAAAAAAAAA"
"AAAAAAD///8Af39/AFhYWAAgICAATU1NAH5+fgBQUFAAZGRkADY2NgB9fX0ARUVFAFlZWQA/Pz8A"
"bm5uAERERABra2sAenp6AEpKSgA7OzsAioqKAFtbWwBVVVUAISEhADAwMAD+/v4AAAAAAAAAAAAA"
"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIAAAACAAIAAgACAgICAAIAAgICAgACA"
"AICAgIAAgACAgICAAIAAgICAgACAAICAgIAAgACAgICAAIAAgICAgACAAICAgIAAgACAgICAAIAA"
"gICAgACAAICAgIAAgACAgICAAIAAgICAgACAAICAgIAAgACAgICAAIAAgICAgACAAICAgIAAgAAA"
"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD//wAA"
"//8AAP//AAD//wAA//8AAP//AAD//wAA";

// ------- i18n: manifest + Sprachen (separat, eigene Routen) -------
static const char I18N_MANIFEST[] PROGMEM = R"json(
{
  "default": "de",
  "languages": [
    { "code": "de", "name": "Deutsch", "dir": "ltr", "file": "de" },
    { "code": "en", "name": "English", "dir": "ltr", "file": "en" }
  ]
}
)json";

static const char I18N_DE[] PROGMEM = R"json(
{
  "app.title": "Mein Webfrontend",
  "settings.themeLight": "Hell",
  "settings.themeDark": "Dunkel",
  "status.title": "Status",
  "status.ok": "System läuft normal ✅"
}
)json";

static const char I18N_EN[] PROGMEM = R"json(
{
  "app.title": "My Web Frontend",
  "settings.themeLight": "Light",
  "settings.themeDark": "Dark",
  "status.title": "Status",
  "status.ok": "System is running ✅"
}
)json";

const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <title>%CONTENTCONTROLLERNAME%</title>
  <meta charset="UTF-8">
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <header class="header">
    <button class="hamburger" id="hamburgerBtn" data-i18n="a11y.menu" data-i18n-attr="aria-label" aria-label="Menü öffnen/schließen" aria-expanded="false" aria-controls="sidebar">☰</button>
    <div class="title" data-i18n="app.title">%CONTENTCONTROLLERNAME%</div>
    <span id="unsavedHint" class="dirty-hint" hidden data-i18n="settings.unsaved"></span>
    <div class="datetime">
      <div id="headerDate"></div>
      <div id="headerTime"></div>
    </div>
  </header>
  <div class="layout">
    <nav class="sidebar" id="sidebar">
      <a class="navlink" data-page="status"   data-i18n="nav.status">Status</a>
      <a class="navlink" data-page="runsettings" data-i18n="nav.runsetting">Betriebseinstellungen</a>
      <a class="navlink" data-page="settings" data-i18n="nav.settings">Systemeinstellungen</a>
      <a class="navlink" data-page="logging" data-i18n="nav.logging">Systemprotokoll</a>
      <a class="navlink" data-page="factory" data-i18n="nav.factory">Werkseinstellungen</a>
    </nav>

  <div class="overlay" id="overlay"></div>

  <main class="content" id="content">

    <!-- status section -->
    <section id="status" class="page active card">
      <h1 data-i18n="status.title">Status</h1>
      <!-- Letztes Update direkt unter dem Statustext -->
      <p class="last-update">
        <span data-i18n="status.updated">Letztes Update:</span>
        <span id="capturedSpan">--</span>
      </p>
      <p style="margin-top:10px">
        <a class="btn" href="/download/history" data-i18n="status.download">CSV herunterladen</a>
      </p>

      <!-- 3 values side by side -->
      <div class="metrics-row">
        <div class="metric">
          <div class="metric-label" data-i18n="status.lastTemperature">Temperatur</div>
          <div class="metric-value">
            <span id="tempSpan">–</span><span class="unit">°C</span>
          </div>
        </div>

        <div class="metric">
          <div class="metric-label" data-i18n="status.lasthumidity">rel. Feuchte</div>
          <div class="metric-value">
            <span id="humSpan">–</span><span class="unit">%</span>
          </div>
        </div>

        <div class="metric">
          <div class="metric-label" data-i18n="status.lastvpd">VPD</div>
          <div class="metric-value">
            <span id="vpdSpan">–</span><span class="unit">kPa</span>
          </div>
        </div>
      </div>
    </section>
    
    <!-- runsettings section -->
    <section id="runsettings" class="page card">
      <h1 data-i18n="runsetting.title">Betriebseinstellungen</h1>

      <div class="form-group">
        <label for="targetTemp" data-i18n="runsetting.targetTemp">Soll-Temperatur:</label>
        <select id="targetTemp">
          <option value="15">15 °C</option>
          <option value="15.5">15.5 °C</option>
          <option value="16">16 °C</option>
          <option value="16.5">16.5 °C</option>
          <option value="17">17 °C</option>
          <option value="17.5">17.5 °C</option>
          <option value="18">18 °C</option>
          <option value="18.5">18.5 °C</option>
          <option value="19">19 °C</option>
          <option value="19.5">19.5 °C</option>
          <option value="20">20 °C</option>
          <option value="20.5">20.5 °C</option>
          <option value="21">21 °C</option>
          <option value="21.5">21.5 °C</option>
          <option value="22">22 °C</option>
          <option value="22.5">22.5 °C</option>
          <option value="23">23 °C</option>
          <option value="23.5">23.5 °C</option>
          <option value="24">24 °C</option>
          <option value="24.5">24.5 °C</option>
          <option value="25">25 °C</option>
        </select>
      </div>

      <div class="form-group">
        <label for="targetVPD" data-i18n="runsetting.targetVPD">Soll-VPD:</label>
          <select id="targetVPD">
            <option value="0.5">0.5 kPa</option>
            <option value="0.6">0.6 kPa</option>
            <option value="0.7">0.7 kPa</option>
            <option value="0.8">0.8 kPa</option>
            <option value="0.9">0.9 kPa</option>
            <option value="1.0">1.0 kPa</option>
            <option value="1.1">1.1 kPa</option>
            <option value="1.2">1.2 kPa</option>
            <option value="1.3">1.3 kPa</option>
            <option value="1.4">1.4 kPa</option>
            <option value="1.5">1.5 kPa</option>
            <option value="1.6">1.6 kPa</option>
            <option value="1.8">1.8 kPa</option>
            <option value="2.0">2.0 kPa</option>
          </select>
      </div>
      <button class="primary" id="saverunsettingsBtn" data-i18n="settings.save">Speichern</button>
    </section>

    <!-- setting section -->
    <section id="settings" class="page card">
      <h1 data-i18n="settings.title">Systemeinstellungen</h1>
      <div class="form-group">
        <label for="boxName" data-i18n="settings.boxName">Boxname:</label>
        <input type="text" data-i18n="settings.boxName.ph" id="boxName" data-i18n-attr="placeholder">
      </div>
      <div class="form-group">
        <label for="language" data-i18n="settings.language">Sprache:</label>
        <select id="language"></select>
      </div>
      <div class="form-group">
        <label for="theme" data-i18n="settings.theme">Theme:</label>
        <select id="theme">
          <option value="light" data-i18n="settings.themeLight">Hell</option>
          <option value="dark"  data-i18n="settings.themeDark">Dunkel</option>
        </select>
      </div>
      <div class="form-group">
        <label for="dateFormat" data-i18n="settings.dateFormat">Datumsformat:</label>
        <select id="dateFormat">
          <option value="YYYY-MM-DD" data-i18n="settings.df_ymd">YYYY-MM-DD</option>
          <option value="DD.MM.YYYY" data-i18n="settings.df_dmy">DD.MM.YYYY</option>
        </select>
      </div>
      <div class="form-group">
        <label for="timeFormat" data-i18n="settings.timeFormat">Zeitformat:</label>
        <select id="timeFormat">
          <option value="24"     data-i18n="settings.tf_HHmm">24h</option>
          <option value="12"   data-i18n="settings.tf_hhmma">12h AM/PM</option>
        </select>
      </div>
      <div class="form-group">
        <label for="tempUnit" data-i18n="settings.tempUnit">Temperatur-Einheit:</label>
        <select id="tempUnit">
          <option value="C" data-i18n="settings.celsius">°C (Celsius)</option>
          <option value="F" data-i18n="settings.fahrenheit">°F (Fahrenheit)</option>
        </select>
      </div>
      <button class="primary" id="saveSettingsBtn" data-i18n="settings.save">Speichern</button>
    </section>

    <!-- system log section -->
    <section id="logging" class="page card">
      <h1 data-i18n="logging.title">Systemprotokoll</h1>
      <div class="weblog-card">
        <div class="weblog-head">
          <strong>System-Log</strong>
          <div class="weblog-actions">
            <a class="btn" href="/download/log">CSV/TXT Download</a>
            <button class="btn" id="clearLogBtn" type="button" title="Log löschen">Clear</button>
          </div>
        </div>
        <pre id="weblog" class="weblog" aria-live="polite" aria-label="Laufende Logausgabe">…</pre>
      </div>
    </section>

    <!-- factory reset section -->
    <section id="factory" class="page card">
      <form action="/factory-reset" method="post" id="factoryResetForm">
        <h1 data-i18n="factory.title">Werkseinstellungen</h1>
        <input type="hidden" name="confirm" value="1">
        <button class="primary" id="factoryResetBtn" type="submit" data-i18n="factory.reset">factory reset</button>
      </form>
    </section>
  </main>
  </div>
  
 <script src="/script.js"></script>
</body>
</html>
)rawliteral";

const char* apPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>%CONTENTCONTROLLERNAME%</title>
  <meta charset="UTF-8">
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <header class="header">
    <button class="hamburger" id="hamburgerBtn" data-i18n="a11y.menu" data-i18n-attr="aria-label" aria-label="Menü öffnen/schließen" aria-expanded="false" aria-controls="sidebar">☰</button>
    <div class="title" data-i18n="app.title">%CONTENTCONTROLLERNAME%</div>
    </div>
  </header>
  <div class="layout">
    <nav class="sidebar" id="sidebar">
      <a class="navlink" data-page="settings"   data-i18n="nav.wifisettings">WIFI Setting</a>
    </nav>

  <div class="overlay" id="overlay"></div>

    <main class="content" id="content">
      <section id="status" class="page active card">
        <form action="/save" method="post">
          <h1 data-i18n="settings.title">WIFI Setting</h1>
          <label for="ssid">WIFI SSID:</label>
          <input type="text" id="ssid" name="ssid" required><br><br>
          <label for="password">WIFI Passwort:</label>
          <input type="password" id="password" name="password" required><br><br>
          <button class="primary" id="saveBtn" data-i18n="settings.save">save & reboot</button>
        </form>
      </section>
      <section id="status" class="page active card">
        <form action="/factory-reset" method="post" id="factoryResetForm">
          <h1 data-i18n="settings.title">Factory Reset</h1>
          <input type="hidden" name="confirm" value="1">
          <button class="primary" id="factoryResetBtn" type="submit" data-i18n="settings.factoryreset.button">factory reset</button>
        </form>
      </section>
    </main>
  </div>
  
 <script src="/script.js"></script>
</body>
</html>
)rawliteral";