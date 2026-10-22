# GrowTent v0.2.30

## Flood S Gen4 status and telemetry

- Added a compact Flood S Gen4 status badge to the header when Flood integration is enabled.
- Header states are `dry`, `water` and `unknown`, with battery percentage and last report time when available.
- Added `sensors.flood.batteryPct` to `/api/state`.
- Flood webhook URLs now include Shelly URL token replacements for battery percentage and the Shelly device Unix timestamp.
- Flood timestamps are stored as UTC ISO-8601 and rendered in the browser's local timezone for a cleaner, unambiguous display.
- Battery percentage is persisted only when it actually changes.
- No Flood polling and no additional FreeRTOS task were added.

## Important after updating

Open **Shelly Settings** in GrowTent and copy the two newly generated Flood webhook URLs into the Flood S Gen4 Actions again. The new URLs contain the battery and timestamp placeholders. Until the Flood sends an event with one of these updated URLs, `sensors.flood.batteryPct` will remain `null`.
