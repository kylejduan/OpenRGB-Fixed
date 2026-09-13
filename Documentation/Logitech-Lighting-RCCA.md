# Logitech lighting recovery

This investigation concerns a G502 X PLUS paired through a Powerplay receiver
(`046D:C53A`). That connection uses `LogitechLightspeedController` and
`logitech_device`; it does not use the newer `LogitechHIDPP20Controller`.
The shared-handle crash repair is a separate correction and remains in place.

## Confirmed driver defects

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

## Correction in source

Before each explicit `0x8071` color update, query software control and RGB power.
Reacquire missing control, restore full RGB power if needed, and verify those
changes before painting. An already-held claim is left intact. Direct and Static
both use the correct `0x8071` preparation. Static colors use parameter `2`,
including black. Effect writes remain volatile and do not write onboard flash.

Hold the shared receiver mutex across the complete preparation and color
transaction. Use nonzero software IDs and match replies to the request; reject
HID++ errors, truncated replies, failed writes, and timeouts. Log failures and
return a signed failure result. Complete short and long replies are accepted;
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
failed before the parameter change. All 18 currently pass under Clang with
AddressSanitizer and UndefinedBehaviorSanitizer.

Full application builds and physical acceptance of the new executable are
required before a release is marked hardware-validated. Full Windows reboot,
sleep/resume, and future Logitech driver updates need separate physical tests.

## Protocol references

- [Logitech HID++ packet layout and software IDs](https://github.com/Logitech/cpg-docs/blob/master/hidpp20/README.rst).
- [RGB Effects function mapping](https://openlogi.org/hidpp/features/x8071-rgb-effects),
  from the OpenLogi protocol implementation documentation.
- [Solaar effect parameter definitions](https://github.com/pwr-Solaar/Solaar/blob/e7304c4c451cc9bb4f206a914844525e67856a28/lib/logitech_receiver/hidpp20.py):
  static color and its ramp parameter; `Default=0`, `No=2`.
- The imported newer OpenRGB driver's `SetZoneEffect()` uses the explicit static
  parameter too. It is a comparison implementation, not the active driver for
  this Powerplay connection.
