# Heating platform repair — 2026-11-13

## Confirmed causes

1. **Synchronous S3 USB diagnostic output delays ESP-NOW discovery and ACKs when the host stops reading.** The installed `HWCDC::write()` waits on its transmit ring and lock. Its default 100 ms timeout allows a stalled write to consume about two seconds, longer than the original's 200 ms discovery response window and 720 ms application ACK window. The receiver logs peer details before transmitting discovery replies/ACKs; several other tasks share the same USB output lock.

   Live reproduction on the intermediate build: pause only the serial capture process, leaving network polling and both controllers running. ESP-NOW initially remains healthy, then data reception stops at 33 packets and the sender enters HTTP fallback. Receiver task age reaches **11,185 ms**; authenticated discovery replies keep increasing without successful recovery data. Authentication, replay, storage and queue-full counters remain zero. Resume the reader and discovery/application exchanges recover. This explains how many successful MAC-level discovery replies can coexist with few authenticated data packets: successful MAC delivery does not mean the sender was still waiting for that nonce.

   Fix: configure `Serial.setTxTimeoutMs(0)` immediately after `Serial.begin()`, before startup logging. Diagnostic output may be dropped under USB backpressure; it cannot wait for a host. A host regression exercises the installed driver's actual `write()` with fake FreeRTOS waits, including a full ring, a held diagnostic lock and resumed delivery.

2. **A failed discovery reply leaves the S3 peer unencrypted.** Previously the receiver replaced its encrypted peer with a clear peer, then skipped restoration if reply transmission failed. Historical capture `historical-discovery-failure.txt` shows a failed reply followed by another clear-peer registration without an intervening encrypted restoration. The supplied 13 reply failures establish that this failure path occurred, but cannot individually identify each RF/MAC failure's initiating cause.

   Fix: attempt encrypted-peer restoration on every discovery outcome, including clear-peer registration failure. Authorize a new sender boot/MAC only after clear-peer setup, successful reply and encrypted-peer setup all succeed. Regression covers every combination and repeated failed replies followed by recovery. HMAC, receiver boot binding, LMK, replay checks and ACK validation are unchanged.

3. **Clock sampled before concurrent telemetry snapshot can underflow ages.** One stalled-reader comparison sample reported task age `4294967293` ms even though all packets were fresh. API status sampled `now` before snapshotting receiver timestamps; an update a few milliseconds later looked about 49 days old after unsigned subtraction. The heating update had the same ordering. Both now sample their clock after the telemetry snapshot. A regression guards both production call sites and checks concurrent-update/wrap arithmetic. This race can cause transient false stale/OFFLINE classifications; it does not explain the supplied continuous 760-second outage.

## Scope and evidence limits

- The authoritative integrated S3 worktree supplied both deployed images. The outdated S3 repository supplied only an existing local dashboard password to the read-only verifier; no firmware was built or deployed from it.
- The dynamically discovered CH340 ESP32 boots the DS18B20 **LED bench** from `esp32-original-http-telemetry-client/poc/original-esp32/firmware`. Its banner says physical outputs are disabled and no motor/relay is connected. The legacy `hot_water_ctrl_arduino` repository is not the deployed sender and was left unchanged.
- Initial serial opens restarted both boards; the initial 760-second incident was therefore interrupted before a simultaneous uninterrupted capture could be made. The USB failure mechanism above was subsequently reproduced. The entire historical outage and all 13 individual discovery transmission failures cannot be reconstructed conclusively from cumulative counters. Earlier sender captures also contain `NO_AP_FOUND` and `AUTH_EXPIRE` during Wi-Fi fallback; their underlying RF/AP cause remains unproven.
- Channel mismatch is not established as the cause. Working discovery and the reproduced stalled-reader run use channel 1. Neither Wi-Fi credentials nor channel policy was changed.
- Current telemetry queues/history are bounded volatile RAM (V10), despite older documentation describing durable telemetry journals. This repair does not change that existing limitation. Heating shutdown obligations remain persisted independently in NVS.

## Changes made in this run

- S3 `src/main.cpp`, `src/BoilerWeb.cpp`: nonblocking native USB diagnostic writes and clock-after-snapshot ordering for freshness/link diagnostics.
- S3 `src/LocalHttpTelemetry.cpp`, `include/NowDiscoveryExchange.h`: restore encrypted peers on failed discovery; retain strict session admission.
- S3 `tests/now_discovery_exchange_test.cpp`, `tests/run_http_checks.py`, `tools/test_usb_serial_latency.py`, `tools/test_telemetry_snapshot_order.py`: discovery failure/recovery and actual-driver backpressure regressions.
- Android `app/src/test/java/android/util/Log.java`: test-only logging stub so the Gradle transport tests run without Android's throwing framework stub. Production Android code/APK is unchanged.
- This evidence directory contains focused test/build logs and runtime summaries. Pre-existing uncommitted heating/Android changes were preserved; they are not attributed to this run.

## Tests and builds

- S3: 14 telemetry/protocol/authentication/acceptance/freshness/replay/link/API/discovery C++ suites, plus actual USB driver backpressure and snapshot/time ordering tests: PASS.
- Original: 15 telemetry/ESP-NOW/channel/transport/storage/LED/target C++ suites and 5 production call-site checks: PASS. Additional Wi-Fi reconnect/backoff, link probe and sensor/controller core suites: PASS.
- Heating: 27 actual production `TuyaControl` scenarios with mocked hardware/NVS/time: PASS. Five policy suites cover timer/target/freshness, restart leases, duration restrictions, gates, network, OTA and history: PASS. Browser dashboard checks: PASS.
- Android: 42 host JUnit tests and DEMO checks: PASS. Gradle `:app:testDebugUnitTest`: 42 PASS after the test-only logging stub. Gradle `:app:assembleDebug`: PASS, minimum API 16.
- S3 TFT build: PASS, final binary **1,772,592 bytes**, SHA-256 `c6a1cda9addb1044f85e03a8fe70293f18a880ba31e98763a68e0e9d1bc138d5`.
- Original `led_bench` build: PASS, flash usage 1,039,332 / 1,310,720 bytes. No original firmware write was needed.
- Secure OTA tests: PASS. Git whitespace checks: PASS.

## Deployment and safety

- Verified S3 address-to-MAC neighbor mapping and native USB descriptor against the requested device. A same-day read from this board established the installed 16 MiB partition layout; its partition bytes match the build. App slots are `0x10000` and `0x650000`, each `0x640000` bytes. Existing private full-flash/NVS backups and USB recovery remain available.
- Three authenticated S3 OTA updates succeeded: peer restoration, nonblocking USB, then snapshot/time ordering. The final image is identified by the hash above. Its reboot was observed at 16:45:57 UTC (uptime 4,093 ms). OTA updated an application slot; no erase, partition, credential, calibration or filesystem operation was issued.
- Android installed APK was pulled read-only and matched the locally built APK byte-for-byte: SHA-256 `b7b33c844ddb68f2594c93cbc8b4822d24c7b550d4147071dd0badba623ac602`. No reinstall was necessary. ADB confirms API 16 and the expected package.
- Live tablet inspection confirmed fresh temperatures, relay OFF, controller transport, local duration/temperature selection and the deliberately unavailable physical power control. Selecting 45 minutes / 40°C changes local selection without issuing a heating command; defaults were restored afterward. Tablet authentication/status recovered after S3 OTA restart.
- No physical ON, OFF, valve or gate-opening command was issued. The 1,800-second cap, target shutdown, restart shutdown obligations, authenticated Tuya verification and OFF retry policy were tested with mocks. Gates and leases were not changed by test shortcuts.

## Runtime results

- Four-minute reader-stop comparison: before the USB fix, 47 samples transitioned from ESP-NOW to HTTP fallback and task age reached 11,185 ms. After the USB fix, all 48 samples remained ESP-NOW, measurement age stayed at or below 3,675 ms, and ACKs increased from 8 to 87. One task-age arithmetic wrap in this comparison led to the separate timestamp ordering fix; valid task ages stayed below one second.
- Bounded `link both off 90` test: OFFLINE appeared after the 35-second communication deadline; heating temperature became unavailable. Sensor/controller evaluation continued and the sender retained backlog. A USB one-shot Wi-Fi reconnect reacquired an IP; after the bounded fault expired, authenticated HTTP restored fresh data and ESP-NOW recovered after its three-success policy. One missed discovery reset that policy correctly; there was no sustained failure loop.
- A hardware reset of the identified LED bench at 16:46:26 UTC produced safe boot and authenticated ESP-NOW pairing by sender uptime 382 ms. Final stability sampling starts at 16:46:43 UTC.
- The final soak includes an additional eight-minute interval with the serial reader stopped.
- Android UI and preset behavior were verified live before ADB disappeared around 16:42 UTC. Subsequently the specified Wi-Fi endpoint returned `No route to host`, ping failed, and the USB ADB transport remained offline. Host USB runtime status was active with autosuspend disabled. Final on-tablet reinspection is blocked; no speculative network, USB-power or credential changes were made. The earlier successful UI checks do not establish its final screen state.

Final post-deployment soak, stalled-reader comparison, bounded outage/restart checks and before/after counter values are recorded in `runtime-summary.json` and `runtime-metrics.jsonl` alongside this report.

## Final verification outcome

Final uninterrupted interval: **2026-11-13T16:46:43.015371+00:00 through 2026-11-13T16:56:51.343611+00:00**, 608.3 seconds, 121 samples. Every sample was ESP-NOW, heating temperature fresh, Tuya connected, relay OFF, control gate false, lease inactive and command ID zero. No poll errors; maximum poll gap 5.165 seconds. Maximum measurement age **3707 ms**; maximum receiver task age **994 ms**. ACKs increased from **8 to 211**. Discovery count remained 2; all discovery failure/rejection, authentication, replay, storage, acceptance, queue-full and ACK-send-failure counters were zero. No timestamp wrap occurred.

The original/S3 telemetry repair is deployed and passes the measured hardware checks. Final end-to-end tablet screen verification remains incomplete because the tablet became unreachable. The current installed APK had matched the tested build and its live UI/presets were verified earlier; reconnecting/waking the tablet and repeating the read-only UI check is the remaining step. No production motor-valve hardware was identified or exercised. No commits or pushes were made.

## Authorized physical-control integration follow-up — 2026-11-13

**Status: Android repair deployed and verified; USB authorization and the physical ON/OFF cycle are blocked by a live competing Tuya proxy. No physical ON or OFF command was sent in this follow-up.** The user's explicit physical-test authorization was recognized; the prerequisite of stopping the competing owner was not bypassed.

### Live safety and ownership checks

The tablet became reachable again over network ADB, and the expected API-16 application was running. The S3 reports authenticated, fresh relay **OFF**, Tuya connected, clock synchronized, fresh ESP-NOW measurements, no heating lease or shutdown obligation, and physical maximum **1800 seconds**. `monitorArmed=true`, `readGate=true`, `controlGate=false`, `available.on=false`, and `available.off=false`.

The native S3 USB descriptor was discovered dynamically and matches the requested S3 MAC. No local Tuya proxy process, TCP listener on port 3000, or established local Tuya connection was found. However, the legacy UDP discovery protocol returned the Android TV host **PRIVATE_DEVICE_IP**, whose **port 3000 `/status` endpoint returns HTTP 200 with `{"status":false}`**. That is a live proxy service, not proof that the S3 is the sole owner. Consequently none of the requested USB arming/control commands were executed.

The TV's network ADB connection is **unauthorized**. SSH on port **8022** is available and matches an existing known host key, but existing local SSH login/identity access was rejected. Access information, TV ADB authorization, or stopping the proxy was requested. The proxy remained running at the final check. No credentials, host keys, authorization policy, router settings or remote processes were altered.

### Android changes and deployment

Changed in `/home/michael/Projects/hot-water-android-tablet`:

- `ButtonGate.java`: routine read-only status polling no longer blocks confirmation or backend-authorized OFF. Pending-command protection, authentication, freshness, backend availability and the physical-duration restriction remain enforced. Added specific unavailable messages.
- `MainActivity.java`: unavailable power taps explain the cause and request a refresh. Confirmation no longer silently disappears during a status poll or changed state. Control errors remain visible for eight seconds across successful polls. The view accepts diagnostic taps while unavailable; the command path still checks every gate. Pending commands disable duplicate taps. Logs distinguish a clickable view from actual command authorization.
- `ButtonGateTest.java`, `LiveControllerTransportTest.java`: regressions for poll/confirmation concurrency, OFF independence, duplicate protection, precise safety errors, and the six exact authenticated duration/target payload combinations.

Host JUnit: **45 PASS**, plus DEMO tests. Gradle `:app:testDebugUnitTest :app:assembleDebug`: **PASS**. The APK was installed with `adb install -r` and started successfully. Installed APK readback matches the tested build byte-for-byte: SHA-256 **`c9c9c85e14c06e57bf97a32373677b9fce697be7db069c76e33b2e053f351bf5`**. Existing login/preferences were preserved. No S3 or original firmware change or flash was needed in this follow-up.

### Verification and remaining step

The physical tablet's power view accepts taps and displays **“S3 USB control authorization is required”** without sending a command. Seven live preset/error checks passed: 15/30-minute selections, 40/50/60°C selections, 45-minute physical-limit warning with control still blocked, and restoration to **15 minutes / 50°C**. The tablet shows actual **OFF**, ESP-NOW and fresh temperatures. Mock transport tests check exact payloads, cookies, Origin and CSRF; mock safety tests verify restricted durations, countdown, timer/target shutdown, temperature freshness and restart obligations. **27 production TuyaControl scenarios** and heating/boiler policy suites pass. No additional physical heating cycles were attempted.

Final S3 check: relay **OFF**, Tuya connected, ESP-NOW active, measurement age **758 ms**, heating temperature fresh, lease inactive, shutdown pending false, command ID **0**. ESP-NOW ACK count reached **2066**, with zero discovery/authentication/replay/storage/queue/ACK-send failures. The USB control gate remains deliberately closed until sole ownership can be verified.

**Remaining prerequisite:** stop the TV proxy through its existing service manager and verify its process/socket and discovery/HTTP service are gone. Then run the documented USB authorization sequence and the single supervised Android-to-S3-to-Tuya ON/OFF cycle. Actual physical ON confirmation, ON/remaining-time UI, immediate OFF confirmation and post-test UI remain **unverified**. This is not a completed physical-control integration.

Detailed build/install logs, live UI checks, final status, APK and access-blocker evidence are in [`android-control-2026-11-13/`](android-control-2026-11-13/).

## Automatic startup control and direct tablet taps — 2026-11-13

This follow-up supersedes the previous USB commissioning requirement, under the user's explicit authorization to change that policy. **The S3 now initializes monitoring, reads and control automatically after every normal boot. The installed Android app sends ON/OFF directly on a tap, without the physical-command confirmation dialog.** Existing HTTP session/CSRF/Origin protections and command safety checks remain in force.

### Implementation

- `include/BoilerPolicy.h`, `src/TuyaControl.cpp`: boot initialization no longer depends on a legacy NVS monitor-authorization byte or USB enable command. Readable lease storage enables monitoring/control; unreadable storage still fails closed. Initialization never queues ON. Existing lease recovery, temperature shutdown, 1800-second maximum, authenticated state verification and OFF retries are unchanged. Optional USB maintenance commands remain available.
- `src/BoilerDashboardPage.h`, `docs/boiler-poc.md`: updated current startup guidance and removed stale claims that normal control requires USB authorization.
- `tools/test_boiler_policy.cpp`, `tools/heating_control_stubs/control_test.cpp`: regressions for boot without a marker/USB calls, reboot without activation, actual ON/OFF admission and lease verification after automatic boot, and Wi-Fi/Tuya reconnect without reauthorization.
- Android `ButtonGate.java` and `ButtonGateTest.java`: closed-runtime-gate errors describe controller health instead of requesting USB authorization. `MainActivity.java`: removed the ON/OFF confirmation dialog; direct taps still pass the same authorization/freshness/duration checks and reserve the pending command before asynchronous dispatch.
- Android `app/build.gradle`, `app/src/androidTest/java/com/hotwater/tablet/OffPathInstrumentation.java`: explicit, separately packaged OFF-only hardware test using the production `LiveController`, app-private saved credentials, HTTP authentication and CSRF. No ON path exists in that test. The temporary test APK was removed after verification; it is not a new service or a production endpoint.

### Tests and deployment

- **30 production TuyaControl scenarios PASS**, including automatic boot, no startup ON, commands, leases, timer/target shutdown, failed storage and reconnect.
- **415 Tuya protocol/client checks PASS**, including authenticated protocol handling, native connection recovery, heartbeat, uncertain commands, backoff/wrap and transport backpressure. Temporary OpenSSL development headers were extracted under `/tmp` for host compilation; no system library or firmware dependency was changed.
- **45 Android JUnit tests and DEMO checks PASS**; Gradle application/test builds PASS. Telemetry, boot policy and browser dashboard tests PASS.
- Authoritative S3 TFT build PASS. Final firmware: **1,772,576 bytes**, SHA-256 **`97cc917fcc97301193196619608028d4fee030f0168704374e274d44137e6270`**. Target identity, matching installed partition table and private full-flash/NVS backup/recovery path were checked. Authenticated OTA completed, followed by a fresh boot and verification of the new dashboard content. No full-flash erase, partition-layout or credential change was made.
- One final-page OTA transfer continued after a failed host preflight assertion because of shell command sequencing. It was terminated before completion while the active firmware continued running. The relay subsequently reported confirmed OFF with its lease cleared. The corrected deployment script requires three consecutive fresh OFF, no-lease, no-shutdown, idle samples before launching upload. The retry completed successfully. Its uploader logged an unexpected intermediate response `32` but returned success; the reboot, reset command counter and final deployed page were verified independently.
- Final dialog-free Android APK installed through ADB and verified byte-for-byte against the build: **91,320 bytes**, SHA-256 **`24c0fabd31b99a0114fca426cdd6cde0884c025f98850321c75c82b5a882ccc5`**. Saved login/preferences were preserved.

### Hardware evidence

After OTA reboot, the S3 reported `monitorArmed=true`, `readGate=true`, `controlGate=true`, and both authenticated ON/OFF actions available. No serial session was opened and **no USB authorization command was sent**. A boot snapshot recorded command ID zero, confirmed OFF and no lease, demonstrating that startup did not activate the heater.

During deployment the user operated the tablet ON and OFF and explicitly confirmed doing so in the conversation. Tablet logs capture fresh authenticated ON state with an active session/lease, confirmed progress, OFF availability and subsequent manual OFF/shutdown verification. These were **user-operated cycles**, not autonomous ON tests. Automated verification issued no ON request.

The separately installed device test authenticated from the tablet and sent OFF using the production Android HTTP client. It passed before and after the final S3 reboot; the post-reboot result was `passed=true`, `relay=OFF`, `commandId=1`. An authenticated Tuya reconnect also passed with relay OFF and control still enabled. Normal Android polling recovered after restart and reflected actual OFF/availability. No extra ON cycle was used to test dialog removal.

Final status: **ESP-NOW**, measurement age **1855 ms**, ACKs **34**, Tuya connected, fresh confirmed **OFF**, monitoring/reads/control enabled, `available.on=true`, `available.off=true`, no lease or pending shutdown, synchronized clock, physical limit **1800 seconds**. The device remains ready for direct authenticated tablet control.

### Verification limits

The USB power/data cable remained physically attached. No serial connection or USB commands were needed, and mock boot tests cover absence of prior USB authorization, but literal cable-unplug operation was not tested: host USB detach permissions and noninteractive sudo were unavailable. Power wiring was not changed.

The old proxy's HTTP status endpoint still responded during this follow-up. That is not proof of an active Tuya TCP session, but sole ownership could not be established from it. The proxy-stopped assumption was used for development as requested; **no autonomous physical activation was attempted**. The user performed the live ON/OFF operations. No new proxy or architecture was introduced.

Build/install logs, authenticated OFF/reconnect checks, user-cycle observations, boot/final status and artifacts are in [`automatic-control-2026-11-13/`](automatic-control-2026-11-13/).
