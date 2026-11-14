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
<html lang="en">
<meta name="viewport" content="width=device-width, initial-scale=1">
<head>
  <title>%CONTROLLERNAME%</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="/style.css?v=%FWVERSION%">
<body data-temp-unit="%UNIT%">
  <header class="header">
    <button class="hamburger" id="hamburgerBtn" data-i18n="a11y.menu" data-i18n-attr="aria-label" aria-label="Menü öffnen/schließen" aria-expanded="false" aria-controls="sidebar">☰</button>
    <div class="title-stack">
      <div class="title" data-i18n="app.title">%CONTROLLERNAME%</div>
      <div class="firmware-version">%FWVERSION%</div>
    </div>
    <span id="unsavedHint" class="dirty-hint" hidden data-i18n="settings.unsaved"></span>
    <span id="headerHmacStatus" class="hmac-header-status" %BACKEND_HMAC_HIDDEN% title="HMAC-SHA256 Backend Write API active">HMAC</span>
    <div id="headerFloodStatus" class="flood-header-status flood-unknown" hidden title="Flood S Gen4">
      <span id="headerFloodMain">Flood: –</span>
      <span id="headerFloodMeta" class="flood-header-meta"></span>
    </div>
    <div class='grow-info'>%CURRENTGROW%<BR>%CURRENTPHASE%</div>
    <div id="grow-line" data-i18n-key="info.growLine"></div>
    <div class="datetime">
      <div id="headerDate"></div>
      <div id="headerTime"></div>
    </div>
  </header>
  <div class="layout">
    <nav class="sidebar" id="sidebar">
      <a class="navlink" data-page="status"   data-i18n="nav.status">Status</a>
      <a class="navlink" data-page="diary"   data-i18n="nav.diary">Grow Diary</a>
      <a class="navlink" data-page="runsettings" data-i18n="nav.runsetting">Betriebseinstellungen</a>
      <a class="navlink" data-page="shelly" data-i18n="nav.shelly">Shelly Einstellungen</a>
      <a class="navlink" data-page="message" data-i18n="nav.message">Push-Einstellungen</a>
      <a class="navlink" data-page="settings" data-i18n="nav.settings">Systemeinstellungen</a>
      <a class="navlink" data-page="logging" data-i18n="nav.logging">Systemprotokoll</a>
      <a class="navlink" data-page="vars" data-i18n="nav.vars">Variablen</a>
      <a class="navlink" data-page="ota" data-i18n="nav.ota">OTA Update</a>
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
      <div class="spacer"></div>
      <h2 data-i18n="status.currentValues">aktuelle Werte</h2>
      <!-- 3 values side by side -->
      <div class="metrics-row">
        <div class="metric">
          <div class="twoinone-label">
            <div class="metric-label" data-i18n="status.temperature">Temperatur</div>
            <div class="metric-value">
              <span id="tempSpan">–</span><span class="unit" data-temp-unit-label>°C</span>
            </div>
          </div>
          <div class="spacer"></div>
          <div class="metric-sub">
            <div class="twoinone-label">
              <span data-i18n="status.targetTemp">Soll</span>
              <div class="metric-value">
                <span id="targetTempStatus" data-celsius-value="%TARGETTEMPERATURE%">%TARGETTEMPERATURE%</span> <span class="unit" data-temp-unit-label>°C</span>
              </div>
            </div>
          </div>
          <div class="spacer"></div>
          <div class="metric-submetric">
            <div class="twoinone-label">
              <div class="metric-label">%DS18B20NAME%</div>
              <div class="metric-value">
                <span id="ext1TempSpan" data-celsius-value="%WATERTEMPERATURE%">%WATERTEMPERATURE%</span><span class="unit" data-temp-unit-label>°C</span>
            </div>
          </div>
        </div>
        </div>

        <div class="metric">
          <div class="twoinone-label">
            <div class="metric-label" data-i18n="status.humidity">Luftfeuchte</div>
            <div class="metric-value">
              <span id="humSpan">–</span><span class="unit">%</span>
            </div>
          </div>
        </div>

        <div class="metric">
          <div class="twoinone-label">
            <div class="metric-label" data-i18n="status.lastvpd">VPD</div>
            <div class="metric-value">
              <span id="vpdSpan">–</span><span class="unit">kPa</span>
            </div>
          </div>
          <div class="spacer"></div>
          <div class="metric-sub">
            <div class="twoinone-label">
              <span data-i18n="status.targetVpd">Soll</span>
              <div class="metric-value">
                <span id="targetVpdStatus">%TARGETVPD%</span> <span class="unit">kPa</span>
              </div>
            </div>
          </div>
          <div class="spacer"></div>
          <div class="metric-sub">
            <div class="twoinone-label">
              <span data-i18n="runsetting.offsetLeafTemperature">Offset Blatttemperatur:</span>
              <div class="metric-value">
                <span id="leafTempStatus" data-celsius-value="%LEAFTEMPERATURE%">%LEAFTEMPERATURE%</span> <span class="unit" data-temp-unit-label>°C</span>
              </div>
            </div>
          </div>
        </div>
      </div>
      <div class="spacer"></div>

    <h2 data-i18n="status.relayControl">Relais Steuerung</h2>
    <div class="relay-row" id="relayRow">
      <div class="relay-card" data-relay="1">
        <div class="relay-title">%RELAYNAMES1%</div>
        <div class="relay-status" id="relay-Status1"></div>
        <div class="spacer"></div>
        <button class="primary" data-i18n="status.toggleRelay" onclick="toggleRelay(1)">Toggle</button>
      </div>
      <div class="relay-card" data-relay="2">
        <div class="relay-title">%RELAYNAMES2%</div>
        <div class="relay-status" id="relay-Status2"></div>
        <div class="spacer"></div>
        <button class="primary" data-i18n="status.toggleRelay" onclick="toggleRelay(2)">Toggle</button>
      </div>
      <div class="relay-card" data-relay="3">
        <div class="relay-title">%RELAYNAMES3%</div>
        <div class="relay-status" id="relay-Status3"></div>
        <div class="spacer"></div>
        <button class="primary" data-i18n="status.toggleRelay" onclick="toggleRelay(3)">Toggle</button>
      </div>
      <div class="relay-card" data-relay="4">
        <div class="relay-title">%RELAYNAMES4%</div>
        <div class="relay-status" id="relay-Status4"></div>
        <div class="spacer"></div>
        <button class="primary" data-i18n="status.toggleRelay" onclick="toggleRelay(4)">Toggle</button>
      </div>
      <div class="relay-card relay-extra" data-relay="5" id="relayCard5" style="display:none;">
        <div class="relay-title">%RELAYNAMES5%</div>
        <div class="relay-status" id="relay-Status5"></div>
        <div class="spacer"></div>
        <button class="primary" data-i18n="status.toggleRelay" onclick="toggleRelay(5)">Toggle</button>
      </div>
    </div>
    <div class="spacer"></div>
    
    <div class="tile-row" id="irrigationSection" style="display:none;">
      <div class="tile">
        <h2 data-i18n="status.irrigation">Irrigation</h2>
        <p id="localIrrigationPlanStatus" role="status"><span data-i18n="status.localPlan.loading">Lokaler Plan: Status wird geladen …</span></p>
        <p id="localIrrigationNextStatus" role="status">Nächste Bewässerung: —</p>
         <div class="relay-row" id="relayRow">
          <div class="relay-card irrigation" data-relay="6" id="irrigationRelay6">
            <div class="relay-title" data-i18n="status.irrigationRelay6">Pumpe 1 Relay 6</div>
            <div class="relay-status" id="relay-Status6"></div>
            <div class="spacer"></div>
            <div class="pump-disabled-action" id="pumpDisabledAction6" data-i18n="status.pumpDisabled">Deaktiviert</div>
            <button type="button" class="primary" id="pumpButton6" data-i18n="status.togglePump10s" onclick="triggerPump10s(6)" disabled hidden>Toggle</button>
          </div>

          <div class="relay-card irrigation" data-relay="7" id="irrigationRelay7">
            <div class="relay-title" data-i18n="status.irrigationRelay7">Pumpe 2 Relay 7</div>
            <div class="relay-status" id="relay-Status7"></div>
            <div class="spacer"></div>
            <div class="pump-disabled-action" id="pumpDisabledAction7" data-i18n="status.pumpDisabled">Deaktiviert</div>
            <button type="button" class="primary" id="pumpButton7" data-i18n="status.togglePump10s" onclick="triggerPump10s(7)" disabled hidden>Toggle</button>
          </div>

          <div class="relay-card irrigation" data-relay="8" id="irrigationRelay8">
            <div class="relay-title" data-i18n="status.irrigationRelay8">Pumpe 3 Relay 8</div>
            <div class="relay-status" id="relay-Status8"></div>
            <div class="spacer"></div>
            <div class="pump-disabled-action" id="pumpDisabledAction8" data-i18n="status.pumpDisabled">Deaktiviert</div>
            <button type="button" class="primary" id="pumpButton8" data-i18n="status.togglePump10s" onclick="triggerPump10s(8)" disabled hidden>Toggle</button>
          </div>

          <!-- watering section -->
          <div class="relay-card" data-relay="watering">
            <div class="relay-title" data-i18n="status.watering">Bewässerung</div>
            <div class="metric-value">
              <span id="irrigationSpan"  >-</span><span class="unit" data-i18n="status.wateringLeft"> verbleibend</span>
            </div>
            <div class="metric-value">
              <span class="unit" data-i18n="status.endIn" >Ende in </span><span id="irTimeLeftSpan"  ></span>
            </div> 
            <div class="spacermini"></div>
            <button class="primary" id="startWateringButton" onclick="toggleWatering()" disabled>Bewässerung starten</button>
          </div>
          <div class="relay-card" data-relay="TankFilling">
            <div class="relay-title" data-i18n="status.tank">Tank Füllung</div>
            <div class="metric-value">
              <span id="tankLevelSpan" >–</span><span class="unit" >&nbsp;%</span>
            </div>
            <div class="metric-value">
              <span id="tankCMDistanceSpan" >–</span><span class="unit" >&nbsp;cm</span>
            </div>
            <div class="spacermini"></div>
              <button class="primary" data-i18n="status.pingTank" onclick="pingTank()">Ping</button>
          </div>

        </div>
      </div>
    </div>

    <div class="spacer"></div>
    <h2 data-i18n="status.shellyControl">Shelly Steuerung</h2>
    <div class="relay-row" id="relayRow">
      <div class="relay-card" data-relay="shellyMainSw">
        <div class="relay-title" data-i18n="status.shellyMainSw">Hauptschalter</div>
        <div id="shelly-main-switch-state" class="shelly-status shelly-off"></div>
        <div class="info" id="shellyMainInfo">—</div>
        <button class="primary" data-i18n="status.toggleRelay" onclick="toggleShellyRelay('mainSw')">Toggle</button>
      </div>
      <div class="relay-card" data-relay="shellyLight">
        <div class="relay-title" data-i18n="status.shellyLight">Grow Licht</div>
        <div id="shelly-light-switch-state" class="shelly-status shelly-off"></div>
        <div class="info" id="shellyLightInfo">—</div>
        <button class="primary" data-i18n="status.toggleRelay" onclick="toggleShellyRelay('light')">Toggle</button>
        <button class="primary shelly-refresh-button" data-i18n="shelly.refreshSchedule" onclick="refreshShellySchedule('light', this)">Schedule neu einlesen</button>
      </div>
      <div class="relay-card" data-relay="shellyHumidifier">
        <div class="relay-title" data-i18n="status.shellyHumidifier">Luftbefeuchter</div>
        <div id="shelly-humidifier-switch-state" class="shelly-status shelly-off"></div>
        <div class="info" id="shellyHumidifierInfo">—</div>
        <button class="primary" data-i18n="status.toggleRelay" onclick="toggleShellyRelay('humidifier')">Toggle</button>
        <button class="primary shelly-refresh-button" data-i18n="shelly.refreshSchedule" onclick="refreshShellySchedule('humidifier', this)">Schedule neu einlesen</button>
      </div>
      <div class="relay-card" data-relay="shellyHeater">
        <div class="relay-title" data-i18n="status.shellyHeater">Heizung</div>
        <div id="shelly-heater-switch-state" class="shelly-status shelly-off"></div>
        <div class="info" id="shellyHeaterInfo">—</div>
        <button class="primary" data-i18n="status.toggleRelay" onclick="toggleShellyRelay('heater')">Toggle</button>
        <button class="primary shelly-refresh-button" data-i18n="shelly.refreshSchedule" onclick="refreshShellySchedule('heater', this)">Schedule neu einlesen</button>
      </div>
      <div class="relay-card" data-relay="shellyFan">
        <div class="relay-title" data-i18n="status.shellyFan">Lüfter</div>
        <div id="shelly-fan-switch-state" class="shelly-status shelly-off"></div>
        <div class="info" id="shellyFanInfo">—</div>
        <button class="primary" data-i18n="status.toggleRelay" onclick="toggleShellyRelay('fan')">Toggle</button>
        <button class="primary shelly-refresh-button" data-i18n="shelly.refreshSchedule" onclick="refreshShellySchedule('fan', this)">Schedule neu einlesen</button>
      </div>
      <div class="relay-card" data-relay="shellyExhaust">
        <div class="relay-title" data-i18n="status.shellyExhaust">Abluft</div>
        <div id="shelly-exhaust-switch-state" class="shelly-status shelly-off"></div>
        <div class="info" id="shellyExhaustInfo">—</div>
        <button class="primary" data-i18n="status.toggleRelay" onclick="toggleShellyRelay('exhaust')">Toggle</button>
        <button class="primary shelly-refresh-button" data-i18n="shelly.refreshSchedule" onclick="refreshShellySchedule('exhaust', this)">Schedule neu einlesen</button>
      </div>
      <div class="relay-card" data-relay="shellyHeatMat" id="shellyHeatMatCard" style="display:none;">
        <div class="relay-title" data-i18n="status.shellyHeatMat">Heizmatte</div>
        <div id="shelly-heatmat-switch-state" class="shelly-status shelly-off"></div>
        <div class="info" id="shellyHeatMatInfo">—</div>
      </div>
    </div>
    
    </section>

    <!-- diary section -->
    <section id="diary" class="page card">
      <h1 data-i18n="diary.title">Grow Diary</h1>

      <div class="diary-grid">
        <div class="diary-kpi">
          <div class="diary-kpi-title" data-i18n="diary.total">Total grow</div>
          <div class="diary-kpi-val">
            <span id="diaryGrowDay">–</span>
            <span class="unit" data-i18n="diary.day">Day</span>
            &nbsp;•&nbsp;
            <span id="diaryGrowWeek">–</span>
            <span class="unit" data-i18n="diary.week">Week</span>
          </div>
        </div>

        <div class="diary-kpi">
          <div class="diary-kpi-title" data-i18n="diary.phase">Phase</div>
          <div class="diary-kpi-val">
            <span id="diaryPhaseName">–</span>
            &nbsp;•&nbsp;
            <span id="diaryPhaseDay">–</span>
            <span class="unit" data-i18n="diary.day">Day</span>
            &nbsp;•&nbsp;
            <span id="diaryPhaseWeek">–</span>
            <span class="unit" data-i18n="diary.week">Week</span>
          </div>
        </div>
      </div>

      <div class="spacer"></div>

      <div class="form-group">
        <label for="diaryNote" data-i18n="diary.note">Note (max 400 characters)</label>
        <textarea id="diaryNote" maxlength="400" rows="4" placeholder="..." data-i18n-attr="placeholder" data-i18n="diary.note.ph"></textarea>
        <div class="diary-foot">
          <span id="diaryCount">0/400</span>
          <span id="diaryStatus" class="muted"> </span>
        </div>
      </div>

      <div class="btn-row">
        <button type="button" class="btn primary" id="diarySaveBtn" data-i18n="diary.save">Save entry</button>
        <a class="btn danger" href="/api/diary.csv" id="diaryDownloadBtn" data-i18n="diary.download">Download CSV</a>
        <button type="button" class="btn danger" id="diaryClearBtn" data-i18n="diary.clear">Clear diary</button>
      </div>

      <div id="diaryList" class="diary-list"></div>
    </section>

    <div class="spacer"></div>
    <!-- shellysettings section -->
    <section id="shelly" class="page card shelly-settings-page">
      <h1 data-i18n="shelly.title">Shelly Einstellungen</h1>
      <form id="shellySettingsForm" onsubmit="event.preventDefault(); window.saveShellySettingsAll(event); return false;">
        <section class="runsettings-panel shelly-auth-panel">
      <h2 class="runsettings-panel-title" data-i18n="status.shellyAuth">Shelly Authentifizierung</h2>
<div class="shelly-auth-grid">
      <div class="form-group shelly-settings-field">
        <label for="shellyUsername" data-i18n="shelly.shellyAuthUser">Shelly Benutzername:</label>
        <input name="webShellyUsername" id="shellyUsername" class="control-sm shelly-other" type="text" value="%SHUSER%">
      </div>

      <div class="form-group shelly-settings-field">
        <label for="shellyPassword" data-i18n="shelly.shellyAuthPassword">Shelly Passwort:</label>
        <input name="webShellyPassword" id="shellyPassword" class="control-sm shelly-other" type="password" autocomplete="new-password" data-i18n="shelly.shellyAuthPassword.ph" data-i18n-attr="placeholder" value="%SHPASSWORD%">
      </div>

</div>
</section>
<div class="shelly-device-grid">
<section class="runsettings-panel shelly-device-panel" data-shelly-role="main" aria-labelledby="shellyPanelMainSw">
<h2 id="shellyPanelMainSw" class="runsettings-panel-title" data-i18n="shelly.panel.main">Hauptschalter</h2>
<div class="shelly-settings-field"><label for="shellyMainSwIP" data-i18n="shelly.shellyIP">Shelly IP-Adresse:</label>
<div class="shelly-device-address"><input name="webShellyMainIP" id="shellyMainSwIP" class="control-sm shelly-ip" maxlength="15" type="text" inputmode="decimal" value="%SHELLYMAINIP%"></div></div>
<p class="shelly-device-generation"><span data-i18n="shelly.panel.generation">Erkannte Generation</span>: <strong class="shelly-gen-detected" id="shellyMainSwGenDetected">—</strong></p>
</section>
<section class="runsettings-panel shelly-device-panel" data-shelly-role="light" aria-labelledby="shellyPanelLight">
<h2 id="shellyPanelLight" class="runsettings-panel-title" data-i18n="shelly.panel.light">Pflanzlicht</h2>
<div class="shelly-settings-field"><label for="shellyLightIP" data-i18n="shelly.shellyIP">Shelly IP-Adresse:</label>
<div class="shelly-device-address"><input name="webShellyLightIP" id="shellyLightIP" class="control-sm shelly-ip" maxlength="15" type="text" inputmode="decimal" value="%SHELLYLIGHTIP%"></div></div>
<p class="shelly-device-generation"><span data-i18n="shelly.panel.generation">Erkannte Generation</span>: <strong class="shelly-gen-detected" id="shellyLightGenDetected">—</strong></p>
<div class="shelly-light-fields"><div class="shelly-settings-field"><label for="shellyLightOnTime" data-i18n="shelly.panel.lightOn">Licht an</label><select name="webShellyLightOnTime" id="shellyLightOnTime" class="control-sm shelly-other"></select></div>
<div class="shelly-settings-field"><label for="shellyLightDayHours" data-i18n="shelly.panel.lightHours">Leuchtdauer (Stunden)</label><select name="webShellyLightDayHours" id="shellyLightDayHours" class="control-sm shelly-other"></select></div>
<div class="shelly-settings-field"><label for="shellyLightOffTime" data-i18n="shelly.panel.lightOff">Licht aus (berechnet)</label><input name="webShellyLightOffTime" id="shellyLightOffTime" class="control-sm shelly-other" type="text" readonly value="—"></div>
</div>
<button type="button" class="primary shelly-refresh-button" data-i18n="shelly.refreshSchedule" onclick="refreshShellySchedule('light', this)">Schedule neu einlesen</button>
</section>
<section class="runsettings-panel shelly-device-panel" data-shelly-role="humidifier" aria-labelledby="shellyPanelHumidifier">
<h2 id="shellyPanelHumidifier" class="runsettings-panel-title" data-i18n="shelly.panel.humidifier">Luftbefeuchter</h2>
<div class="shelly-settings-field"><label for="shellyHumidifierIP" data-i18n="shelly.shellyIP">Shelly IP-Adresse:</label>
<div class="shelly-device-address"><input name="webShellyHumidifierIP" id="shellyHumidifierIP" class="control-sm shelly-ip" maxlength="15" type="text" inputmode="decimal" value="%SHELLYHUMIDIFIERIP%"></div></div>
<p class="shelly-device-generation"><span data-i18n="shelly.panel.generation">Erkannte Generation</span>: <strong class="shelly-gen-detected" id="shellyHumidifierGenDetected">—</strong></p>
</section>
<section class="runsettings-panel shelly-device-panel" data-shelly-role="heater" aria-labelledby="shellyPanelHeater">
<h2 id="shellyPanelHeater" class="runsettings-panel-title" data-i18n="shelly.panel.heater">Heizung</h2>
<div class="shelly-settings-field"><label for="shellyHeaterIP" data-i18n="shelly.shellyIP">Shelly IP-Adresse:</label>
<div class="shelly-device-address"><input name="webShellyHeaterIP" id="shellyHeaterIP" class="control-sm shelly-ip" maxlength="15" type="text" inputmode="decimal" value="%SHELLYHEATERIP%"></div></div>
<p class="shelly-device-generation"><span data-i18n="shelly.panel.generation">Erkannte Generation</span>: <strong class="shelly-gen-detected" id="shellyHeaterGenDetected">—</strong></p>
</section>
<section class="runsettings-panel shelly-device-panel" data-shelly-role="fan" aria-labelledby="shellyPanelFan">
<h2 id="shellyPanelFan" class="runsettings-panel-title" data-i18n="shelly.panel.fan">Lüfter</h2>
<div class="shelly-settings-field"><label for="shellyFanIP" data-i18n="shelly.shellyIP">Shelly IP-Adresse:</label>
<div class="shelly-device-address"><input name="webShellyFanIP" id="shellyFanIP" class="control-sm shelly-ip" maxlength="15" type="text" inputmode="decimal" value="%SHELLYFANIP%"></div></div>
<p class="shelly-device-generation"><span data-i18n="shelly.panel.generation">Erkannte Generation</span>: <strong class="shelly-gen-detected" id="shellyFanGenDetected">—</strong></p>
</section>
<section class="runsettings-panel shelly-device-panel" data-shelly-role="exhaust" aria-labelledby="shellyPanelExhaust">
<h2 id="shellyPanelExhaust" class="runsettings-panel-title" data-i18n="shelly.panel.exhaust">Abluft</h2>
<div class="shelly-settings-field"><label for="shellyExhaustIP" data-i18n="shelly.shellyIP">Shelly IP-Adresse:</label>
<div class="shelly-device-address"><input name="webShellyExhaustIP" id="shellyExhaustIP" class="control-sm shelly-ip" maxlength="15" type="text" inputmode="decimal" value="%SHELLYEXHAUSTIP%"></div></div>
<p class="shelly-device-generation"><span data-i18n="shelly.panel.generation">Erkannte Generation</span>: <strong class="shelly-gen-detected" id="shellyExhaustGenDetected">—</strong></p>
</section>
<section class="runsettings-panel shelly-device-panel" data-shelly-role="heatmat" aria-labelledby="shellyPanelHeatMat">
<h2 id="shellyPanelHeatMat" class="runsettings-panel-title" data-i18n="shelly.panel.heatmat">Heizmatte</h2>
<div class="shelly-settings-field"><label for="shellyHeatMatIP" data-i18n="shelly.shellyIP">Shelly IP-Adresse:</label>
<div class="shelly-device-address"><input name="webShellyHeatMatIP" id="shellyHeatMatIP" class="control-sm shelly-ip" maxlength="15" type="text" inputmode="decimal" value="%SHELLYHEATMATIP%"></div></div>
<p class="shelly-device-generation"><span data-i18n="shelly.panel.generation">Erkannte Generation</span>: <strong class="shelly-gen-detected" id="shellyHeatMatGenDetected">—</strong></p>
<small data-i18n="shelly.heatMatMonitorOnly">Nur Monitoring; die Heizmatte wird vom GrowTent nicht geschaltet.</small>
</section>
</div>
<section class="runsettings-panel shelly-flood-panel">
      <h2 class="runsettings-panel-title" data-i18n="shelly.floodTitle">Flood S Gen4</h2>
      <div class="form-group">
        <label>
          <input name="webFloodEnabled" id="floodEnabled" type="checkbox" %FLOOD_ENABLED_CHECKED% onchange="updateFloodWebhookPreview()">
          <span data-i18n="shelly.floodEnabled">Flood S Gen4 Webhook aktivieren</span>
        </label>
      </div>

      <div class="form-group">
        <label for="floodToken" data-i18n="shelly.floodToken">Webhook Token:</label>
        <div class="twoinone-label">
          <input name="webFloodToken" id="floodToken" class="control-sm" style="width:320px;max-width:100%;" type="text" maxlength="64" autocomplete="off" value="%FLOOD_TOKEN%" oninput="updateFloodWebhookPreview()">
          <button type="button" class="primary" data-i18n="shelly.floodGenerateToken" onclick="generateFloodToken()">Token erzeugen</button>
        </div>
        <small data-i18n="shelly.floodTokenHint">Der Token bleibt ausschließlich im GrowTent-Controller und wird nicht über /api/state ausgegeben.</small>
      </div>

      <h3 data-i18n="shelly.floodApiLinks">API Links für Shelly Actions</h3>
      <div class="form-group">
        <label for="floodAlarmUrl" data-i18n="shelly.floodAlarmUrl">URL für „Flood alarm“:</label>
        <div class="twoinone-label">
          <input id="floodAlarmUrl" class="control-sm" type="text" readonly style="width:100%;max-width:760px;">
          <button type="button" class="primary" data-i18n="shelly.copyLink" onclick="copyFloodUrl('floodAlarmUrl')">Kopieren</button>
        </div>
      </div>
      <div class="form-group">
        <label for="floodClearUrl" data-i18n="shelly.floodClearUrl">URL für „Flood alarm off“:</label>
        <div class="twoinone-label">
          <input id="floodClearUrl" class="control-sm" type="text" readonly style="width:100%;max-width:760px;">
          <button type="button" class="primary" data-i18n="shelly.copyLink" onclick="copyFloodUrl('floodClearUrl')">Kopieren</button>
        </div>
      </div>
      <div class="form-group">
        <small data-i18n="shelly.floodHint">Diese beiden URLs im Flood S Gen4 unter Actions hinterlegen. Der GrowTent-Controller pollt den Batteriesensor nicht.</small>
      </div>


</section>
<div class="shelly-settings-actions"><button type="button" class="primary" id="saveshellysettingsBtn" data-i18n="settings.save" onclick="window.saveShellySettingsAll(event)">Speichern</button></div>
      </form>
    </section>

    <!-- runsettings section -->
    <section id="runsettings" class="page card runsettings-page">
      <div class="runsettings-page-head">
        <h1 data-i18n="runsetting.title">Betriebseinstellungen</h1>
        <div class="runsettings-head-actions">
          <div class="runsettings-phase-control">
            <label for="phaseSelect" data-i18n="runsetting.phase">Aktuelle Phase:</label>
            <select id="phaseSelect" name="webCurrentPhase">
              <option value="1" %PHASE1_SEL% data-i18n="runsetting.phase.grow">Wuchs</option>
              <option value="2" %PHASE2_SEL% data-i18n="runsetting.phase.flower">Blüte</option>
              <option value="3" %PHASE3_SEL% data-i18n="runsetting.phase.dry">Trocknung</option>
            </select>
          </div>
          <button type="button" class="primary runsettings-energy-reset" id="saverunsettingsBtn" data-i18n="runsetting.resetkWh" onclick="resetShellyEnergy(this)">Reset Shelly kWh</button>
        </div>
      </div>

      <div class="runsettings-top-grid">
        <div class="runsettings-panel">
          <h2 class="runsettings-panel-title" data-i18n="runsetting.section.growPhase">Grow-Zeiträume</h2>
          <div class="runsettings-fields runsettings-grow-fields">
            <div class="runsettings-field">
              <label for="webGrowStart" data-i18n="runsetting.startGrow">Start Grow Date:</label>
              <input id="webGrowStart" name="webGrowStart" type="date" value="%GROWSTARTDATE%">
            </div>
            <div class="runsettings-field">
              <label for="webFloweringStart" data-i18n="runsetting.startFlower">Start Flowering Date:</label>
              <input id="webFloweringStart" name="webFloweringStart" type="date" value="%GROWFLOWERDATE%">
            </div>
            <div class="runsettings-field runsettings-field-span-2">
              <label for="webDryingStart" data-i18n="runsetting.startDry">Start Drying Date:</label>
              <input id="webDryingStart" name="webDryingStart" type="date" value="%GROWDRAYINGDATE%">
            </div>
          </div>
        </div>

        <div class="runsettings-panel">
          <h2 class="runsettings-panel-title" data-i18n="runsetting.section.temperature">Temperatur</h2>
          <div class="runsettings-fields">
            <div class="runsettings-field runsettings-field-unit">
              <label for="webTargetTemp" data-i18n="runsetting.targetTemp">Soll-Temperatur:</label>
              <div class="runsettings-control-unit">
                <input name="webTargetTemp" id="webTargetTemp" type="number" step="0.1" min="18" max="30" value="%TARGETTEMPERATURE%" data-celsius-value="%TARGETTEMPERATURE%">
                <span class="unit" data-temp-unit-label>°C</span>
              </div>
            </div>
            <div class="runsettings-field runsettings-field-unit">
              <label for="webOffsetLeafTemp" data-i18n="runsetting.offsetLeafTemperature">Offset Blatttemperatur:</label>
              <div class="runsettings-control-unit">
                <input name="webOffsetLeafTemp" id="webOffsetLeafTemp" type="number" step="0.1" min="-3.0" max="0.0" value="%LEAFTEMPERATURE%" data-celsius-value="%LEAFTEMPERATURE%">
                <span class="unit" data-temp-unit-label>°C</span>
              </div>
            </div>
          </div>
        </div>

        <div class="runsettings-panel" id="vpdSettingsSection">
          <h2 class="runsettings-panel-title" data-i18n="runsetting.vpdSettings">VPD-Einstellungen</h2>
          <div class="runsettings-vpd-enable">
            <label class="inline-checkbox">
              <input type="checkbox" name="webMinVPDMonitoring" id="webMinVPDMonitoring" %MINVPDMONITORING%>
              <span data-i18n="runsetting.lowVPD.enabled">aktivieren</span>
            </label>
          </div>
          <div class="runsettings-compact-grid runsettings-vpd-grid">
            <div class="runsettings-field runsettings-field-unit">
              <label for="webTargetVPD" data-i18n="runsetting.targetVPD">Soll-VPD:</label>
              <div class="runsettings-control-unit">
                <input name="webTargetVPD" id="webTargetVPD" type="number" step="0.01" min="0.50" max="3.50" value="%TARGETVPD%">
                <span>kPa</span>
              </div>
            </div>
            <div class="runsettings-field runsettings-field-unit">
              <label for="webMinVPD" data-i18n="runsetting.minVPD">Offset Min-VPD:</label>
              <div class="runsettings-control-unit">
                <input name="webMinVPD" id="webMinVPD" type="number" step="0.01" min="0.00" max="0.30" value="%MINVPD%">
                <span>kPa</span>
              </div>
            </div>
            <div class="runsettings-field runsettings-field-unit">
              <label for="webHysteresis" data-i18n="runsetting.hysteresis">Hysterese:</label>
              <div class="runsettings-control-unit">
                <input name="webHysteresis" id="webHysteresis" type="number" step="0.01" min="0.00" max="0.1" value="%VPDHYSTERESIS%">
                <span>kPa</span>
              </div>
            </div>
          </div>
        </div>

      </div>

        <div class="runsettings-panel relay-extra runsettings-irrigation-settings-card" id="irrigationSettingsSection" style="display:none;">
          <h2 class="runsettings-panel-title" data-i18n="runsetting.wateringSettings">Bewässerungseinstellung</h2>
          <p class="hint runsettings-panel-hint" data-i18n="runsetting.pumpsHint">Aktivieren Sie nur die Pumpen für tatsächlich verwendete Töpfe.</p>
          <div class="runsettings-irrigation-content">
          <fieldset id="localPlanEditor" disabled>
            <legend data-i18n="plan.title">Bewässerungsplan</legend>
            <label class="inline-checkbox"><input type="checkbox" id="localPlanEnabled"><span data-i18n="plan.enabled">Bewässerungsplan aktivieren</span></label>
            <div class="runsettings-field">
              <label for="localPlanDays" data-i18n="plan.days">Alle X Tage bewässern</label>
              <input id="localPlanDays" type="number" min="1" max="366" step="1" value="1">
            </div>
            <div class="runsettings-field">
              <label for="localPlanDelay" data-i18n="plan.delay">Offset nach Licht-an (Minuten)</label>
              <input id="localPlanDelay" type="number" min="0" max="1440" step="1" value="0">
            </div>
            <div class="runsettings-field">
              <label for="localPlanLast" data-i18n="plan.last">Letzte Bewässerung</label>
              <input id="localPlanLast" type="date">
            </div>
            <p class="local-plan-next" style="grid-column:1 / -1"><strong data-i18n="plan.next">Nächste Bewässerung</strong>: <output id="localPlanNext">—</output></p>
            <p class="hint" data-i18n="plan.hint">Trocknung sperrt die Bewässerung automatisch.</p>
            <button type="button" id="localPlanSave" data-i18n="plan.save">Plan speichern</button>
            <p id="localPlanSaveStatus" role="status" aria-live="polite"></p>
          </fieldset>
          <div class="runsettings-irrigation-checks runsettings-irrigation-checks-stacked">
            <div class="runsettings-pumps-box">
              <label class="inline-checkbox">
                <input type="checkbox" name="webPump1Enabled" id="webPump1Enabled" %PUMP1_ENABLED_CHECKED%>
                <span data-i18n="runsetting.pump1Enabled">Pumpe 1 aktiv</span>
              </label>
              <label class="inline-checkbox">
                <input type="checkbox" name="webPump2Enabled" id="webPump2Enabled" %PUMP2_ENABLED_CHECKED%>
                <span data-i18n="runsetting.pump2Enabled">Pumpe 2 aktiv</span>
              </label>
              <label class="inline-checkbox">
                <input type="checkbox" name="webPump3Enabled" id="webPump3Enabled" %PUMP3_ENABLED_CHECKED%>
                <span data-i18n="runsetting.pump3Enabled">Pumpe 3 aktiv</span>
              </label>
            </div>
            <div class="runsettings-flood-box">
              <label class="inline-checkbox">
                <input type="checkbox" name="webIrrigationStopOnFlood" id="webIrrigationStopOnFlood" %IRRIGATION_STOP_ON_FLOOD_CHECKED%>
                <span data-i18n="runsetting.stopOnFloodAlarm">Aktive Bewässerung bei Flood-Alarm abbrechen</span>
              </label>
              <small data-i18n="runsetting.stopOnFloodAlarmHint">Wirkt nur bei aktiviertem Flood S Gen4 und einer laufenden automatischen Bewässerung.</small>
            </div>
          </div>
          </div>
        </div>

      <div class="runsettings-operations-row">
        <div class="runsettings-panel runsettings-irrigation-params relay-extra" id="irrigationParameterSection" style="display:none;">
          <h2 class="runsettings-panel-title" data-i18n="runsetting.irrigationParameters">Bewässerungsparameter</h2>
          <div class="runsettings-param-grid runsettings-param-grid-inline">
            <div class="runsettings-field runsettings-field-unit">
              <label for="webTimePerTask" data-i18n="runsetting.timePerTask">Bewässerungszeit pro Task:</label>
              <div class="runsettings-control-unit"><input name="webTimePerTask" id="webTimePerTask" type="number" step="1" min="1" max="10" value="%TIMEPERTASK%"><span>s</span></div>
              <small>(min 1s, max 10s, step 1s)</small>
            </div>
            <div class="runsettings-field runsettings-field-unit">
              <label for="webBetweenTasks" data-i18n="runsetting.betweenTasks">Pause zwischen Bewässerungen:</label>
              <div class="runsettings-control-unit"><input name="webBetweenTasks" id="webBetweenTasks" type="number" step="1" min="1" max="10" value="%BETWEENTASKS%"><span>Min</span></div>
              <small>(min 1Min, max 10Min, step 1Min)</small>
            </div>
            <div class="runsettings-field runsettings-field-unit">
              <label for="webAmountOfWater" data-i18n="runsetting.amountOfWater">Wassermenge nach 10 Sekunden:</label>
              <div class="runsettings-control-unit"><input name="webAmountOfWater" id="webAmountOfWater" type="number" step="5" min="10" max="500" value="%AMOUNTOFWATER%"><span>ml</span></div>
              <small>(min 10ml, max 500ml, step 5ml)</small>
            </div>
            <div class="runsettings-field runsettings-field-unit">
              <label for="webIrrigation" data-i18n="runsetting.irrigation">Gesamte Bewässerungsmenge:</label>
              <div class="runsettings-control-unit"><input name="webIrrigation" id="webIrrigation" type="number" step="10" min="100" max="3000" value="%IRRIGATION%"><span>ml</span></div>
              <small>(min 100ml, max 3000ml, step 10ml)</small>
            </div>
            <div class="runsettings-field runsettings-field-unit">
              <label for="webMaxTank">0% Tank:</label>
              <div class="runsettings-control-unit"><input name="webMaxTank" id="webMaxTank" type="number" value="%MAXTANK%"><span>cm</span></div>
            </div>
            <div class="runsettings-field runsettings-field-unit">
              <label for="webMinTank">100% Tank:</label>
              <div class="runsettings-control-unit"><input name="webMinTank" id="webMinTank" type="number" value="%MINTANK%"><span>cm</span></div>
            </div>
          </div>
        </div>

        <div class="runsettings-panel runsettings-heating-panel">
          <h2 class="runsettings-panel-title" data-i18n="runsetting.relayHeating">ESP32 Relay for heating</h2>
          <p class="relay-heating-hint" data-i18n="runsetting.relay.heatingHint">Wird ein Relay für die Heizung verwendet, kann es hier konfigutiert werden.</p>
          <div class="runsettings-heating-grid runsettings-heating-grid-stacked">
            <div class="runsettings-field">
              <label for="heatingSource" data-i18n="runsetting.relay.heatingSource">Heizungsquelle</label>
              <select name="webHeatingSource" id="heatingSource">
                <option value="0" %HEATSRC0_SEL%>Disabled</option>
                <option value="1" %HEATSRC1_SEL%>ESP Relay</option>
                <option value="2" %HEATSRC2_SEL%>Shelly Plug</option>
              </select>
            </div>
            <div class="runsettings-field" id="heatingRelayRow">
              <label for="heatingRelay" data-i18n="runsetting.relay.heatingRelay">Heizungs-Relay</label>
              <select name="webHeatingRelay" id="heatingRelay">
                <option value="" %HEATRELAY0_SEL% data-i18n="runsetting.relay.none">Keines</option>
                <option value="1" %HEATRELAY1_SEL%>%RELAYNAMES1%</option>
                <option value="2" %HEATRELAY2_SEL%>%RELAYNAMES2%</option>
                <option value="3" %HEATRELAY3_SEL%>%RELAYNAMES3%</option>
                <option value="4" %HEATRELAY4_SEL%>%RELAYNAMES4%</option>
              </select>
            </div>
          </div>
        </div>
      </div>


      <section class="runsettings-panel" id="humidifierFanPanel" style="margin-bottom:14px">
        <h2 class="runsettings-panel-title" data-i18n="fan.title">Fan am Luftbefeuchter</h2>
        <p data-i18n="fan.hint">Das ausgewählte Relais folgt den ESP-Schaltbefehlen für den Shelly-Luftbefeuchter. Seine Minutensteuerung pausiert während der Kopplung.</p>
        <div style="display:flex;flex-wrap:wrap;gap:12px;align-items:center">
          <label for="humidifierFanRelay" data-i18n="fan.relay">Fan-Relais</label>
          <select id="humidifierFanRelay" style="width:170px" disabled><option value="0">—</option></select>
          <button type="button" id="humidifierFanSave" data-i18n="fan.save" disabled>Kopplung speichern</button>
          <output id="humidifierFanState" role="status" aria-live="polite">—</output>
        </div>
        <p data-i18n="fan.limit">Auto-Off wird beim Start und stündlich gelesen. Ohne gültigen aktivierten Timer bleibt der Fan aus. Direkte Änderungen am Shelly werden nicht sofort übernommen.</p>
        <p id="humidifierFanMessage" role="status" aria-live="polite"></p>
      </section>

      <div class="runsettings-panel runsettings-relay-panel relay-sched">
        <h2 class="runsettings-panel-title" data-i18n="runsetting.relayScheduling">ESP32 Relay Scheduling</h2>
        <p class="relay-sched-hint" data-i18n="runsetting.relay.minutesHint">Minutenformat: <b>0–59</b> (Minute innerhalb der Stunde).</p>

        <div class="runsettings-relay-table-wrap">
          <div class="runsettings-relay-table relay-sched-list">
            <div class="runsettings-relay-head">
              <div>#</div>
              <div data-i18n="runsetting.relay.name">Bezeichnung</div>
              <div data-i18n="runsetting.relay.enabledShort">Aktiv</div>
              <div data-i18n="runsetting.relay.ifLightOff">auch wenn Licht aus</div>
              <div data-i18n="runsetting.relay.onMinute">Einschaltminute</div>
              <div data-i18n="runsetting.relay.offMinute">Ausschaltminute</div>
            </div>

            <div class="relay-sched-row runsettings-relay-row" id="espRelay1Row">
              <div class="runsettings-relay-number">1</div>
              <div class="relay-sched-name"><div class="relay-sched-name-label" data-i18n="runsettings.espSchedRelay1">Relay</div><div class="relay-sched-name-value">%RELAYNAMES1%</div></div>
              <div class="sched-field chk"><label class="inline-checkbox"><input id="espRelay1Enabled" name="webEspRel1Enbl" type="checkbox" %ESPRELAY1_ENABLED_CHECKED%><span data-i18n="runsetting.relay.enabledShort">Enabled</span></label></div>
              <div class="sched-field chk"><label class="inline-checkbox"><input id="espRelay1IfLightOff" name="webEspRel1ILOff" type="checkbox" %ESPRELAY1_IFLIGHTOFF_CHECKED%><span data-i18n="runsetting.relay.ifLightOff">wenn Licht aus</span></label></div>
              <div class="sched-field minute"><label for="espRelay1OnMin" data-i18n="runsetting.relay.onMinute">Einschaltminute</label><input id="espRelay1OnMin" name="webEspRel1On" type="number" min="0" max="59" step="1" value="%ESPRELAY1_ONMIN%"></div>
              <div class="sched-field minute"><label for="espRelay1OffMin" data-i18n="runsetting.relay.offMinute">Ausschaltminute</label><input id="espRelay1OffMin" name="webEspRel1Off" type="number" min="0" max="59" step="1" value="%ESPRELAY1_OFFMIN%"></div>
            </div>

            <div class="relay-sched-row runsettings-relay-row" id="espRelay2Row">
              <div class="runsettings-relay-number">2</div>
              <div class="relay-sched-name"><div class="relay-sched-name-label" data-i18n="runsettings.espSchedRelay2">Relay</div><div class="relay-sched-name-value">%RELAYNAMES2%</div></div>
              <div class="sched-field chk"><label class="inline-checkbox"><input id="espRelay2Enabled" name="webEspRel2Enbl" type="checkbox" %ESPRELAY2_ENABLED_CHECKED%><span data-i18n="runsetting.relay.enabledShort">Enabled</span></label></div>
              <div class="sched-field chk"><label class="inline-checkbox"><input id="espRelay2IfLightOff" name="webEspRel2ILOff" type="checkbox" %ESPRELAY2_IFLIGHTOFF_CHECKED%><span data-i18n="runsetting.relay.ifLightOff">wenn Licht aus</span></label></div>
              <div class="sched-field minute"><label for="espRelay2OnMin" data-i18n="runsetting.relay.onMinute">Einschaltminute</label><input id="espRelay2OnMin" name="webEspRel2On" type="number" min="0" max="59" step="1" value="%ESPRELAY2_ONMIN%"></div>
              <div class="sched-field minute"><label for="espRelay2OffMin" data-i18n="runsetting.relay.offMinute">Ausschaltminute</label><input id="espRelay2OffMin" name="webEspRel2Off" type="number" min="0" max="59" step="1" value="%ESPRELAY2_OFFMIN%"></div>
            </div>

            <div class="relay-sched-row runsettings-relay-row" id="espRelay3Row">
              <div class="runsettings-relay-number">3</div>
              <div class="relay-sched-name"><div class="relay-sched-name-label" data-i18n="runsettings.espSchedRelay3">Relay</div><div class="relay-sched-name-value">%RELAYNAMES3%</div></div>
              <div class="sched-field chk"><label class="inline-checkbox"><input id="espRelay3Enabled" name="webEspRel3Enbl" type="checkbox" %ESPRELAY3_ENABLED_CHECKED%><span data-i18n="runsetting.relay.enabledShort">Enabled</span></label></div>
              <div class="sched-field chk"><label class="inline-checkbox"><input id="espRelay3IfLightOff" name="webEspRel3ILOff" type="checkbox" %ESPRELAY3_IFLIGHTOFF_CHECKED%><span data-i18n="runsetting.relay.ifLightOff">wenn Licht aus</span></label></div>
              <div class="sched-field minute"><label for="espRelay3OnMin" data-i18n="runsetting.relay.onMinute">Einschaltminute</label><input id="espRelay3OnMin" name="webEspRel3On" type="number" min="0" max="59" step="1" value="%ESPRELAY3_ONMIN%"></div>
              <div class="sched-field minute"><label for="espRelay3OffMin" data-i18n="runsetting.relay.offMinute">Ausschaltminute</label><input id="espRelay3OffMin" name="webEspRel3Off" type="number" min="0" max="59" step="1" value="%ESPRELAY3_OFFMIN%"></div>
            </div>

            <div class="relay-sched-row runsettings-relay-row" id="espRelay4Row">
              <div class="runsettings-relay-number">4</div>
              <div class="relay-sched-name"><div class="relay-sched-name-label" data-i18n="runsettings.espSchedRelay4">Relay</div><div class="relay-sched-name-value">%RELAYNAMES4%</div></div>
              <div class="sched-field chk"><label class="inline-checkbox"><input id="espRelay4Enabled" name="webEspRel4Enbl" type="checkbox" %ESPRELAY4_ENABLED_CHECKED%><span data-i18n="runsetting.relay.enabledShort">Enabled</span></label></div>
              <div class="sched-field chk"><label class="inline-checkbox"><input id="espRelay4IfLightOff" name="webEspRel4ILOff" type="checkbox" %ESPRELAY4_IFLIGHTOFF_CHECKED%><span data-i18n="runsetting.relay.ifLightOff">wenn Licht aus</span></label></div>
              <div class="sched-field minute"><label for="espRelay4OnMin" data-i18n="runsetting.relay.onMinute">Einschaltminute</label><input id="espRelay4OnMin" name="webEspRel4On" type="number" min="0" max="59" step="1" value="%ESPRELAY4_ONMIN%"></div>
              <div class="sched-field minute"><label for="espRelay4OffMin" data-i18n="runsetting.relay.offMinute">Ausschaltminute</label><input id="espRelay4OffMin" name="webEspRel4Off" type="number" min="0" max="59" step="1" value="%ESPRELAY4_OFFMIN%"></div>
            </div>

            <div class="relay-sched-row runsettings-relay-row relay-extra" id="espRelay5Row" style="display:none;">
              <div class="runsettings-relay-number">5</div>
              <div class="relay-sched-name"><div class="relay-sched-name-label" data-i18n="runsettings.espSchedRelay5">Relay</div><div class="relay-sched-name-value">%RELAYNAMES5%</div></div>
              <div class="sched-field chk"><label class="inline-checkbox"><input id="espRelay5Enabled" name="webEspRel5Enbl" type="checkbox" %ESPRELAY5_ENABLED_CHECKED%><span data-i18n="runsetting.relay.enabledShort">Enabled</span></label></div>
              <div class="sched-field chk"><label class="inline-checkbox"><input id="espRelay5IfLightOff" name="webEspRel5ILOff" type="checkbox" %ESPRELAY5_IFLIGHTOFF_CHECKED%><span data-i18n="runsetting.relay.ifLightOff">wenn Licht aus</span></label></div>
              <div class="sched-field minute"><label for="espRelay5OnMin" data-i18n="runsetting.relay.onMinute">Einschaltminute</label><input id="espRelay5OnMin" name="webEspRel5On" type="number" min="0" max="59" step="1" value="%ESPRELAY5_ONMIN%"></div>
              <div class="sched-field minute"><label for="espRelay5OffMin" data-i18n="runsetting.relay.offMinute">Ausschaltminute</label><input id="espRelay5OffMin" name="webEspRel5Off" type="number" min="0" max="59" step="1" value="%ESPRELAY5_OFFMIN%"></div>
            </div>
          </div>
        </div>
      </div>

      <div class="runsettings-actions">
        <button type="button" class="primary" data-i18n="settings.save" onclick="saveRunsettingsAll()">Speichern</button>
      </div>
    </section>

    <!-- runsettings section -->
    <form action="/savemessagesettings" method="POST" id="messageSettingsForm">
    <section id="message" class="page card">
      <h1 data-i18n="message.title">Nachrichteneinstellungen</h1>

      <h2 data-i18n="message.pushoverSettings">Pushover Einstellungen</h2>

      <div class="form-group checkbox">
        <label class="inline-checkbox">
         <input type="checkbox" name="webPushoverEnabled" id="webPushoverEnabled" %PUSHOVERENABLED%>
         <span data-i18n="message.enabled">aktivieren</span>
        </label>
      </div>

      <div class="form-group">
        <label for="webPushoverUserKey" data-i18n="message.pushoverUserKey">Pushover Benutzer:</label>
        <input name="webPushoverUserKey" id="webPushoverUserKey" type="password" autocomplete="new-password" data-i18n="message.pushoverUserKey.ph" data-i18n-attr="placeholder" style="width: 320px;" value="%PUSHOVERUSERKEY%">
      </div>

      <div class="form-group">
        <label for="webPushoverAppKey" data-i18n="message.pushoverAppKey">Pushover Token:</label>
        <input name="webPushoverAppKey" id="webPushoverAppKey" type="password" autocomplete="new-password" data-i18n="message.pushoverAppKey.ph" data-i18n-attr="placeholder" style="width: 320px;" value="%PUSHOVERAPPKEY%">
      </div>

      <div class="form-group">
        <label for="webPushoverDevice" data-i18n="message.pushoverDevice">Pushover Gerät:</label>
        <input name="webPushoverDevice" id="webPushoverDevice" type="text" data-i18n="message.pushoverDevice.ph" data-i18n-attr="placeholder" style="width: 320px;" value="%PUSHOVERDEVICE%">
      </div>

      <h2 data-i18n="message.gotifySettings">Gotify Einstellungen</h2>

      <div class="form-group checkbox">
        <label class="inline-checkbox">
         <input type="checkbox" name="webGotifyEnabled" id="webGotifyEnabled" %GOTIFYENABLED%>
         <span data-i18n="message.enabled">aktivieren</span>
        </label>
      </div>

      <div class="form-group">
        <label for="webGotifyURL" data-i18n="message.gotifyURL">Gotify URL:</label>
        <input name="webGotifyURL" id="webGotifyURL" type="text" data-i18n="message.gotifyUrl.ph" data-i18n-attr="placeholder" style="width: 320px;" value="%GOTIFYURL%">
      </div>

      <div class="form-group">
        <label for="webGotifyToken" data-i18n="message.gotifyToken">Gotify Token:</label>
        <input name="webGotifyToken" id="webGotifyToken" type="password" autocomplete="new-password" data-i18n="message.gotifyToken.ph" data-i18n-attr="placeholder" style="width: 320px;" value="%GOTIFYTOKEN%">
      </div>

      <button class="primary" id="saveMessageBtn" data-i18n="settings.save">Speichern</button>
    </section>
    </form>

    <!-- setting section -->
    <form id="systemSettingsForm" onsubmit="event.preventDefault(); window.saveSystemSettingsAll(event); return false;">
      <section id="settings" class="page card">
        <h1 data-i18n="settings.title">Systemeinstellungen</h1>

        <div class="form-group checkbox">
          <label class="inline-checkbox">
            <input type="checkbox" name="webDebugEnable" id="webDebugEnable" %DBG_CHECKED%>
            <span>Debug mode</span>
          </label>
        </div>

        <div class="form-group">
          <label for="webRelayCount" data-i18n="settings.relayBoard">welches Relay-Board?</label>
          <select name="webRelayCount" id="webRelayCount" class="control-sm shelly-other">
            <option value="4" %RELAYCOUNT4_SEL%>4x</option>
            <option value="8" %RELAYCOUNT8_SEL%>8x</option>
          </select>
        </div>

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

        <h2 data-i18n="settings.backendWrite.title">Backend Write API</h2>
        <p class="hint" data-i18n="settings.backendWrite.hint">Erlaubt dem GrowTent-Backend ausschließlich Soll-Temperatur, Soll-VPD, Blüte-/Trocknungsdatum und aktuelle Phase über eine HMAC-gesicherte Schnittstelle zu ändern.</p>

        <div class="form-group checkbox">
          <label class="inline-checkbox">
            <input type="checkbox" name="webBackendWriteEnabled" id="webBackendWriteEnabled" %BACKEND_WRITE_ENABLED_CHECKED%>
            <span data-i18n="settings.backendWrite.enabled">Backend Write API aktivieren</span>
          </label>
        </div>

        <div class="form-group">
          <label for="webBackendWriteAllowedIp" data-i18n="settings.backendWrite.allowedIp">Erlaubte Backend-IP:</label>
          <input name="webBackendWriteAllowedIp" id="webBackendWriteAllowedIp" type="text" inputmode="decimal" maxlength="15" style="width: 180px;" value="%BACKEND_WRITE_ALLOWED_IP%" placeholder="192.168.178.10">
        </div>

        <div class="form-group">
          <label for="webBackendWriteSecret" data-i18n="settings.backendWrite.secret">HMAC API-Schlüssel:</label>
          <input name="webBackendWriteSecret" id="webBackendWriteSecret" type="password" autocomplete="new-password" maxlength="64" style="width: 520px; max-width: 100%;" value="" placeholder="%BACKEND_WRITE_SECRET_PLACEHOLDER%">
          <div style="margin-top:8px; display:flex; gap:8px; flex-wrap:wrap;">
            <button type="button" class="btn" id="generateBackendWriteSecretBtn" data-i18n="settings.backendWrite.generate">Neuen 256-Bit-Schlüssel erzeugen</button>
            <button type="button" class="btn" id="copyBackendWriteSecretBtn" data-i18n="settings.backendWrite.copy">Schlüssel kopieren</button>
          </div>
          <small data-i18n="settings.backendWrite.secretHint">Der Schlüssel wird nach dem Speichern nicht wieder angezeigt. Leeres Feld bedeutet: vorhandenen Schlüssel behalten.</small>
        </div>

        <h2 data-i18n="settings.powerPrice">Preis pro kWh</h2> 
        <div class="form-group">
          <label for="webPowerPriceKwh">Power price (€/kWh):</label>
          <input name="webPowerPriceKwh" id="webPowerPriceKwh" type="number" step="0.01" min="0" style="width: 120px;" value="%POWERPRICEKWH%">
        </div>

        <h2 data-i18n="settings.DS18B20">DS18B20 Sensor</h2>        
        <div class="form-group checkbox">
          <label class="inline-checkbox">
           <input type="checkbox" name="webDS18B20Enable" id="webDS18B20Enable" %DS18B20ENABLE%>
           <span data-i18n="settings.enabled">aktivieren</span>
          </label>
        </div>

        <div class="form-group">
          <input name="webDS18B20Name" id="webDS18B20Name" type="text" data-i18n="settings.DS18B20Name.ph" data-i18n-attr="placeholder" style="width: 250px;" maxlength="400" value="%DS18B20NAME%">
        </div>

        <h2 data-i18n="settings.relaySettings">Relais Einstellungen</h2>
        <div class="form-group">
          <label for="webRelay1" data-i18n="settings.relay1">Relay 1:</label>
          <input name="webRelayName1" id="webRelayName1" type="text" data-i18n="settings.relay1.ph" data-i18n-attr="placeholder" style="width: 120px;" maxlength="15" value="%RELAYNAMES1%">
        </div>

        <div class="form-group">
          <label for="webRelay2" data-i18n="settings.relay2">Relay 2:</label>
          <input name="webRelayName2" id="webRelayName2" type="text" data-i18n="settings.relay2.ph" data-i18n-attr="placeholder" style="width: 120px;" maxlength="15" value="%RELAYNAMES2%">
        </div>

        <div class="form-group">
          <label for="webRelay3" data-i18n="settings.relay3">Relay 3:</label>
          <input name="webRelayName3" id="webRelayName3" type="text" data-i18n="settings.relay3.ph" data-i18n-attr="placeholder" style="width: 120px;" maxlength="15" value="%RELAYNAMES3%">
        </div>

        <div class="form-group">
          <label for="webRelay4" data-i18n="settings.relay4">Relay 4:</label>
          <input name="webRelayName4" id="webRelayName4" type="text" data-i18n="settings.relay4.ph" data-i18n-attr="placeholder" style="width: 120px;" maxlength="15" value="%RELAYNAMES4%">
        </div>

        <div class="form-group relay-extra" id="settingsRelay5Row" style="display:none;">
          <label for="webRelayName5" data-i18n="settings.relay5">Relay 5:</label>
          <input name="webRelayName5" id="webRelayName5" type="text" data-i18n="settings.relay5.ph" data-i18n-attr="placeholder" style="width: 120px;" maxlength="15" value="%RELAYNAMES5%">
        </div>

        <button class="primary" id="saveSettingsBtn" type="button" data-i18n="settings.save" onclick="window.saveSystemSettingsAll(event)">Speichern</button>
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

    <!-- variables/state section -->
    <section id="vars" class="page card">
      <h1 data-i18n="vars.title" data-i18n="vars.variables">Variablen</h1>
      <p class="hint" data-i18n="vars.hint" data-i18n="vars.debugHint">Debug-Ansicht: alle registrierten Werte (automatisch aus /api/state). Tokens/Passwörter werden maskiert.</p>

      <div class="vars-toolbar">
        <input id="varsSearch" class="input" type="search" placeholder="Search…" aria-label="Search variables">
        <button class="btn" id="varsRefreshBtn" type="button">Refresh</button>
      </div>

      <div id="varsMeta" class="vars-meta">--</div>

      <div class="table-wrap">
        <table class="vars-table" id="varsTable" aria-label="Variables table">
          <thead>
            <tr><th>Key</th><th>Value</th></tr>
          </thead>
          <tbody id="varsTbody">
            <tr><td colspan="2">—</td></tr>
          </tbody>
        </table>
      </div>
    </section>


    <!-- OTA update section -->
    <section id="ota" class="page card">
      <h1 data-i18n="ota.title">OTA Update</h1>
      <p class="hint">
        <span data-i18n="ota.source">Firmware-Quelle:</span>
        <code>https://github.com/syschelle/GrowTent/releases/latest/download/firmware.bin</code>
      </p>

      <div id="otaStatus" class="muted" data-i18n="ota.notChecked">Noch nicht geprüft.</div>
      <div id="otaVersion"></div>
      <pre id="otaChangelog" style="white-space:pre-wrap;"></pre>

      <div id="otaProgressPanel" class="ota-progress hidden" aria-live="polite">
        <div class="ota-progress-head">
          <span id="otaProgressLabel">OTA Update</span>
          <strong id="otaProgressPercent">0%</strong>
        </div>
        <progress id="otaProgressBar" max="100" value="0">0%</progress>
        <div id="otaProgressDetail" class="muted" data-i18n="ota.ready">Bereit.</div>
      </div>

      <div class="spacer"></div>
        <button type="button" class="primary" id="otaCheckBtn" data-i18n="ota.checkButton">Auf Update prüfen</button>
      <div class="spacer"></div>
        <button type="button" class="primary" id="otaInstallBtn" data-i18n="ota.installButton" disabled>Update installieren</button>
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
  
 <script src="/script.js?v=%FWVERSION%"></script>
</body>
</html>
)rawliteral";
