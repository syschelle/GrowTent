# GrowTent v0.5.28

- Publish isOn=true for fresh, reachable PM1 power meters in the ESP state API, representing their permanently connected output.
- Continue publishing actual measured Watt/Wh values, including zero watts. Missing/stale readings still publish null.
- Preserve metering-only capability internally and keep switch commands blocked.
