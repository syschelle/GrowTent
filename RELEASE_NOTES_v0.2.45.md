# GrowTent v0.2.45

## Shelly polling regression fix

- Fixed a Shelly status regression introduced in v0.2.44.
- v0.2.44 reduced the Gen2/Gen3 TCP connection deadline to 1 second. On the affected installations, every configured Shelly status request then failed at approximately 1.0 seconds and no valid runtime values were ever stored.
- Restored the proven Gen2/Gen3 Shelly HTTP transport path from v0.2.43, including its previous connection behavior and Digest-auth discovery flow.
- Kept the v0.2.44 exponential retry backoff for failing Shelly devices (30 s, 60 s, 120 s, 240 s) and the Wi-Fi-link guard.
- Kept elapsed-time information in Shelly failure logs.
- Kept the v0.2.44 `/api/state` and `server.handleClient()` timing diagnostics because the observed multi-second web-server stalls also occurred independently of Shelly polling and therefore require separate diagnosis.
- No changes were made to BME280, DS18B20, ultrasonic tank measurement, relay automation, irrigation, Flood handling, OTA, or persistent settings.
