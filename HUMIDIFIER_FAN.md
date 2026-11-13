# Humidifier fan coupling

In **Operating settings → Humidifier fan**, select the local relay connected to the fan and click **Save coupling**. Select **Disabled** to release the relay. Pumps and the active local heater are excluded. While selected, the relay belongs exclusively to this feature: its saved minute schedule is paused and independent relay controls are rejected. Disabling coupling resumes its existing schedule.

Only ESP-origin commands to channel 0 of the configured humidifier Shelly participate. A validated successful ON response starts/restarts the fan deadline using the cached Auto-Off duration. A failed command does not extend it; an accepted OFF cancels it. Network latency is conservatively counted from the beginning of the switch request. Timer arithmetic handles millis rollover without NTP. GPIO application runs in the main loop, so network/system load can delay physical application; this is not a hardware safety timer.

| Generation | Configuration read | Fields |
| --- | --- | --- |
| Gen1 | `/settings/relay/0` | `auto_off` seconds; zero disables |
| Gen2 / Gen3 | `/rpc/Switch.GetConfig?id=0` | `auto_off`, `auto_off_delay` seconds |

Reads occur at startup/assignment and hourly only when coupled. Existing Basic/Digest credentials are reused. Failures retry hourly rather than on each status cycle. Unknown, malformed or disabled Auto-Off leaves the fan off. Duration must be positive and no greater than 2,147,483 seconds for safe monotonic deadlines. A failed read retains only the same-device last known value and marks it stale. Host, generation or credential changes invalidate the cache and stop the fan. An active deadline is never extended by a config refresh; disabling Auto-Off stops it.

Changing Auto-Off directly on the Shelly can take up to one hour to be reflected. Commands sent directly to the Shelly by another client are not mirrored; an early external OFF or timer extension can diverge from the local fan deadline. There are no extra frequent state queries. Reboot starts the fan off and waits for a fresh cache and a new acknowledged ON command. OTA/restart requests stop the fan.

GET `/api/settings/humidifier-fan` reports ESP-local relay, available relays, cache state, duration, running/remaining time, error and applied/failed request IDs. POST accepts exactly `{"relay": 0}` (disabled) or a permitted one-based relay number, returns 202/requestId and queues a verified NVS/main-loop apply. It follows the existing local dashboard trusted-LAN access boundary. The UI waits for the matching applied ID before showing success. No backend or frequent Shelly state polling is added.

## Device checks before production use

With a test load, verify ON/deadline/OFF for each Shelly generation, failure with and without a cached duration, hourly refresh, reassignment, pump/heater exclusion, a conflicting minute schedule, restart/OTA and physical timing under network load. These device checks are still open.
