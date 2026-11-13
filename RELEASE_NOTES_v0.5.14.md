# GrowTent v0.5.14

## Changes

- Add an optional local fan relay coupled to ESP commands for the humidifier Shelly.
- Read Gen1 `/settings/relay/0` Auto-Off or Gen2/Gen3 `Switch.GetConfig?id=0` at startup and hourly, only while a fan relay is selected.
- Start a local nonblocking deadline after a successful humidifier ON acknowledgement; stop after the cached duration or a successful ESP OFF command.
- Add a German/English operating-settings panel with relay selection, cached duration, remaining time and stale/unknown status.
- Exclude pump and heater relays. Pause the selected relay's minute schedule and reject independent manual relay actions while coupled.
- Fence old configuration responses and out-of-order switch acknowledgements. Keep NVS writes and GPIO changes on the main loop.

## Compatibility

- Coupling defaults to disabled; the selected relay is persisted across reboot.
- No additional frequent Shelly state polling. The settings panel reads ESP-local status only.
- Unknown or disabled Auto-Off leaves the fan off. Failed hourly refreshes retain a same-device cached duration and display stale status.
- Direct changes on the Shelly are not immediately mirrored. An active local deadline retains its duration when the cache refreshes; a disabled Auto-Off stops the fan.
- No backend changes are required. Reboot/OTA does not resume a running fan.

## Validation

Host timer, integration-boundary and production JavaScript runtime tests are included in CI alongside full firmware builds and existing irrigation tests. Physical Gen1/Gen2/Gen3, timing under load and relay tests remain open.
