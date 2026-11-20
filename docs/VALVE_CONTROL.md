# Local valve control — valve-ui-v3

S3 routes the API16 tablet through existing login/Origin/CSRF HTTP controls and
paired, encrypted, HMAC-authenticated ESP-NOW. Original remains the sole valve
controller and mode/target authority. No Tuya/controller gate or timer changes.

GET /api/valve reads Original mode/target, motion, startup restoration and result.
POST /api/valve accepts exactly action=auto|manual|open|close|stop.202 reports
S3_RECEIVED and a command ID plus boot session, never physical movement completion.
Periodic Original status distinguishes ACCEPTED, REJECTED, timed COMPLETED_INFERRED
and FAILED. All positions are inferred. Status older than3s disables movement.
Manual movements require Original MANUAL/readiness; STOP has queue priority.
No uncertain movement is replayed. Existing telemetry records remain compatible.

Full task, safety review, tests and deployment evidence are tracked in the
hot-water-platform Original checkout's tasks/phase-2/13-valve-manual-boot-deployment.md.
The branch includes the prior deployed VALVE-label presentation fix and regression
as a deliberate part of this valve delivery; unrelated working-copy changes stay out.

## Authoritative presentation follow-up (P2-14)

TFT now consumes the same signed Original ValveSnapshot as HTTP valveControl,
independently of legacy temperature transport. Freshness is <=3s. Endpoint text
uses AUTO green / MANUAL yellow; WAIT/movement stays amber and FAULT red.
Unknown, stale or inconsistent phase/target is neutral gray. Mode is redrawn
from fresh Original mode, including reconnect, and never from a sent command.
No valve command, GPIO, persistence, motor timing or Tuya policy changed.

Focused presentation and API C++ tests, telemetry snapshot ordering, scenic
background regression and production TFT build pass. Android uses matching
semantics and an API16 bundled PNG gear. Deployment evidence will be appended
after private identity/layout/recovery and app-only preservation checks.

Live stress testing exposed a boiler-web stack-canary panic in the existing
12KiB HTTP task while serving authoritative status. The task now has24KiB and
reports its FreeRTOS minimum free stack in device.webStackFreeBytes. JSON stays
heap allocated. This addresses HTTP/reconnect reliability; no actuator path,
Tuya protection, transport command or persistence change is involved. Final
live polling below demonstrates stack headroom and increasing uptime.

## Final deployment evidence (2026-11-21)

- valve-ui-v3 production image built through the existing vault. ROM/USB identity,
  flash size/security, installed/compiled partitions and recovery were checked.
  App-only USB write has exact independent readback; all non-app ranges and unused
  application tail match pre-write MD5s, preserving NVS/filesystem/OTA metadata.
- Corrected firmware passes125s concurrent tablet/HTTP stress:220 status samples,
  all fresh ESP-NOW, no uptime reset, minimum HTTP stack headroom12064 bytes.
  Login/CSRF/invalid action/schema/NUL rejection checks pass without valid movement
  requests. Boiler remains OFF with no shutdown lease; no boiler command is sent.
- Native API16 tablet MANUAL and AUTO requests reach the authoritative Original;
  target remains OPEN and transition count stays13. Owner observes TFT MANUAL /
  yellow OPEN and AUTO / green OPEN; simulation label is absent. Actual tablet
  screenshots verify matching semantic color pixels and visible raster gear.
- MANUAL survives S3 reconnect/reboot and tablet process relaunch by reading fresh
  Original status. Offline tablet displays neutral UNKNOWN. OPEN/CLOSED in both
  modes, mismatched target, fault/movement priority and reconnect are host fixtures;
  physical CLOSE is deliberately not issued for a color test. Position is inferred.
- Original firmware remains valve-manual-v1. No controller, GPIO, timing, filesystem,
  credentials, boiler ownership, gate or shutdown-obligation changes. Private
  recovery images, data and crash logs stay outside Git. Review found no remaining
  in-scope correctness/safety issue after the stack fix and live stress test.

Owner follow-up: after the automated mode-only check, the owner confirms testing
MANUAL/CLOSE from the tablet. Final read-only snapshots agree on AUTO/CLOSED,
with Original outputs INPUT/INPUT and fresh S3 ESP-NOW; no setting is overwritten.
An additional read-only actual tablet screenshot verifies green CLOSED in AUTO.
The automatic checks issued no OPEN/CLOSE; later movements were owner tests.
Final readback shows331s S3 uptime and the same12064-byte minimum stack margin.
