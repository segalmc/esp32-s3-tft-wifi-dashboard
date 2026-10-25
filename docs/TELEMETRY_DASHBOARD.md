# Permanent local telemetry TFT dashboard

The 320 x 480 ST7796S shows one dashboard throughout normal operation. Its
layout follows the approved `hot-water-platform/docs/reference/dashboard-design.png`
concept, adapted to portrait orientation. The scenic photo is sent from flash
once at startup; opaque field panels and the graph are updated in place.
There is no timed page switch, touch navigation, or normal-operation diagnostics
screen. An explicit Wi-Fi setup password or OTA recovery panel can temporarily
cover the temperature cards; the dashboard is restored when it closes.

The permanent screen contains boiler and solar temperatures, the Tuya relay and
auto-off countdown, valve and mode, last telemetry age and WSS status, and two
rolling 24-hour temperature curves. The single bottom panel shows the S3 IP,
the authenticated boiler ESP32 peer IP, Tuya status, and uptime. The peer IP is
shown only after WSS authentication. An unavailable reading is displayed as
unknown or a dash; stale telemetry is never presented as current.

`LocalWssServer::telemetry()` copies a short-lived state snapshot under its
mutex. A missed lock is marked `snapshot_ok=false`, so it cannot briefly paint
a false disconnection. The renderer never owns the WSS mutex. WSS runs in its
own task, and the HTTPS server task is pinned to core 0. Neither task initializes
or resets in response to a display update. The chart uses a fixed 1,440-minute
PSRAM ring, scans at most one day of buckets, and redraws no more often than
15 seconds when data changes. It never writes history to NVS or flash.

Temporary serial diagnostics print Wi-Fi association and disconnect reasons,
WSS TLS errors, authenticated peers, WSS close reasons, Ping/Pong send errors,
authentication and telemetry rejects, mutex misses, task loop lag, heap minima,
render time, and boot reset reason. These logs contain no credentials, nonces,
MACs, or telemetry payloads. `Performance` and `Local WSS` snapshots print every
30 seconds. Correlate their timestamps with Wi-Fi events and WSS close reasons
to determine an actual disconnect cause.

Build: `pio run -e esp32-s3-n16r8-tft`. On the isolated development board,
verify the partition table against the installed layout before uploading an
app-only image, and preserve NVS. Capture at least five uninterrupted minutes
of 115200-baud serial output while the graph redraws. Confirm one visible
screen, authenticated WSS, increasing telemetry accepted count, expected Pongs,
no Wi-Fi reconnect, no watchdog reset, and stable heap. The source checkout was
built successfully, but this new firmware has not yet been flashed or observed
on POC hardware in the current workspace. The current workspace exposes no
serial device and blocks network sockets, so the disconnect cause remains
unconfirmed until live logs are available.

Host history test: `g++ -std=c++17 -Iinclude tools/test_temperature_history.cpp
-o /tmp/hwp_temperature_history_test && /tmp/hwp_temperature_history_test`.
