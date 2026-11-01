# GrowTent v0.5.2

## Summary

GrowTent v0.5.2 is a maintenance update for the authenticated backend Grow-profile write API introduced in v0.5.0. It removes ArduinoJson 7 deprecation warnings in the backend PATCH handler without changing the API contract, authentication model, accepted fields, phase/date rules, asynchronous handoff, or Celsius-native temperature behavior.

## Changes

- Replaced deprecated `JsonObject::containsKey()` checks in `PATCH /api/backend/grow-profile` with ArduinoJson 7 `obj[key].is<T>()` checks.
- Uses `is<JsonVariant>()` when field presence must be distinguished from an explicit JSON `null`, preserving the existing date-deletion semantics.
- Simplified numeric validation for `targetTemperature` and `targetVPD` to `is<double>()`; ArduinoJson 7 accepts both floating-point and integer JSON numbers for this check.
- Updated firmware and web asset version markers to v0.5.2.

## Compatibility

- No API key changes.
- No change to HMAC-SHA256 authentication, source-IP restriction, timestamp/nonce replay protection, NVS serialization, or loop handoff.
- No change to TCP ports, relay behavior, watering, Shelly integration, or temperature-unit contract.

## Validation status

The source change was statically reviewed for preservation of missing-vs-null semantics. A PlatformIO build and hardware test must still be performed on the target `nodemcu-32s` environment before release.
