# TuyaLocalClient migration guide

The TFT/Web boiler application is the first consumer of `TuyaLocalClient`.
It keeps its existing single-slot Serial/Web queue and I/O task. The library
owns the Tuya socket, handshake, push receive, heartbeat, and reconnect policy;
`TuyaControl` still validates DPS 1 and owns USB gates, NTP and NVS checks,
the 30-minute obligation, command serialization, and ON/OFF verification.

## Boiler configuration and persisted deadlines

The application uses the compile-time `boiler_config::kActive` in
`include/BoilerConfig.h`. Edit its named `kConfigured...` constants only as a
deliberate firmware change; invalid values fail the build. The defaults retain
the current behavior. The generic library has no boiler timer, USB gate,
Web authorization, or NVS namespace.

| `BoilerConfig` field | Default | Allowed range and effect |
| --- | --- | --- |
| `maxOnSeconds` | 1800 s (30 min) | 60–1800 s. Maximum persisted deadline from an ESP32 ON request; shorter values are allowed, longer values rejected. |
| `offRetryIntervalMs` | 15000 ms | 1000–15000 ms between safety OFF attempts after the deadline is due. |
| `onStateMaxAgeMs` | 30000 ms | 1000–30000 ms maximum age of authenticated OFF before ON can be offered. ON still performs a new authenticated OFF read. |
| `staleStateAfterMs` | 45000 ms | Between `onStateMaxAgeMs` and 45000 ms; older state is displayed as STALE and cannot justify ON. |
| `verificationReadAttempts` | 3 | 3–5 authenticated state reads after each command. These retry reads, never the ON command. |
| `verificationReadDelayMs` | 800 ms | 100–800 ms between verification reads. |
| `clock` | `SynchronizedNtp` | This is the only accepted value. ON requires a valid synchronized wall clock. |
| `persistence` | `VerifiedNvsBeforeOn` | This is the only accepted value. The lease must be written and read back before ON. |

The clock validity floor remains 2024-01-01 UTC; it is not exposed as a
weaker setting. The boiler still rejects ON when NTP is unavailable.

The NVS namespace `boiler`, lease key `lease`, monitor key `monitor`, and
deadline interpretation are deliberately fixed. Changing any of these could
orphan an active shutdown obligation. Existing version-1 leases retain their
exact 30-minute deadline. A shorter configured duration uses a version-2 lease
with the same stored layout. On reboot, the effective deadline is the earlier
of the persisted deadline and `created + configured maximum`; changing
configuration can shorten, but cannot extend, an existing obligation. NVS
refuses a second lease write while any lease record remains. A corrupt or
unrecognized lease still disables normal control pending service. An active
lease clears only after authenticated OFF following the ESP32's own OFF request.

To adopt the library in the original ESP32 temperature controller later:

1. Keep that project unchanged until its current controller behavior and
   safety policies have been recorded with tests. Add both `TuyaLocalClient`
   and the pinned `LynkTuyaLocal` folders to its PlatformIO `lib/` directory.
2. Supply a private device ID, 16-byte Local Key, and IP through its own
   configuration; never copy the TFT application's `secrets.h` or NVS data.
   Construct one client and call `begin` after configuration validation.
3. Choose one owner context for all `poll`, reads, and writes. If temperature
   sampling or display work has a strict latency budget, use an
   application-owned I/O task because a due Tuya exchange has a bounded
   synchronous wait. Do not add a second Tuya socket or heartbeat loop.
4. Register status and push callbacks. Authenticate frames in the library,
   then validate each required DPS and its type in the controller. Treat an
   absent, stale, or disconnected state as unknown, never as OFF.
5. Put each physical command through the controller's own authorization and
   safety checks. Persist any shutdown deadline **before** ON, send ON once,
   then verify the actual state with an authenticated read. Keep uncertain ON
   separate from confirmed state. Clear a shutdown obligation only after a
   verified OFF following the controller's OFF request.
6. Test with the existing mock transport before hardware cutover. Confirm the
   current Tuya owner is stopped and its socket closed before enabling another
   persistent client. Preserve a rollback path to the previous controller.

This guide does not change or authorize changes to the original ESP32 project.
