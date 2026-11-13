# GrowTent v0.5.15

## Changes

- Gray out and disable all scheduling controls for the relay confirmed as the humidifier fan.
- Show a German/English note explaining that fan coupling controls the relay and pauses its minute schedule.
- Update the lock after confirmed assignment changes, on refresh and when changing language. Unsaved selections do not move the lock.
- Skip the coupled relay in the general operating-settings save operation.

## Compatibility

- Existing schedule values are preserved. Removing or moving the coupling restores the previous controls and minute schedule.
- A failed status refresh retains the last confirmed UI lock. No additional Shelly queries or backend changes.
- Existing firmware ownership and pump/heater exclusions remain in effect.

## Validation

Production JavaScript runtime tests cover locking, reassignment, restored values and disabled states, DE/EN hints and release of the schedule. Full firmware and existing layout checks run in CI. Physical device verification remains open.
