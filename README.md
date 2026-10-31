# GrowTent

> ⚡ Smart ESP32 grow-tent controller with climate monitoring, automation, irrigation, Shelly integration and OTA updates.

GrowTent is an ESP32 NodeMCU-32S based controller for climate monitoring and automation in a grow tent. It reads temperature and humidity from a BME280, optionally reads a DS18B20 water-temperature sensor, calculates VPD, controls relays and irrigation, and integrates with Shelly devices.

The firmware provides a German/English web interface and a JSON API. Starting with **v0.4.0**, the application web server is fully based on **ESPAsyncWebServer**. The current release changes are documented in [`RELEASE_NOTES_v0.4.20.md`](RELEASE_NOTES_v0.4.20.md).

For centralized history, multi-tent management and higher-level coordination, GrowTent can be paired with the [GrowTent Backend](https://github.com/syschelle/growtent-backend).

---

## 🚀 Features

- **German/English ESP32 web interface** for live status and configuration
- **Adaptive BME280 smoothing**
  - temperature and humidity are smoothed for stable control values
  - fast environmental changes are still detected quickly
- **VPD calculation and target-based automation**
- **Optional DS18B20 water-temperature sensor**
- **4- or 8-relay configuration**
- **Minute-based relay schedules**
- **Conditional relay operation based on light state**
- **Heating control**
  - ESP relay or Shelly heater
  - predictive temperature trend handling
  - configurable hysteresis and minimum on/off times
- **Shelly integration**
  - main power
  - grow light
  - humidifier
  - heater
  - fan
  - exhaust
  - heating-mat monitoring
  - automatic Shelly generation detection
- **Irrigation support on 8-relay systems**
  - pumps on relays 6–8
  - individual pump enable/disable
  - per-pump calibration
  - sequential watering without pump overlap
  - manual 10-second pump test
- **HC-SR04 tank-level measurement**
- **Shelly Flood S Gen4 integration**
  - event-driven webhook handling
  - watering start interlock
  - optional active-watering abort
- **Grow Diary stored in LittleFS**
- **Built-in system log**
- **GitHub OTA update check and installation**
- **Release-specific static-asset cache busting**
- **Fixed-length Async response transport for important API and asset responses**
- **Designed for stable long-term unattended operation**

---

## 🌡️ Climate measurement and VPD

### BME280

The BME280 supplies:

- temperature in **°C**
- relative humidity in %
- air pressure

Temperature values remain Celsius-native inside the firmware.

The dashboard can display °C or °F, but the conversion is performed only in the browser presentation/input layer.

Absolute temperature conversion:

```text
°F = °C × 1.8 + 32
°C = (°F - 32) / 1.8
```

Temperature differences, such as the leaf-temperature offset:

```text
Δ°F = Δ°C × 1.8
Δ°C = Δ°F / 1.8
```

The following remain Celsius-based regardless of the dashboard display setting:

- BME280 temperature
- DS18B20 / water temperature
- stored target temperature
- stored leaf-temperature offset
- VPD and heater calculations
- `/api/state`
- `/api/stats`
- external backend/API integrations

### Adaptive climate smoothing

The sensor task uses adaptive smoothing to balance stability and responsiveness.

Small sensor changes are smoothed strongly, while larger environmental changes are allowed to influence the control value more quickly. This reduces unnecessary control oscillation without making the controller unresponsive.

### VPD

VPD is calculated from Celsius temperature and relative humidity:

```text
svp = 0.6108 × exp((17.27 × T) / (T + 237.3))
VPD = svp - (RH / 100 × svp)
```

VPD remains independent of the selected dashboard temperature unit.

---

## 🖥️ Web interface

Normal application access:

```text
http://<esp-ip>:8080/
```

The frontend is embedded in firmware:

- `/` — dashboard
- `/style.css` — stylesheet
- `/script.js` — JavaScript

The dashboard provides:

- Status
- Running Settings / Betriebseinstellungen
- System Settings / Systemeinstellungen
- System Log
- Factory Reset
- Messages
- OTA Update
- Grow Diary

The installed firmware version is shown in the dashboard header.

### Status dashboard

<img width="1658" height="1167" alt="GrowTent status dashboard" src="https://github.com/user-attachments/assets/3eaaaf75-0dce-4206-9e91-79ac6b099125" />

The Status page shows the current climate values, VPD, relay state, irrigation state and configured Shelly devices. Temperature values follow the selected °C/°F presentation setting while API values remain Celsius-native.

---

## 🌐 Current web-server architecture

### Normal operation

- Permanent dashboard and API server: **ESPAsyncWebServer**
- Application port: **TCP/8080**
- No normal application listener on TCP/80
- Primary state endpoint: `GET /api/state`
- Compatibility endpoint: `GET /api/stats`
- Health endpoint: `GET /api/async/health`

Async HTTP callbacks do not directly execute long-running or safety-critical hardware, storage, network or restart operations. Such work is handed off to the Arduino loop or the responsible worker/task.

### Wi-Fi provisioning

TCP/80 is reserved exclusively for the asynchronous captive portal while Wi-Fi provisioning is active.

- Captive portal: **TCP/80**
- Captive DNS is active only during provisioning
- Candidate Wi-Fi credentials are tested before replacing stored credentials
- A fallback AP is stopped again after the stored STA connection recovers
- Normal operation returns to STA-only mode with TCP/80 closed

---

## 🔌 Hardware

### Controller

- ESP32 NodeMCU-32S
- PlatformIO target: `nodemcu-32s`
- The ESP32 is part of the existing relay-board hardware platform

### Sensors

- BME280, I2C address `0x76`
- DS18B20, GPIO `4`
- HC-SR04 tank sensor
  - TRIG: GPIO `16`
  - ECHO: GPIO `17`

### Relay mapping

| Relay | GPIO | Typical use |
| ---: | ---: | --- |
| 1 | 32 | configurable relay |
| 2 | 33 | configurable relay |
| 3 | 25 | configurable relay |
| 4 | 26 | configurable relay |
| 5 | 27 | configurable relay |
| 6 | 14 | irrigation pump 1 |
| 7 | 12 | irrigation pump 2 |
| 8 | 13 | irrigation pump 3 |

For a 4-relay configuration only relays 1–4 are active.

---

## 💧 Irrigation

Irrigation is available on 8-relay configurations.

- Pumps use relays 6–8
- Pumps can be enabled/disabled individually
- Each pump has its own calibration
- Watering runs sequentially
- Pump outputs do not overlap
- Disabled pumps are skipped
- Manual 10-second tests are blocked for disabled pumps

GrowTent also supports a Flood interlock. When configured, an active Flood alarm can prevent watering from starting and can optionally abort an active watering cycle.

---

## 🔌 Shelly integration

GrowTent supports Shelly devices for several roles, including:

- main power
- light
- heater
- humidifier
- fan
- exhaust
- heating mat monitoring

The firmware supports the different Shelly API generations used by configured devices and automatically detects the required generation where supported.

Shelly actions are handled asynchronously through queued runtime ownership rather than performing blocking network operations directly inside Async HTTP callbacks.

---

## 📓 Grow Diary / LittleFS

The Grow Diary is stored locally in LittleFS.

Relevant endpoints include:

- `GET /api/diary/list`
- `GET /api/diary.csv`
- `POST /api/diary/add`
- `POST /api/diary/update`
- `POST /api/diary/delete`
- `POST /api/diary/clear`
- `POST /api/fs/format`
- `GET /api/fs/status`
- `GET /api/storage/action-status`

Storage-changing operations use the firmware handoff model and are not performed as long blocking work inside Async HTTP callbacks.

---

## 🔗 API overview

All normal application endpoints use **TCP/8080**.

### State and diagnostics

- `GET /api/state`
- `GET /api/stats`
- `GET /api/async/health`
- `GET /api/hint`
- `GET /api/fs/status`
- `GET /api/logbuffer`
- `POST /api/logbuffer/clear`
- `GET /log`
- `GET /download/log`

`/api/state` is the primary endpoint for the web interface and external integrations.

It includes areas such as:

- temperature / humidity / VPD
- DS18B20 water temperature
- grow phase and targets
- relay configuration and schedules
- irrigation state and pump enable flags
- tank level
- Shelly power/energy telemetry
- Flood sensor state

Sensitive credentials such as Wi-Fi passwords, Shelly passwords, Pushover credentials and Gotify tokens are not exposed through `/api/state`. Notification credentials and the Shelly password are also treated as write-only in the dashboard and are never embedded back into the delivered HTML. The Shelly Flood webhook token is intentionally visible because it is required for webhook configuration.

### Relays

- `GET|POST /api/relay/{1..5}/state`
- `POST /relay/{1..5}/toggle`

### Irrigation

- `GET /api/watering/state`
- `POST /api/watering/action`
- `POST /api/watering/start`
- `POST /api/watering/stop`
- `POST /startWatering`
- `POST /toggleWatering`
- `POST /pump/6/triggerPump10s`
- `POST /pump/7/triggerPump10s`
- `POST /pump/8/triggerPump10s`
- `POST /pingTank`
- `GET|POST /api/flood/event`

### Shelly

- `POST /shelly/main/toggle`
- `POST /shelly/light/toggle`
- `POST /shelly/humidifier/toggle`
- `POST /shelly/heater/toggle`
- `POST /shelly/fan/toggle`
- `POST /shelly/exhaust/toggle`
- `POST /api/shelly/reset-energy`
- `GET /api/shelly/action-status`

### System actions

- `POST /api/system/restart`
- `GET /api/system/restart-status`
- `POST /factory-reset`
- `GET /api/system/factory-reset-status`

### OTA

- `GET /api/ota/check`
- `GET /api/ota/check-status`
- `POST /api/ota/update`
- `GET /api/ota/update-status`

---

## ⚙️ Default configuration

- Wi-Fi setup AP SSID: `new growtent`
- Wi-Fi setup AP password: `GT-12$34`
- Default NTP server: `de.pool.ntp.org`
- Default language: German (`de`)
- Default theme: light
- Default temperature display: Celsius
- Default time format: 24-hour
- BME280 address: `0x76`

Persistent configuration is stored in ESP32 Preferences/NVS. Grow Diary data is stored in LittleFS.

---

## 🛠️ Build

The project uses PlatformIO:

```text
platform = espressif32@6.11.0
board = nodemcu-32s
framework = arduino
```

Important dependencies are defined in `platformio.ini`, including:

- ArduinoJson
- Adafruit BME280 Library
- Adafruit Unified Sensor
- OneWire
- DallasTemperature
- ESP32Async/AsyncTCP 3.5.0
- ESP32Async/ESPAsyncWebServer 3.12.1

Build:

```bash
pio run -e nodemcu-32s
```

USB upload:

```bash
pio run -e nodemcu-32s -t upload
```

---

## 🧪 Diagnostics and regression tests

The `tests/` directory contains three shell diagnostics for real GrowTent hardware.

### `test_async_webserver.sh`

Full release/regression test for the AsyncWebServer firmware. It checks:

- firmware version / Async phase
- TCP/80 isolation
- dashboard and static assets
- `/api/state` and `/api/stats`
- Async transport headers
- settings/action handoffs
- Wi-Fi provisioning behavior
- OTA check/status handling
- burst/load behavior
- recovery after load

> This is **not a read-only test**. Run it only when configuration/action tests are acceptable on the target controller.

```bash
./tests/test_async_webserver.sh <ESP-IP>
```

### `test_async_transport_stress.sh`

Focused **read-only** transport stress test.

It generates parallel requests to:

- `/api/state`
- `/api/stats`
- `/api/diary/list`
- `/api/diary.csv`

The test records HTTP/curl errors, response lengths and health data. Failed requests are deliberately **not retried**, so sporadic transfer stalls remain visible.

```bash
./tests/test_async_transport_stress.sh <ESP-IP>
```

### `test_ota_transport.sh`

Non-flashing OTA transport diagnostic.

It checks:

- OTA update check / target discovery
- DNS
- TLS connectivity
- GitHub release redirects
- redirect-host TLS
- release-asset response headers
- normal runtime and/or exclusive OTA-maintenance mode

No firmware bytes are written by this diagnostic.

```bash
./tests/test_ota_transport.sh <ESP-IP>
./tests/test_ota_transport.sh <ESP-IP> --normal
./tests/test_ota_transport.sh <ESP-IP> --exclusive
./tests/test_ota_transport.sh <ESP-IP> --both
```

Exclusive mode intentionally reboots the ESP after the test because application tasks are stopped for the maintenance environment.

---

## ⬆️ GitHub release and OTA

The release workflow is triggered by a `v*` Git tag.

For the current release the authoritative release-notes file is:

```text
RELEASE_NOTES_v0.4.20.md
```

The release workflow:

1. generates `src/build_version.h` from the Git tag
2. builds the `nodemcu-32s` PlatformIO environment
3. creates `firmware.bin`
4. creates `manifest.json`
5. uses `RELEASE_NOTES_<tag>.md` for the GitHub Release and OTA changelog

The normal OTA installer downloads the published GitHub Release `firmware.bin`.

---

## 📁 Project structure

```text
.github/workflows/release-firmware.yml
include/
lib/
src/
tests/test_async_webserver.sh
tests/test_async_transport_stress.sh
tests/test_ota_transport.sh
platformio.ini
README.md
RELEASE_NOTES_v0.4.20.md
LICENSE
```

Important source files:

- `src/main.cpp` — initialization, Async routes and main-loop handoffs
- `src/function.h` — control logic and API helpers
- `src/runtime.h` — runtime settings and rendering helpers
- `src/java_script.h` — embedded dashboard JavaScript
- `src/index_html.h` — embedded dashboard HTML
- `src/style_css.h` — embedded dashboard CSS
- `src/task_Check_Sensor.h` — sensor and climate-control task
- `src/task_CheckShellyStatus.h` — Shelly polling task
- `src/task_Watering.h` — irrigation task
- `src/ota.cpp` / `src/ota.h` — GitHub OTA handling

---

## 🧪 Troubleshooting

- BME280 not found → check wiring and I2C address `0x76`
- DS18B20 unavailable → check enable setting and GPIO 4
- Relays not switching → verify relay configuration and GPIO mapping
- Dashboard unavailable in normal operation → use port `8080`
- Wi-Fi setup required → connect to the `new growtent` provisioning AP
- OTA issues → use `tests/test_ota_transport.sh`
- suspected Async transport stalls → use `tests/test_async_transport_stress.sh`

---

## Release information

The current release changes are documented in:

[`RELEASE_NOTES_v0.4.20.md`](RELEASE_NOTES_v0.4.20.md)

The README references only the current release notes. Previous release history remains available through Git history and GitHub Releases.

---

## License

Licensed under the **Apache License 2.0**. See [`LICENSE`](LICENSE).
