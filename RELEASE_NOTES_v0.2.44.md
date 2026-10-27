# GrowTent v0.2.44

## Shelly and WebServer network resilience

- Hardened outbound Shelly communication against temporary LAN/Wi-Fi failures.
- Shelly TCP connect attempts are now bounded to 1 second instead of inheriting multi-second library defaults.
- Shelly HTTP/read operations use short bounded LAN deadlines so an unreachable device cannot keep a request open for several seconds.
- Gen2/Gen3 status polling now uses the first successful HTTP response directly when authentication is disabled, removing the previous redundant second GET.
- Authenticated Gen2/Gen3 polling keeps Digest/Basic challenge handling, but failed authenticated responses are now reported as failures instead of being treated as an empty successful response.
- Added exponential retry backoff for failing Shelly devices: 30 s, 60 s, 120 s and 240 s. A successful poll resets the backoff immediately.
- Shelly polling is skipped while the ESP32 STA Wi-Fi link is not connected.
- Shelly failure logs now include `elapsedMs` to show how long the network operation actually took.
- Extended `/api/state` slow-request diagnostics with:
  - `buildMs`
  - `sendMs`
  - requesting client IP
  - Wi-Fi RSSI
- Added slow `server.handleClient()` diagnostics including URI and HTTP method, so future multi-second main-loop stalls can be attributed to the exact request.
- Existing main-loop gap diagnostics now also include RSSI.
- No changes to BME280/DS18B20/ultrasonic acquisition, relay scheduling, VPD control, irrigation, Flood safety logic, Shelly switching behavior, or OTA behavior.
- Updated browser asset cache-busting and local fallback version to v0.2.44.
