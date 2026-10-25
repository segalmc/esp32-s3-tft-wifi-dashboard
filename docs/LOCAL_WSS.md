# Local WSS server (Phase 1.09A)

The ESP32-S3 runs an opt-in HTTPS Server WebSocket endpoint at
`wss://<configured-hostname>:9443/hwp/ws/v1/local`. It accepts read-only
`hwp/ws/v1` original-ESP32 `hello`, `auth-response`, and `state` frames.
There is no boiler or valve command route, no Tuya call, and no second Tuya
socket owner. The original ESP32 firmware and cloud client are separate work.

## Configuration and trust

Copy `include/LocalWssConfig.example.h` to ignored
`include/LocalWssConfig.h`, or run
`python3 tools/generate_local_wss_dev.py` for **isolated development only**.
The generator creates ignored files under `.local-wss/`: a short-lived CA,
hostname-matching server certificate and key, and a client identity with a
unique 32-byte HMAC secret. It refuses to overwrite an existing set. The
default development hostname is `natureglass.local`; provide a trusted local
DNS mapping to the S3 STA address. Alternatively, run the generator with
`--hostname <existing-OTA-hostname.local>` to reuse the board's existing
mDNS name without changing OTA behavior. The simulated client accepts a separate
`--address` locator but still verifies the certificate using the configured
hostname and the explicit `.local-wss/ca.pem` trust anchor. It never disables
TLS verification.

For production, issue a server certificate with a DNS subjectAltName matching
a locally resolvable name. Provision its CA and certificate SHA-256 pin on the
future original ESP32 before enabling that client. The server checks its own
chain, hostname and validity against trusted UTC before listening. The future
client must validate the chain, hostname, time and one of at most two
preprovisioned leaf pins; a newly discovered certificate is never trusted
automatically. Rotate by staging the next pin and optional second HMAC key in
ignored configuration, verifying a new TLS handshake and authentication,
then removing the old pin/key. Neither the private key nor HMAC secret belongs
in Git, HTTP responses, serial logs or screenshots. Development firmware
contains the key in unencrypted flash; physical extraction remains possible.

Missing or invalid configuration disables WSS. The endpoint starts only with
STA IP, online Wi-Fi, no setup AP, trusted NTP time, and a valid certificate.
It stops on Wi-Fi, address, AP, clock, or certificate validity change. A wall
clock jump over 30 seconds latches WSS closed until a fresh Wi-Fi clock-sync
cycle; a producer quality of `stale` or `unknown` also makes the read-only
snapshot stale.
The WebSocket upgrade checks the accepted socket's **local** address against
the current STA IP and rejects AP connections even during AP startup. The
HTTPS listener itself uses the IDF wildcard socket; the route is inaccessible
through AP but TLS may briefly accept a connection before rejecting its
upgrade during a portal transition. This distinction needs physical AP
negative testing before production.

## Authentication and telemetry

The client validates TLS, sends `hello` with fresh boot/device/connection
session IDs, then receives a random 32-byte nonce expiring after 10 seconds.
It returns `auth-response` with a fresh client nonce and HMAC-SHA256 of the
length-prefixed `HWP-WS-V1-AUTH` transcript defined in
`hot-water-platform/contracts/hwp/ws/v1/auth.py`. The server checks the
configured device and installation IDs, role, UTC bounds, one-use challenge,
current session, and MAC in constant time before sending `ready`.
Application frames before `ready` are rejected. The last successful
session IDs are rejected on reconnect.

Only a complete, masked, text `state` frame up to 2048 bytes is accepted.
Unknown fields, duplicate keys, nested/oversized/fragmented frames, stale
timestamps, invalid quality/temperature/valve evidence, and duplicate or
out-of-order sequence numbers are rejected. The thread-safe snapshot records
source session, observation time, receive time and inferred valve state.
`telemetry()` marks it stale after 30 seconds or link loss; it never treats
an inferred valve position as a physical sensor reading. One client is
authenticated at a time. Authentication expires at 10 seconds, ping runs at
20 seconds, pong times out at 10 seconds, and idle traffic at 60 seconds.
The manager task and IDF HTTPS worker have separate stacks and bounded
request buffers.

## Verification

```sh
python3 -m unittest tools/test_local_wss_client.py -v
pio run -e esp32-s3-n16r8-tft
python3 tools/local_wss_client.py --address <verified STA IP>
```

The hardware client validates TLS, rejects the wrong hostname and invalid
HMAC, tests pre-auth rejection, sends simulated telemetry over three fresh
connections, and tests a false physical-position claim. It never sends a
Tuya, boiler, relay, or valve command. Use the actual `/dev/ttyACM*` device
only after esptool proves ESP32-S3 identity, board isolation is confirmed,
and the current partition table matches the build. Upload with the USB
`esp32-s3-n16r8-tft` environment; do not erase NVS or alter the partition
table. Capture 115200 baud serial output and review Wi-Fi, OTA, HTTP, WSS,
heap, PSRAM, task stacks, watchdog/reset and Tuya safety status. Existing
HTTP and OTA are unchanged.

The periodic serial line reports free/minimum heap, PSRAM, manager and HTTPS
worker free-stack watermarks in bytes, active authenticated client count,
authentication failures, HTTPS errors, connection resets, telemetry counts
and age. HTTPS
error events may aggregate handshake and other TLS transport errors; they are
not a per-handshake audit trail.

## Isolated S3 hardware evidence (2026-10-25)

The user reconfirmed an isolated development board with no actuator able to
operate and no active shutdown obligation. Immediately before each USB
upload, esptool identified ESP32-S3 revision 0.2 with 8 MB PSRAM at the
verified USB serial port. Boot diagnostics reported 16 MB flash. The existing
partition table matched the build byte-for-byte; its remaining flash padding
was erased. Uploads used `esp32-s3-n16r8-tft` over USB and did not erase NVS.
Development certificates and HMAC secrets remained in ignored local files.

On the tested non-debug image, the captured reboot reached Wi-Fi association
and DHCP at 2.952 s, requested OTA at 2.960 s, started the HTTP dashboard at
2.979 s, synchronized UTC at 4.660 s, and started WSS on the STA at 5.041 s.
The HTTP login page returned 200. Wi-Fi status later reported AP off, clock
synced and RSSI -43 dBm; SSID and address are withheld. The user observed the
TFT refreshing normally. The 75-second reboot capture contained no watchdog,
panic, crash, or reset loop. Serial status reported no shutdown lease and
the Tuya control gate off; its relay indication is a software report, not
physical proof. No ON/OFF or valve command was issued.

The TLS-validating computer client passed wrong/unknown CA and hostname
rejection; wrong device and installation IDs; pre-auth traffic and invalid
HMAC rejection; 10-second challenge expiry; ten connect/telemetry/disconnect
cycles; replayed auth response and old session rejection; duplicate and
out-of-order state rejection; malformed JSON, duplicate keys, invalid UTF-8,
oversized and fragmented frame rejection; unsupported command and false
physical-position claim rejection; concurrent second-client limit; and
heartbeat ping/pong. A 125-second concurrent serial capture recorded 12
accepted telemetry frames, three rejected telemetry frames, no watchdog or
crash, and free heap returning to about 187 kB after clients closed.
Minimum free heap was about 105 kB; free PSRAM returned to about 8.37 MB.
The HTTPS worker's observed free-stack low-water mark was about 8.6 kB from a
20 kB allocation; the manager retained about 2.1 kB from 6 kB. Later TFT
loop-gap windows during the suite were 31–81 ms. The first 30-second window
included earlier USB-buffered output and a 6-second maximum; its exact cause
was not isolated. The clean reboot capture showed 480 ms startup and 75 ms
later loop-gap maxima.

The existing USB `wifi setup` command activated the protected AP. WSS
stopped and its STA TCP port became unavailable; OTA also stopped for the AP.
A read-only esptool reset closed the temporary AP and restored saved Wi-Fi,
OTA, HTTP and WSS. A fresh authenticated telemetry exchange then passed.
This is a server-stop test, not a direct phone-on-AP port scan.

After an adversarial review, the final image added explicit stale-quality
handling and guaranteed session invalidation after HTTPS teardown. Its build,
native freshness-policy test, authenticated telemetry smoke test, AP
shutdown, reboot recovery and 75-second no-reset serial capture passed.
The full negative and stress suite above ran on the preceding non-debug
image; authentication, framing, TLS and scheduling code was unchanged by
those two final corrections.

A final concurrency correction made the cross-task listener handle and TLS
error count atomic, and made telemetry reads return an empty snapshot if the
mutex is busy. The resulting image built and was verified byte-for-byte in
S3 flash with esptool. After the verification reset and Wi-Fi reconnection,
the TLS-validating client authenticated and sent simulated telemetry. The
full negative and stress suite was not repeated on this final image.

## Remaining limits

This phase has no remote control route, TFT telemetry view, original-ESP32
client, cloud connection, or production credential storage. Local hostname
resolution and original-side CA/pin installation must be supplied before
device integration. The HTTPD TLS listener uses a wildcard bind; route
rejection and server shutdown are the AP isolation controls. Direct access
from an AP-connected phone, forced Wi-Fi loss, clock rollback, long-duration
uptime, and OTA upload under concurrent WSS load were not exercised. The
startup-window loop-gap spike still needs isolated timing diagnosis before
claiming a hard TFT latency bound.
