// index_html.h
#pragma once

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
    <span id="unsavedHint" class="dirty-hint" hidden data-i18n="settings.unsaved"></span>
    <div class="datetime">
      <div id="headerDate"></div>
      <div id="headerTime"></div>
    </div>
  </header>
  <div class="layout">
    <nav class="sidebar" id="sidebar">
      <a class="navlink" data-page="status"   data-i18n="nav.status">Status</a>
      <a class="navlink" data-page="settings" data-i18n="nav.settings">Einstellungen</a>
      <a class="navlink" data-page="factory" data-i18n="nav.factory">Werkseinstellung</a>
    </nav>

  <div class="overlay" id="overlay"></div>

  <main class="content" id="content">

    <!-- status section -->
    <section id="status" class="page active card">
      <h1 data-i18n="status.title">Status</h1>
      <p data-i18n="status.ok">System läuft normal ✅</p>
    </section>
    
    <!-- setting section -->
    <section id="settings" class="page card">
      <h1 data-i18n="settings.title">Einstellungen</h1>
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
      <button class="primary" id="saveBtn" data-i18n="settings.save">Speichern</button>
    </section>

    <!-- factory reset section -->
    <section id="factory" class="page active card">
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