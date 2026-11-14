# GrowTent v0.5.23

## Release summary

Isolate raw VPD calculation from the central control VPD filter.

## Changes

- Raw VPD is a stateless calculation using raw temperature and humidity.
- Only control VPD updates its dedicated filter, once per valid climate sample.
- Preserve Adaptive Climate Smoothing and the existing additional alpha-0.10 control filter.
- Reject nonfinite filter inputs without changing its state.

## Compatibility

API field names, central `vpdKpa` ownership, leaf offset, thresholds and hysteresis are unchanged. Raw VPD now represents an unfiltered calculation. The regulation curve can change because raw reads no longer advance its filter. No backend update is required; backend v0.364 already consumes the ESP control VPD.

## Validation

Production-header host regression tests and existing firmware CI checks cover this change. Real-device response, physical outputs and OTA verification remain open. Monitor VPD and actuator behavior after upgrading; retain the previous firmware for rollback.
