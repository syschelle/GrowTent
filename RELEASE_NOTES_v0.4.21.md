# GrowTent v0.4.21

## Release summary

GrowTent v0.4.21 makes browser polling page-aware so configuration pages are no longer refreshed underneath active user edits. This fixes Shelly grow-light schedule values being restored to the previously saved state before the user could save a new value and reduces unnecessary Async/API traffic while settings pages are open.

## Changes

- Limits periodic `/api/state` polling to the **Status** page.
- Keeps periodic `/api/logbuffer` polling limited to the **System Log** page.
- Removes the 15-second Shelly settings polling timer.
- Loads Shelly settings/state once when the Shelly page is opened instead of continuously refreshing it.
- Stops sensor polling immediately when navigating away from Status.
- Prevents helper/action completion paths from restarting sensor polling while another page is active.
- Pauses periodic browser polling while the browser tab is hidden and resumes only the polling owned by the active page.
- Retains the existing `userDirty` protection for grow-light schedule controls as a second safety layer.
- Adds a regression-test contract that verifies page-aware polling and confirms that the Shelly background timer is absent.
- Advances firmware and static-asset cache-buster metadata to v0.4.21.

## Compatibility

- No API contract changes.
- No relay, irrigation, climate-control, Shelly runtime-control or OTA behavior changes.
- Temperature handling remains Celsius-native internally and in `/api/state` and `/api/stats`; Fahrenheit remains a dashboard presentation/input option only.
- The Shelly Flood webhook token behavior is unchanged.

## Validation

Static source/script checks are included in the package. Real-hardware build, flash and regression testing are still required before release tagging.
