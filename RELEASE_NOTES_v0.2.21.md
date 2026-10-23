## v0.2.21 – Sensor UI and performance fix

### Fixed
- Fixed a frontend error path that could prevent sensor values from being displayed.
- Fixed the fallback sensor renderer so it no longer calls a helper outside of its scope.
- Pump availability updates are now isolated from sensor rendering and can no longer interrupt sensor value updates.
- Removed pump-status-specific full-page HTML replacements from the root request path to reduce memory churn and improve web interface responsiveness.
- Pump and watering buttons now start in a fail-safe disabled state and are enabled only after a valid `/api/state` response.
- Added firmware-version cache busting for `script.js` and `style.css` to avoid stale frontend files after OTA or USB updates.
- Disabled pumps remain clearly marked and their manual test buttons remain blocked in both frontend and backend.
