# Permanent local telemetry TFT dashboard

The 320 x 480 ST7796S shows one dashboard throughout normal operation. Its
layout follows the approved `hot-water-platform/docs/reference/dashboard-design.png`
concept, adapted to portrait orientation. The scenic photo is sent from flash
once at startup; opaque field panels and the graph are updated in place.
There is no timed page switch, touch navigation, or normal-operation diagnostics
screen. An explicit Wi-Fi setup password or OTA recovery panel can temporarily
cover the temperature cards; the dashboard is restored when it closes.

The permanent screen contains boiler and solar temperatures, the Tuya relay and
auto-off countdown, valve and mode, communication and measurement freshness, and two
rolling 24-hour temperature curves. The single bottom panel shows the S3 IP,
the authenticated boiler ESP32 peer IP, Tuya status, and uptime. The peer IP is
shown only after authenticated HTTP telemetry. An unavailable reading is displayed as
unknown or a dash; stale telemetry is never presented as current.

`LocalHttpTelemetry::snapshot()` copies a short-lived state snapshot under its
mutex. A missed lock is marked `snapshot_ok=false`, so it cannot briefly paint
a false disconnection. The HTTP receiver shares the existing STA-bound port 80
server, while authenticated UDP discovery runs in its own task. Neither path
initializes or resets in response to a display update. The chart uses a fixed 1,440-minute
PSRAM ring, scans at most one day of buckets, and redraws no more often than
15 seconds when data changes. It never writes history to NVS or flash.

The TFT treats a valid heartbeat as communication liveness without replacing
temperatures or valve state. Fresh current records may advance measurement
state; queued historical records are accepted durably but cannot make the
display current because their producer boot and uptime do not match the live
request window. Communication is LIVE through 35 seconds, DELAYED through 75
seconds, then OFFLINE. Measurement freshness is tracked separately.

Build: `pio run -e esp32-s3-n16r8-tft`. On the isolated development board,
verify the partition table against the installed layout before uploading an
app-only image, and preserve NVS. Capture at least five uninterrupted minutes
of 115200-baud serial output while the graph redraws. Confirm one visible
screen, authenticated HTTP delivery, bounded queue depth and heartbeat timing,
no Wi-Fi reconnect, no watchdog reset, and stable heap. The source checkout was
built successfully, but this HTTP firmware has not yet been flashed or observed
on POC hardware.

Host history test: `g++ -std=c++17 -Iinclude tools/test_temperature_history.cpp
-o /tmp/hwp_temperature_history_test && /tmp/hwp_temperature_history_test`.
