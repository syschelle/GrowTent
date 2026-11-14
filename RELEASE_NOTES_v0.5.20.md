# GrowTent v0.5.20

## Changes

- Restructure Push settings as two permanent Pushover and Gotify sections using the operating-settings panel style.
- Place each provider enable switch in its heading and align related fields in compact desktop rows.
- Stack fields on smaller displays and remove fixed input widths that could overflow mobile screens.
- Keep the shared Save action compact and right aligned; correct form nesting.

## Compatibility

- All input IDs/names, template values, password masking and provider settings are retained.
- Existing queued save/status handling, test notifications and controlled restart behavior are unchanged.
- German/English translations are reused. No API, polling or backend changes.

## Validation

Browser CI checks DE/EN layout, permanently visible sections, labels, form membership, masked secrets, desktop field alignment, mobile overflow and the production submit handler with synthetic queued/applied/failed responses. Existing UI tests and full firmware builds remain required. Live provider delivery and physical-device verification remain open.
