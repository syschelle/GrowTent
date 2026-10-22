# GrowTent v0.2.29

## Flood webhook URL restore fix

This release fixes the Shelly Flood S Gen4 Action URLs remaining blank after an ESP reboot or a normal browser reload.

### Fixed

- The saved Flood webhook token is now used immediately when the web interface initializes to rebuild both complete Shelly Action URLs.
- `Flood alarm` now reliably shows the persisted URL ending in `alarm=1`.
- `Flood alarm off` now reliably shows the persisted URL ending in `alarm=0`.
- Added a `pageshow` refresh so the URLs are also rebuilt when the browser restores the page from its back/forward cache.

### Root cause

The Flood URL initializer was registered as a second `DOMContentLoaded` listener from inside the application's existing `DOMContentLoaded` handler. At that point the event had already fired, so the nested listener never ran after a normal reload. Generating or editing the token worked because those actions called the preview function directly.

### Unchanged

- The Flood S Gen4 remains event-driven and is never polled.
- The Flood IP address and webhook token are not exposed through `/api/state`.
- No additional FreeRTOS task was added.
- Shelly polling/backoff, Web UI heap streaming, Grow date persistence, irrigation, VPD control and relay scheduling are unchanged.
