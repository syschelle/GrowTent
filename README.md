# GrowTent

> Smart ESP32 grow-tent controller with climate monitoring, automation, irrigation, Shelly integration and OTA updates.

GrowTent is an ESP32 NodeMCU-32S based controller for climate monitoring and automation in a grow tent. It reads temperature and humidity from a BME280, optionally reads a DS18B20 water-temperature sensor, calculates VPD, controls relays and irrigation, and integrates with Shelly devices.

The firmware provides a German/English web interface and a JSON API. Starting with **v0.4.0**, the application web server is fully based on **ESPAsyncWebServer**. The staged migration from the former synchronous `WebServer` implementation is documented in [`RELEASE_NOTES_v0.4.4.md`](RELEASE_NOTES_v0.4.4.md).

For centralized history, multi-tent management and higher-level coordination, GrowTent can be paired with the [GrowTent Backend](https://github.com/syschelle/growtent-backend).

---

## Current architecture

### Normal operation

- Dashboard and application API: `http://<esp-ip>:8080/`
- Permanent application server: `ESPAsyncWebServer` on **TCP/8080**
- No normal application listener on TCP/80
- State endpoint: `GET /api/state`
- Compatibility statistics endpoint: `GET /api/stats`
- Health endpoint: `GET /api/async/health`

### Wi-Fi provisioning

TCP/80 is reserved exclusively for the asynchronous captive portal while Wi-Fi provisioning is active.

- Captive portal: **TCP/80**
- Captive DNS is active only while provisioning is running
- Candidate credentials are tested before they replace the stored Wi-Fi configuration
- A boot-time fallback AP is automatically stopped after the stored STA connection recovers
- After successful recovery the controller returns to STA-only operation and TCP/80 is closed again

Operations that can touch hardware, storage, networking or restart state are queued from the Async HTTP callback and executed by the normal Arduino loop or the responsible worker task.

---

## Features

- Embedded German/English web interface
- Adaptive BME280 temperature/humidity smoothing
- VPD calculation and target-based automation
- Optional DS18B20 water-temperature sensor
- 4- or 8-relay configuration
- Minute-based relay schedules
- Conditional relay operation based on light state
- Heating control using an ESP relay or Shelly device
- Shelly integration for:
  - main power
  - grow light
  - humidifier
  - heater
  - fan
  - exhaust
  - heating mat monitoring
- Automatic Shelly generation detection
- Irrigation support on 8-relay systems
  - Pumps on relays 6-8
  - Individual pump enable/disable
  - Pump calibration
  - Sequential watering
  - Manual 10-second pump test
- HC-SR04 tank-level measurement
- Shelly Flood S Gen4 event integration
  - event-driven webhook handling
  - irrigation start interlock
  - optional active-irrigation abort on Flood alarm
- Grow Diary stored in LittleFS
- Built-in system log
- Runtime/state JSON API
- GitHub OTA update check and installation
- Release-specific static-asset cache busting
- Fixed-length direct-buffer transport for important Async responses

---

## Hardware

### Controller

- ESP32 NodeMCU-32S
- PlatformIO board: `nodemcu-32s`

### Sensors

- BME280, I2C address `0x76`
- DS18B20, GPIO `4`
- HC-SR04 tank sensor:
  - TRIG: GPIO `16`
  - ECHO: GPIO `17`

### Relay mapping

8-relay mapping in the firmware:

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

For a 4-relay configuration only relays 1-4 are active.

---

## Web interface

In normal STA operation open:

```text
http://<esp-ip>:8080/
```

The frontend is embedded in the firmware:

- `/` - dashboard
- `/style.css` - stylesheet
- `/script.js` - JavaScript

The dashboard includes status, operating settings, system settings, logs, factory reset, messages, OTA update and Grow Diary pages.

The installed firmware version is displayed in the dashboard header.

---

## API overview

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

### Grow Diary / LittleFS

- `GET /api/diary/list`
- `GET /api/diary.csv`
- `POST /api/diary/add`
- `POST /api/diary/update`
- `POST /api/diary/delete`
- `POST /api/diary/clear`
- `POST /api/fs/format`
- `GET /api/storage/action-status`

### Relays

- `GET|POST /api/relay/{1..5}/state`
- `POST /relay/{1..5}/toggle`

Manual relay operations use a queued main-loop handoff rather than changing GPIO state directly inside an Async callback.

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

Disabled pumps cannot be started by the manual pump-test endpoint. When the configured Flood protection is active, a known Flood alarm can block a watering start and optionally stop an active watering cycle.

### Shelly

- `POST /shelly/main/toggle`
- `POST /shelly/light/toggle`
- `POST /shelly/humidifier/toggle`
- `POST /shelly/heater/toggle`
- `POST /shelly/fan/toggle`
- `POST /shelly/exhaust/toggle`
- `POST /api/shelly/reset-energy`
- `GET /api/shelly/action-status`

Shelly configuration uses the `/api/settings/shelly/*` and `/api/settings/shelly-dashboard` endpoints on TCP/8080.

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

OTA checks use the GitHub Release `manifest.json`; installation downloads the published `firmware.bin` release asset.

---

## `/api/state`

`/api/state` is the primary endpoint for the web frontend and external integrations. It includes current sensor values, relay state, grow settings, irrigation state and Shelly telemetry.

Examples of exposed areas include:

- temperature / humidity / VPD
- DS18B20 water temperature
- grow phase and targets
- relay configuration and schedules
- irrigation and pump enable state
- tank level
- Shelly power/energy values
- Flood sensor state

Sensitive values such as Wi-Fi credentials, Shelly authentication passwords and the Flood webhook token are not exposed through `/api/state`.

---

## Default configuration

- Wi-Fi setup AP SSID: `new growtent`
- Wi-Fi setup AP password: `GT-12$34`
- Default NTP server: `de.pool.ntp.org`
- Default language: German (`de`)
- Default theme: light
- Default units: metric
- Default time format: 24-hour
- BME280 address: `0x76`

Persistent configuration is stored in ESP32 Preferences/NVS. Grow Diary data is stored in LittleFS.

---

## Build

The project is configured for PlatformIO.

```text
platform = espressif32@6.11.0
board = nodemcu-32s
framework = arduino
```

Important library dependencies are defined in `platformio.ini`:

- ArduinoJson
- Adafruit BME280 Library
- Adafruit Unified Sensor
- OneWire
- DallasTemperature
- ESP32Async/AsyncTCP 3.5.0
- ESP32Async/ESPAsyncWebServer 3.12.1

Build locally with:

```bash
pio run -e nodemcu-32s
```

Upload over USB with:

```bash
pio run -e nodemcu-32s -t upload
```

---

## GitHub release and OTA

The release workflow is triggered by a `v*` Git tag.

For release `v0.4.4`, the authoritative release-notes file is:

```text
RELEASE_NOTES_v0.4.4.md
```

The workflow:

1. generates `src/build_version.h` from the Git tag;
2. builds the `nodemcu-32s` PlatformIO environment;
3. publishes `firmware.bin`;
4. creates `manifest.json` for OTA;
5. uses `RELEASE_NOTES_<tag>.md` for both the GitHub Release text and OTA changelog.

This keeps the GitHub Release and the changelog shown by the ESP32 OTA page synchronized.

---

## AsyncWebServer regression test

The repository contains one current migration/regression test:

```text
tests/test_async_webserver.sh
```

It is intended to be run against a real ESP32 after flashing and covers the final AsyncWebServer architecture, including state transport, settings/actions, provisioning lifecycle, OTA check and load/regression checks.

---

## Project structure

```text
.github/workflows/release-firmware.yml
include/
lib/
src/
tests/test_async_webserver.sh
platformio.ini
README.md
RELEASE_NOTES_v0.4.4.md
LICENSE
```

Important source files:

- `src/main.cpp` - initialization, Async routes and main-loop handoffs
- `src/function.h` - control logic and API helpers
- `src/runtime.h` - runtime settings and rendering helpers
- `src/java_script.h` - embedded dashboard JavaScript
- `src/index_html.h` - embedded dashboard HTML
- `src/style_css.h` - embedded dashboard CSS
- `src/task_Check_Sensor.h` - sensor and climate-control task
- `src/task_CheckShellyStatus.h` - Shelly polling task
- `src/task_Watering.h` - irrigation task
- `src/ota.cpp` / `src/ota.h` - GitHub OTA handling

---

## Release information

The complete v0.4.4 release documentation and AsyncWebServer migration history, including Phases 1-32 and the final corrective validation builds, is documented in:

[`RELEASE_NOTES_v0.4.4.md`](RELEASE_NOTES_v0.4.4.md)

Historical per-version release notes and internal migration patch/validation files are intentionally not kept in the release source package.

---

## License

Licensed under the **Apache License 2.0**. See [`LICENSE`](LICENSE).
