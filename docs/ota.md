# Authenticated Wi-Fi OTA

The existing `default_16MB.csv` layout is unchanged. It contains NVS at
`0x9000` (size `0x5000`), OTA metadata at `0xE000` (size `0x2000`), and two app
slots: `ota_0` at `0x10000` and `ota_1` at `0x650000`, each `0x640000`
(6,553,600 bytes). PlatformIO checks the firmware against this slot size,
and Arduino's `Update.begin` rejects an image larger than the actual inactive
slot. The new `otakey` NVS entry is separate from the existing AP and home Wi-Fi
entries. The upload process does not erase NVS; the first boot of this firmware
adds only the new OTA password entry.

## Check the physical device and back up credentials first

The project setting does not prove which partition table is on a particular
device. Close any Serial monitor, replace `/dev/ttyACM0` with the actual native
USB port, then run these Bash commands. The backup contains Wi-Fi credentials;
the directory is private and **outside the repository**.

```sh
umask 077
backup_dir=$(mktemp -d "$HOME/natureglass-backup.XXXXXX")
python="$HOME/.platformio/penv/bin/python"
esptool="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
parttool="$HOME/.platformio/packages/framework-arduinoespressif32/tools/gen_esp32part.py"

"$python" "$esptool" --chip esp32s3 --port /dev/ttyACM0 read_flash 0x8000 0x1000 "$backup_dir/partitions.bin"
"$python" "$parttool" "$backup_dir/partitions.bin" "$backup_dir/partitions.csv"
cat "$backup_dir/partitions.csv"
```

Confirm the offsets and sizes above. **Stop if they differ**; do not flash a
firmware built for this table onto a device with another layout. Back up NVS
only after confirming its actual offset and size:

```sh
"$python" "$esptool" --chip esp32s3 --port /dev/ttyACM0 read_flash 0x9000 0x5000 "$backup_dir/nvs.bin"
sha256sum "$backup_dir/partitions.bin" "$backup_dir/nvs.bin"
```

Keep this backup privately. Do not commit it, attach it to issues, or use a
full-flash erase. The commands above only read flash. If `read_flash` fails,
resolve the USB port/bootloader issue before uploading.

## First USB installation

With the partition table checked and NVS backed up, build and upload the
standard TFT environment using native USB. This is also the recovery route:

```sh
"$HOME/.platformio/penv/bin/pio" run -e esp32-s3-n16r8-tft
"$HOME/.platformio/penv/bin/pio" run -e esp32-s3-n16r8-tft -t upload --upload-port /dev/ttyACM0
```

The normal upload writes the app and normal boot images, not the NVS region;
it may update OTA boot metadata. The partition table content stays the same.
After reboot, confirm the saved Wi-Fi network reconnects and all eight
dashboard values appear. Send `ota show` followed by a newline to the 115200
baud USB Serial port. A unique 20-character OTA password appears **only on the
TFT** for 60 seconds, even if Wi-Fi is offline. Retain it in a password manager.
The command and Serial logs never print the password. If NVS cannot save the
password, OTA stays disabled while ordinary Wi-Fi continues working.

## Wireless upload

Use a trusted computer on the same local network as the device. The OTA
service starts only after Wi-Fi association and DHCP, and stops if the setup
AP opens or Wi-Fi disconnects. In Bash, enter the device IP and password
without a literal password in shell history:

```sh
read -rp 'Device IP: ' natureglass_ip
read -rsp 'OTA password: ' natureglass_ota_password; echo
PLATFORMIO_UPLOAD_FLAGS="--auth=$natureglass_ota_password" "$HOME/.platformio/penv/bin/pio" run -e esp32-s3-n16r8-tft-ota -t upload --upload-port "$natureglass_ip"
unset natureglass_ota_password
```

`192.0.2.1` in `platformio.ini` is a reserved documentation address so a plain
build does not require a real device IP. Always override it at upload time.
The uploader passes the password as a process argument; other users on the
upload computer may see it. Do not run with shell tracing (`set -x`) or verbose
upload logs. ArduinoOTA uses MD5 challenge authentication and an image digest;
it does **not** encrypt transport or sign firmware. Restrict OTA to a trusted
LAN and keep the password private. Ordinary NVS is readable with physical
flash access.

The top photo panel shows progress in 5% steps; errors remain for ten seconds,
then the clock or setup panel returns. The eight cards keep their normal
once-per-second updates. Serial performance logs report dashboard refresh
work and main-loop gaps. Flash writes can still briefly pause the display;
measure this on the real device before relying on a latency bound. A successful
upload reboots automatically. If an update fails, retry over USB if needed;
two slots do not imply automatic rollback is enabled.

## Physical validation checklist

- [ ] Before uploading, inspect the device's partition CSV and back up NVS as above.
- [ ] First USB installation reconnects with the previously saved Wi-Fi credentials.
- [ ] Six startup tests, the photograph, clock, Wi-Fi badge, and all eight cards remain correct.
- [ ] `ota show` reveals the unique secret on the TFT for 60 seconds and never in Serial.
- [ ] A wrong OTA password is rejected; an error appears without changing firmware.
- [ ] The OTA service is unreachable while the setup AP is active or STA is offline.
- [ ] An interrupted upload leaves the old firmware bootable; USB recovery remains available.
- [ ] A valid OTA upload shows progress, reboots, retains Wi-Fi settings, and shows the new firmware.
- [ ] After an error, the photograph and clock return without black rectangles.
- [ ] During a sustained upload, compare loop gaps, refresh time, heap, PSRAM, and all eight metrics.

No hardware validation is implied by a successful build.
