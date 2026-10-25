# TuyaLocalClient

An ESP32 Arduino wrapper around the pinned, locally hardened `LynkTuyaLocal`
Tuya 3.4 transport. In native mode the wrapper owns **one nonblocking IPv4 socket**, connection lifecycle,
authenticated status reads, push dispatch, heartbeats, 30-second reconciliation
by default, and bounded reconnect backoff. It does not create a task or store
application credentials in NVS. It does not decide whether a physical command
is safe.

## Dependencies and installation

This repository vendors `LynkTuyaLocal` beside this library. Both directories
must be in the PlatformIO project's `lib/` directory. Its crypto and frame
validation remain unchanged; cooperative entry points reuse the same nonce,
HMAC, decryption, padding, return-code, and DPS parsing. The native transport
uses the ESP32 Arduino core's lwIP BSD socket API (`O_NONBLOCK`, zero-timeout
`select`/`SO_ERROR`, and `MSG_DONTWAIT`). Arduino ESP32 core, Wi-Fi, and mbedTLS are supplied by
`framework = arduino` on `espressif32`; ArduinoJson 6.21.5 parses mapped DPS
values. No TFT, WebServer, Preferences, or boiler code is used.

## `TuyaLocalConfig`

Create the structure at startup and pass it to `begin`; invalid combinations
return `false` before network I/O. The library copies the device ID and key.
There is no runtime configuration setter. Durations are unsigned milliseconds
unless the name says otherwise.

| Field | Default | Valid value and effect |
| --- | --- | --- |
| `host` | `0.0.0.0` placeholder | Nonzero IPv4 address of the Tuya device; required before `begin`. |
| `port` | `6668` | 1–65535 TCP destination port. |
| `deviceId` | null | Required, 1–32 letters, digits, `_` or `-`; copied privately. |
| `localKey` | null | Required 16-character local key; copied privately and never logged. |
| `protocol` | `V34` | Only `V34` accepted. `V35` fails initialization because this wrapper's 3.5 path has not been validated. |
| `dps.stateBoolean` | `1` | 1–9999; parsed from authenticated status/push JSON by `onMappedBoolean` and `readMappedBoolean`. |
| `dps.controlBoolean` | `1` | 1–9999; used by `setMappedBoolean`. Explicit `setBooleanDps` can address another numeric DPS. |
| `heartbeatIntervalMs` | `10000` | 1000–60000; interval between authenticated session heartbeats. |
| `statusReconciliationMs` | `30000` | 1000–300000; fallback authenticated status read interval. |
| `connectionTimeoutMs` | `3000` | 100–10000; TCP connect wait for the built-in WiFiClient. An injected generic `Client` uses its own connect behavior. |
| `commandResponseTimeoutMs` | `3000` | 100–10000; bounded Tuya frame/response wait. |
| `partialFrameTimeoutMs` | `300` | 100–10000; deadline for an incomplete unsolicited frame, measured from its first byte. `poll()` retains partial bytes between calls. |
| `reconnectMinimumMs` | `1000` | 250–32000; first automatic reconnect delay. |
| `reconnectMaximumMs` | `32000` | At least the minimum and at most 60000; caps doubling backoff. Explicit `connect()`/`reconnect()` can be requested sooner. |
| `maxBytesPerPoll` | `256` | 16–1024; combined maximum payload bytes sent and consumed from the socket in one native `poll()`. Header probes and kernel bookkeeping are outside this byte count. |
| `maxFramesPerPoll` | `1` | 1–4; maximum complete authenticated frames processed in one native `poll()`. |
| `maxTransactionsPerPoll` | `2` | 1–8; maximum protocol state advances in one native `poll()`. |
| `retry.statusReadAttempts` | `1` | 1–3 authenticated read attempts; each failed attempt closes the session and reauthenticates. |
| `retry.controlWriteAttempts` | `1` | Must equal 1. An uncertain physical command is never resent by the library. |
| `logging` | `Silent` | `Silent`, `Errors`, or `ProtocolMetadata`; controls optional `onDiagnostic` callbacks with command/length/type/error only. No payload or secret logging exists. |
| `diagnostics.countEvents` | `true` | Enable or disable counters. Keep enabled when an application uses counters for health reporting. |
| `diagnostics.captureResponseMetadata` | `true` | Enable or disable the last command/length/type snapshot; `lastError` remains available. |

State freshness is **not** a library setting. The library cannot know whether
a particular DPS is safety critical. The boiler application sets its own
freshness thresholds in `BoilerConfig`; see the
[boiler configuration and migration guide](../../docs/tuya-local-client-migration.md).

## Lifecycle

1. Create one `TuyaLocalClient` for one switch. Populate `TuyaLocalConfig` with the
   device IP, ID, 16-byte Local Key, and timeouts. Keep credentials in an
   ignored local header or another private source. `begin(config, callbacks)`
   validates and copies the strings but opens no socket. Device IDs may contain
   letters, digits, `_` or `-` and are limited to 32 characters; boolean DPS
   IDs must be 1–5 decimal digits for explicit `setBooleanDps` calls.
2. Call `poll(networkAvailable)` repeatedly from **one owner context** while
   monitoring is authorized. Native mode advances the nonblocking connect,
   handshake, initial status read, push reception, heartbeat, and reconciliation
   across calls. The first call only starts connecting; observe `onConnection`
   and `onStatus` for authenticated readiness. An authenticated command `0x08`
   invokes `onPush` when its full frame has arrived. `poll(false)` closes the
   socket and cancels any active transaction.
3. `requestStatus()` and `requestBooleanDps(id, value, timestamp)` queue one
   native-mode transaction without waiting. Consume completion with
   `takeStatusResult(json, succeeded)` or `takeCommandResult(acknowledged)`;
   `onCommand` also reports the latter. An accepted request is never proof of
   relay state. An OFF request can preempt a background status or heartbeat by
   closing and reauthenticating the same socket. An ON request requires an
   already authenticated idle session and is never queued across a failed
   connection. A second control request is rejected while one is active.

   The existing `readStatus(json)`, `reconnect(json)`, and `setBooleanDps(id, value,
   timestamp)` remain synchronous compatibility methods. They drive the same
   native state machine from the caller's context and can wait for configured
   timeouts. Call every method only from the same owner;
   queue requests in the application if Serial, Web, and other callers exist.
   `setBooleanDps` reports only a protocol acknowledgement. **Read status and
   validate the requested DPS after every write.** Never infer relay state
   from an ACK or automatically resend an uncertain ON.
   The `timestamp` argument is Unix seconds; the boiler application supplies
   valid NTP time before any ON request.
4. `disconnect()` closes the socket without dropping configuration. `end()`
   closes it, releases the transport, clears copied credentials and counters,
   and permits another `begin()`. A `Client*` supplied to `begin` for testing is
   borrowed, never deleted.

Native `poll()` returns `NoFrame` while connecting, sending, or waiting for a
partial frame; `Frame` after an authenticated frame; and
`Invalid`/`Disconnected` after a failure. It never waits for a complete
response. It processes bounded socket bytes and state advances. The measured
host loopback maximum was below 1 ms with a 16-byte budget, but ESP32
crypto, scheduling, callbacks, socket allocation, and kernel bookkeeping can
exceed 5 ms; there is no hard real-time guarantee. `diagnostics().maxPollUs`
records the maximum observed call time without storing credentials.

**An injected generic `Client*` keeps the older synchronous compatibility
path.** Its `connect()`/`write()` and response waits may block. The native
socket path is selected by calling `begin()` without an injected transport.

## Callbacks and diagnostics

`onStatus` receives authenticated status JSON from automatic reconciliation;
explicit `readStatus` returns JSON directly. `onPush` receives authenticated
unsolicited DPS JSON. `onMappedBoolean` fires only when the configured state
DPS is present as a JSON boolean. `readMappedBoolean` returns that value
directly. Callbacks run synchronously in the owner's context and must not call
another library operation. A valid protocol frame can still omit the needed
DPS; a boiler must still enforce its own DPS 1 and safety policy.

`onConnection` reports authenticated session arrival and closure. `onCommand`
reports boolean DPS request acknowledgement, **not** verified state. `diagnostics()`
returns session, push, heartbeat, connection, authentication, invalid-response
counters, safe response metadata, and poll duration. No callback or diagnostic includes the
Local Key or session secret. The JSON callbacks contain decrypted device data;
do not log them. `invalidate()` closes a session and schedules backoff if an
application rejects a status payload.

The optional `onDiagnostic` callback receives only a response command, frame
length, response type, and error category. With `logging=Errors` it fires on
failed exchanges; `ProtocolMetadata` also includes successful exchanges.
Disabling event counters does not disable validation or error handling.
Heartbeats have one attempt per interval; a failure closes the session and
uses the configured reconnect backoff. Physical writes always have one attempt.

The wrapper accepts Tuya 3.4 only. The underlying vendored transport still
contains 3.5 code, but it is outside this wrapper's validated API. The
`examples/BasicMonitor` and `examples/ESP32S3Monitor` show read-only use with
placeholder secrets for a standard ESP32 and ESP32-S3 respectively. Copy an
example's `local_secrets.h.example` to its ignored `local_secrets.h` before
building. Neither example issues a physical relay command.
