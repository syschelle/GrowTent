# GrowTent v0.4.23

## Release summary

GrowTent v0.4.23 refines the compact Operating Settings layout introduced in v0.4.22. The Current Phase selector is moved out of the Grow card and into the page header, immediately to the left of the kWh reset action, making the Grow section easier to scan.

## Changes

- Moves the Current Phase selector into the Operating Settings page header.
- Places Current Phase directly to the left of the kWh reset action on desktop layouts.
- Simplifies the former Grow / Phase card so it contains only the grow, flowering and drying date fields.
- Renames the card to Grow periods / Grow-Zeiträume to match its reduced scope.
- Keeps the phase selector responsive and stacks the header controls cleanly on narrow screens.
- Keeps all existing field IDs, form names, values and save behavior unchanged.
- Retains the page-aware polling behavior introduced in v0.4.21.
- Advances firmware and static-asset cache-buster metadata to v0.4.23.

## Compatibility

- No API contract changes.
- No climate-control, relay, irrigation, Shelly runtime-control or OTA behavior changes.
- No changes to the Celsius-native internal/API temperature contract.
- Notification and Shelly credentials remain write-only in the dashboard, while the Flood webhook token remains intentionally visible where required.

## Validation

Static HTML/JavaScript/shell checks are included in the package. Real-hardware build, flash and regression testing are still required before release tagging.
