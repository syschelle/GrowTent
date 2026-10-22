# GrowTent v0.2.25

## Shelly polling stability and UI responsiveness

This release improves Shelly polling behavior and reduces unnecessary network and log load.

### Fixed

- Shelly entries with an empty IP address are now treated as unconfigured.
- Empty Shelly slots are no longer polled.
- Repeated `[SHELLY] Invalid IP: ''` messages are no longer written to the system log.
- Active Shelly switch commands are ignored when no IP address is configured.

### Improved

- A configured Shelly that is temporarily unreachable now uses a 30-second retry backoff after a failed status request instead of immediately being retried during every 10-second polling cycle.
- The last known good value remains available during temporary communication failures.
- Repetitive API-rendering messages for unavailable main/light values were removed. Actual Shelly request failures continue to be logged by the polling layer.
- These changes reduce unnecessary network activity and help keep the GrowTent web interface responsive when one or more Shelly devices are offline.

### Flood S Gen4

The Flood S Gen4 integration introduced previously is unchanged:

- event-driven webhook handling
- no Flood polling
- no additional FreeRTOS task
- no Flood IP or webhook token exposed through `/api/state`
