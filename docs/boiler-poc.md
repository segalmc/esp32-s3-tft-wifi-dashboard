# Direct Tuya boiler POC

This firmware targets the locally configured Tuya 3.4 switch at the previously
observed LAN address, port 6668, using DPS 1. The ESP32 is intended to be the
sole Tuya connection owner after a controlled cutover. The existing Node.js
controller remains installed and unchanged as a rollback option.

## Local setup and gates

Run `python3 tools/prepare_boiler_secrets.py` on the development computer before
building. The script reads `bridge.js` and the active proxy's `server.js`,
requires the proxy's Device ID and Local Key to be available locally, compares
the key with `bridge.js`, and requires the active protocol setting to be 3.4.
It fails closed if a required value is missing or inconsistent. It writes
`include/secrets.h` and `.boiler-web-password` with mode 0600. Both are Git
ignored. Keep the password file private; it contains the separate Web password.
Never copy either file into a commit or report.

On a new image, monitoring is **disarmed** and no Tuya connection is opened.
After the proxy has been stopped and its socket verified closed, a USB Serial
operator can persistently arm monitoring. It then resumes automatically when
Wi-Fi returns or the ESP32 reboots. Physical control remains disabled after
every boot. From a physically connected USB Serial terminal at 115200 baud:

| Command | Action |
| --- | --- |
| `tuya help` | List commands and gates |
| `tuya monitor arm` | Persistently arm sole-owner monitoring after proxy shutdown |
| `tuya monitor disarm` | Stop monitoring and remove the authorization; refused while a shutdown obligation is active |
| `tuya enable reads` | Temporarily start monitoring for this boot; only after proxy shutdown |
| `tuya enable control` | Open physical control for this boot only, after monitoring is armed |
| `tuya status` | Print the last result and queue an authenticated read when allowed |
| `tuya on` | Queue one DPS 1 ON command and verify through a fresh authenticated read |
| `tuya off` | Queue OFF and verify the same way |
| `tuya reconnect` | Reconnect and reconcile through the same Tuya client |
| `tuya diagnostics` | Print counters, connection state, and command progress without secrets |

Existing `wifi` and `ota` USB commands remain available. Serial and Web reserve
one shared control slot; a second request while a control is active is rejected.
The single Tuya task owns the persistent connection, push parsing, heartbeat,
reconnects, status reconciliation, and all Serial/Web commands. It authenticates
and validates DPS 1 before updating the BOILER card or Web state. Web refreshes
the displayed state every 750 ms; TFT rendering uses the next display update.
An acknowledgement never counts as confirmed relay state. ON is sent once and
is never retried after an uncertain result. ON also requires a recent confirmed
OFF state and a fresh OFF read immediately before the command. The last confirmed state remains
visible separately from connection and command progress. A failed or ambiguous
exchange should be treated as uncertain until a subsequent authenticated read.

## Local Web dashboard

When the home Wi-Fi STA is online and provisioning is inactive, open `http://<ESP32-STA-IP>/`.
The server binds the STA address only and stops during provisioning or Wi-Fi
loss. The password is in the private `.boiler-web-password` file. Sessions use
random HttpOnly, SameSite=Strict cookies with idle and absolute expiry; control
POSTs require login, CSRF token, matching Host and Origin. Login failures are
rate limited. The page contains no Tuya Local Key or Device ID. The dashboard
shows confirmed relay state, connection, colored status, ON/OFF controls, and
command verification progress; it has no temperature display. The management
view also shows the monitoring and USB gate states, last authenticated update,
shutdown obligation, safe protocol counters, Wi-Fi and memory information, and
OTA phase. Its authenticated status API reads cached application snapshots;
it does not open another Tuya connection. The CSRF-protected command API accepts
only `refresh`, `reconnect`, `on`, and `off` actions and submits them to the
same Tuya queue used by Serial. The previous `target=on/off` form remains
supported. Monitor arm/disarm and read/control gate changes remain USB-only.
The login and dashboard responses use `Referrer-Policy: same-origin` so a normal
Chrome form POST includes the page's Origin. Missing, `null`, and foreign
origins remain forbidden. The browser sends no referrer to other origins.

**HTTP has no transport confidentiality.** Anyone able to observe traffic on
the local network may see the password or session traffic. Use a trusted LAN;
do not configure router port forwarding or any public internet exposure. The
firmware cannot enforce the router's port forwarding policy.

## Web Dashboard – User Guide

### Open the dashboard

1. Connect the ESP32-S3 to the home Wi-Fi and wait for provisioning to finish.
   Find its current STA IP with `wifi status` in a USB Serial terminal at
   **115200 baud**. Opening a Serial terminal may reset the board; let it finish
   booting before logging in.
2. On a device on the trusted local network, open
   `http://<ESP32-STA-IP>/`, replacing the placeholder with that IP. Sign in
   with the separate Web password stored privately in `.boiler-web-password`
   on the development computer. Never use or enter the Tuya Local Key here.
3. Use **Sign out** when finished. If a reboot or session expiry returns you to
   login, sign in again after the board is online. The dashboard is unavailable
   while Wi-Fi is down or provisioning is active.

### Read the page

| Section | What it tells you |
| --- | --- |
| Live status | Relay `ON` or `OFF` only after an authenticated DPS 1 update; `UNKNOWN` before one, or `STALE` when the last confirmed state is old or the connection is lost. The last confirmed value is shown separately. Also shows the Tuya connection, update age, freshness, shutdown obligation, and command progress/result. A queued command is **not** a verified result. |
| Monitoring and connection | Whether persistent monitoring is armed and whether the read and physical control gates are open. The buttons request a fresh read or reconnect. |
| Physical relay control | ON and OFF buttons. ON asks for browser confirmation; both use the same serialized Tuya command path as USB Serial. |
| Diagnostics | Authenticated session and push-event counts, heartbeat, connection, authentication and invalid-response counts, safe last-response metadata, and the status reconciliation interval. |
| Device information | Wi-Fi SSID, STA IP, RSSI, uptime, free heap and PSRAM, clock synchronization, and OTA phase. |
| Commands | Explanations of the Web actions and which commands require physical USB Serial. |

**Refresh authenticated status** queues one Tuya DPS 1 read. **Reconnect Tuya**
closes and reopens the ESP32's persistent Tuya session, then reconciles state.
**Turn ON** sends one ON request only after the safety checks pass, then waits
for an authenticated state read to verify ON. **Turn OFF** sends OFF and also
requires an authenticated OFF read before reporting success or clearing an
active shutdown obligation. Watch **Live status** for the final result; do not
interpret a request receipt or command acknowledgement as relay confirmation.

Buttons may be disabled while another request is running, Wi-Fi or monitoring
is unavailable, or the required gate is closed. ON also needs a **fresh**
confirmed OFF state, synchronized time, working NVS storage, and no active
shutdown obligation. `STALE` or `UNKNOWN` is never treated as OFF. The Web page
cannot open either gate: `tuya monitor arm`, `tuya monitor disarm`,
`tuya enable reads`, and `tuya enable control` are USB-only. The temporary
`tuya enable reads` command is for this boot only; normal ownership handover
uses `tuya monitor arm`, which opens monitoring and survives reboot. Physical
control always closes again on reboot; an active shutdown obligation can still
trigger an automatic safety OFF attempt.

### Hand Tuya ownership from the Android TV proxy to the ESP32

Do this under supervision, with physical access to turn the boiler OFF. **Only
one persistent Tuya client should own the switch at a time.** Do not arm the
ESP32 while the Node.js proxy still has its Tuya socket. Use a USB Serial
terminal at 115200 baud for the commands below.

1. While the existing proxy still owns the switch, check its `/status` endpoint
   and confirm the relay is OFF. Check that no ESP32 shutdown obligation is
   active. Arrange independent physical OFF access before proceeding.
2. Stop the Node.js proxy with its existing service manager. Confirm its process
   has stopped and its established socket to the switch on port 6668 is gone.
   On the proxy host, `ss -tnp` can show established sockets. Confirm the
   proxy's `/status` endpoint is no longer serving. Do not use an ESP32 read
   to test whether the proxy socket has closed.
3. Only after the socket is gone, send this over ESP32 USB Serial:

   ```text
   tuya monitor arm
   tuya diagnostics
   tuya status
   ```

   Wait for monitoring to report armed, the read gate open, an authenticated
   session, and a **fresh** `OFF` status. `tuya status` prints the current
   snapshot and queues a read; check it again for the result. Compare the
   BOILER TFT card and Web **Live status** with the Serial state. Monitoring
   now restarts automatically after Wi-Fi return or reboot. Leave the control
   gate closed until monitoring and physical-button events have been validated
   under supervision, as described in [Controlled migration and rollback](#controlled-migration-and-rollback).
4. When real ESP32 control is authorized, open its per-boot control gate over
   USB, then confirm the gates and a fresh OFF state:

   ```text
   tuya enable control
   tuya diagnostics
   tuya status
   ```

   In Web **Live status**, wait for fresh `OFF`, **Control gate** open, and a
   synchronized clock. To switch on, select **Turn ON** and accept the
   confirmation dialog; wait for authenticated `ON` verification and an active
   shutdown obligation. To switch off, select **Turn OFF** and wait for
   authenticated `OFF` verification and **Shutdown obligation: None**. Do not
   resend an uncertain ON. If OFF cannot be verified, use independent physical
   OFF access and resolve the fault before leaving the boiler unattended.

### Return ownership to Node.js

1. While the ESP32 still owns the Tuya connection, send OFF if the relay is ON
   **or a shutdown obligation is active**, even if an external action already
   turned it OFF. Wait for authenticated `OFF` verification and **Shutdown
   obligation: None**. If the per-boot control gate closed after a reboot,
   reopen it with `tuya enable control` over USB before requesting manual OFF.
   A physical OFF or push event alone does not clear the ESP32's persisted
   obligation. If it remains active, disarm is refused; keep the proxy stopped,
   make the boiler physically safe, and resolve verification before handover.
2. Over ESP32 USB Serial, send:

   ```text
   tuya monitor disarm
   tuya diagnostics
   ```

   Confirm monitoring is disarmed, both gates are closed, and the ESP32 Tuya
   socket has closed. A reboot by itself does **not** disarm persisted
   monitoring.
3. Restart the Node.js proxy through its normal service manager. Confirm its
   `/status` endpoint responds and it has regained the Tuya connection.

### Troubleshooting and safety

| Symptom | Operator action |
| --- | --- |
| `UNKNOWN` | No authenticated DPS 1 state has been received. Check Wi-Fi, monitor/read gate, proxy handover, and **Diagnostics**. Once the proxy is stopped, use **Refresh authenticated status** or **Reconnect Tuya**, then wait for a fresh state. |
| `STALE` | The last confirmed state is too old or the connection was lost; the displayed last confirmed ON/OFF is historical. Refresh or reconnect and wait for a fresh authenticated result before ON. |
| Connection unavailable | Check Wi-Fi and that the Node.js proxy is stopped and its socket closed. Check connection, heartbeat and authentication counters, then reconnect. Do not start a second Tuya owner. |
| Button locked or request rejected | Check monitor/read/control gates, pending command, clock, lease and state freshness. Only USB Serial can arm monitoring or enable control; Web cannot bypass a closed gate. |
| Verification failed or uncertain | An acknowledgement does not prove a relay state. Never repeat an uncertain ON automatically. If OFF is not verified, use the independent controller or physical switch to ensure the boiler is OFF; retain the shutdown obligation until the ESP32 confirms OFF after its own OFF request. |

Use the dashboard only on a trusted LAN. **Plain HTTP does not protect the
password or session from someone who can observe local traffic.** Keep router
port forwarding disabled. The persisted 30-minute shutdown obligation is a
software recovery measure, not a guaranteed hardware cutoff: ESP32 power,
network, storage, or device failure can prevent timely OFF. Provide independent
physical or hardware protection; see [Shutdown obligation and limitations](#shutdown-obligation-and-limitations).

## Shutdown obligation and limitations

The ESP32 persists its own
versioned 30-minute shutdown deadline in the separate `boiler` NVS namespace
**before** sending ON. ON is rejected without valid NTP time or a verified NVS
write. A second ON cannot extend an active obligation. On expiry, the ESP32
attempts OFF and verifies DPS 1, retrying after bounded intervals until OFF is
confirmed. An obligation recovered after reboot can trigger OFF even while the
per-boot manual gates are closed; it never triggers ON. With invalid time after
reboot, OFF is attempted when Wi-Fi permits. While powered, elapsed uptime is
also checked so a backward wall-clock correction cannot extend the deadline.
The obligation stays active after an uncertain ON or an external OFF; it is
cleared only after an authenticated OFF confirmation following an ESP32 OFF.

This is a software recovery mechanism, **not a guaranteed hardware safety
timer**. Power loss, NVS damage, network loss, device failure, or loss of the
ESP32 can prevent shutdown at the deadline. An independent hardware timer or
thermal protection is needed for a guaranteed limit. The old Node.js proxy's
timer is not part of the ESP32 safety path.

The persistent connection processes authenticated command `0x08` updates,
sends serialized heartbeats every 10 seconds, and reconciles status every 30
seconds. Reconnect backoff is bounded between 1 and 32 seconds. A missing or
invalid update never establishes relay state; stale state remains visible as
stale. Only one TCP owner is expected. Do not arm monitoring while the Node.js
proxy holds the Tuya socket.

## Offline checks and hardware stages

Run the host policy/DPS/Web authorization test, mock protocol test, existing
repository tests, and both `esp32-s3-n16r8-tft` and `esp32-s3-n16r8-tft-ota`
builds. The Nature Glass image, other seven cards, Wi-Fi, and OTA code paths
are preserved. Only card 0's label and its 120 × 20 value region are changed
to the BOILER state/LED. Stage 1 confirmed the displayed BOILER/UNKNOWN card and
the unchanged seven other cards and Nature Glass design.

## Controlled migration and rollback

No upload or cutover is included in the offline implementation. After separate
approval, upload the USB image without erasing NVS. Confirm `monitor_armed=no`,
`read_gate=off`, and `control_gate=off` from `tuya diagnostics`; verify boot,
Wi-Fi, OTA, TFT, and Web. Keep the proxy running during these checks.

For a separately approved cutover, first confirm the relay is OFF through the
existing proxy and arrange independent physical OFF access. Stop the Node.js
proxy using its normal service manager, then confirm the process and its
connection to the switch's TCP port 6668 are gone. On the proxy host, check
the service status and use `ss -tnp` to confirm there is no established socket
to the switch on port 6668; the proxy's HTTP endpoint should also stop
responding. Do not use an ESP32 read as the socket-closure check. Only then
send `tuya monitor arm` over
USB. Confirm `monitor_armed=yes`, an authenticated DPS 1 read, stable heartbeats,
and the proxy remaining stopped. Press the physical button under supervision
and compare each authenticated event against the TFT and Web display. Restore
the relay to OFF physically and confirm an authenticated OFF read. Control
stays closed throughout this event validation. Real ESP32 ON/OFF commands need
their own explicit approval after monitoring passes.

If monitoring fails, send `tuya monitor disarm` over USB, confirm both gates
closed and the ESP32 Tuya socket disconnected, then restart the existing proxy
and confirm its `/status` endpoint and Tuya connection. If USB is unavailable,
powering off or flashing the ESP32 closes its socket; confirm it is gone before
restarting the proxy. Do not disarm while a persisted shutdown obligation is
active; first obtain authenticated OFF confirmation or use independent physical
control. A reboot alone does not disarm persisted monitoring.

### Stage 2 diagnostic follow-up (offline)

The first bounded live read window produced two invalid responses, with no
authenticated DPS 1 state. The proxy status endpoint remained responsive and
reported OFF. The ESP32 read and control gates were closed afterward.

Offline comparison with tuyapi 7.5.1 found two protocol differences: the
ESP32 status query previously sent only `{}`, while tuyapi sends `gwId`,
`devId`, string timestamp, empty `dps`, and `uid`; and the ESP32 assumed every
3.4 response included a four-byte return code, while tuyapi handles responses
that omit it. The active Node.js installation resolves `^7.5.1` to 7.7.0;
the exact 7.5.1 package was separately inspected for this comparison.
Without safe frame metadata from the failed reads, neither difference can be
confirmed as the device's actual failure point.

The firmware now sends the full query and accepts both response layouts only
after HMAC verification. `tuya diagnostics` and Serial protocol lines report
only command ID, declared frame length, response type, and failure category.
They never report device identifiers, keys, packet bytes, or decrypted data.
The next hardware test requires separate approval and must remain read-only;
Stage 3 control testing is gated on a confirmed authenticated DPS 1 read.

No firmware upload, live Tuya communication, relay switching, flash erase,
push, or PR is part of the offline implementation.

### Second read-only retest and offline handshake fix

The second bounded read window received a command `0x04` frame totaling 120
bytes. Its outer HMAC passed, but the earlier parser rejected it because it
allowed only 100 or 104 bytes. For a 120-byte response, the 16-byte header
declares 104 following bytes: 4-byte return code, 64-byte AES ciphertext,
32-byte HMAC, and 4-byte suffix. The ciphertext begins at offset 20 and the
HMAC at offset 84. Both tuyapi 7.5.1 and the installed 7.7.0 derive the payload
boundary from the header, verify the outer HMAC, decrypt, remove PKCS7 padding,
then interpret the 16-byte device nonce and 32-byte HMAC of the local nonce.
Their parser also accommodates an optional 15-byte `3.4` prefix and an omitted
return code. The safe live metadata did not contain decrypted content, so it
cannot distinguish a 48-byte nonce/HMAC body plus a full padding block from a
63-byte prefixed body plus one padding byte.

The firmware now derives the handshake ciphertext length from the authenticated
frame and accepts those two content layouts only after block alignment,
decryption, exact padding, optional prefix, inner HMAC, and nonce checks. It
continues to reject a nonzero return code. Mock tests cover the 120-byte case,
the no-return-code variant, and malformed/authentication failures. A successful
mock handshake does not establish the real device's decrypted layout or DPS 1
state; those need a separately approved read-only retest.

The Web `401` during that window occurred too soon for the configured 15-minute
idle or 8-hour absolute expiry. The test script logged in before opening USB
Serial; opening the port can reset the ESP32-S3, which clears its in-memory Web
session. A Wi-Fi/IP change or a second login would also clear or replace the
session. No boot/Wi-Fi events were captured in that window, so the exact cause
is unconfirmed. The next approved test should open Serial and establish a stable
boot before logging in, and record boot/Wi-Fi events if a session is lost.
