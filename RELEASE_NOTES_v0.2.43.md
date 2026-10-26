# GrowTent v0.2.43

## GitHub release race-condition fix

- Fixed a GitHub Actions race condition that could occur when two push events for the same release tag started the firmware release workflow at nearly the same time.
- Added per-ref workflow concurrency so release jobs for the same tag are serialized instead of creating the same GitHub Release in parallel.
- Added a defensive fallback: if release creation still collides with another run, the workflow waits briefly and updates the already-created release instead of failing with HTTP 422 `Release.tag_name already exists`.
- Existing releases continue to be updated in place and `firmware.bin` / `manifest.json` are uploaded with `--clobber`.
- No irrigation, sensor, relay, Shelly, Flood, OTA-runtime, or other ESP32 control logic was changed.
- Updated browser asset cache-busting and local fallback version to v0.2.43.
