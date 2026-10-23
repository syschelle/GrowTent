# GrowTent v0.2.34

## OTA release information fix

- Fixed missing release details on the OTA Update page.
- The GitHub Actions release workflow now reads `RELEASE_NOTES_<version>.md` directly from the tagged source tree.
- The same release-notes file is used for both the GitHub Release body and the OTA `manifest.json`.
- The OTA manifest therefore contains the complete release information instead of only the annotated Git tag message.
- If a version-specific release-notes file is missing, the workflow still falls back to the annotated tag text.
- No changes were made to grow control, irrigation, Shelly polling, Flood S Gen4 handling, relay scheduling, or OTA installation logic.
