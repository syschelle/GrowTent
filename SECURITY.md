# Security — v0.5.5

v0.5.5 fixes status localization and asset cache versioning. Only assets requested with the matching firmware version receive immutable caching; missing or obsolete versions use no-store. Irrigation/HMAC behavior is unchanged.

The new local-plan/event APIs require the existing allowed source IPv4, enabled backend-write configuration, raw-body HMAC-SHA256, ±60-second timestamp and nonce checks. Responses are signed and bound to request method/path/nonce. A full replay window returns 429 rather than evicting live entries. GET query parameters are rejected. No signing keys are exposed in UI, logs or state.

All new HTTP mutations are queued; main-loop code verifies NVS persistence before scheduled starts and claims occurrences durably. The event queue never overwrites unacknowledged records. HMAC provides no encryption; existing general local UI/control routes are outside this HMAC channel. The local plan is disabled by default, and backend cutover is a separate, required change.

See [IRRIGATION_PLAN_API.md](IRRIGATION_PLAN_API.md) for the complete protocol and failure behavior. Real-device power-failure, timing, HTTP/HMAC and push-provider tests remain open; host tests do not establish physical safety or exactly-once push delivery.

## Existing security context

# Security
