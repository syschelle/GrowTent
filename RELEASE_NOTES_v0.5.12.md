# v0.5.12 — report the next planned irrigation to the backend

## Changes

- Return next_run_at (Unix seconds or null) and next_run_status in the HMAC-protected plan response.
- Derive the next planned time on the ESP from its interval, historical/handled dates, cached weekday Shelly ON times and elapsed light-on offset.
- Honor midnight offsets, local DST changes and missed/claimed occurrences. Report clear reasons for disabled, Drying, clock, pump/Flood, running, buffer and schedule states.
- Keep the shortened irrigation settings hint from v0.5.11.

## Compatibility

Backend v0.355 displays this value for both classic ESP32 and ESP32-S3. This is a read-only forecast from the ESP's current plan/cache, not a promise of physical execution. Scheduling, HMAC protection and existing interlocks remain authoritative. No new automatic backend starts are introduced.
