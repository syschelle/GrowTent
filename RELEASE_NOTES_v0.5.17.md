# GrowTent v0.5.17

## Changes

- Replace uneven Shelly cards with seven compact aligned rows for function, IP address and detected generation.
- Move the light schedule into its own full-width section, keeping device rows equally compact.
- Collapse shared credentials and Flood S Gen4 setup into keyboard-accessible expandable sections.
- Stack each row on mobile, with explicit generation labels and full-width IP fields.
- Retain all existing control IDs, names, values and save/detection/webhook actions.

## Compatibility

- This release refines the v0.5.16 layout; APIs, saved configuration, relay control and polling remain unchanged.
- Credentials and Flood settings remain part of the same save operation, including when collapsed.
- German and English are supported. Passwords continue to be retained when left blank.

## Validation

Production save-handler and existing UI checks remain required. Browser regression tests cover aligned rows, separated light schedule, collapsible sections, form associations and DE/EN desktop/mobile overflow. Full firmware builds run in CI. Device verification remains open.
