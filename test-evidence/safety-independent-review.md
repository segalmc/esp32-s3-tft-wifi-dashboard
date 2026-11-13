# Independent software safety review

Scope: actual production TuyaControl.cpp, HeatingSessionPolicy.h, BoilerPolicy.h, BoilerWeb.cpp/API, and main telemetry wiring. No flash, install, control-gate changes, or physical commands.

PASS: The physical max remains 1800 seconds; 2700/3600-second requests reject explicitly. Legacy v1/v2 lease layout and deadlines remain unchanged. New target metadata is separate, checked against lease timestamps/duration, and verified before lease persistence and ON. No existing lease is overwritten to extend a session.

PASS: Actual production controller host integration compiles production source/header/policies with mocked storage, tasks, network status, time and relay. 27 scenarios pass, including default settings, selected duration, target already met, invalid/stale/disconnected temperature, persistence write/readback failures, ON uncertainty and no resend, OFF verification/removal failure, command serialization, USB gate, NTP/Wi-Fi loss, legacy/corrupt recovery, actual timer/target precedence, OFF retries, and reconnection after a latched target shutdown.

Review findings fixed during implementation: legacy recovered sessions must have no temperature target; recovered remaining time needs a monotonic budget; backward time before creation triggers conservative shutdown; lease deletion readback failure retains an uncertain shutdown obligation; ON must recheck time after asynchronous status work.

PASS: Original ESP32 source/config unchanged (19 tracked files compared). S3 communication/network/receive/ACK-independent client/transport source hashes remain unchanged. main.cpp only reads an existing telemetry snapshot and passes its valid fresh boiler value to the controller. No packet, receive, retry, reconnect, sensor or scheduling implementation changes were found.

PASS: Namespace-open failure now exposes an unknown active shutdown obligation, pending recovery, storage fault and uncertain progress; rejects ON; cannot clear the obligation while NVS is unreadable. It does not infer authorization for automatic relay traffic from unreadable storage. Integration test verifies no ON/OFF writes with closed gates. Service/independent physical OFF may be required until storage and authorization recover.

Persistence limitation: A missing new metadata key is indistinguishable from a legacy lease, so recovery retains the original timer deadline and disables temperature-target recovery. A present but corrupt or mismatched metadata key triggers conservative recovery OFF. No extended-duration lease format is enabled; 45/60-minute migration remains pending separate review/approval.

FAIL (environment): Full pre-existing Tuya protocol suite compiled and ran 322 checks; 314 pass and 8 native loopback-listener preconditions fail because sandbox denies socket creation. Those native socket cases are NOT TESTED. Tests remain unchanged.

NOT TESTED: Hardware shutdown, physical Tuya verification, NVS power-cut behavior, live Wi-Fi/ESP-NOW stability, live countdown/screen behavior. Independent hardware protection and separately approved deployment validation are still required. 45/60-minute physical operation remains disabled pending a separately reviewed migration and approval.
