# GrowTent v0.5.18

## Changes

- Make Authentication and Flood S Gen4 permanent, always-visible sections in Shelly settings.
- Remove expand/collapse controls and their styling.
- Retain the compact device rows and separate light schedule from v0.5.17.

## Compatibility

- Existing fields, values, control IDs, action handlers and save behavior are unchanged.
- No API, polling or backend changes. German/English and mobile layouts are preserved.

## Validation

Browser checks verify that credentials, Flood enable/token and both webhook URLs are visible without interaction, with no disclosure controls. Existing save, form, alignment and overflow checks remain in CI alongside full firmware builds. Physical device verification remains open.
