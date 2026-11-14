# Testing — v0.5.21

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

## v0.5.13 local next-run UI

Production JS tests cover the shared date/time output on settings/status, German/English reasons and unavailable data. Existing policy, parser, layout and firmware-build checks remain required. No live device/pump test is claimed.

## Humidifier fan coupling (v0.5.14)

`python tests/test_humidifier_fan.py` exercises the production timer policy (failed commands, repeated ON, explicit OFF, disabled/unknown duration, late responses, hourly refresh, millis rollover and duration bounds) and integration boundaries. `node tests/test_humidifier_fan_ui.mjs` exercises the shipped editor: available relays, DE/EN status, dirty selection, confirmed/rejected saves, unavailable status and cached values. Existing irrigation policy/editor/UI checks remain required. CI builds the full firmware for this board.

Hardware validation remains OPEN: authenticated Gen1/Gen2/Gen3 configuration responses, fan GPIO deadlines under network load, hourly cache refresh, unreachable Shelly with cached values, reboot/OTA, reassignment, timer changes and physical relay behavior. No hardware test or production installation is claimed.

## v0.5.15 schedule UI lock

The fan editor runtime test also verifies confirmed relay ownership, dirty/unconfirmed selection, reassignment, DE/EN notes, restoration of pre-existing disabled states and schedule values, and removal of the lock. The general settings save skips the owned row. Existing policy, editor and firmware/layout checks remain required. Device verification remains open.

## Shelly settings cards (v0.5.16)

`node tests/test_shelly_settings.mjs` exercises the production save handler, password omission/replacement and light schedule dirty handling. `node tests/test_shelly_layout.cjs` checks DE/EN cards, field labels, form membership, actions, mobile/desktop overflow and generates previews in CI. Full firmware and existing regression checks remain required. Device verification remains open.

## v0.5.17 compact Shelly layout

The Shelly browser regression now checks seven aligned device rows, separate light-plan fields, closed/open credentials and Flood details, valid labels/form membership and no overflow in German/English at desktop/tablet/mobile widths. Screenshot fixtures contain no live credentials. Production save tests still verify password retention/replacement, schedule dirty handling and Flood fields. Hardware verification remains open.

## v0.5.18 always-visible sections

The Shelly layout test rejects details/summary controls, requires Authentication and Flood S Gen4 to be regular sections, and verifies that all credential/Flood fields and webhook links are visible and enabled without clicks in DE/EN at 1640/1000/800/390px. Existing alignment, form, overflow, save and firmware checks remain required. Device verification remains open.

## v0.5.19 compact operating settings

The irrigation browser check measures the gaps after date inputs, before the drying note and before Save at 1640/1000/390px. It verifies the empty save-status row is hidden, nonempty feedback remains visible and the removed fan explanation is absent. Existing production plan/fan/save tests and full builds remain required. Device verification remains open.

## v0.5.20 Push settings

`node tests/test_push_settings_layout.cjs` renders production DE/EN fields at desktop/tablet/mobile widths and verifies permanent sections, associated labels, form membership, password masking, responsive alignment and no overflow. It executes the production submit listener with browser FormData and synthetic HTTP responses, checking provider flags/fields, status confirmation and recovery from save failure. No real notification is sent. Full firmware and existing checks remain required; live provider delivery and device validation remain open.

## v0.5.21 pump shutdown and climate validity

Run `python tests/test_safety_runtime.py`. It compiles the actual relay/pump/sample functions plus production climate_safety.cpp against fault-injecting GPIO/FreeRTOS/BME/WiFi stubs, with -Wall -Wextra -Werror. Cases: mutex timeout retains a due pulse; GPIO write refusal retains OFF retry; explicit stop arms an immediate retry; later successful OFF clears the timer; millis rollover; no/invalid/infinite/out-of-range-humidity measurements; sample recovery; 30-second expiry; heater ON denial and local OFF retry; fresh-operation silence, fault-only Shelly OFF requests/backoff, network outage and recovery. Wiring checks cover watering-stop/OTA confirmation, worker placement, registry null/freshness data and early independent safety ticks. CI builds both the new module and each board's existing production paths; S3 GPIO contract tests remain required.

Live hardware checks remain OPEN: BME disconnection/recovery with local/Shelly heating and humidification; delay under actual network load; relay-lock contention and GPIO OFF feedback; OTA interlocks with active pumps; unreachable Shelly handling. Verify with disconnected loads first. Software GPIO readback is not mechanical relay or water-flow feedback. No TLS, VPD smoothing or Shelly-cache change is included in this release.
