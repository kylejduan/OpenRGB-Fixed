# Logitech lighting recovery

This investigation concerns a G502 X PLUS paired through a Powerplay receiver
(`046D:C53A`). That connection uses `LogitechLightspeedController` and
`logitech_device`; it does not use the newer `LogitechHIDPP20Controller`.
The shared-handle crash repair is a separate correction and remains in place.

## Driver findings

1. The legacy driver claimed RGB Effects `0x8071` software control only during
   detection. Color updates continued without checking whether that claim had
   been released. The mouse was observed returning control `0` after previously
   returning `3`, while the same OpenRGB process remained running.
2. `setDirectMode()` used function `0x80` for both RGB feature generations.
   That function selects software control on `0x8070`, but selects RGB power on
   `0x8071`. Selecting Direct therefore did not reacquire the mouse's software
   lighting control, which requires function `0x50`.
3. Static effects used the default transition parameter `0`. The explicit
   no-ramp parameter `2` is used by the newer driver. In the hardware sequence
   below, reacquiring control and using the old default packet was insufficient
   after a control handoff.
4. A lighting write consumed one HID reply without matching its device slot,
   feature, function, or software ID. Receiver notifications could therefore be
   mistaken for replies. Unsigned return values also lost negative HID errors.
5. The first recovery candidate painted immediately after RGB power read back
   as full. Hardware tests showed that this readback can precede rendering
   readiness: the mouse acknowledged those frames but remained dark.
6. A matched wireless color reply took about 487 ms in a later isolated test,
   exceeding the earlier 300 ms lighting response deadline.

Items 1, 2, and 4 are established by source and regression tests. The tests
validate packets and transaction behavior; they do not emulate physical LEDs.

## Controlled hardware observations, 2026-09-13

The installed earlier crash-fix build remained running. Logitech services were
left running, and no rescan, app restart, or onboard-profile-mode change was
used for these experiments. Raw HID tests opened their own receiver handle.

| Experiment | Physical observation |
| --- | --- |
| Release software control; apply red through the existing GUI | Red ignored |
| Reacquire software control; apply the same GUI red | Red still ignored |
| Send the old red packet directly, with control `3` and power `1` verified | Red ignored; off or another color |
| Send red with static parameter `2`; ask the operator to wake the mouse | Solid red |
| Send the old blue packet after that recovery | Solid blue |
| Release and reacquire control, then send the old red packet; mouse awake | Wrong color or off |
| Send corrected red after that reproduced failure; mouse awake | Solid red again |

The old packet's success after recovery is significant: the failure depends on
lighting state. This evidence does not show that every default-parameter packet
fails, or identify the internal firmware transition responsible. The original
historical loss of control has not been attributed to a particular service.
The later wake tests below also show that parameter `2` alone is insufficient.
The original sequence does not isolate that parameter from elapsed time and
prior frames, so a marker-only explanation is not established.

### First installed candidate and RGB wake

The complete build at `d84e189` physically applied green, then recovered from
an explicit software-control release with a blue color-only SDK update. The
same process remained running, with no mode change or rescan.

Further tests explicitly set RGB power to off (`3`), then restored full power
(`1`). Control and power readbacks and matched replies were recorded. Each row
below began with another RGB power-off step; frames used the same corrected
static format. The mouse's onboard-profile mode remained `1`.

| First update after restoring RGB power | Physical observation |
| --- | --- |
| Installed candidate sends red immediately | No light |
| Installed candidate sends blue immediately; existing ownership left untouched before power-off | No light |
| Two ownership reads, then one red frame at about 48 ms | No light |
| Ownership SET plus readback, then one red frame at about 45 ms | No light |
| Two immediate red frames at about 49 and 63 ms | No light |
| One red frame after a one-second interval | Solid red |
| One blue frame after the same interval | Solid blue |
| Logitech lighting service stopped; installed candidate sends red immediately | No light |
| Service still stopped; one blue frame after the interval | Solid blue |

An identical red command issued later, without another power-off step, also
worked. Reasserting ownership and duplicating an immediate frame were therefore
insufficient corrections. The stopped-service comparison establishes that
Logitech's lighting service is not required to reproduce this wake failure;
the service was restored afterwards.

The evidence supports allowing the rendering path to settle after power
recovery. One second is an experimentally successful interval, not a documented
firmware deadline or a guarantee for every device. The internal firmware
transition remains unproven. The exposed power getter supplies no rendering
readiness indication. RGB power-save (`2`) transitions and full OS sleep/resume
were not physically tested in this sequence.

## Correction in source

Before each explicit `0x8071` color update, query software control and RGB power.
Reacquire missing control, restore full RGB power if needed, and verify those
changes before painting. An already-held claim is left intact. Direct and Static
both use the correct `0x8071` preparation. Static colors use parameter `2`,
including black. Effect writes remain volatile and do not write onboard flash.

After restoring non-full RGB power to full, allow one second before painting,
then read power and ownership again. Reject an unexpected power change and
reacquire a lost claim. Remember the pending interval even if the wake reply is
malformed or missing, so an immediate follow-up update cannot skip it merely
because power now reads as full. The interval applies only to power recovery. It runs
under the same receiver lock, temporarily delaying other lighting commands to
the paired mouse and mat during recovery. Existing firmware timers and stored
settings are preserved.

Hold the shared receiver mutex across the complete preparation and color
transaction. Use nonzero software IDs and match replies to the request; reject
HID++ errors, truncated replies, failed writes, and timeouts. Log failures and
return a signed failure result. The lighting reply deadline is one second to
accommodate the observed slower wireless reply. Complete short and long replies are accepted;
queued stale reports are drained before requests. The shared OpenRGB software
ID is `0x07`, matching the newer driver. An identical delayed reply arriving
after the drain cannot be distinguished solely by this four-bit identity.
Preserve the `0x8070` Powerplay mode and color
payloads.

This recovery runs when OpenRGB sends an update. It adds state-query round trips
to `0x8071` updates; it is not a background reconnect or idle/wake monitor. It
does not change services, mouse button/DPI profiles, or firmware idle timers.
Continuous contention from another RGB writer is outside that guarantee.

## Validation status

The standalone suite includes the two existing lifetime tests plus packet,
control-recovery, RGB-power, reply-correlation, and failure-path tests. The new
control tests failed on the earlier implementation; the static-packet test
failed before the parameter change. The power tests now model immediate power
readback with temporarily unavailable rendering, plus state changes during
settling. Those cases failed before the wake correction. A delayed-reply test
failed with the old response deadline. A failed-wake-ACK test also verifies
that the immediately following color update observes the pending interval.
All 22 pass under Clang with
AddressSanitizer and UndefinedBehaviorSanitizer. The modeled transition time is
a regression fixture, not a claim about firmware internals.

### Installed executable acceptance, 2026-09-13

The complete Windows and Linux applications built successfully from application
source equivalent to the rewritten commit
`9cbe4eff76f629da8f2ecdf35f817c489a050813`. All 22 regression tests passed on
both platforms; Linux also used AddressSanitizer and UndefinedBehaviorSanitizer.
The original build produced the Windows package used below; its private CI
receipt was retained when commit identities were sanitized. Its 52 payload files were verified
against the package manifest before installation and after acceptance testing.

Installed `OpenRGB.exe` SHA256:
`2b6a6d77bb46cdad0372d46446e7a0d8157e309b90d40f6f604ab6f3cd3cb2ea`.

Logitech's lighting and updater services were running during these tests.
Fault injection changed only the specified RGB state. The color-only tests
used OpenRGB's SDK to call the installed driver, without changing mode,
rescanning, or restarting between the injected fault and color update.

| Installed-build test | Observation |
| --- | --- |
| RGB power off, existing ownership untouched; first green update | Physically solid green; driver logged its wake interval before the single color frame |
| Release software lighting control; blue update | Physically solid blue; control read back as `3`, power as `1` |
| Powerplay green update | Physical mat logo solid green |
| Two SDK rescans and one user-operated GUI rescan | All completed in the same process; five controllers and both RAM modules remained detected |
| GUI Direct mode, red, Apply Colors To Selection after rescan | Physical mouse solid red |
| Clean exit, then launch through the normal elevated startup task | Exit code `0`; same executable and unchanged saved profile; mouse physically changed from test red back to the saved magenta |

The temporary SDK listener was closed for the normal startup-task test. Both
Logitech services remained running with automatic startup. Full Windows reboot,
sleep/hibernation/resume, receiver reconnection, RGB power-save transitions, and
future Logitech driver updates still need separate physical tests.

### Resume configuration follow-up, 2026-09-13

The existing **Set Profile on Resume** setting was disabled in the tested
installation, matching its default. It was enabled for the saved profile, then
OpenRGB was cleanly restarted through the same elevated login task. The setting
persisted, the saved profile was unchanged, and the startup log recorded profile
loading for all five controllers. This verifies configuration persistence and
startup loading; no physical Windows sleep/wake or hibernation test was run.

This is a local configuration choice, not a change to the fixed.2 binary or its
defaults. The existing Windows resume handler requests the configured profile
once per notification; it does not add receiver reconnection or readiness
retries. Users can enable it with the
[resume setup instructions](Troubleshooting-Fixed.md#sleep-hibernation-and-resume).
The installed binary retains the checksum above. Release packages rebuilt after
commit identity cleanup have new source identifiers and checksums; the application
code and the scope of physical acceptance are unchanged.

## Reconnect and takeover follow-up, 2026-09-16

The installed fixed.2 build started from the normal logon task after the fifth
Windows boot of the evening and logged a successful Main profile load for the
G502 X PLUS with no lighting warnings. The mouse later showed its onboard blue
instead of the saved magenta.

| Time (PDT) | Observation |
| --- | --- |
| 19:25 | Startup log: Main applied to all five controllers, no `Warning` lines |
| 19:45 | Read-only HID++ probe: software control `0`, event flags `6`, RGB power `2`, onboard mode `1` |
| 19:48 | Mouse slot gave no HID++ replies (deep sleep); the Powerplay slot answered |
| 20:08 | On wake: control `0`, flags `6`, power `1`; a direct claim `3/5` and static magenta frame restored the physical color |

OpenRGB claims control with flags `5`; flags `6` add user-activity events and
drop effect-sync events, so another host wrote them. `logi_lamparray_service`
(Logitech LampArray Service, driver 1.1.91.2790, running and automatic) contains
HID++ client types for `0x8071` software-control configuration, RGB power mode,
user-activity events, and receiver device-connect events. Windows Dynamic
Lighting was off, so that service left the mouse in firmware lighting. The same
`0/6` reading appeared on 2026-09-13 immediately after the mouse was woken. The
service is therefore the likely writer; the attribution is not proven by a trace
of its writes. With the operator's approval the service was set to Manual and
stopped on 2026-09-16 at 20:12.

The detection logs also explain the unregistered-mouse case. The receiver's
device connection report for slot 1 carries flags `A2` when the mouse is awake and
`62` when it is asleep; bit `0x40` marks a link that is not established. In the
19:09 startup the mouse was asleep and detection logged ten "Not Connected"
retries without registering it.

The fixed.2 recovery only ran inside an explicit update, and nothing requested
one after the takeover. Fixed.3 adds the receiver watcher described in
[Fixes](Fixes.md#lightspeed-reconnect-recovery). It does not depend on the
service attribution: any loss of control on a linked device is repainted.

### First fixed.3 installation

The first fixed.3 build (`a343580`) installed cleanly at 21:07 and started its
watcher, but the G502 X PLUS was not registered. Its creation ran from 6.4 s to
70.8 s after launch with no "Not Connected" lines, so the receiver reported the
link up while every initialization attempt failed validation. A read-only probe
at 21:09 got normal replies. A restart with debug logging at 21:10 registered the
mouse on its first attempt, with replies about 7 ms apart.

The upstream initialization queries accepted the next report within 300 ms
without matching it to the request. Earlier wake tests measured a 487 ms reply,
and fixed.2 logs contain 14 s and 35 s registrations consistent with retried
attempts. Fixed.3 pairs each initialization query with its reply using a
one-second deadline, retries linked devices that still fail detection, and allows
the watcher's control read one second.

### Restart with the first fixed.3 build

Windows restarted at 21:36 and the logon task started that first build at 21:37.
Enumeration logged the receiver's acknowledgement (`10 FF 80 02`) followed by two
empty reads, then ten failed attempts on a nonexistent slot 0 over 21 s. Neither
the mouse nor the mat was registered. At 21:39 a probe read the mouse at software
control `0`, event flags `0`, full RGB power: firmware lighting with no host
control, while the LampArray service stayed stopped. The corrected enumeration
matches register replies, waits up to 3 s for every paired device's
announcement, and ignores the acknowledgement. The watcher also registers a slot
first seen through a later link notification.

### Installed acceptance of `d0c4617`, 2026-09-16

The build from CI run 35183061133 was installed over the first fixed.3 build
with verified package hashes, a clean exit of the previous process, and a start
through the normal logon task. A separate read-only listener on the receiver's
short-report interface recorded the startup traffic.

| Check | Observation |
| --- | --- |
| Receiver reporting register after the 21:36 restart | Read `00`: wireless notifications were off. Enumeration set the bit and the receiver acknowledged it |
| Enumeration | Count `2`; announcements for slot 1 (`A2`) and slot 7 before the acknowledgement |
| Detection | Powerplay mat and G502 X PLUS registered; watcher on 2 slots; Main applied; detection completed at 7.8 s |
| Forced takeover: control set to `0` with flags `6` | Watcher logged the loss at its first 30 s poll; control read back `3/5` after 16.2 s |
| Whole OpenRGB process, one-minute sample after the test | 0.77 CPU-seconds per minute (1.3 % of one core), 20 threads, 52 MB working set |

A later per-thread sample attributed that cost: ten controller worker threads, five
for detected devices and five for placeholder controllers kept from the sizes file,
each woke about 570 times per second in their 1 ms idle sleep, at 0.1 to 0.2 % of one
core each. The watcher thread woke 4.2 times per second and measured 0.0 %.

### Review follow-up and final build `832f53c`, 2026-09-16

Two code reviews of the fixed.3 changes found four defects, all fixed before this
installation: late device creation held the background lock through ten attempts,
late creation could consume the watcher's link notifications from the shared handle,
no-reply backoff shortened polls configured above 120 s, and the watcher's own handle
could listen to a different receiver than the bundled one when two receivers share
a product ID.

| Check on `832f53c` | Observation |
| --- | --- |
| Installation | Package hashes verified; previous build exited cleanly; logon task started the new build |
| Detection | Slots 1 and 7 announced; both devices registered; watcher on 2 slots with its own handle; Main applied at 8.9 s |
| Forced takeover, then receiver re-announcement | Control `3/5` again 2.1 s later: the watcher acted on the link notification |
| Forced takeover without a link event | Control `3/5` again after 28.3 s, within the 30 s poll |

Deep sleep and wake, a cold boot, and starting OpenRGB while the mouse sleeps have
not been observed physically on this build.

### Colour drift with ownership held, 2026-09-17

The release build started at 04:08 after a 04:07 restart, registered both devices
and applied the saved profile. About sixteen hours later the mouse showed its
onboard blue while a read-only probe reported software control `3`, event flags
`5` and full RGB power, and the log contained no watcher activity: ownership had
never been lost, so nothing triggered a repaint. Forcing a takeover made the
watcher re-apply the profile, and the operator confirmed the colour returned.

The device therefore repaints its own default while keeping the host's software
control. This is consistent with the 2026-09-13 wake tests, where frames were
acknowledged while the RGB engine was not rendering. Ownership state does not
describe what the LEDs show, and `0x8071` offers no way to read the current
colour back, so a periodic re-apply is the only available detection.

Fixed.4 re-applies colours 1.5 s and 10 s after a link comes up, when RGB power
returns to full, and every five minutes for host-painted colours. Refreshes skip
device-side animations and devices in RGB power save. A read-only monitor of
control, power and receiver notifications was started to identify which
transition precedes the drift; that attribution is still open.

## Protocol references

- [Logitech HID++ packet layout and software IDs](https://github.com/Logitech/cpg-docs/blob/master/hidpp20/README.rst).
- [RGB Effects function mapping](https://openlogi.org/hidpp/features/x8071-rgb-effects),
  from the OpenLogi protocol implementation documentation.
- [Solaar effect parameter definitions](https://github.com/pwr-Solaar/Solaar/blob/e7304c4c451cc9bb4f206a914844525e67856a28/lib/logitech_receiver/hidpp20.py):
  static color and its ramp parameter; `Default=0`, `No=2`.
- The imported newer OpenRGB driver's `SetZoneEffect()` uses the explicit static
  parameter too. It is a comparison implementation, not the active driver for
  this Powerplay connection.
