# GrowTent v0.2.37

## Passive Shelly heating-mat monitoring

- Added a new **Shelly IP address for heating mat** field in **Shelly Settings**, directly below the exhaust configuration and above the Flood S Gen4 section.
- Added generation selection for the heating-mat Shelly (Gen1 / Gen2 / Gen3).
- The heating-mat Shelly is **monitoring only**. GrowTent does not expose a toggle, schedule, automatic control path, or relay action for this device.
- The existing Shelly status task polls the heating mat only when an IP address is configured. An empty IP is skipped without network requests or log noise.
- When configured, the state API exposes the following additional tags:
  - `settings.shelly.heatmat.ip`
  - `settings.shelly.heatmat.gen`
  - `settings.shelly.heatmat.line`
  - `cur.shelly.heatmat.isOn`
  - `cur.shelly.heatmat.Watt`
  - `cur.shelly.heatmat.Wh`
  - `cur.shelly.heatmat.Cost`
- When no heating-mat IP is configured, these `heatmat` tags are omitted from the state payload entirely.
- Existing grow control, heater control, irrigation, Flood S Gen4, relay scheduling, OTA and connectivity diagnostics are unchanged.
