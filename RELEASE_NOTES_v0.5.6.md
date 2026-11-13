# v0.5.6 — configure irrigation planning on the ESP

## Release summary

Configure the existing local irrigation plan directly in Operating Settings on GrowTent and GrowTent-S3, using the same persistent plan as the backend API.

## Changes

- Enable or disable local scheduling, choose an interval of 1–366 days, and set a delay of 0–1440 minutes after light-on from the Shelly scheduler.
- Display read-only **Last watering**, using the shared stored last-run date; no next-run forecast. Dates are localized for German and English.
- Save the plan separately with durable-apply confirmation. Preserve unsaved edits during polling and reject stale revisions instead of overwriting backend changes.
- Record dated manual watering starts even while local scheduling is disabled. Editing the plan never fabricates a watering date.
- Preserve automatic blocking in Drying, pump mapping, amounts/calibration, hourly/phase-change Shelly refresh and backend HMAC protection.

## Compatibility

The new `/api/settings/irrigation-plan` route belongs to the existing local dashboard trust boundary; it does not require or disclose the backend HMAC secret. Backend routes continue to require HMAC. Local and backend writes share one queue, journal and revision counter.

**Before enabling local scheduling, disable the backend's independent scheduler to prevent duplicate watering.** Backend event ingestion and push integration remain a separate task. The durable event outbox remains bounded and blocks further local runs when full until acknowledged.

Last watering is the stored start/claim date, including imported backend history; it does not certify successful completion or measure water delivery. Unknown history is shown as no watering recorded. Hardware watering, reboot and offline integration tests remain open. Use only firmware intended for your board.

## Validation

Host tests cover production JavaScript parsing, DE/EN rendering, editor bounds, dirty-state preservation, stale revisions, durable-save confirmation and failed requests. Irrigation policy, schedule parser and board-specific checks run in CI with full firmware builds. See TESTING.md.
