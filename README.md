# OpenRGB Fixed

[![Build and tests](https://github.com/kylejduan/OpenRGB-Fixed/actions/workflows/build.yml/badge.svg)](https://github.com/kylejduan/OpenRGB-Fixed/actions/workflows/build.yml)

An independent [OpenRGB](https://openrgb.org/) fork that fixes shared Logitech
receiver handle ownership, controller shutdown races, and lighting-control
recovery. It keeps Powerplay and its paired mouse enabled during device rescans.

Based on OpenRGB **1.0rc3.1**, commit `5e81e26`. This is the complete application
source, including the upstream device controllers, GUI, profiles, and SDK.
It is not an official OpenRGB release.

## Download

Get the **Windows x64 portable ZIP** and its SHA256 checksum from
[Releases](https://github.com/kylejduan/OpenRGB-Fixed/releases).
Extract the whole ZIP into a dedicated folder and run `OpenRGB.exe`.
The [Microsoft Visual C++ x64 runtime](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)
is required. RAM/SMBus access also requires PawnIO and an administrator launch.

Linux and other platforms can [build from source](Documentation/Building-Fixed.md).
CI builds the Windows application and Ubuntu application, and runs the protocol and lifetime
regression tests. Hardware compatibility still depends on upstream device support.

## What is fixed

- **Shared Powerplay/mouse handles:** the final controller owner closes each
  receiver HID handle exactly once, preventing the rescan double-close.
- **Failed detection retries:** failed controller objects are released, and
  success on the last permitted attempt is accepted.
- **Shutdown ordering:** active controller workers finish before their hardware
  drivers and buses are destroyed. Cleanup also waits for background controller
  work such as startup profile application.
- **Legacy Logitech lighting recovery:** explicit color updates reacquire lost
  `0x8071` software control and RGB power, use an explicit static-color parameter,
  allow the RGB engine to settle after power recovery, and validate receiver
  replies. See the [lighting RCCA](Documentation/Logitech-Lighting-RCCA.md).

Read the [root-cause analysis, validation, and provenance](Documentation/Fixes.md).

## Known limits

Lighting recovery runs on the next color or profile update. There is no new
background reconnect or idle/wake monitor. Another application continuously
writing to the same device can still interfere. The historical trigger for the
original loss of control remains unconfirmed.

Full Windows reboot, sleep/hibernation/resume, receiver reconnection, and future
driver-update behavior need physical testing on your hardware. The application
does not disable Logitech services or Windows Dynamic Lighting for you.

## Setup, startup, and updates

See [Troubleshooting and persistence](Documentation/Troubleshooting-Fixed.md)
for missing RAM, ignored colors, elevated startup, and rollback. Save your own
profile and use only one startup entry. To request saved colors after Windows
wakes, enable **Set Profile on Resume** using the
[resume setup instructions](Documentation/Troubleshooting-Fixed.md#sleep-hibernation-and-resume).
That setting is separate from login startup and is disabled by default.

This portable fork does not automatically replace its executable; updates are
intentional downloads from this repository.

## Build and test

[Build instructions](Documentation/Building-Fixed.md) cover the complete app.
The regression tests need only a C++17 compiler and CMake:

```sh
git clone https://github.com/kylejduan/OpenRGB-Fixed.git
cd OpenRGB-Fixed
cmake -S tests -B build/regression -DCMAKE_BUILD_TYPE=Debug
cmake --build build/regression --config Debug --parallel
ctest --test-dir build/regression -C Debug --output-on-failure
```

## Contributing and license

Report fork-specific bugs with the
[bug-report template](https://github.com/kylejduan/OpenRGB-Fixed/issues/new?template=bug_report.md)
and submit [pull requests](https://github.com/kylejduan/OpenRGB-Fixed/pulls).
Include the build revision, OS, device model, connection type, reproduction
steps, and relevant log excerpts. Remove personal device paths and serials
before posting logs. See [Contributing](CONTRIBUTING.md).

OpenRGB Fixed preserves OpenRGB's **GPL-2.0-or-later** licensing and original
copyright notices. See [LICENSE](LICENSE) and
[third-party notices](THIRD_PARTY_NOTICES.md). OpenRGB and its device support are
the work of [the upstream project and contributors](README.upstream.md); this
fork carries focused receiver and lighting repairs and its own release tooling.
