# Phase 2: ST7796S TFT preparation

This page records the original pre-connection signal plan and firmware
diagnostics. The user now reports that the earlier diagnostics dashboard
worked on the physical display; the final wiring and supply measurements have
not been recorded here. The measured
ESP32-S3 3V3 pin is 3.27 V and its 5V0 pin is approximately 1.34 V on USB.
Neither measurement establishes a safe TFT supply.

## Display and interface

- Estardyn 3.5-inch IPS TFT, ST7796S controller, 320 × 480 pixels.
- 4-wire SPI, with IM0/IM1/IM2 jumpers all set to 1 according to the module
  information. Recheck the physical jumper positions and module documentation
  before connecting anything.
- The firmware starts at 10 MHz SPI. `TFT_ROTATION=0` is portrait; 1 is
  landscape, and 2/3 are the opposite orientations. Resolution shown on the
  dashboard follows the selected rotation.
- MISO is not used. Controller ID and initialization success cannot be read
  back from this wiring.

## Planned wiring, pending electrical verification

| TFT header | Intended ESP32-S3 signal | Status |
| --- | --- | --- |
| GND | GND | Leave disconnected for now |
| VCC | **NOT YET VERIFIED** | Leave disconnected; neither 3.3 V nor 5 V is approved |
| SCL / SCK | GPIO12 | Planned only |
| SDA / MOSI | GPIO11 | Planned only |
| CS | GPIO10 | Planned only |
| DC | GPIO9 | Planned only |
| RST | GPIO8 | Planned only |
| BL | **NOT YET VERIFIED** | Leave disconnected; no GPIO assignment |
| SDA-O / MISO | No connection | Readback unavailable |

The `6206A` regulator marking does not establish this module's permitted VCC
range, regulator wiring, logic input tolerance, or backlight input/current.
Obtain the exact Estardyn board schematic or verified electrical specification
for **VCC, SPI logic levels, and BL** before wiring. Investigate the abnormal
5V0 reading on the ESP32 board independently. Do not power the display until
those points are resolved. Once the power plan is confirmed, connect the signal
pins as shown and use a common ground as required by that plan.

## Library and firmware configuration

The enabled environment pins `bodmer/TFT_eSPI@2.5.43`, which supports ST7796
and ESP32-S3 under the installed Arduino framework. Its options are supplied
by `include/TftConfig.h` through PlatformIO's `-include` flag; the dependency's
`User_Setup.h` is not edited. The header selects ST7796, 320 × 480, GPIO8–12,
no MISO, no backlight control, and 10 MHz SPI.

`TFT_HARDWARE_ENABLED` defaults to `0`. The default `esp32-s3-n16r8` environment
compiles only serial diagnostics and does not call SPI or set TFT GPIOs.
The `esp32-s3-n16r8-tft` environment sets the flag to `1`; **build this only
for review until the electrical checks are complete**. To choose rotation in
that environment, add `-DTFT_ROTATION=1` under its `build_flags` for landscape.
This changes the intended firmware only; it does not make the wiring safe.

From the project directory:

```sh
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -e esp32-s3-n16r8-tft
```

Do not run an upload target until you explicitly decide to flash after the
power and backlight requirements are confirmed. The enabled firmware sends
ST7796 initialization commands, then shows red, green, blue, white, black,
graphics, and a dashboard. Each test phase is logged over 115200-baud Serial.
The [Nature Glass dashboard](nature-glass.md) updates changed value fields
once per second and reports its update time in microseconds. Initial
full-screen fills are intentional; the dashboard does not repeatedly clear
the whole display.

## Troubleshooting after safe wiring is established

| Symptom | Check |
| --- | --- |
| Blank screen | Confirm the documented VCC and BL requirements and the actual supply with appropriate measurements. Then check common ground, CS/DC/RST and SPI signal continuity. A Serial “init commands sent” line does not prove the TFT accepted them. |
| Backlight on, no picture | Confirm IM0/IM1/IM2 positions, 4-wire SPI mode, ST7796S marking, GPIO mapping and 10 MHz clock. Check CS and DC for swapped wires. |
| Red and blue swapped | Compare the color test against the module's documented RGB/BGR order. If needed, configure TFT_eSPI's `TFT_RGB_ORDER` in `TftConfig.h` after observation. |
| Washed-out or inverted colors | Compare the white/black phases; inspect inversion requirements in the module documentation before changing the driver's inversion setting. |
| Rotated or clipped image | Set `TFT_ROTATION` to 0, 1, 2, or 3 and rebuild. Portrait is 320 × 480; landscape is 480 × 320. |
| Intermittent pixels | Recheck signal wiring, ground, and supply integrity; keep SPI at 10 MHz while diagnosing. |
| No touch support warning | Expected: this display integration has no configured touch controller. |
