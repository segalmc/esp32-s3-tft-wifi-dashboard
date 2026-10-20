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

## Prepare private uploader credentials

After `ota show` displays the device password, enter it twice into the helper:

```sh
python3 tools/set_ota_password.py
```

It writes `~/.config/nature-glass/ota-password` atomically, with file mode
`0600` inside a `0700` directory. The password is not echoed, logged, put in
an environment variable, or passed as a process argument. Keep this file out
of Git and any shared backup. Uploads fail closed if the file is absent,
malformed, symlinked, owned by another user, or has looser permissions.

## Wireless upload from VS Code or terminal

The default PlatformIO environment is now `esp32-s3-n16r8-tft-ota`. With the
device online at `192.168.31.230`, use VS Code's PlatformIO **Upload** button
or run:

```sh
"$HOME/.platformio/penv/bin/pio" run -t upload
```

No password prompt is needed after the file is prepared. Set
`NATURE_GLASS_OTA_IP` in the VS Code process environment for a persistent local
IPv4 override, or use `--upload-port` for one terminal upload:

```sh
"$HOME/.platformio/penv/bin/pio" run -e esp32-s3-n16r8-tft-ota -t upload --upload-port 192.168.31.231
```

The local environment variable takes precedence if both are set. The
PlatformIO post script replaces the normal ESPOTA command with an uploader
that reads the secret file within its own process; even verbose build output
shows only script, IP, and firmware paths. Do not use the old
`PLATFORMIO_UPLOAD_FLAGS=--auth=...` approach. The service starts only after
Wi-Fi association and DHCP, and stops if the setup AP opens or Wi-Fi
disconnects.

ArduinoOTA uses MD5 challenge authentication and an image digest; it does
**not** encrypt transport or sign firmware. Restrict OTA to a trusted LAN.
Ordinary device NVS is readable with physical flash access.

## Rotate an exposed OTA password

If the old password was exposed, install this firmware over USB using the
checked and backed-up procedure above rather than reusing that password for
another wireless upload. With this firmware installed, connect the native USB
Serial port at 115200 baud and send `ota rotate` followed by a newline. The
device disables OTA, replaces only its `otakey` NVS entry, and reboots to
activate the new digest.
It does not print either password. If NVS rejects the write, the device reports
failure and retains the previous password. After reboot, send `ota show` over
USB, read the new password on the TFT, and run
`python3 tools/set_ota_password.py` again to atomically replace the local
file. Uploads using the old file fail until it is updated. The old secret may
still exist in historical, unencrypted NVS flash pages; physical flash
security requires separate hardware provisioning.

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
- [ ] The local secret file is mode `0600`; VS Code Upload works without a prompt or secret in verbose logs.
- [ ] `ota rotate` reboots; the old file is rejected, and entering the new TFT secret restores upload.
- [ ] A wrong OTA password is rejected; an error appears without changing firmware.
- [ ] The OTA service is unreachable while the setup AP is active or STA is offline.
- [ ] An interrupted upload leaves the old firmware bootable; USB recovery remains available.
- [ ] A valid OTA upload shows progress, reboots, retains Wi-Fi settings, and shows the new firmware.
- [ ] After an error, the photograph and clock return without black rectangles.
- [ ] During a sustained upload, compare loop gaps, refresh time, heap, PSRAM, and all eight metrics.

No hardware validation is implied by a successful build.
