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

Physical reboot, sleep/resume, and future driver-update behavior have not been
validated. See [Troubleshooting](Troubleshooting-Fixed.md) if a detected device
ignores color commands. Protocol replies and an updated preview alone do not
prove the LEDs changed.

## Provenance

- Imported upstream source archive SHA256:
  `83fbf2c628508733ec3b73877e5bdb34f904b2135645763b1ca7c61d92e87e5d`.
- Recovered original local repair patch SHA256:
  `6987fac148d1edc4a5c10def7a1c9f3533def997784ba0a6edc957062a473262`.
- Public builds retain the same lifetime corrections and report the fork's
  actual Git revision instead of the old local build's fixed revision string.

The full source and original authors' notices are preserved under
[GPL-2.0-or-later](../LICENSE). Dependencies retain their own licenses.
