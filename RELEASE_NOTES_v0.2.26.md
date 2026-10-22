# GrowTent v0.2.26

## Flood S Gen4 webhook link display

This release improves the Flood S Gen4 configuration UI.

### Changes

- Added a clearly visible **API Links for Shelly Actions** section.
- Displays the complete webhook URL for `Flood alarm`.
- Displays the complete webhook URL for `Flood alarm off`.
- Added a **Copy** button next to each URL for direct copy & paste into the Shelly Flood S Gen4 Actions configuration.
- No Flood polling was added.
- No additional FreeRTOS task was added.
- Flood IP address and webhook token remain excluded from `/api/state`.

### Shelly Actions

Use the displayed URL ending in `alarm=1` for the Shelly `Flood alarm` action.

Use the displayed URL ending in `alarm=0` for the Shelly `Flood alarm off` action.
