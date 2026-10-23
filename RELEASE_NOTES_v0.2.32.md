# GrowTent v0.2.32

## Wi-Fi and backend connectivity diagnostics

- Pinned the PlatformIO ESP32 platform to `espressif32@6.11.0`. Local builds and GitHub release builds now use the same ESP32/Arduino runtime instead of silently moving to a newer platform in CI.
- Added Wi-Fi link transition logging. Short disconnects that were previously invisible now produce `link lost` and `link restored` entries with downtime, RSSI and disconnect count.
- Added explicit logs for normal reconnect attempts and hard reconnect attempts.
- Added a main-loop responsiveness watchdog. Gaps of at least 1 second are logged because the synchronous web server cannot service backend requests during such a gap.
- Added slow `/api/state` diagnostics for responses taking 250 ms or more. Normal backend polling is not logged, avoiding log spam.
- Replaced ambiguous Shelly `JSON parse error: EmptyInput` messages with explicit empty-response diagnostics where applicable.

No changes were made to VPD control, temperature control, irrigation, relay schedules or Flood S Gen4 event handling.
