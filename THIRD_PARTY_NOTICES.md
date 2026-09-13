# Third-party notices

OpenRGB Fixed is derived from OpenRGB 1.0rc3.1. Original authorship and license
notices remain in the source files. OpenRGB is GPL-2.0-or-later; the license text
is in [LICENSE](LICENSE).

The Windows application dynamically links the following libraries. Their own
licenses continue to apply; they are not relicensed by this fork.

| Component | Version/source | License |
| --- | --- | --- |
| Qt | 5.15.2 in the Windows build; [source archives](https://download.qt.io/archive/qt/5.15/5.15.2/submodules/) | LGPL-3.0 or the applicable Qt open-source alternative; third-party code within Qt has its own notices |
| HIDAPI | 0.14.0 headers and vendored Windows library; [source](https://github.com/libusb/hidapi/tree/hidapi-0.14.0) | HIDAPI offers BSD, original HIDAPI, or GPL-3.0 licensing; retain its license notices |
| libusb | 1.0.27; [source](https://github.com/libusb/libusb/tree/v1.0.27) | LGPL-2.1-or-later |
| PawnIOLib | Vendored upstream OpenRGB client snapshot; [source](https://github.com/namazso/PawnIO) | LGPL-2.1-or-later |

The PawnIO kernel driver is installed separately and is not included in the
portable ZIP. The precompiled SMBus modules remain the upstream OpenRGB
distribution's modules; [PawnIO module source](https://github.com/namazso/PawnIO.Modules)
is maintained separately.

Other dependencies incorporated into the application, including nlohmann/json,
mbedTLS, and hueplusplus, retain the notices in `dependencies/` and their source
headers. The complete OpenRGB source snapshot is available from the release tag.

Qt runtime DLLs remain separate and replaceable. A Qt source archive and the Qt
license notices accompany binary releases. Build tools such as Microsoft Visual
C++ and jom are not included in the portable application package. The Microsoft
Visual C++ runtime is obtained separately from Microsoft.
