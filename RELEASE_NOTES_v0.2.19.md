## v0.2.19 – Selectable irrigation pumps

### Changed
- Added individual enable/disable switches for Pump 1, Pump 2 and Pump 3 in **Operating Settings → Irrigation settings**.
- All three pumps remain enabled by default to preserve the behavior of existing installations.
- Disabled pumps are skipped during automatic watering cycles.
- Disabled pumps cannot be started through the manual 10-second pump test.
- Watering cannot start when all irrigation pumps are disabled.
- The estimated watering duration now uses only the pumps that are currently enabled.
- Pump enable states are persisted in NVS and exposed through `/api/state`.
