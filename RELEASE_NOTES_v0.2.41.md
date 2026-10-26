# GrowTent v0.2.41

## Optional Flood alarm irrigation safety stop

- Added a new checkbox under **Operating Settings → Irrigation settings**:
  **Stop active irrigation on Flood alarm**.
- The option is disabled by default so existing installations keep their previous behavior.
- When enabled, an authenticated `Flood alarm` webhook from the configured Shelly Flood S Gen4 immediately aborts a currently running automatic irrigation cycle.
- The Flood safety stop reuses the existing irrigation stop path introduced in v0.2.39:
  - all irrigation pump relays are switched off immediately,
  - remaining irrigation runs are set to `0`,
  - the remaining-time display is reset to `00:00`,
  - the watering task observes the stop request during pump runtime and pauses within at most 100 ms.
- `Flood alarm off` / dry events do not affect irrigation.
- Manual 10-second pump pulses are intentionally not stopped by this option; it applies only to an active automatic irrigation cycle.
- The setting is stored persistently in ESP32 Preferences and restored after reboot/OTA updates.
- No additional FreeRTOS task, Flood polling, or backend communication was added.
- BME280, DS18B20, ultrasonic tank measurement, Shelly polling, relay scheduling, VPD control, and the existing dashboard irrigation toggle are unchanged.
- Updated browser cache-busting for `script.js` and `style.css` to v0.2.41.
