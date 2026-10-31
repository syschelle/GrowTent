# GrowTent v0.4.20

## Release summary

GrowTent v0.4.20 prevents stored notification credentials and the Shelly password from being returned to dashboard clients. The Shelly Flood webhook token intentionally remains visible because it is required to configure the Flood webhook.

## Security hardening

- Pushover App Token is no longer embedded into the dashboard HTML.
- Pushover User Key is no longer embedded into the dashboard HTML.
- Gotify App Token is no longer embedded into the dashboard HTML.
- Shelly password is no longer embedded into the dashboard HTML.
- Secret input fields are now write-only password fields.
- Leaving one of these secret fields empty keeps the currently stored value instead of clearing it.
- `/api/state` continues to return masked values for notification credentials and the Shelly password.
- Shelly dashboard settings expose only whether a Shelly password is configured, not the password itself.
- The Shelly Flood webhook token remains visible by design and continues to be returned by the Shelly dashboard settings API.
- Wi-Fi provisioning continues to use a blank password field and never returns the stored STA password.

## Compatibility

No climate-control, relay, irrigation, temperature-unit, Shelly runtime, OTA or backend API behavior is intentionally changed. Internal/API temperature values remain Celsius-native.
