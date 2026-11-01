# GrowTent v0.5.3

## Release summary

GrowTent v0.5.3 finalizes the authenticated Backend Write API for release and adds a visible security-status indicator to the dashboard header. When the Backend Write API is enabled and a 256-bit HMAC key is configured, a green `HMAC` label is shown in the header. When HMAC protection is not active, no label is displayed.

This release also improves the OTA changelog generated for `manifest.json`. OTA users now receive a more useful summary of the current release directly in the update dialog instead of only a minimal note.

## Highlights

- Added a compact green `HMAC` status label to the dashboard header.
- The HMAC label is rendered only when the Backend Write API is enabled and a 64-character HMAC-SHA256 key is configured.
- No secret material is exposed to the page; the label is derived only from the stored enabled/configured state.
- Retains the authenticated `PATCH /api/backend/grow-profile` endpoint with source-IP restriction, HMAC-SHA256 signatures, ±60-second timestamp validation, nonce/replay protection and the existing five-field allowlist.
- Retains asynchronous handoff of backend Grow-profile changes to the Arduino loop so AsyncWebServer callbacks do not perform direct NVS writes.
- Retains serialization between dashboard and backend Grow-profile writes to prevent overlapping Preferences/NVS transactions.
- Uses ArduinoJson 7 `obj[key].is<T>()` validation without deprecated `containsKey()` calls.
- Expanded the OTA changelog generator to include the current release summary, highlights/changes and compatibility section when available.
- Removed the generic external-release-details footer from the OTA changelog so the update text stays self-contained.
- Updated firmware and web-asset version markers to v0.5.3.

## Compatibility

- No breaking API changes.
- `/api/state` and `/api/stats` remain Celsius-native regardless of the UI temperature-unit setting.
- No change to normal application port TCP/8080 or the provisioning-only use of TCP/80.
- No change to relay control, irrigation sequencing, flood interlock behavior, Shelly integration or OTA installation flow.
- Existing Backend Write API configuration remains compatible; no HMAC key rotation is required for this update.

## Validation status

The v0.5.2 predecessor compiled successfully for the `nodemcu-32s` target. The v0.5.3 source changes have been statically reviewed, but v0.5.3 itself must still be built and tested on the target hardware before the Git tag is created.
