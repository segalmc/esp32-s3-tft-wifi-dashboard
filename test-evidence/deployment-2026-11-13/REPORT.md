# S3 deployment and verification — 2026-11-13

Deployment completed on ESP32-S3 MAC `PRIVATE_DEVICE_MAC`, at `PRIVATE_DEVICE_IP`. The running firmware returns the heating API alongside the existing original-controller telemetry and ESP-NOW diagnostics. Android recognizes heating support. Relay feedback remains **OFF**, no heating/shutdown lease is active, and **controlGate=false**.

## Deployed source and image

- Source: `/home/michael/Projects/esp32-s3-heating-deployment`, a separate detached worktree based on stable communications commit `5c9ed85`, with the tested heating changes integrated without a commit.
- Environment: `esp32-s3-n16r8-tft`; explicit native USB upload, not the default OTA target.
- Firmware: 1,772,240 bytes; SHA-256 `185de8b66b21aa108bd2814b95ebe7d91b8b8d626779d525f23a5ba59ceadeff`.
- Build and upload passed; esptool verified the written data hashes. Source manifest: [deployed-source-sha256.json](deployed-source-sha256.json). Reviewable changes: [integration.patch](integration.patch).
- Production changes: BoilerPolicy, HeatingSessionPolicy, BoilerWebApi, TuyaControl header/source, BoilerWeb command/status handling, and main.cpp temperature wiring. Transport implementations, HTTP telemetry routes, web session table, display implementation and network configuration retain the stable baseline.

The requested checkout (`feat/landscape-dashboard`, HEAD `8969397`, plus existing uncommitted heating changes) contained WSS-only telemetry, while the device was running ESP-NOW/HTTP firmware. Its first deployment exposed heating but removed the existing telemetry endpoints. Verification detected this, and the original bootloader, app and OTA metadata were restored from the private backup; fresh OFF and restored telemetry were confirmed. The final image integrates the same tested heating controller/policies into the communications baseline instead. The requested checkout's source files were not changed.

## Identity, preservation and recovery

- Only `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_E0:72:A1:D6:A1:78-if00` was opened or flashed. Esptool positively identified ESP32-S3 QFN56 revision 0.2, 40 MHz crystal, 16 MB quad flash and embedded 8 MB octal PSRAM.
- Installed partitions matched the build byte for byte: NVS `0x9000/0x5000`, OTA metadata `0xe000/0x2000`, app0 `0x10000/0x640000`, app1 `0x650000/0x640000`, SPIFFS `0xc90000/0x360000`, coredump `0xff0000/0x10000`.
- A complete 16 MB flash backup, original app with validated image digest, bootloader, OTA metadata and NVS are private in `/home/michael/s3-deployment-backup.Z5w5i3` (directory mode 0700, binary files 0600). The deployed image is also saved there. No credential-bearing binaries are included with this report.
- Installed Tuya identity/key/IP, web password salt/hash and telemetry configuration matched the build configuration. No credentials were regenerated or rotated.
- After the final upload, the complete NVS region and partition table matched their predeployment bytes exactly. NVS SHA-256: `a64beadb44342d4f89b2d7db1b96ddfb16281ef45b3203c19e321222b4762700`. See [final-flash-verification.json](final-flash-verification.json).
- Native USB bootloader recovery was exercised successfully. Full-flash erase was never used. Opening native serial triggered USB resets; these were observed as USB reset boots, not unexplained crashes.
- The original ESP32's CH340 port was never opened or flashed. All 22 recorded source-file hashes in the read-only original repository remained unchanged. Android source and installed APK were not modified.

## Safety and API results

Before deployment and rollback, authenticated Tuya 3.4 status reported fresh OFF, no lease and closed control. No Tuya ON or OFF relay command was issued, and control was never enabled. OFF evidence is authenticated switch DPS feedback; relay contacts/load current were not independently measured.

The final status includes all eleven heating fields: active, shutdownPending, durationSeconds, targetC, remainingSeconds, deadlineEpoch, shutdownReason, temperatureFresh, boilerC and physicalMaxSeconds. Inactive defaults are 1800 seconds/50°C, with zero remaining time/deadline and physicalMaxSeconds=1800. No session or shutdown obligation was created. Existing 45/60-minute restrictions remain; oversized ON requests were tested offline only.

Heating uses the existing authenticated boiler measurement, boiler-valid flag, communication freshness and sender sample age. It respects the existing 10-second telemetry freshness threshold and the heating policy's limit. Solar temperature cannot satisfy boiler target shutdown. Stale boiler measurements are exposed as unavailable.

Live HTTP checks passed: unauthenticated status 401; existing-password login and authenticated status 200; missing CSRF 403; foreign Origin with valid CSRF 403; authorized read-only refresh 202. The telemetry challenge endpoint returned a valid 200 response, and an empty unsigned telemetry POST was rejected with 413. No synthetic telemetry was injected. See [api-checks.json](api-checks.json) and [http-route-checks.json](http-route-checks.json).

## Communications and Android

- Wi-Fi reconnected using preserved settings; DHCP address `.229` and Israel clock synchronization were confirmed. The configured OTA default `.230` is not the current device address and was not used.
- Tuya authenticated successfully and reconciled OFF. During monitored operation, authentication, connection and heartbeat error counters remained zero.
- Across 31 authenticated samples over 61.99 seconds, ESP-NOW data and ACK counters increased from 2 to 24, with zero authentication/replay rejects and ACK-send failures. Fresh boiler temperatures reached both original telemetry and heating (24.81°C in the last sample). Relay/gate/lease assertions passed throughout. See [soak-summary.log](soak-summary.log) and [soak.json](soak.json).
- HTTP fallback was receiving live original-controller measurements before deployment. Its code/routes and credentials are preserved; final HTTP endpoint checks and all fallback host tests pass. A forced live failover was not performed; final sustained traffic used ESP-NOW.
- TFT boot logs confirm ST7796S initialization, 320×480 dashboard rendering, photo rendering and periodic updates. At 90 seconds: approximately 197 kB free heap and 8.13 MB free PSRAM; no panic, watchdog or reset loop in the 120-second capture. TFT has no MISO readback; visual panel inspection was not available.
- ADB connected to `PRIVATE_DEVICE_IP:5555`. Final BUTTON_GATES reports signedIn=true, connected=true, heatingSupported=true, heatingActive=false, leaseActive=false, shutdownPending=false, s3ControlGate=false, presetsEnabled=true and powerEnabled=false. `heating_object_missing` cleared; stable power blocker is only `s3_on_not_available`. See [android-final-gates.log](android-final-gates.log).

## Tests and limits

PASS: 27 actual production heating-controller host scenarios; heating session/deadline/recovery/preset tests; boiler/DPS/queue/authentication/CSRF and stable multi-session tests; 415 Tuya protocol/native transport checks; 13 HTTP/ESP-NOW/authentication/freshness/replay programs; 42 Android host tests plus DEMO checks. Network, history, OTA policy and WebSocket regression checks also passed.

One pre-existing OTA helper test failed because its fake `serve()` function uses obsolete argument names. Three other tests pass; inspection confirms the installed ESPOTA signature matches the production uploader. No OTA upload was attempted and the fixture was not changed.

Physical heating, timer-triggered relay OFF, target-triggered relay OFF, power-loss recovery under load, forced Wi-Fi/ESP-NOW failover and independent physical relay-contact measurements were not performed. Original-controller valve telemetry remains explicitly inferred LED-bench evidence.

No deployment blocker remains. Use the integrated worktree for subsequent builds; uploading the unchanged landscape checkout would remove the existing communications again. Review/consolidate the integration patch before the next development cycle. No commit or push was made.
