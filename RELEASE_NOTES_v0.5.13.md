# v0.5.13 — show next irrigation on the ESP

## Changes

- Display Next irrigation with date and time in Operating Settings and on the Status page, in German and English.
- Expose irrigation.plan.nextRunAt and irrigation.plan.nextRunStatus through /api/state, using the same read-only ESP projection as the signed backend response.
- Label disabled, Drying, missing/stale schedules, running and blocked states explicitly. Failed status requests do not leave an apparently current prediction on screen.
- Refresh status immediately after saving the plan. Display uses Europe/Berlin, including daylight-saving changes.

## Compatibility

No changes to scheduling, pump control, HMAC or saved settings. Backend v0.356 independently corrects false-stale plan flags, local-time formatting and diagnostic messages. Existing signed next-run metadata remains compatible with backend v0.355. Real-device display validation remains open.
