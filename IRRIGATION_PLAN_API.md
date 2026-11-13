# Local irrigation plan — firmware API v1 (v0.5.4)

This change implements **firmware only** for both `syschelle/GrowTent` and `syschelle/GrowTent-S3`. The backend is unchanged and will be adapted separately. The local plan is disabled by default. Existing irrigation settings (shared volume/calibration, pulse duration, pause, enabled pumps) remain authoritative; the new plan does not introduce per-pump volume fields.

## Ownership and safety

Once an authenticated plan is enabled, the ESP owns automatic execution. Old backend API start paths (`/startWatering`, `/api/watering/start` and API start/toggle actions) cannot start another automatic cycle; their queued operation fails with `local_plan_owns_schedule`. A 202 is an acceptance receipt, never evidence that a pump switched. The local dashboard remains available for manual watering outside drying.

Phase **3 (drying)** blocks every complete watering-cycle start, discards the applicable day's pending schedule, and stops a running automatic/manual cycle. The watering task checks phase/stop in at most 100 ms delay slices. Dedicated 10-second pump calibration tests remain available during drying. Existing disabled-pump, Flood, no-overlap and hardware-layout checks still apply. GPIO ON/OFF is verified before a managed cycle can be reported completed.

A local civil date (the date of the Shelly light-on occurrence) is durably claimed **before** starting a scheduled cycle. Plan revisions cannot roll back that claim. Reboots never repeat an interrupted occurrence; recovery records `aborted/restart_interrupted`. Missed deadlines are recorded and never caught up. The execution window is the first 60 seconds after due time, accommodating normal loop/network jitter. A busy/blocked occurrence is recorded as missed, not postponed.

## Shelly time source

The ESP reads the configured light Shelly on startup, on grow-phase changes, and once per hour. Phase changes invalidate any in-flight response from an older generation. The Shelly task performs regular fetches; failed HTTP/JSON/unsupported schedules preserve the last valid same-device cache and expose `schedule_stale`. Switching to a different IP clears the old device cache. An explicitly empty/disabled schedule cancels future starts.

- Gen1: `/settings/relay/0`, enabled `schedule_rules`, fixed HHMM and active weekday digits 0=Sunday..6=Saturday.
- Gen2/3: `/rpc/Schedule.List`, enabled `Switch.Set` calls with `params.id=0`. Fixed second=0, numeric hour/minute, daily day/month wildcards; weekday names, numeric lists and ranges are supported.
- Multiple distinct ON times for one weekday, solar/step/calendar expressions, incomplete/paginated responses and invalid values are not guessed. The cache stays stale rather than silently selecting one job.
- Device/ESP timezones must agree. Delay is elapsed minutes after the scheduled light-on occurrence. Calendar-day recurrence is independent of 23/25-hour DST days. Delays crossing midnight use the preceding light-on date.
- Invalid ESP wall clock triggers nonblocking NTP synchronization (at most once per minute) and blocks scheduled starts until valid. A free-running valid clock continues during a backend outage.

The signed status exposes the cached Monday..Sunday ON minutes (-1=no ON occurrence), last successful fetch epoch and stale flag. The status page also displays non-secret readiness, sync time and queue count via the existing `/api/state` metadata fields `irrigation.plan.*`.

## Authentication (all backend endpoints)

TCP/8080, same write-only 64-character hex key, backend-enabled setting and exact allowed source IPv4 as the existing backend write API. No secret is exposed in a response, state, HTML or log. New paths do not widen the five-field `/api/backend/grow-profile` endpoint.

Request headers:

- `X-GrowTent-Timestamp`: Unix seconds, within ±60 s.
- `X-GrowTent-Nonce`: fresh 16 random bytes encoded as 32 lowercase hex characters.
- `X-GrowTent-Signature`: HMAC-SHA256, 64 lowercase hex characters, using **hex-decoded** key bytes.

Canonical request (no trailing newline):

```text
METHOD
/PATH
TIMESTAMP
NONCE
SHA256_HEX(EXACT_RAW_BODY_BYTES)
```

GET uses an empty body. Query parameters are rejected; the complete protected path is signed. Only JSON POST bodies up to 1024 bytes are accepted. HMAC is checked before queuing. A separate 32-entry replay window retains all live nonces for 121 s and returns 429 when full instead of evicting an unexpired nonce. Poll at 15 s or slower, and ACK batches rather than individual events. After a reboot, revision/occurrence/event-sequence persistence also prevents old mutations from repeating work.

Authenticated responses, including validation errors after authentication, carry the same three headers. The response nonce echoes the request nonce, the response timestamp is freshly generated and the canonical form is:

```text
RESPONSE
REQUEST_METHOD
/PATH
HTTP_STATUS_CODE
RESPONSE_TIMESTAMP
REQUEST_NONCE
SHA256_HEX(EXACT_RESPONSE_BODY_BYTES)
```

The backend must verify signature, request nonce binding and response timestamp **before** accepting results. Authentication failures are unsigned error responses and must never be interpreted as a successful transfer. HMAC is authentication/integrity, not encryption.

## Transfer the plan

`POST /api/backend/irrigation-plan`

```json
{
  "revision": 1,
  "enabled": true,
  "every_n_days": 2,
  "offset_after_light_on_min": 60,
  "last_run_date": "2026-11-13"
}
```

The four existing backend plan fields are retained. `last_run_date` is required and may be null. A null initial baseline means the preceding local date. Non-null dates must be valid and not in the future. The local execution ledger never moves backwards, even if a later revision supplies an older last-run date. `revision` is a positive increasing uint32; identical retries with the same revision return `unchanged`, conflicting or older revisions return 409. `every_n_days`: 1..366; delay: 0..1440 minutes. Unknown fields, incomplete bodies and changes while watering are rejected. No plan upload is allowed until NTP time is valid.

202 response returns `requestId`; this only means queued. Poll signed `GET /api/backend/irrigation-plan` until `last_applied_request` reaches that ID and the expected durable revision/configuration appears. `last_failed_request`/`last_error` report application failure. Authentication configuration changes between queuing and application invalidate the queued operation. NVS writes, scheduler state and GPIO changes are owned by the main loop, not Async callbacks.

The namespace `gt_irrig_v1` stores one verified atomic JSON blob containing configuration, cache, execution claim, event stream and sequence. Storage failure/corruption fails closed and is visible in public status. Factory reset resets this namespace before restarting. NVS/flash wear and power-failure behavior require hardware tests.

## Events and backend notifications

`GET /api/backend/irrigation-events` returns the oldest eight unacknowledged events, `stream_id` and `has_more`. Do not use a query cursor. Each event includes a monotonically increasing `seq`, `type`, `reason`, original `occurred_at` (null if the clock was invalid during restart recovery), `scheduled_at`, `count`, plan `revision`, `phase` and `run_id`.

Types: `started`, `completed`, `aborted`, `missed`.

`started/local_schedule_accepted` means the durable local cycle has been accepted for execution; it is not a physical water-flow measurement. Failure to energize/de-energize a pump produces `aborted`, never `completed`. Missed dates during longer downtime are summarized with a count; no GPIO catch-up is performed. Drying cancellations are intentionally silent, because watering is disabled in that phase.

The ESP sends **no direct Pushover/Gotify notification for locally scheduled cycles**. The future backend integration produces start, successful-end and missed notifications from this event stream. Aborted cycles must be labelled as aborted. Existing manual/legacy notification behavior is retained outside managed cycles.

The ESP keeps up to 64 events without overwriting unacknowledged entries. It reserves space for start/end and refuses new scheduled starts when that space is exhausted. This bounded offline horizon is reported as `event_queue_full`; the backend should alert before capacity is exhausted. It is not unlimited offline storage.

Only after durably ingesting events, the backend sends:

`POST /api/backend/irrigation-events/ack`

```json
{"stream_id":"COPY_FROM_RESPONSE","through_seq":8}
```

The ACK is signed and queued; poll plan status for application confirmation. The stream identity must match and the sequence cannot exceed the generated stream. Deduplicate backend records by `(controller, stream_id, seq)`. Original event times stay unchanged; transmission uses a fresh HMAC timestamp. Reliable event ingestion and notification deduplication belong to the later backend change; firmware alone cannot guarantee exactly-once delivery to an external push service.

## Backend cutover (later, not included)

1. Persist events idempotently and verify every response HMAC.
2. Read existing backend plan and last-run date; stop backend-side automatic triggering for this controller.
3. Upload the versioned plan and wait for durable application confirmation.
4. Poll/ingest/ACK events and send notifications; show stale scheduler/clock/storage/queue warnings.
5. Never enable concurrent backend and ESP scheduling. A rollback must explicitly disable the local plan and confirm it before restoring backend scheduling.
