# TuyaLocalClient polling and transport

The reusable client now uses one nonblocking lwIP IPv4 socket in native mode.
The ESP32-S3 boiler application still owns it from its existing `tuya-io` task;
there is no new task and no second Tuya connection. An injected generic
Arduino `Client*` remains available for compatibility tests and retains that
transport's synchronous behavior.

## Socket and protocol flow

The pinned ESP32 Arduino core and ESP-IDF provide the required BSD socket API.
`TuyaSocketTransport` creates one `O_NONBLOCK` socket, starts connection with
`EINPROGRESS`, checks completion with zero-timeout `select()` and `SO_ERROR`,
and sends/receives with `MSG_DONTWAIT`. `EAGAIN`/`EWOULDBLOCK` leave the current
operation pending. A fixed 308-byte transmit buffer retains partial writes;
the socket closes and clears the queue on fatal errors or disconnect.

`TuyaLocalClient::poll()` advances these phases across calls:

1. Disconnected or reconnect backoff
2. TCP connecting
3. Handshake command 3 sending and authenticated command 4 waiting
4. Handshake command 5 sending
5. Ready, including unsolicited authenticated 0x08 push handling
6. Status, heartbeat, or control frame sending and matching response waiting

Each phase has a monotonic deadline. The vendored frame accumulator retains
partial headers and bodies and validates declared lengths before reading the
body. The existing HMAC, encrypted nonce, inner HMAC, session key, decryption,
padding, return-code, and DPS checks remain in `LynkTuyaLocal`. A response
does not complete a transaction unless its authenticated command matches.
Authenticated pushes are dispatched while a response is pending. A missing
response closes the session and schedules bounded reconnect backoff.

The native client has one pending transaction slot. A second control request
is rejected. OFF can preempt a background status or heartbeat by closing that
session and reauthenticating one socket before sending OFF. ON requires an
authenticated idle session, is staged once, and is never automatically resent
after an uncertain outcome. The boiler application's USB authorization,
fresh authenticated OFF requirement, NTP/NVS lease, deadline recovery, and
verified OFF clearing rules remain outside this library.

## Poll budgets and measurements

`TuyaLocalConfig` sets `maxBytesPerPoll` (default 256, range 16–1024),
`maxFramesPerPoll` (default 1, range 1–4), and
`maxTransactionsPerPoll` (default 2, range 1–8). These limit payload
send/receive bytes, completed frames, and state advances respectively.
`diagnostics().lastPollUs` and `maxPollUs` record call duration without payload
or credential data.

In the offline host loopback fixture, a 16-byte budget forced fragmented
handshake/status frames and partial sends. The observed maximum native `poll()`
duration was 112 microseconds in one run. The suite also checked responsiveness
when handshake and status replies were withheld, during heartbeat failure,
and while a connection was refused. These are host observations, not ESP32
timing guarantees. Socket creation/close, crypto, JSON parsing, callbacks, and
OS scheduling can exceed the practical 5 ms target. The implementation avoids
known multi-second socket waits inside native `poll()` but cannot promise hard
real-time execution.

## Remaining synchronous paths

- `connect()`, `readStatus()`, `reconnect()`, and `setBooleanDps()` remain
  compatibility calls. On native transport they repeatedly drive the same
  state machine and yield until completion or timeout. The boiler uses them
  from its dedicated I/O task, so they do not run in the TFT/Web loop.
- Passing an injected generic `Client*` selects the legacy synchronous path.
  Its `connect`, `write`, handshake, and response waits can block; the library
  cannot bound arbitrary injected implementations. One existing host mock
  measured a 100 ms `poll()` call with a 100 ms missing-handshake timeout.
- User callbacks run inside `poll()` and must return promptly. Reentrant
  requests are rejected; a callback's session invalidation is deferred until
  the current protocol step finishes.

## Hardware validation boundary

Only offline tests and firmware builds have been performed for this refactor.
Before any hardware test, keep the Node.js proxy as Tuya owner until a
separately approved cutover stops it and confirms its socket closed. First
validate read-only handshake, push, heartbeat, and reconnect behavior with
physical control gated off. Confirm the boiler's NVS shutdown obligation and
authenticated OFF path before a separately approved controlled relay test.
