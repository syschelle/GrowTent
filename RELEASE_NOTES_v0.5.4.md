# v0.5.4 — local irrigation scheduling (firmware only)

## Release summary

Add autonomous ESP irrigation planning for GrowTent and GrowTent-S3. Backend integration is prepared through HMAC-protected APIs but is deliberately not implemented in this release. The local plan is disabled by default.

## Changes

- Persist existing backend plan fields, increasing revision, Shelly light schedule cache, execution claims and a bounded event outbox in verified NVS storage.
- Derive starts from the light Shelly scheduler plus configured delay; refresh at boot, phase change and hourly. Preserve valid cache on failures and honor weekdays.
- Resynchronize invalid wall clock using NTP. Skip missed starts, prevent duplicates across reboot/revision/clock rollback and record missed occurrences.
- Drying blocks complete automatic/manual cycles and interrupts a running cycle; maintenance pump pulses remain available.
- Sign plan/event requests and responses using the existing backend HMAC configuration, source-IP checks, timestamp window and a replay cache without live-entry eviction.
- Expose signed plan status, batch event retrieval and authenticated durable acknowledgement. Start/end/missed pushes are delegated to the future backend integration.
- Preserve board-specific relay mapping and existing irrigation quantity/calibration settings; verify pump ON/OFF and report aborts instead of false success.
- Add host policy/security integration tests and CI firmware builds.

## Compatibility

Use the firmware built for the matching repository/board. Never flash classic ESP32 firmware onto ESP32-S3. Keep a recoverable prior image and back up settings/diary before hardware tests. The backend is unchanged, and local scheduling must remain disabled until its later integration is ready. Read `IRRIGATION_PLAN_API.md`, `TESTING.md` and `SECURITY.md`. Build success does not establish hardware validation.
