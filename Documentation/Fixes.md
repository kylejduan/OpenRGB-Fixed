# Fixes and validation

OpenRGB Fixed is an independent fork of upstream OpenRGB
`5e81e26fcc65d3dacfb76b0a30ec0142ec7bb131` (`release_candidate_1.0rc3.1`).
Original source: <https://github.com/CalcProgrammer1/OpenRGB/tree/5e81e26fcc65d3dacfb76b0a30ec0142ec7bb131>.
The upstream project is maintained at <https://gitlab.com/CalcProgrammer1/OpenRGB>.

## Shared Logitech receiver handles

A Powerplay receiver can expose both the mat and its paired mouse through the
same HID handles. The original controller objects each closed those shared
handles during destruction. Rescanning could therefore close a live sibling's
handle and later close it again. The observed Windows crash was heap corruption
(`0xc0000374`) in `hidapi!hid_close`.

The usage map now holds `std::shared_ptr<hid_device>` with a `hid_close` deleter.
The final owner closes each handle exactly once. Failed detection attempts are
destroyed promptly, and a device that succeeds on the final retry is accepted.
Powerplay and its paired mouse remain enabled.

## Controller worker shutdown

The RGB worker can still be using a hardware driver when a derived controller
destructor releases it. Stopping the worker in the base destructor happens too
late. Cleanup now joins every hardware controller worker before deleting any
controller or bus. It also serializes cleanup with the background work that
uses controllers after detection, including startup profile application.

## Evidence and limits

The standalone C++ tests compile the actual protocol and worker implementations:

| Test | Behavior checked |
| --- | --- |
| `logitech_receiver_lifetime` | Mat-first and mouse-first destruction, continued use by the survivor, failed initialization attempts, and exactly one close per handle |
| `device_worker_lifetime` | A blocked hardware transaction must finish before driver destruction; repeated worker stop is safe |

Both tests failed on the unpatched baseline at their intended assertions and
passed with the fixes under AddressSanitizer and UndefinedBehaviorSanitizer.
The worker test exercises `StopDeviceThread()` directly; it does not execute
the entire `ResourceManager::Cleanup()` or its background-task mutex.

The original Windows build was exercised with repeated real GUI rescans,
profile loads, and clean exits while Powerplay, a paired G502 X PLUS, Kingston
Fury DDR5, an ASUS Z790 board, and an RTX 4090 remained detected. These are
results from one hardware setup, not coverage of every upstream device.
CI validates compilation and the standalone tests without physical RGB devices.

## G502 lighting recovery

On 2026-09-12, a G502 X PLUS was detected but ignored color commands. A rescan
and profile reload after stopping Logitech's LampArray service restored visible
red. Restarting that service and applying blue also worked. This does not
establish that the service caused the original failure, and it does not justify
disabling that service for every user.

The subsequent [lighting RCCA](Logitech-Lighting-RCCA.md) reproduced failure after
a software-control handoff and recovery with an explicit static parameter,
without a rescan or restart. The legacy driver now checks control and power
before explicit updates, uses the correct feature-generation commands, and
validates replies. These changes are separate from the lifetime fixes.

Physical reboot, sleep/resume, and future driver-update behavior were not part of
that validation; see the reconnect recovery below. See
[Troubleshooting](Troubleshooting-Fixed.md) if a detected device ignores color
commands. Protocol replies and an updated preview alone do not prove the LEDs
changed.

## Lightspeed reconnect recovery

On 2026-09-16, after several reboots, the G502 X PLUS showed its onboard blue
although OpenRGB had applied the saved magenta profile without errors. A direct
HID++ read showed RGB Effects software control `0` with event flags `6`.
OpenRGB claims control as `3` with flags `5`, so another host had taken the
device back to firmware lighting. Logitech's LampArray service contains a HID++
`0x8071` client with software-control, user-activity, and device-connect
handlers, which makes it the likely writer. The same `0/6` state had appeared on
2026-09-13 immediately after the mouse woke. The fixed.2 driver only reclaimed
control inside an explicit update, and nothing requested one. A mouse that was
asleep during detection was also never registered.

Each legacy Lightspeed receiver now gets one watcher thread:

- It waits in a 250 ms read on the receiver's short-report interface for device
  connection notifications, which identify the slot and whether its link is up.
- For each linked `0x8071` device it reads software control 2 s and 10 s after a
  link comes up and every 30 s otherwise: one 20-byte request with a one-second
  reply window. It sends nothing while the receiver reports the link down, and
  backs off to 120 s when a device does not answer. Devices without `0x8071`, such as the Powerplay mat, are not
  polled.
- When control is lost, it requests the controller's normal mode update. The
  existing driver path reclaims control, restores RGB power with its settle
  interval, and repaints. A 5 s quiet window prevents repeated requests.
- When a slot that failed detection links up, was already linked when detection
  failed, or links up without having been enumerated at all, it retries creation
  0.5 s, 3 s, 10 s, 60 s and 5 min later.
  A successful attempt registers the controller, applies the last loaded profile
  to it, and updates it.

`ResourceManager::Cleanup()` stops watchers before any controller or handle is
released. Late creation and re-apply wait until detection, startup profile
application, and cleanup are idle, and give up if the watcher is stopping.
Device initialization now holds the receiver mutex so a late device cannot
interleave with its sibling's lighting transaction.

The first fixed.3 installation exposed an older detection fault. Every
initialization query wrote a request and accepted the next report within 300 ms.
After a wake, replies had been measured at about 487 ms, so one late reply shifted
every later answer and the attempt failed validation. In that start all ten
attempts failed over 64 s, although the mouse answered normally minutes later;
fixed.2 logs show the same fault as occasional 14 s and 35 s registrations. The
queries now use the matched request path with a one-second deadline and abandon
an attempt on a missing reply.

A Windows restart at 21:36 the same evening exposed the same fault one step
earlier. Receiver enumeration also read a fixed number of reports: at boot the
receiver acknowledged the reconnect request first and announced both devices
after the 300 ms reads. Detection invented slot 0, retried it for 21 s, and
registered neither the mouse nor the Powerplay mat, leaving the mouse in firmware
blue. Register reads now wait for their own replies, and announcements are
collected until every paired device has reported or 3 s pass. The watcher starts
even when enumeration finds nothing, so late announcements still register devices.

| Test | Behavior checked |
| --- | --- |
| `logitech_lighting_control_read` | One GET, no claim; `-1` on no reply within the deadline or write failure; `-2` and no I/O without `0x8071` |
| `logitech_lighting_init_locked` | A new device sends nothing while a sibling holds the receiver mutex |
| `logitech_lighting_init_slow` | Name, feature and LED queries stay paired with their replies when every reply takes 450 ms |
| `background_worker` | Idle wait runs work under a free mutex and abandons the wait on stop while the mutex stays held |
| `logitech_enumeration_*` | Both paired devices found when announcements precede or follow the acknowledgement by 800 ms, with a sleeping mouse, with a stale report queued before a register reply, and when notifications must be enabled |
| `logitech_watcher_*` | Late creation after link up, when linked at detection, or for a slot enumeration missed, with retries; no polls while the link is down; re-apply after link up and on periodic loss; quiet window; no-reply backoff; unsupported devices ignored; malformed reports ignored; prompt stop |

Deliberately breaking link-down gating, the quiet window, backoff, unsupported
device handling, post-link checks, link-state parsing, creation retries, the
linked-slot schedule, or name reply matching made the matching test fail. The suite also passes under ThreadSanitizer. Physical acceptance of the installed build is recorded in the
[lighting RCCA](Logitech-Lighting-RCCA.md#reconnect-and-takeover-follow-up-2026-09-16).

## Provenance

- Imported upstream source archive SHA256:
  `83fbf2c628508733ec3b73877e5bdb34f904b2135645763b1ca7c61d92e87e5d`.
- Recovered original local repair patch SHA256:
  `6987fac148d1edc4a5c10def7a1c9f3533def997784ba0a6edc957062a473262`.
- Public builds retain the same lifetime corrections and report the fork's
  actual Git revision instead of the old local build's fixed revision string.

The full source and original authors' notices are preserved under
[GPL-2.0-or-later](../LICENSE). Dependencies retain their own licenses.
