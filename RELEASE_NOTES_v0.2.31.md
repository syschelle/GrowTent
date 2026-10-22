# GrowTent v0.2.31

## OTA update check reliability

- Reworked OTA update checking to use a small `manifest.json` release asset instead of parsing GitHub's complete latest-release API response on the ESP32.
- Updated the GitHub Actions release workflow to publish `manifest.json` together with `firmware.bin`.
- Reduced OTA check memory pressure by using a compact manifest, forced redirects and HTTP/1.0 connection-close responses.
- Added serial diagnostics for OTA checks, including HTTP status, content length, free heap and largest allocatable heap block.
- Improved the OTA page so errors show the controller-provided reason instead of only `HTTP 500`.
- Firmware installation still downloads `firmware.bin` from the latest GitHub release.

### First update to v0.2.31

Because older releases do not contain `manifest.json`, install v0.2.31 manually once (USB or manual OTA if available). From v0.2.31 onward, releases produced by the included workflow contain both OTA assets.
