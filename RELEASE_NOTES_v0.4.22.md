# GrowTent v0.4.22

## Release summary

GrowTent v0.4.22 redesigns the Operating Settings page for better use of desktop space and clearer grouping while preserving the existing controls, API contracts and runtime behavior. The visual update deliberately stays within the existing GrowTent dark-blue/neutral palette instead of introducing additional decorative colors.

## Changes

- Reorganizes Operating Settings into compact, clearly separated sections for Grow/Phase, Temperature, VPD, Irrigation, Heating and Relay Scheduling.
- Uses a responsive two-column desktop layout where appropriate and collapses to a single-column layout on smaller screens.
- Moves the kWh reset action into the page header and keeps the primary Save action clearly separated at the bottom.
- Groups irrigation pump enable flags and the Flood-abort option into compact blocks.
- Places irrigation timing, calibration, amount and tank parameters into a responsive parameter grid.
- Presents relay scheduling as a compact table on desktop and card-like rows on narrow screens.
- Keeps the existing field IDs, names, ranges and save logic so runtime behavior remains compatible.
- Keeps 4-/8-relay visibility behavior intact and extends it to the separated irrigation-parameter panel.
- Retains the v0.4.21 page-aware polling behavior; configuration pages do not gain background polling.
- Advances firmware and static-asset cache-buster metadata to v0.4.22.

## Compatibility

- No API contract changes.
- No climate-control, relay, irrigation, Shelly runtime-control or OTA behavior changes.
- No changes to the Celsius-native internal/API temperature contract.
- The Shelly Flood webhook token remains intentionally visible where required for webhook configuration.
- Notification and Shelly credentials remain write-only in the dashboard as introduced in v0.4.20.

## Validation

Static HTML/JavaScript/shell checks are included in the package. Real-hardware build, flash and regression testing are still required before release tagging.
