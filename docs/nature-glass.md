# Nature Glass dashboard

The TFT build displays a 320 × 480 photograph with translucent navy cards.
The eight cards show Chip, Uptime, Free Heap, PSRAM Total, PSRAM Free, Flash,
Resolution, and Refresh. The five color fills and graphics startup test still
run before the dashboard. There is no live weather. After NTP sync, a small
top photo panel shows local Israel time and date; the eight diagnostic cards
remain in place. Wi-Fi and NTP setup are documented in [Wi-Fi setup](wifi-setup.md).

## Photograph and license

The source file is `assets/source/mountain_sunset_over_lake.jpg`, downloaded
from [Wikimedia Commons](https://commons.wikimedia.org/wiki/File:Mountain_sunset_over_lake.jpg).
It is a photograph by **FUBARMAXUSMC**, titled *Mountain sunset over lake*.
The author released it under [Creative Commons Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
The crop, translucent overlays, and RGB565 conversion are adaptations; the
derived background asset is offered under the same license. Source SHA-256:
`70350ea010897fc1472aa11adaecbefc170d4a80a9c36131c731ea6b5622d944`.
The attribution and license should accompany redistributed firmware or assets.

To regenerate the embedded image offline, install Pillow (`python3-pil` or
`pip install Pillow`) and run:

```sh
python3 tools/build_background.py
```

The script accepts `--input PATH` for another appropriately licensed real
photograph. It center crops to exactly 320 × 480, composites the static glass
cards, and writes `src/BackgroundAsset.cpp`. `--preview PATH` also exports a
PNG for visual review. The committed source photo makes the build independent
of network access. If the generated asset is missing or corrupt, regenerate it
from the source photo before building. At runtime, a dimension or checksum
mismatch falls back to solid navy cards and logs the reason on Serial.

## Rendering and measurement

RGB565 costs exactly 307,200 bytes in firmware flash and no full-screen RAM
buffer. Its simple row streaming avoids a JPEG decoder, file system, and
separate filesystem upload. JPEG could save flash but would add decode code,
temporary MCU blocks, asset loading failure modes, and startup work. Raw
RGB565 was chosen because the 16 MB flash has ample space and a full-screen
transfer occurs only once in the normal build after diagnostics. At 10 MHz SPI, the pixel data
alone needs at least 246 ms; actual frame time is logged as `frame=... us`.
The optional diagnostic build includes a second 307,200-byte photo-only array
for isolating the photograph from the precomposed cards; the normal build
excludes it at compile time.

The static photo and cards are drawn once. Every second the code compares
formatted values with cached strings. A changed value restores only its
120 × 20 pixel area from the precomposed image in flash, then draws transparent
white text. Its 20 scan lines use one 640-byte aligned stack buffer and one
address window. No full-screen redraw or heap sprite is used during updates.

The converter writes native RGB565 words (red is `0xF800`). On ESP32-S3,
TFT_eSPI 2.5.43 defaults to a byte-swapped pixel buffer format for
`pushImage`. The dashboard now calls `setSwapBytes(true)` before drawing the
photograph or restoring card pixels. `fillRect` and text do not use this
setting. The full image is sent with one 320 × 480 `pushImage` call; TFT_eSPI's
const overload copies one flash row at a time. Card restores use one address
window per value and copy rows from offset `(y + line) * 320 + x` into an
aligned buffer before calling `pushPixels`. Each value's `x` is even and the
120-pixel restore width is even, keeping source rows aligned and avoiding an
odd-width tail read in the library's faster path. TFT_eSPI's
`const uint16_t*` overload copies flash pixels into an aligned row buffer.
The user confirmed that this byte-order fix renders the photograph and cards
correctly on the physical display.

**Refresh** is the elapsed microseconds for one dashboard sampling and changed
value rendering pass, from just before the Chip sample through the Resolution
value. It excludes the Refresh card's own drawing and routine Serial logging
so it does not measure itself. It is a render workload measurement, not display
frame rate. Serial also logs available heap and PSRAM at initial frame draw
and after each sampling pass. Uptime is duration since boot, not wall time.
The first Refresh sample includes temporary value tracing over Serial; later
samples have the normal workload.

## Missing-value fix

The initial Nature Glass build selected TFT_eSPI font 2 for values and the
large title, but only `LOAD_GLCD` (font 1) was compiled. The first update was
already scheduled immediately after the frame, and each cache entry was reset
to an empty string. TFT_eSPI simply had no font 2 glyphs to draw. The TFT setup
now also defines `LOAD_FONT2`; GPIO, SPI frequency, rotation, and driver
settings are unchanged. `setSwapBytes(true)` controls 16-bit image buffers and
does not affect text drawing.

On the first dashboard update, Serial temporarily logs the font mask, swap
state, each generated value, its drawing coordinates, and the width returned
by `drawString`. Expect `font2=loaded` and a positive `drawn_width` for all
eight values. Subsequent updates keep the usual concise memory and refresh
log. The update restores a value region before drawing its text, so the
restore does not erase the new glyphs.

## Hardware check

Build with `~/.platformio/penv/bin/pio run -e esp32-s3-n16r8-tft`. The firmware
image is `.pio/build/esp32-s3-n16r8-tft/firmware.bin`. Once the display's VCC,
logic levels, and backlight requirements in [Phase 2](phase2-tft.md) have been
verified, connect and upload with:

```sh
~/.platformio/penv/bin/pio run -e esp32-s3-n16r8-tft -t upload --upload-port /dev/ttyACM0
~/.platformio/penv/bin/pio device monitor -b 115200 --port /dev/ttyACM0
```

Check the six startup phases, then verify the photo orientation, all eight
values, and one-second updates. Watch Uptime and heap/PSRAM values for several
minutes: changed text should leave no black rectangle, and Serial should
continue reporting frame time, refresh time, free heap, and free PSRAM. The
byte-order fix was confirmed on the physical display before Wi-Fi integration.

## On-device image diagnostic

For a focused hardware check, build and flash the temporary diagnostic
environment, which inherits the same GPIO, SPI, rotation, and PSRAM settings:

```sh
~/.platformio/penv/bin/pio run -e esp32-s3-n16r8-tft-diagnostic -t upload --upload-port /dev/ttyACM0
```

After the original six startup phases, it shows each of these for five seconds:

1. The cropped photograph alone, without glass cards or text, in one
   320 × 480 `pushImage` call.
2. The photograph with precomposed glass cards, still without text, through
   the same image call.
3. Red, green, blue, and white blocks from the previously working `fillRect`
   path. These establish the baseline color order.
4. The same small RGB565 test image tiled in two columns from flash. The left
   uses `setSwapBytes(false)`; the right uses `setSwapBytes(true)`. The right
   should show red/green above blue/white, matching the reference blocks.
   Check for pixel noise and seams at the 32-pixel tile boundaries. The normal
   dashboard follows.

Serial logs identify each stage and report full-frame draw time. If solid
blocks and the right test image are correct while the left has different
colors, the byte-order mismatch is confirmed. Seams or random pixels in the
left column can also indicate a fast-path transfer issue. If both image
columns have misplaced or noisy pixels, capture the
diagnostic screen and investigate the pixel transfer path with the unchanged
10 MHz setup. If the photo-only stage is corrupt, the card overlays cannot be
the cause. If photo-only is correct but the precomposed card stage is corrupt,
inspect the overlay conversion and generated array. The normal TFT environment skips the
extra screens and starts the dashboard after the six original phases.
