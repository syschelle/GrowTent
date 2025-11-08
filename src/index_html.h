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
<meta name="viewport" content="width=device-width, initial-scale=1">
<head>
  <title>%CONTROLLERNAME%</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <header class="header">
    <button class="hamburger" id="hamburgerBtn" data-i18n="a11y.menu" data-i18n-attr="aria-label" aria-label="Menü öffnen/schließen" aria-expanded="false" aria-controls="sidebar">☰</button>
    <div class="title" data-i18n="app.title">%CONTROLLERNAME%</div>
    <span id="unsavedHint" class="dirty-hint" hidden data-i18n="settings.unsaved"></span>
    <div class='grow-info'>%CURRENTGROW%</div>
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
        <a class="btn" href="/deletelog" data-i18n="status.delete">CSV löschen</a>
      </p>

      <!-- 3 values side by side -->
      <div class="metrics-row">
        <div class="metric">
          <div class="metric-label" data-i18n="status.lastTemperature">Temperatur</div>
          <div class="metric-value">
            <span id="tempSpan">–</span><span class="unit">°C</span>
          </div>
          <div class="spacer"></div>
          <div class="metric-sub">
            <span data-i18n="status.targetTemp">Soll</span>
            <div class="metric-value">
              <span id="targetTempStatus">%TARGETTEMPERATURE%</span> <span class="unit">°C</span>
            </div>
          </div>
          <div class="spacer"></div>
          <div class="metric-submetric">
          <div class="metric-label" data-i18n="status.lastWaterTemperature">Wassertemperatur</div>
          <div class="metric-value">
            <span id="waterTempSpan">%WATERTEMPERATURE%</span><span class="unit">°C</span>
          </div>
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
          <div class="spacer"></div>
          <div class="metric-sub">
            <span data-i18n="status.targetVpd">Soll</span>
            <div class="metric-value">
              <span id="targetVpdStatus">%TARGETVPD%</span> <span class="unit">kPa</span>
            </div>
          </div>
          <div class="spacer"></div>
          <div class="metric-sub">
            <span data-i18n="runsetting.offsetLeafTemperature">Offset Blatttemperatur:</span>
            <div class="metric-value">
              <span id="leafTempStatus">%LEAFTEMPERATURE%</span> <span class="unit">°C</span>
            </div>
          </div>
        </div>
      </div>
      <div class="spacer"></div>
      <div class="metrics-row averages-row">
        <div class="metric">
          <div class="metric-label" data-i18n="status.avgTemperature">Ø Temperatur</div>
          <div class="metric-value">
            <span id="avgTempSpan">%AVGTEMP%</span><span class="unit">°C</span>
          </div>
          <div class="spacer"></div>
          <div class="metric-submetric">
            <div class="metric-label" data-i18n="status.avgWaterTemperature">Ø Wassertemperatur</div>
            <div class="metric-value">
              <span id="avgWaterTempSpan">%AVGWATERTEMP%</span><span class="unit">°C</span>
            </div>
          </div>
        </div>
        <div class="metric">
          <div class="metric-label" data-i18n="status.avgHumidity">Ø rel. Feuchte</div>
          <div class="metric-value">
            <span id="avgHumSpan">%AVGHUM%</span><span class="unit">%</span>
          </div>
        </div>
        <div class="metric">
          <div class="metric-label" data-i18n="status.avgVpd">Ø VPD</div>
          <div class="metric-value">
            <span id="avgVpdSpan">%AVGVPD%</span><span class="unit">kPa</span>
          </div>
        </div>
      </div>
      <div class="metrics-row averages-row">
      <!-- ... wie oben eingefügt ... -->
    </div>

    <h2 data-i18n="status.relayControl">Relais Steuerung</h2>
    <div class="relay-row" id="relayRow">
      <div class="relay-card" data-relay="1">
        <div class="relay-title">Relay 1</div>
        <div class="relay-status" id="relayStatus1">OFF</div>
        <button class="relay-btn" onclick="toggleRelay(1)">Toggle</button>
      </div>
      <div class="relay-card" data-relay="2">
        <div class="relay-title">Relay 2</div>
        <div class="relay-status" id="relayStatus2">OFF</div>
        <button class="relay-btn" onclick="toggleRelay(2)">Toggle</button>
      </div>
      <div class="relay-card" data-relay="3">
        <div class="relay-title">Relay 3</div>
        <div class="relay-status" id="relayStatus3">OFF</div>
        <button class="relay-btn" onclick="toggleRelay(3)">Toggle</button>
      </div>
      <div class="relay-card" data-relay="4">
        <div class="relay-title">Relay 4</div>
        <div class="relay-status" id="relayStatus4">OFF</div>
        <button class="relay-btn" onclick="toggleRelay(4)">Toggle</button>
      </div>
    </div>
    </section>
    
    <!-- runsettings section -->
    <form action="/saverunsettings" method="POST">
    <section id="runsettings" class="page card">
      <h1 data-i18n="runsetting.title">Betriebseinstellungen</h1>

      <div class="form-group">
        <div class="tile-right-settings">
          <div class="form-group">
            <label for="webGrowStart" data-i18n="runsetting.startGrow">Start Grow Date:</label>
            <input id="webGrowStart" name="webGrowStart" type="date" style="width: 170px;" value="%GROWSTARTDATE%">
          </div>
          <div class="form-group">
            <label for="webFloweringStart" data-i18n="runsetting.startFlower">Start Flowering Date:</label>
            <input id="webFloweringStart" name="webFloweringStart" type="date" style="width: 170px;" value="%GROWFLOWERDATE%">
          </div>
          <div class="form-group">
            <label for="webDryingStart" data-i18n="runsetting.startDry">Start Drying Date:</label>
            <input id="webDryingStart" name="webDryingStart" type="date" style="width: 170px;" value="%GROWDRAYINGDATE%">
          </div>
        </div>

        <div class="form-group">
        <label for="phaseSelect" data-i18n="runsetting.phase">Phase:</label>
        <select id="phaseSelect" style="width: 170px;" name="phaseSelect">
          <option value="grow"   data-i18n="runsetting.phase.grow">Wuchs</option>
          <option value="flower" data-i18n="runsetting.phase.flower">Blüte</option>
          <option value="dry"    data-i18n="runsetting.phase.dry">Trocknung</option>
        </select>
        </div>

      <div class="form-group">
        <label for="targetTemp" data-i18n="runsetting.targetTemp">Soll-Temperatur:</label>
        <input name="webTargetTemp" id="webTargetTemp" style="width: 65px;" type="number" step="0.5" min="18" max="30" value="%TARGETTEMPERATURE%">&nbsp;°C
      </div>

      <div class="form-group">
        <label for="leafTemp" data-i18n="runsetting.offsetLeafTemperature">Offset Blatttemperatur:</label>
        <input name="webOffsetLeafTemp" id="webOffsetLeafTemp" style="width: 65px;" type="number" step="0.1" min="-3.0" max="0.0" value="%LEAFTEMPERATURE%">&nbsp;°C
      </div>

      <div class="form-group">
        <label for="targetVPD" data-i18n="runsetting.targetVPD">Soll-VPD:</label>
        <input name="webTargetVPD" id="webTargetVPD" style="width: 65px;" type="number" step="0.1" min="0.5" max="1.5" value="%TARGETVPD%">&nbsp;kPa
      </div>
      <button class="primary" id="saverunsettingsBtn" data-i18n="settings.save">Speichern</button>
    </section>
    </form>

    <!-- setting section -->
    <form action="/savesettings" method="POST">
      <section id="settings" class="page card">
        <h1 data-i18n="settings.title">Systemeinstellungen</h1>

        <div class="form-group">
          <label for="webBoxName" data-i18n="settings.boxName">Boxname:</label>
          <input name="webBoxName" id="webBoxName" type="text" data-i18n="settings.boxName.ph" data-i18n-attr="placeholder" style="width: 320px;" value="%CONTROLLERNAME%">
        </div>

        <div class="form-group">
          <label for="webNTPServer" data-i18n="settings.ntpserver">NTP-Server:</label>
          <input name="webNTPServer" id="webNTPServer" type="text" data-i18n="settings.ntpserver.ph" data-i18n-attr="placeholder" style="width: 250px;" value="%NTPSERVER%">
        </div>

        <div class="form-group">
          <div class="label-inline">
            <label for="webTimeZoneInfo" data-i18n="settings.timeZoneInfo">Zeitzone:</label>
            &nbsp;<a href="https://github.com/nayarsystems/posix_tz_db/blob/master/zones.json" target="_blank" rel="noopener noreferrer">🌐</a>
          </div>
          <input name="webTimeZoneInfo" id="webTimeZoneInfo" type="text" data-i18n="settings.timeZoneInfo.ph" data-i18n-attr="placeholder" style="width: 350px;" value="%TZINFO%">
        </div>

        <div class="form-group">
          <label for="language" data-i18n="settings.language">Sprache:</label>
          <select name="webLanguage" id="language" style="width: 100px;">
            <!-- Optionen vermutlich per JS/i18n gefüllt -->
          </select>
        </div>

        <div class="form-group">
          <label for="theme" data-i18n="settings.theme">Theme:</label>
          <select name="webTheme" id="theme" style="width: 100px;">
            <option value="light" data-i18n="settings.themeLight">Hell</option>
            <option value="dark"  data-i18n="settings.themeDark">Dunkel</option>
          </select>
        </div>

        <div class="form-group">
          <label for="dateFormat" data-i18n="settings.dateFormat">Datumsformat:</label>
          <select name="webDateFormat" id="dateFormat" style="width: 140px;">
            <option value="YYYY-MM-DD" data-i18n="settings.df_ymd">YYYY-MM-DD</option>
            <option value="DD.MM.YYYY" data-i18n="settings.df_dmy">DD.MM.YYYY</option>
          </select>
        </div>

        <div class="form-group">
          <label for="timeFormat" data-i18n="settings.timeFormat">Zeitformat:</label>
          <select name="webTimeFormat" id="timeFormat" style="width: 100px;">
            <option value="24" data-i18n="settings.tf_HHmm">24h</option>
            <option value="12" data-i18n="settings.tf_hhmma">12h AM/PM</option>
          </select>
        </div>

        <div class="form-group">
          <label for="tempUnit" data-i18n="settings.tempUnit">Temperatur-Einheit:</label>
          <select name="webTempUnit" id="tempUnit" style="width: 140px;">
            <option value="C" data-i18n="settings.celsius">°C (Celsius)</option>
            <option value="F" data-i18n="settings.fahrenheit">°F (Fahrenheit)</option>
          </select>
        </div>

        <button class="primary" id="saveSettingsBtn" data-i18n="settings.save">Speichern</button>
      </section>
    </form>

    <!-- system log section -->
    <section id="logging" class="page card">
      <h1 data-i18n="logging.title">Systemprotokoll</h1>
      <div class="weblog-card">
        <div class="weblog-head">
          <strong>System-Log</strong>
          <div class="weblog-actions">
            <a class="btn" href="/download/log">CSV/TXT Download</a>
            <button class="btn" id="toggleScrollBtn" type="button">AutoScroll: ON</button>
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