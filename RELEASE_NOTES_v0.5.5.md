# v0.5.5 — local plan status display fix

- Fix the persistent “Local plan: loading …” display caused by cached JavaScript from an older firmware.
- Version JavaScript/CSS URLs with the actual firmware version. Only matching asset versions receive immutable caching.
- Translate all local irrigation plan states through the selected German/English UI language and refresh immediately when the language changes.
- Show an explicit unavailable state on failed status requests or missing metadata. A disabled plan no longer displays a misleading Shelly wait message.
- Add production-JavaScript UI regression checks to CI.

## Compatibility and validation

Irrigation, pump mapping, HMAC and backend behavior are unchanged from v0.5.4. Backend migration and hardware validation remain open; local planning remains disabled by default. Use the firmware for your board. See TESTING.md for host coverage and remaining hardware checks.
