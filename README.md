# GrowTent

GrowTent is a compact ESP32-based grow-tent controller that reads temperature, humidity and computes VPD (Vapor Pressure Deficit) using a BME280 sensor. It provides a small web frontend (German/English) with gauges, basic operating settings and the ability to control up to 4 relays (fans / humidifier). The project is designed to run on an ESP32 and serves static HTML/CSS/JS from program memory.

---

## Features

- Reads temperature, humidity from Adafruit BME280
- Computes VPD and exposes last measured values to the UI
- Controls 4 relays (default: left fan, right fan, pod fan, humidifier fan)
- Web UI with gauges (temperature, humidity, VPD), settings, and factory reset
- Offline-friendly: supports Access Point (AP) mode for initial setup
- Internationalized UI (German and English)
- Stores configuration in Preferences (NVS)
- NTP support and timezone configuration
- Automatic periodic sensor sampling (default every 30s)

---

## Hardware

- ESP32 (any compatible dev board)
- Adafruit BME280 (I2C at default address 0x76)
- 4x relay channels (driven by ESP32 GPIOs)
- Power supply appropriate for ESP32 and relays

Default sensor and relay configuration in code:
- BME I2C address: `0x76` (BME_ADDR)
- Relay count: `4` (NUM_RELAYS)
- Relay pins (in order): `{ 32, 33, 25, 26 }`
- Relay names (default): `"left fan"`, `"right fan"`, `"pod fan"`, `"humidifier fan"`

Wiring notes:
- Connect BME280 SDA / SCL to ESP32 SDA / SCL (Wire library pins)
- Make sure common ground between relays and ESP32
- Relays are initialized as OUTPUT and driven LOW = OFF by default

---

## Defaults & Constants

- AP SSID: `new growtent`
- AP password: `12345678`
- Measurement interval: `30000 ms` (30 seconds)
- Default NTP server: `de.pool.ntp.org`
- Default timezone (POSIX-style): Western European Time, example:
  `WEST-1DWEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00`
- Default language: `de` (German)
- Default theme: `light`
- Default units: `metric`
- Default time format: `24h`
- Default VPD targets (per phase): `{ 0.0, 0.8, 1.2, 1.4, 1.0 }`
  - Phase names: `["", "Seedling/Clone", "Vegetative", "Flowering", "Drying"]`

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
  <img width="1234" height="980" alt="image" src="https://github.com/user-attachments/assets/4bd1b7b7-3e81-4212-8a57-6be878d5dbcb" />


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
- VPD targets per growth phase are defined in `config.h` and can be tuned to your needs.

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

## Troubleshooting

- BME280 not detected: check wiring, I2C address (0x76 by default), and power.
- Relays not switching: verify wiring, correct GPIO pins, and that your relay board is compatible with ESP32 logic levels.
- UI not reachable: if device in AP mode, connect to SSID `new growtent` (password `12345678`) or configure WiFi via serial/console or pre-populated preferences.

---

## Contributing

Contributions, bug reports and improvements welcome. Please open issues or pull requests describing the change and rationale.

---

## License

Add license information here (no license file present in the source snapshot). If you want to use a permissive license, consider adding an SPDX header and LICENSE file (MIT, Apache-2.0, etc.).

---

If you want, I can:
- Generate a ready-to-commit README.md updated with more precise build commands for your preferred toolchain (PlatformIO / Arduino IDE / Arduino CLI).
- Create a wiring diagram (text-based) for the BME280 and relays.
- Create a sample config example or instructions for changing NTP/timezone/initial WiFi via the code.
