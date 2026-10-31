# GrowTent v0.4.19

## Release summary

GrowTent v0.4.19 hardens notification credential handling in Serial output and the built-in system log. Pushover and Gotify credentials are no longer printed in clear text while persistent settings are loaded, and notification HTTP response bodies are no longer copied into logs.

## Security changes

- Hides the Pushover application token during Preferences loading.
- Hides the Pushover user key during Preferences loading.
- Hides the optional Pushover device identifier during Preferences loading.
- Hides the Gotify application token during Preferences loading.
- Replaces raw Pushover HTTP response logging with a status-code and response-length summary.
- Replaces raw Gotify HTTP response logging with a status-code and response-length summary.
- Keeps the Gotify token-bearing request URL internal and never writes it to Serial or the system log.
- Notification request bodies, including Pushover credentials, are not logged.
- Existing masked notification-token fields in `/api/state` remain masked.

## Compatibility

No climate-control, relay, irrigation, Shelly-control, OTA-install or API temperature behavior is changed. Internal and API temperature values remain Celsius-native; Fahrenheit remains a web-interface presentation/input option only.

## Validation scope

The change is limited to notification logging and release metadata. Static source checks verify that sensitive notification Preferences loads use hidden logging and that raw Pushover/Gotify response bodies are no longer passed to `logPrint()`. Hardware validation is still required before treating the release as production-validated.
