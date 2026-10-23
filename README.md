# GrowTent

> ⚡ Smart, adaptive grow tent controller with real-time climate stabilization and OTA updates

GrowTent is a compact ESP32-based grow-tent controller that reads temperature, humidity and computes VPD (Vapor Pressure Deficit) using a BME280 sensor.

It provides a web interface (German/English), relay control, automation logic and integrates with Shelly devices. The system is designed for **stable long-term operation** and **minimal manual intervention**.

For centralized history, multi-tent management, irrigation control (if installed), and basic remote control, this firmware can be paired with the [GrowTent Backend](https://github.com/syschelle/growtent-backend). The backend is intended to offload history recording and higher-level coordination from the ESP32, keeping the controller focused on reliable real-time tent operation.

---

## 🚀 Features

- **ESP32 web interface** for live status and configuration
- **Adaptive sensor smoothing (EMA)**
  - dynamic smoothing based on real-time changes
  - fast response to environmental shifts (e.g. opening tent/window)
  - stable readings in steady conditions
- **Raw vs smoothed sensor values**
  - full transparency for debugging and tuning
- **Stable VPD calculation**
  - based on smoothed values
  - reduces oscillation in control loops
- **4x or 8x relay board support** (selectable in UI)
- **Relay scheduling** (minute-based within each hour)
- **Conditional relay logic** (e.g. based on light state)
- **Shelly integration**
  - Main power
  - Grow light
  - Humidifier
  - Heater
  - Fan (only on 8x boards)
  - Exthaust (only on 8x boards)
- **Heater control by temperature**
  - ESP relay or Shelly device
- **Irrigation support (only on 8x boards)**
  - peristaltic pumps (relay 6–8)
  - each pump can be enabled or disabled independently in Operating Settings
  - disabled pumps are skipped by automatic watering and blocked for manual 10-second tests
- **Tank level monitoring (HC-SR04) (only on 8x boards)**
  - distance-based water level detection
  - usable for irrigation safety (empty tank protection)
- **Built-in log buffer**
- **History and averages**
- **OTA update from GitHub Release binary**
- **Designed for stability**
  - optimized for unattended operation

---

## 🌡️ Adaptive Climate Smoothing

The system uses an **adaptive exponential moving average (EMA)** to balance
stability and responsiveness of sensor readings.

### How it works

- Raw sensor values are read from the BME280
- The system calculates the difference between consecutive readings
- Based on this delta, a dynamic smoothing factor (`alpha`) is applied:
  - small changes → strong smoothing
  - large changes → fast reaction

### Example

- Stable environment → smooth values  
- Opening a window → immediate response  

### Benefits

- Reduced sensor noise  
- Stable VPD calculation  
- Faster reaction to real changes  
- Improved control behavior  

## Hardware

- ESP32 4x or 8x relay board (any compatible dev board)
- Adafruit BME280 (I2C at default address 0x76)
- 4x relay channels (driven by ESP32 GPIOs)
- Power supply appropriate for ESP32 and relays
- HC-SR04 sensor 

### Default sensor and relay configuration in code:
- BME I2C address: `0x76` (BME_ADDR)
- DS18B20 sensor pin 4
- Relay count: `4` (NUM_RELAYS)
- 4x Relay pins (in order): `{ 32, 33, 25, 26 }` 
- 8x Relay pins (in order): `{32, 33, 25, 26, 27, 14, 12, 13 }`
- only for 8x Relay board connect Relay 6, 7, 8 to a peristaltic pump for watering the pods
- Pump 1, Pump 2 and Pump 3 can be enabled independently in **Operating Settings → Irrigation settings**; all three are enabled by default after an upgrade
- Shelly devices (Gen1/Gen2/Gen3 depending on endpoint support)

### Wiring notes:
- Connect BME280 SDA / SCL to ESP32 SDA / SCL (Wire library pins)
- Make sure common ground between relays and ESP32
- Relays are initialized as OUTPUT and driven LOW = OFF by default

### Relay Boards
- 4-channel relay board
- 8-channel relay board

---

## Defaults & Constants

- AP SSID: `new growtent`
- AP password: `GT-12$34`
- Measurement interval: `30000 ms` (30 seconds)
- Default NTP server: `de.pool.ntp.org`
- Default timezone (POSIX-style): Western European Time, example:
  `WEST-1DWEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00`
- Default language: `de` (German)
- Default theme: `light`
- Default units: `metric`
- Default time format: `24h`
- Default VPD targets (per phase): `{ 0.0, 0.8, 1.2, 1.4, 1.0 }`
- Phase names: `["", "Vegetative", "Flowering", "Drying"]`

Preferences keys stored in NVS:
- `ssid` / `password` (WiFi)
- `boxName`
- `lang`
- `theme`
- `unit`
- `timeFmt`
- `ntpSrv`
(Namespace: `growtent`)

---

## Web UI

The web frontend is embedded in program memory and served by the ESP32:
- Main page: `/` (index)
- Stylesheet: `/style.css`
- JavaScript: `/script.js`
- i18n manifest and language JSONs are served from memory
- Factory reset endpoint available via `POST /factory-reset` (form in UI)

UI highlights:
- Gauges for Temperature, Humidity and VPD
- Settings page for language, theme, date/time format, temperature unit, and NTP/timezone configuration
- Uses localStorage + Preferences for persistent settings across reloads
- Statuspage
 <img width="1658" height="1167" alt="Bildschirmfoto_26-7-2026_191323_192 168 178 88" src="https://github.com/user-attachments/assets/3eaaaf75-0dce-4206-9e91-79ac6b099125" />





Languages provided (i18n):
- German (`de`)
- English (`en`)

---

## VPD Calculation

VPD (kPa) is calculated from the measured temperature (°C) and relative humidity (%) using saturation vapor pressure (svp) approximation:

svp = 0.6108 * exp((17.27 * T) / (T + 237.3))
VPD = svp - (RH / 100) * svp

Values are updated by a background task and stored in:
- `lastTemperature`
- `lastHumidity`
- `lastVPD`

---

## Software / Libraries

- Arduino framework for ESP32
- Wire (I2C)
- Adafruit_BME280
- Preferences (NVS)
- Adafruit Sensor (sensor abstraction)

Make sure to install the Adafruit_BME280 and Adafruit_Sensor libraries before building.

---

## Project Structure

- `src/main.cpp`
Web routes, setup, loop, task startup
- `src/function.h`
Core logic (control, API builders, handlers)
- `src/runtime.h`
Preferences load and HTML placeholder rendering
- `src/java_script.h`
Frontend behavior (status polling, UI updates)
- `src/index_html.h`
Embedded web page template
- `src/style_css.h`
Embedded styles
- `src/task_Check_Sensor.h`
Sensor/control task
- `src/task_CheckShellyStatus.h`
Shelly poll task
- `src/task_Watering.h`
Irrigation task

---

## Build & Flash (quick)

These are general instructions — adapt to your toolchain (Arduino IDE, PlatformIO, Arduino CLI, etc.)

1. Install required libraries:
   - Adafruit_BME280
   - Adafruit_Sensor

2. Select the correct ESP32 board in your build environment.

3. Build and flash to your ESP32 as usual.

Example (PlatformIO):
- pio run --target upload

Example (Arduino CLI):
- arduino-cli compile --fqbn <fqbn> .
- arduino-cli upload -p <port> --fqbn <fqbn> .

---

## Configuration & Tuning

- WiFi: You can set the device to Station mode or keep it in AP mode for direct connection.
- NTP and timezone: Set the NTP server and timezone string via the web UI.
- Relay behavior: By default relays are set LOW at startup. Update logic in code to change behavior.

---

## API Endpoints (Connectors)

### Core
- `GET /api/state`
Unified state payload for frontend and external integrations (recommended primary endpoint).
Pump selection is exposed as `irrigation.pump1.enabled`, `irrigation.pump2.enabled` and `irrigation.pump3.enabled`.
Disabled pumps are visibly marked in the status UI, and their manual 10-second test buttons cannot be operated.

Shelly Flood S Gen4 state is exposed event-driven as:
- `sensors.flood.configured`
- `sensors.flood.alarm` (`null` until the first valid event)
- `sensors.flood.status` (`disabled`, `unknown`, `ok`, `alarm`)
- `sensors.flood.lastSeen`

The Flood sensor IP address and webhook token are intentionally **not** exposed through `/api/state`. The battery-powered sensor is never polled by GrowTent.

### Control
- `POST /relay/{1..5}/toggle`
- `POST /pump/6/triggerPump10s`
- `POST /pump/7/triggerPump10s`
- `POST /pump/8/triggerPump10s`
  - returns `409 pump_disabled` when the selected pump is disabled in Operating Settings
- `POST /startWatering`
  - starts only when at least one irrigation pump is enabled
- `POST /pingTank`

### Shelly
- `POST /shelly/main/toggle`
- `POST /shelly/light/toggle`
- `POST /shelly/humidifier/toggle`
- `POST /shelly/heater/toggle`
- `POST /api/shelly/reset-energy`
- `GET|POST /api/flood/event?token=<token>&alarm=0|1`
  - event receiver for Shelly Flood S Gen4 URL actions
  - authenticated with a dedicated per-controller token
  - persists the last known flood state across ESP restarts

### Scheduling / Settings
- `POST /api/relay/schedule/save-all`
- `POST /savesettings`
- `POST /saverunsettings`
- `POST /saveshellysettings`
- `POST /savemessagesettings`

### Logs
- `GET /api/logbuffer`
- `POST /api/logbuffer/clear`
- `GET /log`

### OTA
- `POST /api/ota/update`
Triggers OTA update from the configured firmware URL.

---

## ⚙️ Configuration

- WiFi: AP or Station mode
- NTP + timezone configurable via UI
- Relay logic configurable via UI

---

## Project Structure (high level)

- src/
  - main.cpp (setup, loop, initialization)
  - config.h (hardware and global configuration)
  - task_Check_Sensor.h (background sensor read + VPD logic)
  - index_html.h (web frontend HTML + i18n JSON)
  - java_script.h (client JS embedded)
  - style_css.h (client CSS embedded)
  - other .h / helper files

All web UI assets are embedded as string literals (PROGMEM) and served by the device.

---

## 🧪 Troubleshooting

- BME280 not found → check wiring & I2C address  
- Relays not switching → check GPIO + logic level  
- No UI → connect to AP:  
  - SSID: `new growtent`  
  - Password: `GT-12$34`

---

## Contributing

Contributions, bug reports and improvements welcome. Please open issues or pull requests describing the change and rationale.

---

## License

Add license information here (no license file present in the source snapshot). If you want to use a permissive license, consider adding an SPDX header and LICENSE file (MIT, Apache-2.0, etc.).

---


### v0.2.21 UI robustness
- Pump status rendering is now handled after `/api/state` is loaded, reducing server-side HTML processing.
- Sensor rendering is isolated from pump UI updates so pump controls cannot suppress sensor values.
- Static assets use a firmware-version query string to avoid stale browser cache after updates.


### v0.2.22 pump status UI
- Disabled pumps now show a translated text label instead of a grey disabled test button.
- The manual 10-second test button is only visible for enabled pumps.


### v0.2.24 Flood S Gen4 integration

> Build fix: the internal Flood token generator now uses `HEX_CHARS` instead of `HEX` to avoid a collision with the Arduino `HEX` macro.

- Added event-driven Shelly Flood S Gen4 integration without a polling task.
- Added a dedicated webhook token and generated Flood alarm / Flood alarm off URLs in Shelly settings.
- Persisted the last known flood state across ESP restarts to support the sensor's sleep behavior.
- Added `sensors.flood.*` fields to `/api/state` without exposing the Flood IP address or token.


### v0.2.25 Shelly polling stability
- Empty Shelly IP fields are treated as unconfigured and are not polled.
- Empty Shelly slots no longer generate repeated `Invalid IP: ''` log messages.
- Unreachable configured Shellys use a 30-second retry backoff after a failed status request, reducing network/task load.
- Last known good Shelly values are preserved during transient failures.
- Removed repetitive API-rendering log messages for unavailable main/light values; actual request failures remain logged by the polling layer.
- Flood S Gen4 remains event-driven and unchanged.


### v0.2.26 Flood webhook API link display

- Added a dedicated **API Links for Shelly Actions** section to the Flood S Gen4 settings.
- Shows the complete webhook URLs for `Flood alarm` and `Flood alarm off`.
- Added Copy buttons for both webhook URLs.
- The URLs continue to use the GrowTent controller address and the local Flood webhook token; neither is exposed through `/api/state`.

### v0.2.27 Web UI heap stability

- The main web interface is no longer assembled as one >52 KB Arduino `String` in heap.
- HTML is rendered and sent in newline-aligned chunks of about 4 KB.
- Dynamic placeholders are still resolved server-side for every chunk.
- This avoids requiring one very large contiguous heap block when opening `/`.
- The change is intended to keep the web UI available even when free heap is fragmented and the largest free block is around 50–60 KB.
- Flood S Gen4 behavior, sensor tasks, Shelly control logic, irrigation and VPD control are unchanged.
- Shelly status polling is now staggered: one Shelly is checked per step instead of all six back-to-back, avoiding multi-second timeout bursts when devices are offline.


### v0.2.28 Grow date persistence hardening

- Loads all persistent runtime Preferences before the HTTP server is started.
- Removes the boot-time race where the web UI could temporarily render empty/default grow values before NVS was restored.
- Protects `webGrowStart` from being overwritten by an empty or malformed value.
- Keeps slow sensor/network initialization in the deferred initialization task.
- No changes to Flood S Gen4 polling policy, irrigation control, VPD control, or relay scheduling.

### v0.2.30 Flood header status, battery telemetry and time formatting

- Shows the Flood S Gen4 state in the header whenever Flood integration is enabled.
- Adds `sensors.flood.batteryPct` to `/api/state`.
- Adds Shelly webhook token replacement for battery percentage and device Unix time.
- Stores Flood event timestamps as UTC ISO-8601 and formats them locally in the browser.
- Keeps the integration event-driven: the GrowTent controller still never polls the Flood S Gen4.


### v0.2.31 OTA manifest reliability

- OTA update checks now use a small `manifest.json` release asset instead of parsing GitHub's complete latest-release API response on the ESP32.
- The release workflow publishes `manifest.json` together with `firmware.bin`.
- OTA checking uses forced redirect following and HTTP/1.0 connection-close responses to reduce buffering and heap pressure.
- OTA check diagnostics now report HTTP status, content length, free heap and largest allocatable heap block on Serial.
- The web UI now displays the controller's detailed OTA error reason instead of only a generic HTTP status.
- Firmware download and installation continue to use the existing `firmware.bin` latest-release URL.

### v0.2.29 Flood webhook URL restore fix

- Fixed Flood S Gen4 webhook URLs remaining blank after an ESP restart or normal browser reload.
- The persisted Flood token is now converted into both complete Shelly Action URLs immediately when the web UI initializes.
- Webhook URL previews are also refreshed on `pageshow` so browser back/forward cache restores remain correct.
- No changes to Flood event handling, Shelly polling, VPD control, irrigation, relay scheduling, or FreeRTOS task layout.



### v0.2.32 Wi-Fi / backend connectivity diagnostics

- Pins PlatformIO to `espressif32@6.11.0` so local and GitHub release builds use the same ESP32/Arduino runtime.
- Logs Wi-Fi link loss and restoration, including downtime, RSSI and reconnect attempts.
- Logs main-loop stalls of at least one second because they can delay the synchronous web server and `/api/state`.
- Logs `/api/state` responses that take at least 250 ms without logging every normal backend poll.
- Replaces ambiguous Shelly `EmptyInput` JSON errors with explicit empty-response diagnostics.
- Does not change grow control, irrigation, Flood S Gen4 handling or relay scheduling.

### v0.2.33 OTA progress feedback

- Adds a real-time progress bar for online OTA firmware installation.
- Shows actual transfer percentage and transferred/total firmware bytes from the ESP32 `HTTPUpdate` progress callback.
- Displays immediate start feedback, successful completion/restart state, and detailed OTA errors in the web UI.
- Streams compact progress events over the existing OTA request without adding another FreeRTOS task.

### v0.2.34 OTA release information fix

- OTA manifests now use the version-specific `RELEASE_NOTES_<version>.md` file.
- GitHub Release notes and the OTA changelog are generated from the same source.
- Annotated tag text is only used as a fallback when no release-notes file exists.


### v0.2.35 Firmware version in header

The installed firmware version is shown directly below the tent/controller name in the top-left header. The value comes from the same `FW_VERSION` generated from the release tag.


### v0.2.36 Header version label cleanup

The top-left header now shows only the installed version (for example `v0.2.36`) below the tent/controller name, without the `Firmware:` prefix.
