# Hot Water Platform — final implementation report

Software implemented and built offline. No firmware flashed, APK installed, OTA triggered, NVS erased, gate enabled, credential changed, or real relay command sent. No commits, pushes, resets, cleans, branch switches, or tag changes. Existing dirty work preserved; starting revisions/status are recorded here and in Android evidence.

## Implemented features

- Timer presets 15/30/45/60 minutes and targets 40/50/60°C, with explicit selections distinct from implicit 30-minute/50°C defaults. Repeated selection returns to default; DEMO retained.
- Controller-owned 15/30-minute heating sessions. Verified metadata and unchanged shutdown lease are persisted before one ON attempt, followed by authenticated relay verification. Already-satisfied fresh boiler targets avoid activation.
- Timer, fresh valid boiler temperature, and manual OFF end sessions. Timer wins simultaneous timer/target events. Shutdown requests latch through network loss and uncertain OFF; retries retain the obligation until verified OFF and verified lease removal. Solar temperature is excluded.
- Authenticated ON settings and authoritative `heating` status, including remaining time, effective settings, telemetry freshness, shutdown reason and pending uncertainty. Existing authentication, Origin/CSRF checks, queue/IDs, and USB authorization remain.
- Android displays controller settings, temperature, per-second monotonic countdown, progress and uncertainty. Existing two-second synchronization does not resend ON or extend controller deadlines. Restart restores from status. Countdown reaching zero says “Turning off...” until verification.
- Persisted default-enabled “Keep screen on while heating” uses only the window flag. It retains screen-on through active/disconnected/pending/uncertain states and releases after verified OFF. User OFF remains available for controller-authorized uncertain sessions.

## Modified files

S3: `include/{BoilerPolicy,BoilerWebApi,TuyaControl,HeatingSessionPolicy}.h`, `src/{BoilerWeb,TuyaControl,main}.cpp`, `tools/test_boiler_policy.cpp`, `tools/test_heating_session.cpp`, `tools/test_heating_control.py`, `tools/heating_control_stubs/{Runtime.h,control_test.cpp}`, and `docs/boiler-poc.md`.

Android: `ControllerState.java`, `DemoController.java`, `HeatingPresentation.java`, `LiveController.java`, `LiveStatusParser.java`, `MainActivity.java`, `PresetGate.java`; `activity_main.xml`, `activity_settings.xml`; new `HeatingPresentationTest.java`, `LiveControllerTransportTest.java`; `tools/build_offline.py`; README/API/architecture documentation. Paths are under the existing app package/resource/test directories. `Prefs` already provided persisted default-enabled screen settings and was reused.

Evidence and task-only rollback patches added in both `test-evidence` directories. Original ESP32 files and existing Android `dist` APK were untouched. Preexisting S3 tooling/config/log files and Android artifacts remain intact.

## Tests and builds

| Result | Check | Evidence |
|---|---|---|
| PASS | 27 scenarios compiling actual production TuyaControl with mocked relay, storage, clock, queues and Wi-Fi | `heating-control-integration.log` |
| PASS | Session policy, strict settings parsing, temperature validity/freshness, all preset duration models, 45/60 restriction, NVS corruption/readback, legacy migration | `heating-session.log` |
| PASS | Existing and extended lease/DPS/API/authentication/Origin/CSRF/queue tests | `boiler-policy.log` |
| PASS | 11 communication/network/history/WSS regression checks | `communication-tests.json` |
| PASS | Android 22 JUnit tests, including actual in-memory HTTP transport, plus existing DEMO tests | Android `android-unit-tests.log`, `android-demo-tests.log` |
| PASS | S3 TFT USB and TFT OTA firmware builds | `s3-final-build.log` |
| PASS | Complete API16 Android APK built with local SDK tools; v1 signing verification | Android `android-sdk-build.log`, `hot-water-heating-debug.apk` |
| FAIL (environment) | Existing Tuya protocol suite: 314/322 checks passed; eight native socket listener prerequisites fail under sandbox socket restrictions. Native cases NOT TESTED; tests unchanged | `tuya-protocol-mocks.log` |
| NOT TESTED | Gradle task completion: daemon socket blocked; complete SDK APK build succeeded instead | Android `android-build.log` |
| NOT TESTED | Wireless ADB: local smartsocket permission denied | Android `adb-diagnostics.log` |

## Communication stability assessment

PASS offline: all 19 tracked original ESP32 files match their starting hashes. S3 NetworkManager, WSS receive/session/clock/retry policies, history, Tuya protocol and transport match their starting hashes. `main.cpp` only passes an existing telemetry snapshot to the heating controller; it does not add receive/network traffic or change scheduling. Hardware stability is NOT TESTED. See `communication-final-hashes.json` and `communication-assessment.md`.

## Safety migration and restrictions

**Physical 45/60-minute operation remains disabled.** These presets are selectable/model-tested, but ON requests above 1800 seconds return HTTP 422. `BoilerConfig.h` and its physical limit were not increased. An extended-duration persistence migration is not approved or enabled.

Legacy v1/v2 leases retain their exact stored deadlines; no ON can replace/extend one. New target metadata is verified separately. Reboot recovery bounds remaining time monotonically; clock loss/backward uncertainty requests conservative OFF. Missing metadata is indistinguishable from a legacy lease: its timer remains authoritative, with no recovered target. Present corrupt/mismatched metadata or corrupt leases trigger recovery OFF. Unreadable NVS exposes an unknown obligation and blocks ON without inventing relay authorization; the obligation cannot be cleared while storage remains unreadable. See `safety-independent-review.md`.

Deployment remains pending separate approval and hardware validation. Software/network/power failures can prevent timely physical OFF; build and simulation results do not establish hardware safety.

## Pending hardware tests

NOT TESTED: physical relay confirmation and shutdown timing; sensor target crossings/disconnection; reboot/power-cut NVS recovery; real Wi-Fi/NTP loss; ESP-NOW/HTTP fallback under load; API16 tablet rendering/lifecycle/countdown/screen timeout; deployment and rollback. No original ESP32 rebuild was needed because its firmware was unchanged.

## Rollback and next step

Installed devices are unchanged. Source rollback is reviewable without discarding earlier dirty work: in each modified repository inspect `test-evidence/implementation.patch`, run `git apply --reverse --check test-evidence/implementation.patch`, then apply it in reverse only if desired. Both reverse checks PASS. The patches exclude initial dirty edits and generated evidence; do not use reset/clean or revert the initial changes. Before any later firmware rollback, obtain verified physical OFF and ensure no outstanding lease; preserve NVS. Review the safety changes and separately authorize deployment/hardware tests before enabling any real extended session.

## Focused safety review and Android wake follow-up

Reviewed actual `TuyaControl.cpp`, heating/lease policies, Web authentication/API, USB gate call sites, telemetry wiring and Android transport/lifecycle/presentation. No confirmed S3 defect required a change; S3 source remains unchanged in this follow-up.

- Persistence/reboot: verified lease before ON, unchanged stored deadlines, conservative corrupt/clock recovery and monotonic recovered budget remain in place. Missing new metadata still falls back to the legacy timer without a target; unreadable NVS still blocks ON without inventing control authorization.
- Limits: the physical ceiling remains 1800 seconds; 45/60-minute ON is rejected. No migration/gate enabled.
- Temperature: only valid fresh boiler telemetry can trigger target shutdown; stale/disconnected telemetry cannot satisfy the target, and the persisted timer remains active. Target shutdown stays latched across communication loss. This is not a guarantee of temperature shutdown without valid telemetry.
- Tuya: uncertain ON is never resent; ACK is not relay verification; OFF retries and verified lease removal retain the obligation. Blocking reads/reconciliation execute in the dedicated Tuya worker, not the Wi-Fi receive path. Existing finite network/verification waits and 15-second OFF retry mean physical shutdown timing still needs hardware validation.
- Access: session authorization, same-origin/CSRF validation, request admission/serialization and USB-only control enablement remain intact. Android cached state is display-only and cannot authorize commands. Existing local HTTP transport is unchanged.
- Communications: current hashes match the saved baseline for all 19 original ESP32 files and 14 protected S3 communication/transport/history files (`review-protected-hashes.json`). Prior communication regressions were reviewed; unchanged firmware was not rebuilt or deployed again. Live ESP-NOW/Wi-Fi stability remains untested.
- Android: fixed UI monitor blocking, UNKNOWN-on-wake rendering, initial retry delay and omitted commandBusy copying. Added display-cache process recovery, monotonic countdown estimates, coalesced foreground polling, callback lifecycle guards and wake timestamps. Active/pending/disconnected heating retains screen-on policy; confirmed OFF releases it. No services or Wi-Fi locks added.

PASS again: **27 actual production TuyaControl host scenarios** (`review-heating-control.log`), heating-session and boiler policy/DPS/auth/CSRF/queue suites (`review-policy.log`; an initial host compile omitted ArduinoJson's include directory and was corrected). Android: **35 host tests plus DEMO**, SDK APK min API16 packaging and v1 signature verification pass. Details, source list, current artifact hash and limitations: [Android WAKE-REVIEW.md](../../hot-water-android-tablet/test-evidence/WAKE-REVIEW.md).

Retried blocked checks once: Tuya suite still reports 314/322 successful checks, with the same eight native IPv4 listener prerequisites failing (`review-tuya-retry.log`). IPv4 socket creation independently returns EPERM; these are untested native cases, not protocol assertions. Gradle's single-use daemon and ADB smartsocket likewise still cannot create TCP listeners; API16 hardware logs and Wi-Fi sleep policy cannot be inspected. No unsafe workaround or production command was attempted.

**The reported 10-second wake delay has no measured root-cause attribution here, and the 2-second target is not claimed.** Next step is separately authorized tablet installation and passive screen-wake/logcat measurement when ADB is available. No firmware flash, APK installation, real actuator test, credential change, commit or push occurred in this follow-up.

## Deployed API investigation

User-provided runtime status confirms no heating object and controlGate=false. Current source always emits heating; committed predecessor 8969397 does not. Identical serializer fixtures reproduce that difference. The existing implementation report records no deployment, so the running API is inconsistent with the current heating implementation; exact running binary identity remains unverified. The closed control flag independently forces available.on=false by design. No code/gate/deployment change is required or authorized here. Focused controller/session/policy tests pass again. See [DEPLOYED-API-REVIEW.md](DEPLOYED-API-REVIEW.md) for evidence, source locations and a plan requiring explicit flash approval.
