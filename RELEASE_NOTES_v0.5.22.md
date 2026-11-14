# GrowTent v0.5.22

## Changes

- Stop treating last-known Shelly readings as current after a failed status request; retain their age internally but publish unavailable switch/power/energy/cost values as JSON null.
- Expire unrefreshed status after 60 seconds. Controllers consume the same fresh snapshots as the API; switch acknowledgements never renew telemetry age or revive a failed status.
- Fence status reads with a configuration epoch and per-device request sequence. IP/generation/credential edits clear both caches and retry backoff; delayed responses, including A-to-B-to-A changes and out-of-order reads, cannot restore old values.
- Keep the existing staggered polling and 30-second failure backoff. The minimum-VPD controller waits for the polling worker rather than bypassing backoff with direct retries.
- Show unknown Shelly states with a neutral indicator and German/English Unknown text rather than a false OFF state. Add per-device fresh/ageMs diagnostics.
- Encode every Pushover form value and the Gotify query token byte-for-byte. Serialize Gotify JSON with safely quoted text, including quotes, backslashes, UTF-8, line breaks and all JSON control bytes.

## Compatibility

- TLS settings, VPD smoothing, hourly schedule/Auto-Off configuration refresh, pump mapping, irrigation and the v0.5.21 actuator/sensor safeguards are retained.
- No backend, endpoint, credential storage or push-provider configuration changes. Existing API measurement keys can now be null while stale/unavailable; fresh/ageMs fields are additive.
- Healthy Shelly polling frequency is unchanged. Push logs remain limited to status and response length; no credentials, raw bodies or token-bearing URLs are logged.

## Validation

Production-source host tests cover fresh/failed/expired readings, cache invalidation, IP/credential/ABA edits during HTTP, out-of-order responses, generation discovery, empty slots, mutex failure and retry rollover. Receiver-side tests execute the actual notification senders against HTTP stubs and decode form/JSON/query data, including UTF-8 and control bytes. UI tests check unknown/ON/OFF for Gen1/2/3 and German/English. Full firmware, existing safety/plan/fan/S3 tests and browser/layout CI remain required. No live Shelly or push service is contacted.

Hardware validation remains open: disconnect/reconnect a Shelly, change its IP/credentials while polling, verify null/unknown state and recovery, and send a test notification with quotes, umlauts and line breaks.
