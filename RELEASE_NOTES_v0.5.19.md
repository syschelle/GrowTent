# GrowTent v0.5.19

## Changes

- Reduce vertical whitespace around the next irrigation time, drying-phase note and save button in operating settings.
- Remove default paragraph margins and hide the empty save-status row while retaining visible save feedback.
- Remove the humidifier fan Auto-Off explanation paragraph from the UI in both German and English.

## Compatibility

- Irrigation, drying-phase protection, Shelly Auto-Off caching and fan control behavior are unchanged.
- The next irrigation date/time and drying-phase note remain visible. Only spacing and explanatory UI text change.
- Authentication and Flood S Gen4 remain permanently visible as in v0.5.18.

## Validation

Browser regression checks measure compact spacing at desktop/tablet/mobile widths, verify nonempty save feedback remains visible and ensure the fan explanation is absent. Existing production UI/save tests and full firmware builds remain in CI. Physical device verification remains open.
