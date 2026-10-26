# GrowTent v0.2.40

## Status page regression fix

- Fixed a JavaScript scope regression introduced in v0.2.39.
- `updateWateringToggleFromState()` was defined outside the main `DOMContentLoaded` scope while directly accessing the block-scoped `I18N` table.
- This caused a `ReferenceError` during every `/api/state` render on the Status page.
- Because the exception occurred directly after the irrigation counters were rendered, the shared error handler reset temperature, humidity, VPD and last-update fields to `N/A`, while tank, pump availability and relay rendering were never reached.
- Moved the watering toggle state renderer into the same scope as `I18N`.
- The v0.2.39 irrigation start/stop backend logic is unchanged.
- BME280 handling, DS18B20 handling, ultrasonic tank measurement, relay handling, pump enable settings, Shelly polling, Flood S Gen4 handling and `/api/state` generation are unchanged.
- Updated the browser cache-busting version for `script.js` and `style.css` to v0.2.40.
