# Troubleshooting and persistence

## Windows setup

Extract the release into a dedicated folder and run `OpenRGB.exe`. Install the
[Microsoft Visual C++ x64 runtime](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)
if it is missing. This is a portable application; keep its DLLs and `.bin` files
beside the executable.

Save a profile after setting up your own devices. Existing OpenRGB installations
normally share `%APPDATA%\OpenRGB`, so back up that directory before trying a
different build and run only one OpenRGB instance at a time.

## RAM is missing

RAM detection uses SMBus access and is separate from the shared-HID crash.
Install the upstream-supported [PawnIO driver](https://pawnio.eu/) and launch
OpenRGB as administrator. Check the log for `Permission Denied` or PawnIO
initialization errors. See [SMBus access](SMBusAccess.md).

## Detected mouse ignores colors

Select the mouse's whole lighting zone, choose Direct or Static mode, and apply
a clearly different color. Confirm the physical LEDs change.

On builds containing the lighting recovery fix, applying a color or loading a
profile reacquires lost legacy `0x8071` control. When the driver restores RGB
power, it allows a one-second settling interval and checks state again before
painting. Wake a sleeping mouse and apply the color again. If it still ignores
the change, check the log for a rejected or
timed-out lighting transaction, then rescan and reload your profile.

Check whether another RGB application or Windows Dynamic
Lighting controls that device. Avoid assigning the same device to multiple RGB
applications. A controlled stop/retest can help diagnose a conflict; restore
services if that experiment does not establish a cause. This fork does not
automatically disable Logitech services or Windows lighting settings.

The [G502 investigation](Logitech-Lighting-RCCA.md)
records what was observed and what remains unproven.

## Lightspeed reconnect watcher

Devices behind a legacy Lightspeed or Powerplay receiver are watched in the
background. When one returns to its onboard colors after sleep, wake, reboot, or
another program's takeover, OpenRGB repaints it within about 30 seconds, or a
few seconds after it reconnects. The log records each repaint as
`[Lightspeed watcher] ... lost software lighting control`. A warning saying the
device keeps losing control means another program is actively driving it.

Settings live in `OpenRGB.json` in the OpenRGB configuration folder. Close
OpenRGB before editing the file:

```json
"LogitechLightspeed": {
    "reconnect_watcher": true,
    "ownership_poll_seconds": 30
}
```

`ownership_poll_seconds` accepts 5 to 600. Set `reconnect_watcher` to `false`
to turn the watcher off. Both are read during device detection.

Logitech's LampArray service can take lighting back from OpenRGB. If you do not
use Windows Dynamic Lighting or G HUB lighting, setting that service to Manual
removes the contention; the watcher works either way. To restore it, run in an
administrator PowerShell:

```powershell
Set-Service logi_lamparray_service -StartupType Automatic
Start-Service logi_lamparray_service
```

## Login and restart persistence

For devices requiring administrator access, use Windows Task Scheduler to start
the chosen executable at your user logon, with **Run only when user is logged
on** and **Run with highest privileges**. Set its working directory to the
executable's folder and arguments to:

```text
--startminimized --profile "YourProfileName"
```

Replace the profile name with one you saved. Use a single startup entry; if a
scheduled task owns startup, turn off the application's separate startup option.
Verify that the saved profile is physically applied at the next login.

## Sleep, hibernation, and resume

The login task does not run again merely because Windows wakes from sleep.
OpenRGB has a separate **Set Profile on Resume** option, disabled by default:

1. Save a profile with the colors you want restored.
2. In OpenRGB's Settings page, enable **Set Profile on Resume** and choose that
   saved profile in the adjacent list.
3. Restart OpenRGB and confirm the option and profile selection remain set.
4. When convenient, test an actual sleep/wake cycle and check the physical LEDs.

On a Windows resume notification, the existing handler requests that profile.
The repaired Logitech driver then prepares lighting control and RGB power as
part of its color updates. This option does not add an automatic device rescan
or retry loop if a receiver is unavailable when the notification arrives.
Lightspeed and Powerplay devices do not need this option to recover from their
own sleep; the reconnect watcher repaints them. If colors are still wrong after
wake, wake the mouse and reload the profile. If a device is still missing or
unresponsive, rescan and reload the profile.

In the recorded fixed.2 setup, enabling a saved resume profile persisted through
an application restart. Actual Windows sleep/resume, hibernation, full reboot,
and receiver reconnection were not physically tested. The RGB power-off recovery
experiment is a separate test and does not establish OS sleep compatibility.

## Updates and rollback

This portable fork has no automatic binary updater. Download a new release and
replace its folder intentionally. An installer or package manager can overwrite
a copy placed inside its managed OpenRGB installation directory. A dedicated
folder avoids that overlap; it does not prevent a user or another program from
replacing files explicitly.

The original OpenRGB WinGet package does not update this fork as a separate
product. If you deliberately install the fork over a WinGet-managed copy, a
blocking pin can stop routine WinGet upgrades, but manual installers and forced
updates still need care. Back up the executable folder and profiles before
upgrading, check the release checksum, and repeat the physical device tests.
