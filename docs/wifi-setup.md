# Nature Glass Wi-Fi setup

Only the `esp32-s3-n16r8-tft` firmware enables Wi-Fi. The serial-only
`esp32-s3-n16r8` environment remains network-free at runtime. No touch panel
or button is required. The existing TFT pins, 10 MHz SPI setting, rotation,
startup diagnostics, photograph, and eight metric cards are unchanged.

## First setup

1. Flash the TFT firmware and allow the six original display tests to finish.
2. The device creates the WPA-protected `NatureGlass-Setup` access point. Its
   **unique 16-character password** appears on the TFT for 60 seconds after
   the dashboard first appears. Record it in a password manager. It is never
   printed to Serial or included in the firmware source.
3. Connect your phone to `NatureGlass-Setup` with that password. Open
   `http://192.168.4.1/`. A captive-portal popup may appear, but phones differ;
   use the direct address if it does not.
4. Select a scanned 2.4 GHz WPA2/WPA3 personal network or enter a hidden SSID,
   supply its 8–63 character password, and tap **Connect**. Open and enterprise
   networks are not supported.
5. The page reports whether Wi-Fi association and DHCP succeeded. Credentials
   are saved only after both succeed. A missing internet connection or an NTP
   failure leaves Wi-Fi connected and does not open recovery setup.

The AP password disappears from the display after 60 seconds. To reveal it
again, connect the native USB serial port at 115200 baud and send `wifi show`
followed by a newline while the AP is active. It appears on the TFT for another
60 seconds, never in Serial output. Send `wifi setup` to open the protected AP
if it is currently closed. `wifi status` prints state and memory readings but
no credentials. Setup started over USB while already online closes after ten
minutes if unused; first-boot and offline recovery setup remain available.

## Reconnection and recovery

On subsequent boots the device uses saved credentials in STA mode. Failed
association or DHCP attempts have a 20-second deadline and retry with delays
of 1, 2, 4, 8, 16, 32, then 60 seconds. The display loop does not wait for
them. A short disconnection does not expose the setup AP. After five minutes
without Wi-Fi association **and** DHCP, the protected recovery AP opens while
STA retries continue. It closes when the saved network reconnects. A failed
replacement attempt retains the previous saved credentials and leaves setup
available for another try. If you did not retain the AP password, use USB
`wifi show` once the AP is active.

## Clock and security

After obtaining a local IP address, the device starts NTP and applies the
Asia/Jerusalem POSIX rule `IST-2IDT,M3.4.4/26,M10.5.0`. The top photo panel
shows `HH:mm` with the English weekday and `DD/MM/YYYY` underneath once NTP
has succeeded. Before the first sync it shows no clock. It samples local time
once a second and redraws only that panel when the minute or date changes.
After a successful sync, the clock continues through a Wi-Fi outage using the
running system clock. The setup password temporarily occupies the same panel
while revealed; the clock returns when the password hides. It tracks clock sync
separately from local Wi-Fi connectivity; an NTP failure does not change the
Wi-Fi indicator or trigger setup. The rule was
checked against IANA `Asia/Jerusalem` immediately around the March and October
2026 transitions. Future changes to Israel's DST law may require an update.

The AP is protected, but the local setup page uses HTTP. A person who knows
the AP password can access it, and HTTP itself does not authenticate the device
or encrypt the form at the application layer. Enter credentials only after
checking the expected AP and direct address. Credentials are held in ordinary
ESP32 NVS: they are absent from source/Git/Serial logs, but **physical flash
extraction can reveal them**. Encrypted NVS would require separate device
security provisioning. Avoid enabling verbose Arduino core logging: the TFT
environment fixes `CORE_DEBUG_LEVEL=0`, because the bundled WebServer can log
form bodies at verbose level. The portal caps accepted form content at 256
bytes, uses a per-boot form token, and serves setup only on its AP interface.
The bundled WebServer parses a request before the application validates it, so
a hostile client that already knows the AP password can still send oversized
requests; its processing runs in a separate task to protect the display loop.

## Build and inspect

```sh
~/.platformio/penv/bin/pio run -e esp32-s3-n16r8 -e esp32-s3-n16r8-tft
~/.platformio/penv/bin/pio run -e esp32-s3-n16r8-tft -t upload --upload-port /dev/ttyACM0
~/.platformio/penv/bin/pio device monitor -b 115200 --port /dev/ttyACM0
```

Replace `/dev/ttyACM0` with the actual port. Every 30 seconds Serial reports
the maximum main-loop gap, dashboard render workload, portal handler duration,
and available heap/PSRAM. Compare AP inactive, idle AP, phone page load, scan,
and a slow client. The on-screen **Refresh** metric remains the original
dashboard render workload measurement, not a network or frame-rate measure.

## Physical verification checklist

- [ ] All six startup color/graphics diagnostics, photograph, labels, and all
      eight metric values remain correct; updates have no black rectangles.
- [ ] First boot shows a unique AP password for 60 seconds; it disappears and
      `wifi show` reveals it on the TFT for another 60 seconds without a Serial
      secret.
- [ ] A phone joins `NatureGlass-Setup` and opens the direct setup address;
      scanning, manual SSID entry, and progress work on a narrow screen.
- [ ] Wrong password and DHCP failure show an error, retain old credentials,
      and allow retry. A successful connection saves credentials across reboot.
- [ ] A brief outage leaves the AP closed. A five-minute association/DHCP
      outage opens recovery; reconnecting closes it. No internet or blocked
      NTP alone does not open recovery.
- [ ] `wifi setup` and `wifi show` work over USB while online; the portal is not
      usable through the home-network IP.
- [ ] Serial reports LAN online separately from pending NTP; when internet is
      available, clock synchronization is reported. Before that message, no
      time/date is shown. Afterwards, compare the 24-hour clock, weekday, and
      date with a phone set to Asia/Jerusalem; check a minute rollover.
- [ ] Leave the display running for several hours and inspect refresh, loop
      gap, heap, and PSRAM logs both with and without portal activity.

These checks require physical hardware. A successful build does not perform
them.
