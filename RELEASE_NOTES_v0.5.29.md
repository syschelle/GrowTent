# GrowTent v0.5.29

- Remove the unnecessary four-second Stream timeout wait after received Shelly HTTP replies.
- Drain available bytes without readString while preserving fragmented replies, idle timeout behavior and clock-wrap handling.
- Preserve light schedules, VPD thresholds and actuator safety decisions; this addresses polling latency, not a proven physical shutdown cause.
