# GrowTent v0.2.28

## Grow date persistence hardening

This release fixes a boot-time race that could expose empty/default runtime settings in the web interface immediately after a reboot or firmware upload.

### Fixed

- Full persistent runtime Preferences are now loaded before the HTTP server is started.
- The Grow Start Date can no longer be overwritten by an empty or malformed `webGrowStart` value.
- Slow sensor and network initialization remains deferred so startup responsiveness is preserved without risking configuration integrity.
- Saved Grow, Flowering, Drying, Shelly, irrigation and controller settings are available before the web interface can be opened.

### Unchanged

- Shelly Flood S Gen4 remains event-driven and is never polled.
- No additional FreeRTOS task was added.
- VPD control, irrigation control and relay scheduling behavior are unchanged.

## Upgrade note

A normal PlatformIO firmware upload does not intentionally erase NVS. If the Grow Start Date was already overwritten before installing v0.2.28, restore the correct date once in Operating Settings after the upgrade.
