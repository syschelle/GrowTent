# GrowTent v0.5.24

## Release summary

Compact system settings matching the operating-settings UI.

## Changes

- Six permanently open cards: Device, Display, Time, DS18B20 sensor, Backend access (HMAC) and Relay names.
- Electricity price appears under Device; the POSIX timezone-list link remains beside the timezone configuration.
- Compact responsive fields with German/English and light/dark support.
- Board-specific relay controls preserved, including fixed six-channel S3 and reserved pump outputs.

## Compatibility

Existing field IDs/names, validation, HMAC secret retention, configuration acknowledgement and save/restart behavior remain unchanged. No climate, irrigation, fan timer or GPIO logic changes.

## Validation

Production HTML/CSS/JavaScript browser checks cover layout, translations, board-specific fields and synthetic save/ACK/failure flows. Existing host and full firmware checks run in CI. Real-device settings save/reboot and OTA checks remain open.
