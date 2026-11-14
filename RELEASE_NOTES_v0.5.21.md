# GrowTent v0.5.21

## Changes

- Keep pump Auto-Off and stop requests armed after a relay-lock timeout or an unconfirmed GPIO OFF; retry until the output reads OFF.
- Confirm pump shutdown before acknowledging a watering stop or releasing the watering task/platform for exclusive OTA.
- Reject invalid climate samples immediately and expire otherwise valid climate data after 30 seconds without a new valid BME measurement.
- Block heater/humidifier ON commands while climate data is unavailable, invalid or stale. Retry the local heater OFF from the main loop and send rate-limited Shelly heater/humidifier OFF requests from the existing Shelly worker.
- Publish unavailable climate values as JSON null, with sensors.climate.fresh, sensors.climate.status and sensors.climate.ageMs diagnostics. Resume normal control after valid measurements return.

## Compatibility

- Retain board-specific pump mapping, S3 pump isolation, irrigation scheduling and Watering-task diagnostic cadence.
- No changes to TLS settings, the VPD smoothing algorithm, Shelly status caching, backend protocol or existing ventilation settings.
- Healthy operation adds no Shelly polling. During a climate fault, OFF requests are limited to one per affected device per 10 seconds; unreachable devices cannot be physically switched by software alone.
- GPIO readback confirms the commanded ESP pin level, not mechanical relay contacts or water flow. Timer retries are software-driven and depend on task/main-loop progress.

## Validation

Production-source host tests inject relay lock timeouts, refused GPIO writes, expired manual pulses, stop retries, stale/invalid/absent measurements, sensor recovery and millis rollover. Tests also cover heater ON gating, local OFF retries and fault-only Shelly OFF backoff/recovery. Existing plan/fan/UI/S3 contracts and full firmware/layout CI remain required. No physical relay, BME sensor, live Shelly or push provider is contacted by automated tests.

Hardware validation remains open: disconnect the BME during heating/humidification, verify OFF/recovery and null API/UI readings; verify retained pump OFF requests under contention; test OTA with pump outputs active and an unreachable Shelly. Perform actuator tests with pumps/heaters disconnected from their loads first.
