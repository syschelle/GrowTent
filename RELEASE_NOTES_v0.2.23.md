# GrowTent v0.2.23

## Shelly Flood S Gen4 integration

- Adds an event-driven webhook receiver for the Shelly Flood S Gen4.
- No polling loop and no additional FreeRTOS task are introduced.
- Adds a dedicated per-controller webhook token.
- The Shelly settings page shows the two URL actions for `Flood alarm` and `Flood alarm off`.
- Persists the last known flood state so the sensor may sleep without being treated as offline.
- Extends `/api/state` with:
  - `sensors.flood.configured`
  - `sensors.flood.alarm`
  - `sensors.flood.status`
  - `sensors.flood.lastSeen`
- The Flood sensor IP address and webhook token are not exposed in `/api/state`.
- Existing BME280, VPD, Shelly actuator, relay and watering tasks remain unchanged.

## Shelly configuration

After enabling the Flood webhook in **Shelly Einstellungen** and saving, copy the generated URLs into the Flood S Gen4 actions:

- `Flood alarm` → URL ending in `&alarm=1`
- `Flood alarm off` → URL ending in `&alarm=0`

The Flood S Gen4 may sleep normally between events.
