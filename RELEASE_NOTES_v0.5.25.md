# GrowTent v0.5.25

- Support Shelly Plug PM Gen3 through PM1.GetStatus for current power and energy readings.
- Probe PM1 after a received Switch RPC failure and reuse the discovered component for later polls. Preserve Gen1 and Switch devices.
- Report a meter as reachable without inventing a relay state; block switch commands to known metering-only devices.
