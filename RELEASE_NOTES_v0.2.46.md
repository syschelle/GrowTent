# GrowTent v0.2.46

## Roll back the v0.2.44/v0.2.45 network experiments

- Reverts the ESP32 runtime network changes introduced in v0.2.44 and retained in v0.2.45.
- Restores the Shelly polling task to the v0.2.43 behavior, including the fixed 30-second retry delay after a failed poll.
- Removes the exponential 30/60/120/240-second Shelly retry backoff that could leave recovered Shelly devices stale for several minutes after a temporary outage or firmware restart.
- Restores the v0.2.43 `getShellyValues()` implementation and removes the additional elapsed-time instrumentation from the Shelly request path.
- Restores the v0.2.43 main-loop and `/api/state` handling, removing the additional v0.2.44/v0.2.45 web timing instrumentation.
- Keeps all functional features added through v0.2.43, including Flood irrigation protection, API-origin irrigation notifications, heating-mat monitoring, OTA manifest updates and the release-workflow race fix.
- Does not change sensor acquisition, relay automation, irrigation calculations, Flood webhook handling, stored settings or the web UI rendering logic.
- This release intentionally does not change the pinned Arduino-ESP32 2.0.17 runtime. The upstream WebServer hang fix available in Arduino-ESP32 3.3.12 will be evaluated separately as a controlled platform migration.
