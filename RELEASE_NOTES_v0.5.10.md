# v0.5.10 — HMAC manual watering and backend-owned notifications

## Release summary

Prepare GrowTent and GrowTent-S3 for backend v0.354. The ESP owns irrigation configuration and automatic execution; the backend can edit the plan, request explicit manual cycles and send notifications from signed events.

## Changes

- Add signed POST `/api/backend/irrigation-start` with a 32-hex command_id and expires_at. Claims persist before accepting a start; duplicate commands do not repeat a cycle, including after reboot.
- Bound manual command retention to 16 records; unexpired records are never evicted. Expired unknown commands cannot run. A five-minute maximum command lifetime limits ambiguous late retries.
- Manual starts still check Drying, disabled pumps, Flood, OTA/restart, clock, overlap, valid quantities and event capacity. No legacy API fallback is introduced.
- Record all full manual cycles in the persistent event stream, including with automatic scheduling disabled. Watering-cycle pushes are delegated to the backend; calibration pulses are unchanged.
- Expose protocol version 2, backend push ownership and command results in signed plan state.
- Preserve real last-run history on backend edits and recheck dates before applying updates. Editing a plan never invents a watering date.

## Compatibility

Install this firmware before backend v0.354. Configure the existing backend HMAC key/source-IP restriction on each controller. The backend no longer runs an independent irrigation scheduler. Existing local plan settings and NVS history are retained. An upgraded backend must explicitly transfer its previous plan if the ESP has none.

Clock synchronization and free event capacity are now required for full manual cycles even when local automatic scheduling is disabled. The 64-event buffer still bounds unattended offline operation. Physical start/end, power interruption and push-provider integration tests remain open. HMAC provides integrity, not encryption.
