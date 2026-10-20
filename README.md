# ESP32-S3 N16R8 TFT diagnostics

Phase 1 ESP32 serial diagnostics passed on the user's hardware. Phase 2 ST7796S
firmware is implemented and builds in both safe and TFT-enabled modes. The
Estardyn display is **not connected**. Its VCC and BL electrical requirements
are **NOT YET VERIFIED**, so no TFT power connection or upload is recommended.
See [Phase 2 wiring and diagnostics](docs/phase2-tft.md).

## Project

`platformio.ini` retains the generic `esp32-s3-devkitc-1` board with overrides
for 16 MB flash and N16R8 octal PSRAM. The default environment is
`esp32-s3-n16r8`; it keeps Serial at 115200, reports chip, flash, PSRAM, heap,
and uptime, and leaves TFT SPI and GPIOs untouched. The RGB LED remains disabled
until its GPIO and LED type are confirmed.

`esp32-s3-n16r8-tft` is an explicit hardware-enabled build. It uses TFT_eSPI
2.5.43 with an ST7796S setup in `include/TftConfig.h`. It is for compilation
and review until display power and backlight safety have been established.

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
press RST to repeat the boot report. No Phase 2 firmware has been uploaded.

## Status and next gate

The default and TFT-enabled builds compile. TFT color tests and dashboard have
not been observed on hardware. Resolve the display's VCC, logic-level and BL
requirements before connecting it, then decide whether to flash the enabled
build. Phase 3 Wi-Fi dashboard work has not started.
