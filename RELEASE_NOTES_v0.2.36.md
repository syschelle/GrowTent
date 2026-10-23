# GrowTent v0.2.36

## Header version label cleanup

- Removed the `Firmware:` prefix from the version shown below the tent/controller name in the top-left header.
- The header now displays only the installed version, for example `v0.2.36`.
- The value still comes directly from `FW_VERSION`, so it remains tied to the installed firmware build.
- The same compact version display is used in normal and AP mode.
- No changes were made to grow control, irrigation, Shelly polling, Flood S Gen4 handling, relay scheduling, connectivity diagnostics, or OTA logic.
