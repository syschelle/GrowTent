# GrowTent v0.4.18

## Release summary

GrowTent v0.4.18 improves the project diagnostics documentation and aligns the shipped shell-test metadata with the current release. No climate-control, relay, irrigation, Shelly, OTA installer or API behavior is changed.

## Changes

- Expanded the README diagnostics section with a concise description of each shipped shell test.
- Documented which diagnostic is read-only and which full regression test can exercise configuration/action paths.
- Documented the focused State/Stats/Diary transport stress test and its no-retry behavior.
- Documented the non-flashing OTA transport diagnostic and the intentional reboot after exclusive-mode testing.
- Added all three current shell diagnostics to the README project structure.
- Keeps the README release reference limited to the current release notes only.
- Advances firmware/test/cache-buster metadata to v0.4.18.

## Diagnostics

The repository ships:

- `tests/test_async_webserver.sh` — full AsyncWebServer release/regression test.
- `tests/test_async_transport_stress.sh` — focused read-only State/Stats/Diary transport stress test.
- `tests/test_ota_transport.sh` — non-flashing OTA transport diagnostic.

## Compatibility

All runtime behavior remains unchanged from v0.4.17.

The temperature-unit contract remains unchanged: internal values, `/api/state`, `/api/stats`, VPD/control logic and stored temperature targets remain Celsius-native; Fahrenheit is limited to dashboard presentation and input conversion.
