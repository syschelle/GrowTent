# GrowTent v0.2.24

## Build fix for Shelly Flood S Gen4 integration

This release fixes the compilation error introduced in v0.2.23 while keeping the Flood S Gen4 implementation unchanged.

### Fixed

- Fixed a name collision with Arduino's built-in `HEX` macro in the Flood webhook token generator.
- Renamed the internal hexadecimal lookup table to `HEX_CHARS`.
- Restores successful compilation of the Flood S Gen4 integration.

### Shelly Flood S Gen4 integration

- Event-driven webhook receiver for the Shelly Flood S Gen4.
- No polling loop and no additional FreeRTOS task.
- Dedicated per-controller webhook token.
- Separate webhook URLs for `Flood alarm` and `Flood alarm off`.
- Persistent last-known flood state so the sleeping sensor is not treated as offline.
- `/api/state` exposes only:
  - `sensors.flood.configured`
  - `sensors.flood.alarm`
  - `sensors.flood.status`
  - `sensors.flood.lastSeen`
- Flood sensor IP address and webhook token are not exposed through `/api/state`.
- Existing BME280, VPD, Shelly actuator, relay and watering tasks remain unchanged.

## Shelly configuration

After enabling the Flood webhook in **Shelly Einstellungen** and saving, copy the generated URLs into the Flood S Gen4 actions:

- `Flood alarm` → URL ending in `&alarm=1`
- `Flood alarm off` → URL ending in `&alarm=0`

The Flood S Gen4 may sleep normally between events.
