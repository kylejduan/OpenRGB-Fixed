# 2026-09-16: G502 X PLUS shows firmware blue instead of saved magenta

Installed build: OpenRGB Fixed 1.0rc3.1-fixed.2, commit 8fc2a339 (unchanged since the
2026-09-13 acceptance). Windows booted 19:24:49 PDT (fifth boot of the evening, two of them
Kernel-Power 41). OpenRGB launched from the logon task at 19:25:17 and logged
"Profile loading: Succeeded for G502 X PLUS" with no lighting warnings.

## Evidence

| Time (PDT) | Source | Observation |
| --- | --- | --- |
| 19:25:38 | OpenRGB_20260916_192520.log | Main profile (G502 Direct, FF00FF) applied, no Warning lines |
| 19:45:43 | lighting-state-20260916-194542.json (2026-09-13-g502-rcca) | 0x8071 sw control = 0 (firmware), events = 6, RGB power = 2 (power save), onboard mode 1 |
| 19:48:52 onward | after-reload / paint-magenta / probe-slot error traces | Mouse slot 1 gives no HID++ reply (deep sleep); Powerplay slot 7 replies normally |
| 19:53 | wake-watch-20260916-195259.jsonl | Detached watcher started (PID 28532): waits for the mouse, records wake state, paints FF00FF, logs control transitions for 2 h |

Saved profile Main.orp: G502 X PLUS active mode 1 (Direct), LED color #FF00FF. Unchanged.

## Interpretation

- OpenRGB claims software control with control=3, events=5 (EFFECTS_SYNC + NO_USER_ACTIVITY_TIMEOUT).
- The mouse now reports control=0, events=6 (USER_ACTIVITY + NO_USER_ACTIVITY_TIMEOUT).
  The same 0/6 state appeared spontaneously on 2026-09-13 at 00:46 right after the operator woke
  the sleeping mouse, and the operator reported the mouse turning "light blue".
- `logi_lamparray_service.AMD64.exe` (Logitech LampArray Service 1.1.91.2790, running, Auto)
  contains HID++ client types `sw_control_config@feature_8071`, `rgb_power_mode@feature_8071`,
  `user_activity_event@feature_8071` (EventsHandler subscriber) and
  `device_connect_event@feature_1895`. Events=6 is that subscription; OpenRGB never writes 6.
- Windows Dynamic Lighting is off (HKCU\Software\Microsoft\Lighting AmbientLightingEnabled=0), so
  the service leaves the mouse in firmware (autonomous) lighting, i.e. the onboard default blue.
- Working hypothesis: on each Lightspeed reconnect (wake from deep sleep, reboot), the LampArray
  service re-initialises the mouse to control=0/events=6. OpenRGB only re-asserts control on its
  next explicit update and has no reconnect/wake handler, so the mouse stays blue until a profile
  reload. The 09-13 acceptance never idled the mouse, which is why it passed.

## Still to confirm (needs the operator)

1. Read `wake-watch-*.jsonl`: expect `wake-state` 0/6, `painted`, then whether control flips back
   to 0/6 after a `no-reply` (sleep) to reply (wake) transition while the LampArray service runs.
2. Discriminating test (needs UAC): stop `logi_lamparray_service`, reload the Main profile in
   OpenRGB, let the mouse sleep and wake. If control stays 3/5 and the mouse stays magenta, the
   attribution is confirmed. Restore the service afterwards unless it is to be left disabled.

## Actions, 2026-09-16 evening

- 20:08 Detached watcher woke with the mouse, read control 0/6, claimed 3/5 and painted FF00FF.
  Operator confirmed the mouse turned magenta.
- 20:12 `logi_lamparray_service` set to Manual and stopped via elevated `disable-lamparray-service.ps1`
  (record: `lamparray-disable-20260916-201220.json`, restore command inside).
- Durable fix implemented in OpenRGB-Fixed as fixed.3 (commits 351d2d3..a343580, pushed to main):
  per-receiver watcher repaints on lost 0x8071 control (30 s poll, checks 2 s/10 s after link up,
  no traffic while link down) and registers devices that were asleep at detection.
  Design: `docs/superpowers/specs/2026-09-16-lightspeed-reconnect-watchdog-design.md` (local, ignored).
- Install tooling: `install-fixed3.ps1 -Revision <sha>` (backup to `rollback-before-fixed3`),
  acceptance: `takeover-test.ps1` (forces 0/6 and times the reclaim), `notify-log.ps1` (link reports).
  Diagnostics now load `hidapi\hidapi.dll` (copy) so they never lock the install directory.

## First fixed.3 install (a343580), 21:07

- Installed cleanly (`installed-fixed3.json`); watcher started, but the G502 was not registered:
  creation ran 6.4 s -> 70.8 s with no "Not Connected" lines (link up, every attempt invalid).
- 21:09 read-only probe: mouse answering, control 3/5 (left from the fixed.2 process).
- 21:10 `restart-debug.ps1` (loglevel 5): G502 registered on first attempt, replies ~7 ms apart
  (`debug-detection-20260916-211050.log`).
- Root cause: upstream init queries accept the next report within 300 ms without matching; slow
  post-wake replies (487 ms measured 09-13) shift every answer. fixed.2 logs show the same fault as
  14 s / 35 s registrations. Fixed in fe2a916 (matched queries, 1 s deadline) and 45b6769 (retry
  linked devices that fail detection, 1 s watcher read deadline). CI run 35181364562.
- 21:18 notification logger restarted on the private DLL copy (PID 48160, 8 h).

## 21:36 reboot and corrected install (d0c4617), 21:58

- 21:37 logon start of a343580: enumeration read the ACK then empty reads, invented slot 0, registered
  neither mouse nor mat; mouse at control 0/0 (firmware blue) with the LampArray service stopped.
- Fixed upstream enumeration (fa61f8f), unknown-slot registration and always-on watcher (768405c).
- 21:45 stopgap `paint-magenta-local.ps1`: control 0 -> 3/5, magenta frame sent.
- 21:58 `install-fixed3.ps1 -Label fixed3b` (backup `rollback-before-fixed3b`; true fixed.2 rollback
  remains `rollback-before-fixed3`). Receiver had wireless notifications off; enumeration enabled them.
  Both devices registered, watcher on 2 slots, detection done at 7.8 s.
- 21:58:55 `takeover-test.ps1`: forced 0/6, watcher reclaimed 3/5 after 16.2 s (log line at 37.6 s).
- Process cost sample: 0.77 CPU-s/min, 20 threads, 52 MB. Notification logger stopped at 22:01.
- Still to observe physically: color after takeover, deep sleep/wake, cold boot, start while asleep.

## Review fixes and final install (832f53c), 22:35

- Code reviews found 4 defects in the watcher wiring; fixed in b1ed622, d74ebfd, 832f53c.
- 22:33 an install attempt with a placeholder revision argument was refused before any change
  (`install-fixed3c-aborted-wrong-revision-arg*.txt/json`).
- 22:35 installed 832f53c (`installed-fixed3c.json`, backup `rollback-before-fixed3c`).
- `link-takeover-test.ps1`: forced 0/6 plus receiver re-announcement -> reclaimed 3/5 in 2.14 s.
- `takeover-test.ps1`: forced 0/6, no link event -> reclaimed in 28.3 s.
- Not yet observed: real deep sleep/wake, cold boot, OpenRGB start while the mouse sleeps.

## Release v1.0rc3.1-fixed.3, 2026-09-17 00:10

- Release commit decb2df (README fix list); CI run 35190967505: Linux and Windows builds, version checks,
  45 regression tests each (Linux under ASan/UBSan). Privacy scan of outgoing commits, tree and package:
  clean apart from the previously reviewed numeric match in Qt's unchanged qwebp.dll.
- Installed the exact CI package (`installed-fixed3-release.json`, backup `rollback-before-fixed3-release`);
  OpenRGB.exe 13173370ab4e9173ee5da39189aedb28c26c720eaca5017bc9852066fa561329.
- On that build: link-takeover reclaim 2.14 s; poll-path takeover reclaim 24.9 s.
- Published https://github.com/kylejduan/OpenRGB-Fixed/releases/tag/v1.0rc3.1-fixed.3 (pre-release) with
  the Windows ZIP and the unchanged third-party source archive; public download re-verified.
- fixed.2 rollback remains `rollback-before-fixed3`.

## Colour drift with ownership held, 2026-09-17 20:03

- Operator reported the mouse light blue. Probe: control 3, events 5, power 1 (OpenRGB owned lighting);
  log since the 04:07 boot had no watcher lines, so ownership was never lost and nothing repainted.
- `link-takeover-test.ps1` forced a takeover; the watcher re-applied and the operator confirmed pink.
  So the device repaints its own default while keeping host software control; 0x8071 cannot read the
  current colour back, so only a periodic re-apply can detect it.
- fixed.4 (c0c6eb7, f494c8f): re-apply 1.5 s and 10 s after link-up, re-apply when RGB power returns to
  full, and refresh host-painted colours every 5 min (`repaint_seconds`, 0 off). Refreshes skip
  device-side animations and devices in RGB power save. 50 regression tests; each new rule
  mutation-checked; TSan clean.
- Installed CI package (`installed-fixed4.json`, backup `rollback-before-fixed4`), exe 1a16184471…
  `lighting-traffic-*.log` (passive listener on the long-report collection) shows OpenRGB's control+power
  reads every 30 s and colour writes (fn 0x10, sw id 7) at 20:49:01 and 20:54:01 — the 5 min refresh.
- Published release v1.0rc3.1-fixed.4; public download verified; installed exe == released exe.
- Open: which device transition precedes the drift. Monitors (`drift-monitor.ps1`,
  `watch-lighting-traffic.ps1`) stopped; their logs from 20:14-20:56 are kept here.
