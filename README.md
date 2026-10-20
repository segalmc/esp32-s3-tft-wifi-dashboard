# ESP32-S3 N16R8 TFT diagnostics

Phase 1 ESP32 serial diagnostics passed on the user's hardware. The user reports
that the earlier Phase 2 ST7796S dashboard worked on the physical display.
The Nature Glass image showed severe corruption; a byte-order fix and optional
image diagnostic have been built but still need an on-device check. The exact
VCC and BL electrical requirements have not been documented in this repository.
See [Phase 2 wiring and diagnostics](docs/phase2-tft.md).
The TFT dashboard now has a photographic [Nature Glass design](docs/nature-glass.md),
including source attribution, asset regeneration, and display validation steps.
A [320 × 480 background preview](assets/dashboard-preview.png) shows the photograph
and card surfaces; text is drawn by the firmware.

## Project

`platformio.ini` retains the generic `esp32-s3-devkitc-1` board with overrides
for 16 MB flash and N16R8 octal PSRAM. The default environment is
`esp32-s3-n16r8`; it keeps Serial at 115200, reports chip, flash, PSRAM, heap,
and uptime, and leaves TFT SPI and GPIOs untouched. The RGB LED remains disabled
until its GPIO and LED type are confirmed.

`esp32-s3-n16r8-tft` is the hardware-enabled build. It uses TFT_eSPI 2.5.43
with the unchanged ST7796S setup in `include/TftConfig.h`.

## Build on Ubuntu

Open this folder in VS Code with PlatformIO, or run:

```sh
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -e esp32-s3-n16r8-tft
```

The TFT library is fetched only for the enabled environment. On this machine,
network access was restricted, so the same pinned TFT_eSPI 2.5.43 library from
an existing local PlatformIO project was placed in the ignored `.pio/libdeps`
cache. A normal networked PlatformIO setup resolves `lib_deps` itself.

For Serial port discovery, compare `pio device list` or
`ls -l /dev/serial/by-id/` before and after connecting the ESP32-S3 native USB
port with a data cable. Open the detected `/dev/ttyACM*` port at 115200 and
press RST to repeat the boot report.

## Status and next gate

The default, TFT, and optional TFT image diagnostic builds compile. The user
reports that the previous diagnostics dashboard worked on hardware. The
Nature Glass byte-order fix needs a physical retest; see
[Nature Glass debugging](docs/nature-glass.md#on-device-image-diagnostic).
Phase 3 Wi-Fi dashboard work has not started.
