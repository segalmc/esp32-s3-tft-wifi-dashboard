# Permanent local telemetry TFT dashboard

The installed ST7796S uses a 320 × 480 portrait viewport. The scenic RGB565
asset is sent once at startup. Opaque panels and text fields are updated in
place; no chart plotting scan, timed page switch or touch navigation runs on
the normal dashboard. Setup and OTA password/progress panels can temporarily
cover the temperature values and restore the dashboard afterward.

| Portrait region | Contents | Source |
| --- | --- | --- |
| Header, y=8–52 | HOT WATER, compact Wi-Fi state and clock | Network manager; local time |
| Two large cards, y=58–206 | BOILER and SOLAR numeric °C, DS18B20 quality | Authenticated original ESP32 telemetry snapshot |
| Two status cards, y=212–306 | BOILER RELAY ON/OFF/UNKNOWN and auto-off obligation; LED VALVE OPEN/CLOSED/OPENING/CLOSING/UNKNOWN/FAULT | Confirmed fresh Tuya status; inferred LED bench record code |
| Controller card, y=312–385 | ESP-NOW/HTTP FALLBACK/OFFLINE and sample age/freshness | ESP-NOW/HTTP receiver snapshot |
| Footer, y=391–472 | S3 IP, last received transport, uptime and Tuya verification | Network, receiver and Tuya status |

The valve card is explicitly labeled `LED VALVE` and `SIMULATED / NO MOTOR`.
OPEN and CLOSED are inferred LED-bench states, **not physical valve-position
measurements**. The relay card uses the existing Tuya snapshot and shows
UNKNOWN unless its last authenticated state is connected, fresh and no command
is pending. The display
does not issue any command.

The TFT uses the same `OriginalTelemetryApi::view` as authenticated
`GET /api/status`: communication is connected through 35 seconds after an
authenticated message, while a measurement is fresh only when its effective
age is at most 7 seconds. Effective age includes the record's age at receipt.
Only finite, valid-flagged readings in −50…150°C are shown while fresh.
Missing, invalid, stale or disconnected readings become `--.-`; their LED
valve state becomes UNKNOWN. The link indicator remains independent of sample
freshness, so it can be green while the sample reads STALE. The original LED
bench sender now queues a full measurement every 3 seconds while either sensor
has a valid recent observation, even at unchanged temperature. No DEMO value
enters this display. A heartbeat cannot refresh an old measurement.

`TemperatureHistory` remains allocated in PSRAM and receives/ages the same
trusted records as before. The bounded S3 journal and any backend reporting
are unchanged. Removing the chart eliminates its minute scans, plot draws,
15-second redraw timer and chart-specific PSRAM warning. Normal text updates
occur at most once per second and only when the field text or color changes.

Geometry is defined in `include/TftDashboardLayout.h` with compile-time bounds
and separation assertions. Implementation is in `src/DisplayManager.cpp` and
`include/DisplayManager.h`. The web dashboard and Android tablet layout were
not changed for this TFT redesign.

Offline checks passed: 13 S3 telemetry host tests, the boiler-policy test,
the temperature-history test and the background-asset test. Both
`esp32-s3-n16r8-tft` and `esp32-s3-n16r8-tft-ota` firmware builds passed.
The redesigned screen has **not been flashed or visually verified on hardware**.
The read-only physical login probe confirmed that the password currently
accepted by the S3 does **not** match the hash embedded in this build. This
image must not be flashed. Credential configuration needs a separately
approved, secure correction and a new build before any separately approved
flash. USB recovery and partition preflight are described in `docs/ota.md`.
