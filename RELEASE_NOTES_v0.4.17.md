# GrowTent v0.4.17

## Release summary

GrowTent v0.4.17 fixes the dashboard temperature-unit handling. Sensor, control and API values remain Celsius-native, while the web interface now consistently converts temperatures to Fahrenheit whenever **°F** is selected in System Settings.

## Highlights

- Converts the BME280 dashboard temperature from °C to °F for display when Fahrenheit is selected.
- Converts the optional DS18B20 water-temperature display in the same way.
- Updates all visible temperature unit labels on the Status page and in Running Settings.
- Converts the Status-page target temperature to the selected display unit.
- Converts the leaf-temperature offset correctly as a **temperature difference** (`Δ°F = Δ°C × 1.8`), without adding 32.
- Converts the Running Settings target-temperature input between °C and °F.
- Converts the Running Settings leaf-temperature-offset input between °C and °F.
- Converts Fahrenheit Running Settings values back to Celsius before writing `/api/settings/grow-targets`, preserving the existing firmware/API contract and control logic.
- Keeps the canonical target-temperature range at 18.0–30.0 °C, displayed as 64.4–86.0 °F when Fahrenheit is active.
- Keeps the canonical leaf-offset range at -3.0–0.0 °C, displayed as -5.4–0.0 °F when Fahrenheit is active.
- Persists the selected device unit and exposes the boot-loaded unit through `settings.ui.unit`.
- Updates dashboard cache-busters and regression-test firmware metadata to v0.4.17.

## Compatibility and control behavior

No BME280, VPD, heater, irrigation or Shelly control calculations were converted to Fahrenheit. Internal temperature values, saved grow targets and API payloads remain in Celsius. Fahrenheit is deliberately restricted to the presentation/input layer.

This keeps the existing automation behavior unchanged while allowing the UI to use either temperature unit safely.

## OTA

The OTA implementation is unchanged from v0.4.16. The already validated Arduino `HTTPUpdate` path, exclusive maintenance mode and OTA request-status lifecycle remain intact.

## AsyncWebServer migration history

The completed AsyncWebServer migration remains at phase 32. For the full migration and OTA validation history through v0.4.16, see `RELEASE_NOTES_v0.4.16.md` in the previous release/tag history.
