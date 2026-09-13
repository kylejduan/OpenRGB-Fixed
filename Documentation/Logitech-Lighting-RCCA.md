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
sleep/resume, RGB power-save transitions, and future Logitech driver updates
still need separate physical tests.

## Protocol references

- [Logitech HID++ packet layout and software IDs](https://github.com/Logitech/cpg-docs/blob/master/hidpp20/README.rst).
- [RGB Effects function mapping](https://openlogi.org/hidpp/features/x8071-rgb-effects),
  from the OpenLogi protocol implementation documentation.
- [Solaar effect parameter definitions](https://github.com/pwr-Solaar/Solaar/blob/e7304c4c451cc9bb4f206a914844525e67856a28/lib/logitech_receiver/hidpp20.py):
  static color and its ramp parameter; `Default=0`, `No=2`.
- The imported newer OpenRGB driver's `SetZoneEffect()` uses the explicit static
  parameter too. It is a comparison implementation, not the active driver for
  this Powerplay connection.
