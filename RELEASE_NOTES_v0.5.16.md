# GrowTent v0.5.16

## Changes

- Restructure Shelly settings into the same panel style as operating settings.
- Place shared credentials in a dedicated top panel and give each of the seven device roles its own card.
- Label light-on time, duration in hours and calculated off time individually; retain the schedule refresh action.
- Keep Flood S Gen4 token, webhook links and copy/generate actions together in a separate panel.
- Add responsive three/two/single-column device layouts and German/English headings.
- Correct form nesting and associate device IP labels with their actual inputs.

## Compatibility

- All existing control IDs, names, stored-value placeholders and action bindings are retained.
- Saving, generation detection, light schedule handling, credentials and webhook configuration use the existing logic and APIs.
- No backend, relay-control or polling changes.

## Validation

Existing production JavaScript checks and dedicated Shelly form/save/layout regression checks are included in CI alongside full firmware builds. Physical device verification remains open.
