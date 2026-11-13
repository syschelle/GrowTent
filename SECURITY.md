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
