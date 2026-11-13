# Testing — v0.5.4

## Local irrigation plan

`python tests/test_irrigation_plan.py` passes host policy/integration checks: civil dates and leap days, recurrence, no catch-up/grace boundary, already-claimed dates, drying/busy/invalid clock, Gen1 weekday rules, Gen2 weekday lists/ranges/names, ambiguous/unsupported schedules, replay saturation without eviction and DST calendar behavior. Wiring checks verify dry-phase/start ownership guards, factory reset, main-loop persistence and lifecycle hooks. Tests exercise the production policy header rather than a separate reimplementation.

CI also runs the actual full board build. See current pull-request checks for the authoritative compile result. Existing S3 host mapping tests, where present, remain required.

### Hardware and integration gates (OPEN)

- Signed raw-byte request/response canonicalization with real backend client; wrong key/IP/timestamp/nonce, replay saturation, revision retries and ACK validation.
- NVS power cut before/after occurrence claim, queued update and ACK; restart-interrupted event, no duplicate start; full/corrupt flash fails closed.
- Gen1/2/3 real-device schedules, weekly differences, enabled/empty/unsupported schedules, same-IP configuration edits, device replacement and timeout/cache behavior.
- Phase change from dashboard and backend, same-loop start conflict, drying during pulse/pause, manual cycle denial and allowed maintenance pulse.
- NTP missing/recovery, timezone agreement, midnight/DST delay, missed days and backward clock changes without catch-up.
- Event queue fills during backend outage; no unacknowledged loss, reserved finish space, no false completion, backend deduplication/notification behavior after reconnection.
- Board relay ON/OFF mapping, disabled pumps, overlap, Flood, OTA interruption and rollback to the previous firmware with local scheduling explicitly disabled.

No backend code was changed; end-to-end event ingestion and push delivery cannot be validated until its later integration.

## Prior checks and board reference

