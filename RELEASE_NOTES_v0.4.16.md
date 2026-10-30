# GrowTent v0.4.16

## Release summary

GrowTent v0.4.16 is a controlled end-to-end OTA validation release. The HTTPUpdate installer, exclusive maintenance mode, Async handoff/status flow and v0.4.15 transport diagnostics are intentionally unchanged. Only the firmware version, web asset cache-busters and release/test metadata advance so an installed v0.4.15 device can perform a real OTA update to a newer published firmware without introducing another transport variable.

## Highlights

- No OTA transport or installer logic changes from v0.4.15.
- Keeps Arduino `HTTPUpdate` with forced redirects and the existing exclusive maintenance mode.
- Keeps the Async OTA worker and `/api/ota/update-status` progress model unchanged.
- Keeps all non-flashing OTA transport diagnostics from v0.4.15.
- Provides a new versioned GitHub Release target for a real v0.4.15 -> v0.4.16 OTA hardware test.
- Keeps normal GitHub Release `manifest.json` and `firmware.bin` assets for backward compatibility with older firmware.
- Updates dashboard cache-busters and test metadata to v0.4.16.

## v0.4.6 OTA request-status lifecycle fix

The OTA implementation previously used one shared runtime status object for both update checks and firmware updates. As soon as the next OTA job was queued, that object was reset and reused. A browser or release-gate poll that still referenced the completed request ID could therefore receive:

```text
request_not_found
```

v0.4.6 preserves the last terminal OTA check and update status before reusing the active-job slot. The archive is deliberately lightweight: it retains the request/result fields needed by the API contract but does **not** duplicate the potentially multi-kilobyte changelog string, avoiding additional OTA heap pressure.

The Async release test now explicitly queues a second OTA job and then queries the previous completed request ID. The old request must remain `state:"done"` and may no longer regress to `request_not_found`.


## v0.4.16 end-to-end OTA validation target

The v0.4.15 cold-first diagnostic reproduced the HTTPUpdate-equivalent request as the first network operation after the exclusive shutdown and still reached the signed firmware asset with HTTP 200 and the expected content length. The earlier v0.4.12 connection refusal could therefore no longer be reproduced in DNS, TLS, redirect, hash/header or first-connection diagnostics.

v0.4.16 deliberately makes no further OTA implementation changes. Its purpose is to create a clean, newer release target so the complete real `HTTPUpdate` path can be tested from v0.4.15:

1. OTA check discovers v0.4.16.
2. The Async request queues the install.
3. Exclusive maintenance stops the application tasks.
4. Arduino `HTTPUpdate` performs the GitHub release download and flash.
5. Progress is exposed through `/api/ota/update-status`.
6. On success the ESP restarts into v0.4.16.

Any failure can therefore be compared directly with the already-passing non-flashing v0.4.15 diagnostics without another architecture change.

## v0.4.15 cold-first preflight comparison

v0.4.14 proved that an HTTPUpdate-equivalent request succeeds after the diagnostic has already established several TLS connections. The real v0.4.12 failure differed in one remaining way: HTTPUpdate was the first TLS connection attempted after the exclusive shutdown.

v0.4.15 changes only the diagnostic order. In exclusive mode it performs the HTTPUpdate-equivalent preflight immediately after the maintenance environment is ready, before any other DNS/TLS/HTTP probe. The existing simple transport checks and warm preflight still run afterward.

If the cold-first preflight passes with HTTP 200 and the expected firmware length, the v0.4.12 connection refusal is no longer reproducible even under the same first-connection ordering. If it fails while the later warm preflight succeeds, the first TLS connection itself becomes the primary suspect.

## v0.4.14 HTTPUpdate preflight comparison

The v0.4.13 normal and exclusive diagnostics both reached the signed GitHub asset with HTTP 200 and the exact firmware size. That rules out DNS, TLS, GitHub release redirect handling and the exclusive maintenance shutdown as the immediate cause of the v0.4.12 `start_ssl_client: -1` failure.

The remaining pre-request difference is the work performed inside Arduino-ESP32 2.0.17 `HTTPUpdate::handleUpdate()`: running-sketch MD5/SHA-256 calculation, ESP-specific request headers, HTTP/1.0, the HTTPUpdate User-Agent, its 8-second HTTP timeout and forced redirects.

v0.4.14 reproduces those operations in the non-flashing diagnostic. It closes the connection immediately after final response headers; no firmware body is consumed and no updater/flash API is called.

## v0.4.13 non-flashing OTA transport diagnostic

The v0.4.12 hardware test failed before any HTTP response was received:

```text
WiFiClientSecure.cpp: connect(): start_ssl_client: -1
HTTPUpdate.cpp: handleUpdate(): HTTP error: connection refused
```

Arduino-ESP32 maps HTTPClient error `-1` to the generic text `connection refused`, so that label alone does not identify the actual failing TLS stage.

v0.4.13 therefore leaves the real installer unchanged and adds a dedicated diagnostic worker. A successful normal OTA check first caches the exact version-pinned firmware URL. The diagnostic then performs these operations on the ESP32 itself without flashing:

1. Resolve the firmware host with `WiFi.hostByName()`.
2. Open a direct insecure `WiFiClientSecure` TLS connection to that host.
3. Send the same HTTP `GET` method used by HTTPUpdate, but stop after response headers and do not read the firmware body.
4. Read the release redirect internally and expose only its host name.
5. Resolve and directly TLS-connect to the redirect host.
6. Send a GET to the signed asset URL, again stopping after response headers.
7. Capture the real `WiFiClientSecure::lastError()` code/text, timings and heap measurements.

The shell test runs the sequence first during normal runtime and then, by default, in the exact exclusive OTA maintenance state. Exclusive mode intentionally reboots after publishing the result because Sensor/BME/Shelly/Watering tasks have been self-deleted.

The signed release-asset URL and its JWT are never printed, returned by the API or written to the shell log.

## v0.4.12 HTTPUpdate transport simplification

The v0.4.11 diagnostic run showed that exclusive maintenance mode increased the largest contiguous internal block substantially and that TLS connection setup itself was healthy. The remaining native OTA failure occurred when the GitHub Release Asset endpoint returned HTTP 618.

Rather than introduce a second OTA hosting channel, v0.4.12 returns the actual firmware installer to the `HTTPUpdate` path already used successfully by GrowTent v0.2.46. The Async migration is preserved at the API boundary: `/api/ota/update` still queues work and returns immediately, and the dedicated OTA worker performs the blocking download/flash operation.

Before `HTTPUpdate` starts, the v0.4.11 exclusive maintenance mode still shuts down Sensor/BME/Shelly/Watering tasks and releases nonessential services. TCP/8080 remains available so the browser can poll `/api/ota/update-status`.

The `HTTPUpdate` progress callback performs only scalar assignments plus sparse Serial diagnostics. It does not take the OTA mutex and does not build `String` objects. The status endpoint overlays those volatile progress counters while the worker is streaming, preserving the live percentage/byte display without reintroducing the callback locking problem found earlier in the v0.4.x experiments.

The release workflow is intentionally unchanged: `manifest.json` and `firmware.bin` remain ordinary GitHub Release assets. This preserves the update path used by older v0.2.x devices.

## v0.4.11 exclusive OTA maintenance mode

Normal GrowTent operation is unchanged. The sensor, BME/control, Shelly polling and Watering tasks remain active until the user explicitly starts a firmware installation.

After the `/api/ota/update` POST has been acknowledged to the browser, the OTA worker waits one second and requests exclusive maintenance mode. The main loop immediately forces irrigation pumps off while keeping TCP/8080 available for the OTA status endpoint. Each application task then observes the maintenance flag from its own loop, acknowledges shutdown and calls `vTaskDelete(nullptr)` itself. This avoids externally deleting a task while it may own a mutex or be inside network code.

The OTA worker does not open the GitHub firmware TLS connection until all registered application tasks have stopped and the main loop has additionally closed LittleFS, Preferences and I2C and released the in-memory log deque. Heap diagnostics are printed both before the shutdown and after the exclusive environment is ready.

Once exclusive mode has begun, the normal application is intentionally not reconstructed in-place. A successful OTA ends in the normal firmware reboot. A failed OTA also schedules a reboot, which returns to the still-valid existing firmware partition and recreates the normal runtime through the standard boot path.

The browser stops normal `/api/state` and web-log polling during installation but continues polling `/api/ota/update-status` every 500 ms. `/api/state` and `/api/stats` return a lightweight `503 ota_in_progress` response while maintenance mode is active, preventing normal dashboard/backend state generation from competing with OTA. After successful completion the ESP reboots and the page reloads.

## v0.4.10 OTA transport diagnostics

v0.4.9 did not establish that larger HTTP buffers solved GitHub HTTP 618. Its first native OTA begin attempt instead ran until the configured connection timeout and returned `ESP_ERR_HTTP_CONNECT`. The automatic second attempt then failed immediately in `mbedtls_ssl_setup()` with `-0x7F00`, which is `MBEDTLS_ERR_SSL_ALLOC_FAILED`.

The exact Arduino-ESP32 2.0.17 ESP32 SDK configuration uses internal RAM for mbedTLS, a symmetric 16384-byte TLS content size, no asymmetric content lengths, and no dynamic TLS buffers. Increasing both ESP HTTP client buffers from 1024 to 8192 bytes also increases ESP HTTPS OTA's own upgrade buffer because that buffer is sized from `http_config->buffer_size`. The v0.4.9 change therefore materially increased peak internal-RAM demand before TLS setup.

v0.4.10 deliberately does not introduce another transport fix. It restores the 1024-byte HTTP buffers and performs one native OTA begin attempt only. A dedicated HTTP event callback records which host was connected, which redirect host was announced, and the exact socket/TLS error details on disconnect. The signed redirect URL itself is never logged.

This release exists to identify the first failing transport stage before any further OTA behavior change is made.

## v0.4.9 GitHub redirect / HTTP 618 hardening

Hardware testing of v0.4.8 showed successful OTA manifest retrieval and successful TLS initialization, but `esp_https_ota_begin()` then failed while following GitHub's release-asset redirect:

```text
esp_https_ota: Server error (618)
esp_https_ota: Failed to establish HTTP connection
```

The failure occurred only a few seconds after the OTA request began, so it was not consistent with a signed URL simply aging past GitHub's short expiration window. The running v0.4.8 configuration used only 1024-byte RX and TX buffers for the native ESP HTTP client.

Espressif has documented the same GitHub `618` symptom and specifically recommends larger HTTP client RX/TX buffers because GitHub release downloads use long redirect headers and short-lived signed release-asset URLs.

v0.4.9 therefore uses 8192-byte RX and TX buffers for the native OTA HTTP client. It also permits one complete `esp_https_ota_begin()` retry. A retry starts again from the stable, version-specific GitHub release URL, forcing GitHub to issue a fresh signed release-asset redirect instead of relying on connection state from the failed attempt.

The actual OTA data path is unchanged: firmware is still written incrementally by ESP-IDF through `esp_https_ota_perform()` and finalized with `esp_https_ota_finish()`.

## v0.4.8 native IDF certificate-bundle correction

Hardware testing of v0.4.7 reached `esp_https_ota_begin()` with healthy heap but failed before downloading firmware:

```text
arduino_esp_crt_bundle_attach(): Failed to attach bundle
esp_https_ota: Failed to establish HTTP connection
```

Arduino-ESP32 2.0.17 contains two distinct certificate-bundle implementations. Its `WiFiClientSecure` library ships an Arduino-specific `arduino_esp_crt_bundle_attach()` implementation whose internal bundle remains empty until `arduino_esp_crt_bundle_set()` is supplied with a user-generated bundle. ESP-IDF 4.4 itself separately provides the native `esp_crt_bundle_attach()` implementation. With `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE` enabled, that native function initializes the framework-embedded certificate bundle automatically on first use.

v0.4.8 deliberately binds the native ESP-IDF symbol. The local Arduino `esp_crt_bundle.h` is not included by `ota.cpp`, preventing PlatformIO's library include path from silently selecting the Arduino wrapper declaration again.

No OTA transfer or flash algorithm is changed in this release; the incremental native ESP-IDF OTA transport introduced in v0.4.7 remains intact.

## v0.4.7 native ESP-IDF OTA transport

The previous v0.4.x experiments exposed three separate failure classes: an oversized manifest, post-TLS heap fragmentation at Arduino `Update.begin()`, and a later transfer abort after substantial firmware progress. The final failure could not be symbolized because the exact `firmware.elf` from the GitHub Actions build had not been retained.

A source review of the exact Arduino-ESP32 2.0.17 stack showed that `Update.writeStream()` relies on Arduino `Stream::readBytes()` / `timedRead()` and that the dual-core path does not add the single-core yield present in the implementation. Rather than continue patching around that transport, v0.4.7 moves installation to ESP-IDF 4.4's native incremental HTTPS OTA API.

`esp_https_ota_perform()` is intentionally called in a loop. ESP-IDF returns from it after each HTTP read operation, which gives GrowTent a stable point to update progress, yield the worker task and surface the exact ESP-IDF error if the connection, flash write or validation fails.

The native client uses the full Espressif certificate bundle enabled by Arduino-ESP32 2.0.17, supports GitHub redirects, and writes through ESP-IDF's OTA subsystem rather than Arduino `Update.writeStream()`.

The release workflow now retains `firmware.elf` plus build metadata as a GitHub Actions artifact. Any future crash can therefore be decoded against the exact binary that produced it instead of inferring the failing function from raw addresses.

## v0.4.5 OTA preallocation fix

Hardware diagnostics on v0.4.4 showed the exact failure condition:

```text
[OTA HEAP] before_Update.begin ... internalFree=7316 internalLargest=3444
Updater.cpp: begin(): malloc failed
```

Arduino-ESP32 `Update.begin()` requires a contiguous 4096-byte flash-sector buffer. The total internal heap was still larger than 4 KiB, but TLS fragmentation left the largest contiguous block at only 3444 bytes.

v0.4.5 changes the order of operations. The GitHub workflow adds the exact firmware size to `manifest.json`; after a successful update check the OTA worker can call `Update.begin(firmwareSize)` before opening the firmware TLS connection. The 4 KiB sector buffer is therefore reserved while the heap is still contiguous. The firmware is then downloaded with `HTTPClient` and written directly via `Update.writeStream()`.

For direct update requests where no cached manifest size exists, v0.4.5 performs a metadata request first, fully closes TLS, waits for heap coalescing, reserves the Update buffer, and only then reconnects to stream the firmware.

## v0.4.4 OTA heap-pressure fix

On hardware running the Async OTA worker, the firmware download could reach `HTTPUpdate` but fail before flashing with:

```text
Updater.cpp: begin(): malloc failed
HTTPUpdate.cpp: runUpdate(): Update.begin failed!
```

Arduino-ESP32's `Update.begin()` allocates a 4 KiB flash-sector buffer. The dedicated GrowTent OTA worker previously reserved 18,432 bytes of task stack from the internal heap. Runtime stack high-water measurements from the OTA check path showed that most of this reservation was unused.

v0.4.4 reduces the OTA worker stack to 8,192 bytes, returning about 10 KiB to the internal heap during the TLS/HTTPUpdate phase. Additional diagnostics now report the largest internal free block exactly when `HTTPUpdate` invokes its start callback, immediately before `Update.begin()` allocates the flash buffer.

## v0.4.3 OTA dashboard reinstall fix

The OTA API already supports downloading and flashing the current GitHub firmware image because the update worker does not pass the installed firmware version as an HTTPUpdate version constraint. The dashboard previously hid that capability by leaving **Update installieren / Install update** disabled whenever `updateAvailable` was false.

v0.4.3 changes the dashboard contract: after any successful OTA check, the action remains available. If a newer version is present it shows the normal install action; if the installed and latest versions match, it shows **Firmware erneut installieren / Reinstall firmware** and deliberately queues the same OTA update endpoint.

The Async regression test now checks for this same-version reinstall behavior so it cannot silently regress.

## v0.4.2 OTA streaming fix

Hardware testing of the v0.3.09 → v0.4.1 upgrade showed that manifest discovery worked correctly, but the firmware transfer could start and then stall after roughly 1% progress. The OTA worker's progress callback was taking the same FreeRTOS mutex used by the status path while running inside `HTTPUpdate::update()`. v0.4.2 removes semaphore and `String` work from the streaming callbacks and publishes progress through simple scalar snapshots instead.

During the firmware stream, only Shelly status polling is paused; sensor acquisition and watering/control tasks continue to run. The GitHub firmware transfer also uses a bounded 60-second HTTP read timeout and reports the final HTTPUpdate error/result explicitly in the serial log.

## AsyncWebServer migration history

The migration was intentionally split into small hardware-testable phases. Each phase kept unsafe hardware, network, storage or restart operations out of Async HTTP callbacks and moved ownership into the normal Arduino loop or a dedicated worker where required.

| Phase | Development version(s) | Migration scope / result |
| ---: | --- | --- |
| 1 | v0.2.47 | Initial read-only Async sidecar introduced alongside the legacy server. |
| 2 | v0.2.48-v0.2.49 | Expanded read-only routes, safe log snapshots and fixes for large-page/static-asset rendering. |
| 3 | v0.2.50 | Cache policy plus read-only Grow Diary routes moved to Async. |
| 4 | v0.2.51 | Dedicated Async state cache and HTML streaming tuning. |
| 5 | v0.2.52 | First harmless Async write route and bounded JSON POST probe. |
| 6 | v0.2.53 | First persistent NVS write migrated: theme. |
| 7 | v0.2.54 | Presentation/UI settings persistence migrated. |
| 8 | v0.2.55 | Request-lifecycle diagnostics added for Async validation; later retired after stabilization. |
| 9 | v0.2.56 | Transport and Wi-Fi diagnostics added; later retired after stabilization. |
| 10 | v0.2.57 | Chunked `/api/state` and `/api/stats` transport introduced; later superseded by fixed-length transport. |
| 11 | v0.2.58 | Diagnostic cleanup and persistent box-name handling. |
| 12 | v0.2.59 | Additional safe system/display settings moved to permanent Async APIs. |
| 13 | v0.2.60 | NTP server and time-zone persistence migrated. |
| 14 | v0.2.61 | Restart-applied DS18B20 enable state and active relay count migrated with guarded persistence. |
| 15 | v0.2.62 | Persistent Shelly configuration migrated. |
| 16 | v0.2.63 | Static dashboard asset transport moved to the Async path and hardened. |
| 17 | v0.2.64-v0.2.65 | Grow target runtime handoff introduced; v0.2.65 corrected GET/precheck behavior. |
| 18 | v0.2.66 | Heater tuning runtime handoff migrated to main-loop ownership. |
| 19 | v0.2.67 | NVS/default handling and task-stack diagnostics hardened. |
| 20 | v0.2.68 | Heating source/output runtime handoff migrated. |
| 21 | v0.2.69-v0.2.70 | Grow-light schedule runtime handoff migrated; v0.2.70 supplied the corrective build fix. |
| 22 | v0.2.71 | Relay minute-schedule runtime handoff migrated. |
| 23 | v0.2.72 | Manual relay actions migrated; TCP/8080 became the permanent dashboard/API target. |
| 24 | v0.2.73 | Irrigation, pump, tank-ping and Flood runtime handoffs migrated; the r2 package corrected a test-only asset-length expectation. |
| 25 | v0.2.74-v0.2.79 | Grow Diary mutations, LittleFS recovery, static-asset transport finalization, relay-dashboard acknowledgement/state ownership and retirement of the legacy TCP/80 web UI. |
| 26 | v0.2.80-v0.2.85 | `Betriebseinstellungen` split-save migration plus corrective dashboard-save handling and fixed-length state/stats transport hardening. |
| 27 | v0.2.86-v0.2.87 | Shelly runtime actions migrated to permanent TCP/8080 and state/stats String transport corrected. |
| 28 | v0.2.88-v0.2.95 | Shelly dashboard settings save migration, direct permanent-port form binding, Shelly-only save independence from light timing, Gen1 compatibility, automatic generation discovery and Gen1 dashboard metric/status corrections. |
| 29 | v0.2.96-v0.2.97 | System Settings dashboard migrated away from legacy `/savesettings`; restart became a queued main-loop action. ArduinoJson warning cleanup followed without changing the phase architecture. |
| 30 | v0.2.98 | Factory Reset migrated to the permanent port with destructive work kept in the main loop; AP Wi-Fi provisioning remained the explicit TCP/80 exception. |
| 31 | v0.2.99-v0.3.02 | Wi-Fi AP provisioning itself migrated to AsyncWebServer with captive DNS and transactional credential testing. DE/EN provisioning was completed and state/stats transport received additional shared-cache hardening. |
| 32 | v0.3.03-v0.3.09 | Final legacy synchronous `WebServer` removal. Message settings, New Grow and OTA paths migrated; normal-STA TCP/80 removed; state/health transport, system-settings logging, OTA real-check/UI binding and fallback-AP lifecycle were subsequently hardened on hardware. |

### Phase 25 - storage and legacy UI retirement

Phase 25 completed the remaining diary/LittleFS mutation paths using guarded queue/main-loop ownership. It also finalized dashboard asset transport, corrected relay-dashboard acknowledgement/state ownership and retired the old browser UI from TCP/80. From this point onward the normal dashboard target was TCP/8080 only.

### Phase 26 - Betriebseinstellungen split save

The previous broad `/saverunsettings` dependency was replaced by dedicated permanent-port settings endpoints. Run profile, grow targets, irrigation configuration, heating output and relay schedules became independently validated handoffs. Corrective builds then hardened dashboard payload handling and finalized state/stats fixed-length delivery.

### Phase 27 - Shelly runtime actions

Shelly toggle, schedule-refresh and energy-reset actions were moved to queued main-loop ownership on TCP/8080. Dry-run coverage proved that the HTTP callback itself performs no Shelly network operation.

### Phase 28 - Shelly dashboard configuration

Shelly dashboard configuration moved completely away from `/saveshellysettings`. Corrective builds removed the legacy form fallback, allowed Shelly-only saves without forcing light-schedule fields, restored Gen1 compatibility and replaced manual generation selectors with automatic Shelly generation discovery performed outside the Async callback.

### Phase 29 - System Settings and restart ownership

The System Settings page stopped using the broad legacy `/savesettings` route. Existing dedicated settings APIs became authoritative and restart requests were queued for the normal main loop. The compatibility route on the permanent port was explicitly retired.

### Phase 30 - Factory Reset

Factory Reset became an Async-queued operation. `Preferences.clear()` and restart scheduling stay outside the HTTP callback. At this stage TCP/80 was still deliberately retained for AP Wi-Fi provisioning only.

### Phase 31 - Async Wi-Fi provisioning

The captive portal and Wi-Fi provisioning path itself moved to `ESPAsyncWebServer` on TCP/80. Candidate credentials are tested transactionally while the AP remains available; credentials are persisted only after a successful STA connection. Failed candidates leave the previous stored configuration intact. The provisioning UI was completed in German and English.

### Phase 32 - final synchronous WebServer removal

Phase 32 migrated the last real synchronous application scopes:

- message settings;
- New Grow;
- OTA check;
- OTA update.

After those paths were migrated, `WebServer.h`, the legacy application server object and `server.handleClient()` were removed from normal GrowTent application operation.

Final ownership after Phase 32:

- **Normal STA:** only the permanent Async application server on TCP/8080.
- **Provisioning/AP mode:** dedicated Async captive portal on TCP/80 plus captive DNS.
- **No synchronous WebServer remains in either mode.**

## Post-Phase-32 corrective validation builds

The migration phase number stayed at 32 while hardware testing finalized the release architecture:

- **v0.3.04:** replaced the remaining state/stats callback-tail path with native shared-cache direct-buffer fixed-length responses after a reproducible tail stall.
- **v0.3.05:** moved `/api/async/health` to a persistent direct-buffer fixed-length response after hardware preflight exposed a large-health-response timeout.
- **v0.3.06:** System Settings logging changed to changed-values only; same-value saves no longer create misleading log entries.
- **v0.3.07:** hardened the real outbound GitHub OTA manifest check; added a real check-only regression without performing firmware installation.
- **v0.3.08:** fixed OTA dashboard event binding/translation scope and removed the dependency on inline `onclick` handlers.
- **v0.3.09:** fixed the boot-fallback AP lifecycle so captive DNS, TCP/80 and SoftAP are automatically stopped once stored STA credentials recover.

These corrections are included in the v0.4.2 release baseline; they do not introduce a Phase 33.

## Final network ownership

### Normal operation

- Dashboard/API: `http://<device-ip>:8080`
- TCP/80: closed
- Wi-Fi mode after successful STA recovery: STA only

### Wi-Fi provisioning

- Captive portal: TCP/80
- Captive DNS: active only while provisioning is active
- The permanent application server remains on TCP/8080

## Safety and ownership model

A central migration rule is retained in v0.4.2:

> **Async HTTP callback = parse, validate, persist where safe, and queue**  
> **Main loop / dedicated worker = runtime hardware, network, storage, restart or OTA action**

This applies to relay control, Shelly operations, irrigation, grow/heating/light runtime changes, diary/LittleFS operations, Factory Reset, restart handling and OTA processing.

## OTA

The GitHub release workflow builds `firmware.bin` from the tagged source and publishes:

- `firmware.bin`
- `manifest.json`

The manifest contains the release version, firmware URL and a compact release summary for the OTA dashboard.

For the `v0.4.2` release, the workflow uses `RELEASE_NOTES_v0.4.2.md` as the full GitHub release text. To keep OTA checks reliable on already-installed firmware, only the release summary and highlights are embedded in `manifest.json`; the complete Phase 1-32 migration history remains in the GitHub release notes.

## Release validation

The canonical hardware regression test is:

```bash
chmod +x tests/test_async_webserver.sh
./tests/test_async_webserver.sh <ESP-IP>
```

The release candidate must report:

```text
OVERALL v0.4.2 RELEASE GATE: PASS
```

The test covers, among other checks:

- firmware/Phase-32 compatibility preflight;
- normal-STA TCP/80 shutdown and provisioning lifecycle;
- dashboard/static asset integrity and cache policy;
- shared state cache behaviour;
- fixed-length asset and state/statistics transport;
- persistent settings write/restore safety;
- grow/heating/light/relay main-loop handoffs;
- irrigation/Flood safe handoff paths;
- diary/LittleFS guarded handoff paths;
- Shelly runtime and dashboard settings paths;
- System Settings and Factory Reset behaviour;
- Wi-Fi captive portal ownership;
- OTA GitHub manifest check;
- parallel, burst and recovery load tests.

## Upgrade note

v0.4.2 keeps the existing ESP32 persistent settings/NVS layout and the established GrowTent hardware-control behaviour. The release changes and finalizes the web-server architecture and network ownership; it does not intentionally reset configured grow, relay, irrigation or Shelly values.

After installation, the dashboard is expected on port `8080`.
