# Testing — v0.5.10

## Local plan status UI

`node tests/test_local_plan_ui.mjs` parses the shipped JavaScript and exercises its renderer with the production translation dictionary: German/English loading, disabled, ready, blocked, stale, missing metadata and failed-fetch states. It checks language/fetch hooks and firmware-versioned asset URLs. Full firmware compilation runs in CI. Browser/device update verification remains open.

## Local irrigation plan

`python tests/test_irrigation_plan.py` passes host policy/integration checks: civil dates and leap days, recurrence, no catch-up/grace boundary, already-claimed dates, drying/busy/invalid clock, Gen1 weekday rules, Gen2 weekday lists/ranges/names, ambiguous/unsupported schedules, replay saturation without eviction and DST calendar behavior. Wiring checks verify dry-phase/start ownership guards, factory reset, main-loop persistence and lifecycle hooks. Tests exercise the production policy header rather than a separate reimplementation.

The production JSON schedule parser is additionally compiled and executed with ArduinoJson after the CI build: enabled/disabled Gen1 rules, weekly ON/OFF times, Gen2/3 jobs, malformed/ambiguous/solar/paginated responses and switch-id filtering.

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


## v0.5.6 editor coverage

`node tests/test_local_plan_editor.mjs` exercises the shipped renderer/save functions with a mock DOM/HTTP transport: history display, no synthetic date, unsaved edits, revision conflicts, invalid bounds, persistence acknowledgement, stale polling responses, language and network errors. Contract checks cover queue/auth separation and absence of flash writes in Async callbacks. Full board builds and schedule parser tests run in CI. Hardware/reboot, simultaneous real backend writes and browser checks remain OPEN.

## v0.5.7 date/layout coverage

The editor test covers the native date input, unknown history, preserving unsaved dates, date bounds, full-width tile ordering, and confirmation/error states. The production C++ historyDateAllowed policy is tested for unknown, same-day, advancing, future, erased and backward history. Both request acceptance and main-loop apply use it. Hardware history import and restart checks remain OPEN.

## v0.5.10 backend integration

See IRRIGATION_PLAN_API.md for protocol v2 and signed idempotent manual starts. Command claims persist before starts; unknown expired commands cannot run. Full manual cycles use the backend event stream and require a valid clock and event capacity even with scheduling disabled. Real-device power-failure, pump/phase interlock and push integration validation remain OPEN.

## v0.5.12 next-run projection

Production policy tests cover daily/multi-day intervals, already handled dates, missed deadlines, weekly schedules, cross-midnight offsets and both Europe/Berlin DST transitions. Real-device timing/display verification remains open.
