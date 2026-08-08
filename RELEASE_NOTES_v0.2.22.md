## v0.2.22 – Replace disabled pump button with text state

### Changed
- Replaced the grey disabled manual pump test button with a clear `Deaktiviert` / `Disabled` text indicator.
- Disabled pumps now hide the 10-second test button completely instead of showing a non-clickable grey button.
- Enabled pumps still show the normal 10-second test button.
- The disabled state remains clearly visible and translated through `java_script.h`.
- Backend protection against starting disabled pumps remains unchanged.
