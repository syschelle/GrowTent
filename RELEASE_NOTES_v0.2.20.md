## v0.2.20 – Disabled pump controls in the status UI

### Changed
- Disabled irrigation pumps are now clearly marked as `Deaktiviert` / `Disabled` on the status page.
- The manual 10-second test button of a disabled pump is visibly disabled and cannot be clicked.
- The frontend now blocks disabled pump test requests before sending them to the controller.
- The existing backend validation remains in place as a second safety layer.
- Disabled pump cards are visually dimmed so their inactive state is immediately recognizable.
- Pump disabled labels and tooltips are available in both German and English through `java_script.h`.
- The persisted pump state is rendered immediately when the page is opened and remains synchronized through `/api/state`.
