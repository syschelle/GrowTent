# GrowTent v0.4.31

## Release summary

GrowTent v0.4.31 fixes Min-VPD exhaust control when the real Shelly exhaust state changes outside the Min-VPD controller. The controller no longer relies on a private static ON/OFF cache that could become stale after manual dashboard actions, direct Shelly operation, or other external state changes.

## Changes

- Uses the most recent real Shelly exhaust state for every Min-VPD control decision.
- Removes the private `exhaustStateCache` from `controlMinVPD()`.
- Reuses the regular Shelly polling result to avoid extra network traffic during normal operation.
- Performs a direct exhaust status read only when no valid status is available yet or the Shelly generation is unresolved.
- Synchronizes both exhaust runtime caches immediately after a successful Min-VPD switch command.
- Adds explicit Min-VPD decision, success, status-unavailable, and switch-failure log messages.
- Rate-limits status-unavailable diagnostics to avoid log spam.
- Adds a static regression check that prevents the stale private state cache from being reintroduced.
- Advances firmware and static asset metadata to v0.4.31.

## Control behavior

The existing threshold contract is unchanged:

- Effective minimum VPD = `targetVPD - minVPD offset`.
- When exhaust is OFF, it turns ON below the effective minimum.
- When exhaust is ON, it remains ON until VPD reaches `effective minimum + hysteresis`.

## Compatibility

- No API contract changes.
- No temperature-unit contract changes.
- No relay, watering, OTA, or dashboard layout behavior changes.
