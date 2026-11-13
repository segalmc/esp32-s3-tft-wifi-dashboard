# Communication regression evidence

PASS: 11 pre-existing host checks: NetworkPolicy reconnect/recovery/NTP independence and persistence; TemperatureHistory gap/invalid/clock rollback handling; WSS policy, receive, session, clock recovery, TLS failure, listener/orphan/receive callsites, and mock client.

NOT TESTED: Real ESP-NOW packet delivery, ACK/retry behavior, Wi-Fi recovery under physical loss, HTTP fallback under physical loss, real sensor and valve behavior. No firmware was flashed, no relay commands were sent, and no hardware validation is claimed.

FAIL (environment): Existing Tuya suite compiled with local Node OpenSSL headers and runtime libcrypto.so.3. 322 checks executed, 314 passed; 8 native socket listener preconditions failed because sandbox prohibits socket creation. Pure mock protocol/authentication/fragmentation/ON-OFF/uncertainty checks passed; native network cases NOT TESTED. No test was changed. See tuya-protocol-mocks.log.

Starting firmware tracked-source hashes are saved in *-starting-hashes.json for final unchanged-code verification. Original ESP32 source/config must remain identical. New S3 integration should consume existing snapshots without modifying the packet, retry, initialization, sensor, or Wi-Fi code paths.

Wireless ADB: NOT TESTED; sandbox denied ADB smartsocket listener. See Android test-evidence/adb-diagnostics.log. No APK was installed.
