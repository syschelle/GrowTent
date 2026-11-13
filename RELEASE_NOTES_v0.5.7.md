# v0.5.7 — irrigation settings layout and last-watering date

## Release summary

Move Irrigation settings into a full-width tile above Irrigation parameters and Optional heating relay. Add a date field for transferring the last known watering date from the backend when the ESP has no history.

## Changes

- Arrange the plan, interval, light-on delay and last-watering date in a wider, responsive layout. Pump/Flood settings remain in the same tile.
- Show the stored date in a native date field. Unknown history stays empty; never assume that no watering has occurred or invent a date.
- Allow an explicit known last-run date to be saved with the shared revisioned plan. Reject future/invalid dates, clearing stored history and moving it backwards.
- Recheck history immediately before durable main-loop application, including runs that start after the request was queued. Preserve handled-date guards to prevent duplicate runs.
- Keep older four-field local requests compatible by preserving their existing history.

## Compatibility

Pump mapping, Drying interlocks, HMAC backend routes and the shared persistent journal remain unchanged. An imported date is a planning baseline, not evidence of a completed watering cycle. Transfer only the actual known backend date. Disable independent backend scheduling before enabling the ESP plan. Push/event integration remains separate; the existing bounded event outbox still requires backend acknowledgements.

## Validation

Production JavaScript/editor tests and C++ history policy tests cover date import, future/backward/clear rejection, dirty fields, revision conflicts and durable-save confirmation. Full firmware and parser checks run in CI. Physical watering, imported-history/reboot and real-backend integration tests remain open.
