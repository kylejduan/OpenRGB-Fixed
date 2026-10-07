# OpenRGB diagnostics archive

Historical evidence from the September 2026 Logitech lighting investigations,
moved from a Windows troubleshooting folder on 2026-10-07.

- `2026-09-12-g502/`: initial G502 lighting, service, restart, and UI probes.
- `2026-09-13-g502-rcca/`: lighting ownership and hardware acceptance evidence.
- `2026-09-16-g502-blue/`: color drift, reconnect, and subsequent build validation.

## Public evidence and privacy

Text evidence is sanitized for an open source repository. Personal names,
Windows user paths, account SIDs, and host-specific device instance identifiers
are replaced with placeholders. Device instance placeholders retain consistent
relationships across records. Device models, VID/PID values, timestamps,
protocol traces, executable checksums, and upstream source attribution remain
available for diagnosis. Text was normalized to UTF-8; XML declarations match.
The seven investigation screenshots were visually reviewed for personal
information. Bundled upstream documentation images were retained.

`MIGRATION-MANIFEST.json` lists every imported file, its current SHA256 and size,
and whether it is eligible for Git. Verify hashes against the imported copies;
checksums embedded in historical records describe their original captures and
may differ from sanitized text. They do not represent new hardware testing.

## Local binary snapshots

Executable packages, libraries, driver binaries, and saved hardware profiles
(`.exe`, `.dll`, `.bin`, `.orp`, `.ors`) are preserved locally and explicitly
ignored by this directory's `.gitignore`. These 420 files are not public evidence
and were not certified free of embedded personal data. Do not force-add them.
They will not accompany a Git clone; use official releases for distributable
binaries. The remaining 669 imported files are eligible for Git.

## Historical scripts

These are archived investigation scripts, including installation, service,
process, and hardware-changing probes. Personal paths were replaced with
placeholders; `C:\OpenRGB-Diagnostics` is a generic historical path, not the
current location. Review and adapt paths and account placeholders before reuse.
They were not executed as part of this move.
