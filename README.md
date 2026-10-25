# ESP32-S3 N16R8 TFT diagnostics

Phase 1 ESP32 serial diagnostics passed on the user's hardware. The user reports
that the earlier Phase 2 ST7796S dashboard worked on the physical display.
The Nature Glass image showed severe corruption; the user confirmed that the
byte-order fix restored the photograph and cards on the physical display. The exact
VCC and BL electrical requirements have not been documented in this repository.
See [Phase 2 wiring and diagnostics](docs/phase2-tft.md).
The TFT dashboard now has a photographic [Nature Glass design](docs/nature-glass.md),
including source attribution, asset regeneration, and display validation steps.
The TFT firmware also has a [Wi-Fi setup and recovery portal](docs/wifi-setup.md).
The direct Tuya boiler proof of concept, USB gates, local Web dashboard, and
hardware approval stages are documented in [boiler POC](docs/boiler-poc.md).
For authenticated wireless firmware updates, follow the
[OTA installation and backup guide](docs/ota.md) before the first USB upload.
A [320 × 480 background preview](assets/dashboard-preview.png) shows the photograph
and card surfaces; text is drawn by the firmware.

## Project

`platformio.ini` retains the generic `esp32-s3-devkitc-1` board with overrides
for 16 MB flash and N16R8 octal PSRAM. The default environment is the
authenticated `esp32-s3-n16r8-tft-ota` target so VS Code's Upload button uses
Wi-Fi. The serial-only `esp32-s3-n16r8` and USB TFT upload environments remain
available. The RGB LED remains disabled until its GPIO and LED type are confirmed.

`esp32-s3-n16r8-tft` is the hardware-enabled build. It uses TFT_eSPI 2.5.43
with the unchanged ST7796S setup in `include/TftConfig.h`.

## Build on Ubuntu

Open this folder in VS Code with PlatformIO, or run:

```sh
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -e esp32-s3-n16r8-tft
~/.platformio/penv/bin/pio run -e esp32-s3-n16r8
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

The default and TFT environments compile. The user confirmed the Nature Glass
background and cards on hardware before Wi-Fi was added. Wi-Fi provisioning,
portal behavior, and the small status overlay require the physical checks in
[Wi-Fi setup](docs/wifi-setup.md#physical-verification-checklist); successful
builds do not establish hardware behavior.

# Local secure WebSocket server

The opt-in, read-only ESP32-S3 local WSS endpoint and isolated development
procedure are documented in [docs/LOCAL_WSS.md](docs/LOCAL_WSS.md).
