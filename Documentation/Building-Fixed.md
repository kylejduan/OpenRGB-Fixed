# Building OpenRGB Fixed

Clone this fork, including its bundled upstream dependencies:

```sh
git clone https://github.com/kylejduan/OpenRGB-Fixed.git
cd OpenRGB-Fixed
```

## Regression tests: no RGB hardware or Qt required

Install a C++17 compiler, CMake 3.16 or newer, and a build tool. Then run:

```sh
cmake -S tests -B build/regression -DCMAKE_BUILD_TYPE=Debug
cmake --build build/regression --config Debug --parallel
ctest --test-dir build/regression -C Debug --output-on-failure
```

With GCC or Clang on Linux, add `-DOPENRGB_FIXED_SANITIZERS=ON` when configuring
to enable AddressSanitizer and UndefinedBehaviorSanitizer. Tests keep assertions
enabled even when built in Release mode.

## Windows x64

The release workflow uses Visual Studio 2022, Qt 5.15.2 for MSVC 2019 x64, and
jom 1.1.4. The repository supplies the HIDAPI, libusb, and PawnIO client files.
Install Visual Studio's C++ desktop build tools and the matching Qt package;
open a Developer PowerShell with `cl.exe`, `qmake.exe`, and `windeployqt.exe`
available on PATH.

```powershell
.\scripts\package-windows.ps1
```

The script downloads a hash-verified jom binary from Qt, builds OpenRGB, deploys
the Qt runtime libraries, and produces `build/dist/OpenRGB-Fixed-Windows-x64.zip`
with a SHA256 file and source revision manifest. It does not install drivers,
change services, or create a startup task.

## Linux: Ubuntu 24.04

```sh
sudo apt-get update
sudo apt-get install build-essential qtbase5-dev qt5-qmake qttools5-dev-tools \
  libusb-1.0-0-dev libhidapi-dev libmbedtls-dev pkgconf
mkdir -p build/application
cd build/application
qmake ../../OpenRGB.pro CONFIG-=debug_and_release CONFIG+=release
make -j2
./openrgb --version
```

Follow the upstream [USB access](USBAccess.md), [udev rules](UdevRules.md), and
[SMBus access](SMBusAccess.md) instructions for physical hardware access.
The Linux CI build is a build check, not a portable AppImage.

## Other platforms

The full upstream platform support is retained. See
[the upstream build instructions](Compiling.md) for macOS, FreeBSD, and other
Linux distributions, substituting this fork's clone URL. This fork's release
CI covers Windows x64 and Ubuntu x64; other targets have not been validated.

## Updating the fork

Keep upstream imports separate from local corrections. When changing the
upstream base, review both lifetime fixes, run the regression suite and full
builds, and repeat real rescans, profile changes, and clean exits with shared
receiver devices enabled. Preserve a working release for rollback. New upstream
device support is not automatically included in this pinned fork.
