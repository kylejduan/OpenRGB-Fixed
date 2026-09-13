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

If the preview changes but the mouse does not, rescan devices, reload your
profile, and try again. Check whether another RGB application or Windows Dynamic
Lighting controls that device. Avoid assigning the same device to multiple RGB
applications. A controlled stop/retest can help diagnose a conflict; restore
services if that experiment does not establish a cause. This fork does not
automatically disable Logitech services or Windows lighting settings.

The [G502 investigation](Fixes.md#g502-lighting-ownership-remains-a-separate-issue)
records what was observed and what remains unproven.

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
Sleep/resume and receiver reconnects require separate physical testing.

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
