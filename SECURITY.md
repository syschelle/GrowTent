# Security — v0.5.10

v0.5.5 fixes status localization and asset cache versioning. Only assets requested with the matching firmware version receive immutable caching; missing or obsolete versions use no-store. Irrigation/HMAC behavior is unchanged.

The new local-plan/event APIs require the existing allowed source IPv4, enabled backend-write configuration, raw-body HMAC-SHA256, ±60-second timestamp and nonce checks. Responses are signed and bound to request method/path/nonce. A full replay window returns 429 rather than evicting live entries. GET query parameters are rejected. No signing keys are exposed in UI, logs or state.

All new HTTP mutations are queued; main-loop code verifies NVS persistence before scheduled starts and claims occurrences durably. The event queue never overwrites unacknowledged records. HMAC provides no encryption; existing general local UI/control routes are outside this HMAC channel. The local plan is disabled by default, and backend cutover is a separate, required change.

See [IRRIGATION_PLAN_API.md](IRRIGATION_PLAN_API.md) for the complete protocol and failure behavior. Real-device power-failure, timing, HTTP/HMAC and push-provider tests remain open; host tests do not establish physical safety or exactly-once push delivery.

## Existing security context

# Security

## v0.5.6 local editor

GET/POST `/api/settings/irrigation-plan` uses the same trusted-LAN dashboard boundary as existing operating-setting routes, not backend HMAC authentication. Do not expose the dashboard directly to untrusted networks. POST accepts JSON with expected_revision, enabled, every_n_days and offset_after_light_on_min only. v0.5.7 accepts an optional last_run_date baseline: no future, invalid, cleared or backward dates. Rechecked on the main loop before persistence. Local and authenticated backend mutations share one queue and NVS journal; revisions prevent stale dashboard overwrites. NVS writes remain on the main loop, with persistence confirmed before reporting applied. No keys are returned. Enabling local scheduling requires a valid clock; independent backend scheduling must be disabled.

## v0.5.10 backend integration

See IRRIGATION_PLAN_API.md for protocol v2 and signed idempotent manual starts. Command claims persist before starts; unknown expired commands cannot run. Full manual cycles use the backend event stream and require a valid clock and event capacity even with scheduling disabled. Real-device power-failure, pump/phase interlock and push integration validation remain OPEN.

## v0.5.14 humidifier fan coupling

The new local dashboard GET/POST `/api/settings/humidifier-fan` endpoint shares the existing trusted-LAN boundary and is not a backend HMAC route. JSON-only mutations validate a single relay field, exclude pumps and the active local heater, queue application to the main loop and verify NVS persistence before reporting applied. No Shelly credentials are exposed. Only successful, parsed ESP switch acknowledgements can start the fan timer; an unrecognized acknowledgement cannot turn it on. Responses from an old configuration and older concurrent commands are fenced out. Minutes schedules and manual relay actions cannot override the owned relay.

The cache is refreshed at startup/assignment and hourly, including hourly retry after failures. A failed refresh retains a same-device last known duration and marks it stale. Changing the Shelly host/generation/credentials invalidates it. Unknown/disabled Auto-Off keeps the fan off. Active timers are not extended by cache refresh. Commands use a monotonic bounded deadline from HTTP request start. Reboot does not restore an active fan. Direct Shelly commands or timer edits between refreshes are outside this coupling; it is not a physical state mirror or hard real-time safety mechanism. Hardware validation remains open.

## v0.5.15 schedule UI lock

The UI disables scheduling controls only for the confirmed fan relay and skips that row in the general save. This is a presentation constraint; existing firmware ownership enforcement remains authoritative. Failed status requests do not release the previous lock. No new API or authentication boundary is introduced.

## v0.5.16 Shelly settings layout

This release changes presentation, labels and form nesting only. Existing local dashboard access boundaries, write-only password handling, generation detection and Flood token rules remain in place. No credentials are introduced into screenshots or tests; all fixtures are synthetic.

## v0.5.17 compact Shelly layout

Expandable credentials and Flood sections change presentation only; collapsed controls still participate in the existing save operation. No new endpoints, authentication or network requests are introduced. Password and Flood token handling are unchanged.

## v0.5.18 always-visible sections

Authentication and Flood S Gen4 are now static sections. Password masking, write-only retention behavior, Flood token rules and dashboard access boundaries remain unchanged. No new endpoints or network requests are introduced.

## v0.5.19 operating-settings presentation

Only layout spacing and explanatory text change. Drying protection, fan Auto-Off validation/caching, pump interlocks and all access boundaries remain unchanged. Technical fan behavior and limitations remain documented in HUMIDIFIER_FAN.md.

## v0.5.20 Push settings presentation

The provider form layout changes without altering access boundaries, secret retention, save/status endpoints, notification delivery or restart logic. Credential inputs retain password masking and new-password autocomplete. Browser regression fixtures are synthetic and never contact real notification providers.

## v0.5.21 actuator safety and sensor freshness

This release retains pump OFF retry state after failed GPIO/mutex operations and gates stop/OTA acknowledgements on GPIO OFF. Climate validity is shared through atomic state, expires after 30 seconds and blocks heater/humidifier ON dispatch while invalid. The main loop handles local heater OFF retries independently of the BME task; the existing Shelly worker performs rate-limited fault-only OFF requests. An unreachable remote actuator or stalled main loop cannot be made fail-safe by software alone; hardware/load and outage tests remain open. No new write endpoint, credential exposure, TLS change or HMAC/protocol change is introduced.

## v0.5.22 status-cache integrity and notification payloads

Status snapshots are bound to configuration epochs and request sequences under a short cache mutex; network I/O occurs outside that lock. Failed, expired or superseded status data cannot be reused as fresh control/API values. Credentials are copied only for the request lifetime and are not logged. Form/query components use percent encoding; Gotify text is JSON quoted (including control bytes) before object serialization. Provider logs remain status/length-only. Existing authentication, HMAC and TLS policy are unchanged; no new write endpoint is introduced. Real-device/provider validation remains open.

## v0.5.23 VPD state isolation

Raw VPD telemetry cannot mutate the control VPD filter. Nonfinite inputs do not advance that filter; existing climate freshness and actuator safety gates remain in place. No authentication or API protocol changes.

## v0.5.24 system settings layout

This release reorganizes existing controls only. HMAC secrets remain password fields with empty-input retention, and the existing validated save/ACK/restart sequence is unchanged. The external timezone lookup opens with noopener/noreferrer. No authentication, GPIO or control-policy changes.
