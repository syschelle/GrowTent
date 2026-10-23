# GrowTent v0.2.33

## OTA progress feedback

- Added a real-time OTA progress bar to the web interface.
- The progress bar is driven by the existing `HTTPUpdate` byte counters and shows the actual firmware transfer/install percentage from 0 to 100%.
- The UI now displays transferred bytes and the total firmware size while the update is running.
- The OTA page gives immediate feedback as soon as **Update installieren** is pressed instead of appearing unresponsive until the request finishes.
- Successful updates end with **100% / Update erfolgreich / ESP wird neu gestartet** before reboot.
- OTA failures are shown directly in the progress panel with the firmware updater's error message.
- The update button is disabled while an OTA operation is active to prevent duplicate update requests.
- Progress is streamed over the existing OTA HTTP response as compact NDJSON messages; no additional FreeRTOS task or OTA polling endpoint was added.
- Grow control, irrigation, Shelly polling, Flood S Gen4 handling and relay scheduling are unchanged.
