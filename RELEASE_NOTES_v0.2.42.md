# GrowTent v0.2.42

## Flood start interlock and API irrigation notification

- Extended the existing optional **Stop active irrigation on Flood alarm** safety setting.
- When this option is enabled and the configured Flood S Gen4 has a known active Flood alarm, GrowTent now blocks every new automatic irrigation start until the Flood alarm is cleared.
- The start interlock applies to both:
  - the dashboard irrigation start/toggle, and
  - `POST /startWatering` API starts.
- A blocked start does not enable any pump, leaves irrigation runs at `0`, and logs the source (`api` or `dashboard`).
- API-originated irrigation starts now state the source explicitly in Pushover:
  - German: `Bewässerung über API gestartet`
  - English: `Irrigation started via API`
- Dashboard-originated Pushover wording remains unchanged.
- The existing Flood webhook stop behavior remains unchanged: an active cycle is still stopped immediately when a Flood alarm arrives and the safety option is enabled.
- No additional FreeRTOS task or Flood polling was added.
- BME280, DS18B20, ultrasonic tank measurement, Shelly polling, relay scheduling and irrigation volume calculations are unchanged.
- Updated web asset cache-busting to v0.2.42.
