# GrowTent v0.2.38

## Heating mat dashboard and energy reset integration

- Added a dedicated **Heating Mat** tile under **Shelly Control** on the dashboard.
- The tile is shown only when a heating-mat Shelly IP address is configured.
- The tile is monitoring-only and shows switch state, current power, accumulated energy and calculated cost.
- No toggle button, schedule control or automatic switching was added for the heating mat.
- Added the heating-mat Shelly to **Reset kWh counter** in Operating Settings.
- The heating-mat counter is touched only when its IP address is configured; an empty IP is skipped without a network request.
- Added a persistent heating-mat energy-offset preference so reset behaviour matches the existing Shelly devices.
- Existing Grow control, irrigation, VPD control, Flood S Gen4 handling and Shelly switching logic are unchanged.
