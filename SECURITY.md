# Security — v0.5.7

v0.5.5 fixes status localization and asset cache versioning. Only assets requested with the matching firmware version receive immutable caching; missing or obsolete versions use no-store. Irrigation/HMAC behavior is unchanged.

The new local-plan/event APIs require the existing allowed source IPv4, enabled backend-write configuration, raw-body HMAC-SHA256, ±60-second timestamp and nonce checks. Responses are signed and bound to request method/path/nonce. A full replay window returns 429 rather than evicting live entries. GET query parameters are rejected. No signing keys are exposed in UI, logs or state.

All new HTTP mutations are queued; main-loop code verifies NVS persistence before scheduled starts and claims occurrences durably. The event queue never overwrites unacknowledged records. HMAC provides no encryption; existing general local UI/control routes are outside this HMAC channel. The local plan is disabled by default, and backend cutover is a separate, required change.

See [IRRIGATION_PLAN_API.md](IRRIGATION_PLAN_API.md) for the complete protocol and failure behavior. Real-device power-failure, timing, HTTP/HMAC and push-provider tests remain open; host tests do not establish physical safety or exactly-once push delivery.

## Existing security context

# Security

## v0.5.6 local editor

GET/POST `/api/settings/irrigation-plan` uses the same trusted-LAN dashboard boundary as existing operating-setting routes, not backend HMAC authentication. Do not expose the dashboard directly to untrusted networks. POST accepts JSON with expected_revision, enabled, every_n_days and offset_after_light_on_min only. v0.5.7 accepts an optional last_run_date baseline: no future, invalid, cleared or backward dates. Rechecked on the main loop before persistence. Local and authenticated backend mutations share one queue and NVS journal; revisions prevent stale dashboard overwrites. NVS writes remain on the main loop, with persistence confirmed before reporting applied. No keys are returned. Enabling local scheduling requires a valid clock; independent backend scheduling must be disabled.
