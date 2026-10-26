# GrowTent v0.2.39

## Irrigation dashboard stop/toggle fix

- Fixed the dashboard irrigation button so it now acts as a real start/stop toggle.
- While no irrigation cycle is active, the button shows **Start watering** / **Bewässerung starten**.
- While irrigation is active, the button shows **Stop watering** / **Bewässerung beenden** and remains enabled so the cycle can always be stopped.
- Added a dedicated `/toggleWatering` endpoint for the dashboard control while keeping the existing `/startWatering` endpoint for compatibility.
- Stopping irrigation immediately sets the remaining run count to zero and turns all irrigation pump relays off.
- The watering task now uses interruptible waits for pump runtime and pauses, checking for a stop request at least every 100 ms.
- After a manual stop, the task does not continue with another pump or irrigation run.
- A manually stopped cycle does not send the normal "irrigation completed" notification.
- Reduced the idle wake-up delay of the watering task so a newly started cycle begins promptly without adding another FreeRTOS task.
- Existing irrigation volume calculation, pump enable settings, tank measurement, VPD control, Shelly handling and Flood S Gen4 integration are unchanged.
