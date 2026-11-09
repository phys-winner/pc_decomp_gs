# Grief Syndrome for Android

Static recompilation of Grief Syndrome for Android, with ARM64/ARM32 support, touch controls, widescreen rendering and LAN co-op.

**Version: 1.0** · [Download](https://github.com/phys-winner/pc_decomp_gs/releases/tag/v1.0) · [Release notes](CHANGELOG.md) · [Build instructions](docs/BUILDING.md)

An unofficial Android port of Grief Syndrome. The original Windows PE32
executable is statically recompiled from x86 to C, then built for ARM.
Original game files must be installed separately.

## Features

- Android 7.0+ (API 24), ARM64 and ARM32 with NEON; OpenGL ES 3.0+.
- Touch controls, a layout editor, touch menus and character selection.
- Widescreen gameplay, adapted UI and optional panoramic backgrounds.
- LAN co-op for two or three devices, with room discovery and a lobby.
- Japanese game data; Russian language support with the corresponding archives.
- Graphics, anti-aliasing and FPS-limit settings; persistent saves stored separately.

## Device requirements

- **Android 7.0 or later**.
- **ARM64**, or **ARMv7 with NEON** on a system that supports 32-bit apps.
- **OpenGL ES 3.0 or later**.
- At least **1.5 GiB of free storage** to install the full package.
- For LAN co-op, devices connected to the same **Wi-Fi network**.

## Downloads

| File | Contents |
| --- | --- |
| [grief-syndrome-1.0.apk](https://github.com/phys-winner/pc_decomp_gs/releases/download/v1.0/grief-syndrome-1.0.apk) | Android app with ARM64 and ARM32 native libraries |
| [grief-syndrome-widescreen-data.zip](https://github.com/phys-winner/pc_decomp_gs/releases/download/v1.0/grief-syndrome-widescreen-data.zip) | Optional panoramic backgrounds: `gs_widescreen.dat` |

The widescreen ZIP contains additional backgrounds only. It does not include
the original game executable or the required game-data archives.

## Installation

1. Download and install `grief-syndrome-1.0.apk` from the release above.
2. Supply your own game files: the Japanese **v1.00** `griefsyndrome.exe`,
   `gs00.dat` and `gs01.dat`. Supported SHA-256 hashes are listed in [config/inputs.json](config/inputs.json).
3. Grant file access in the launcher. In **Game files**, select the game folder
   or import a ZIP containing those files.
4. For Russian language support, add `gs02.dat` and `gs03.dat`.
5. For panoramic backgrounds, extract `gs_widescreen.dat` from the optional
   widescreen ZIP into the selected game folder. Fully stop the game before
   replacing data files, then launch it again.
6. Tap **Play**.

The default game-data folder is `/sdcard/GriefSyndrome/game`; saves and settings
are stored in `/sdcard/GriefSyndrome/userdata`. For LAN, connect the devices to
the same Wi-Fi network: one player creates a room, the others join, then the
host starts the game.

Port version `1.0` and original EXE version `1.00` refer to different components.
Original EXE/DLL/DAT files, APK builds, signing keys and local logs are excluded
from the source repository. The APK and optional widescreen ZIP are distributed
as release attachments.

## Source layout

| Directory | Purpose |
| --- | --- |
| `app/src/` | Android UI, settings, touch controls and LAN |
| `app/native/host/` | Win32/D3D9/audio/input host and game adaptations |
| `app/native/runtime/` | Recompilation runtime |
| `app/native/scripts/` | Adapted game Squirrel scripts |
| `app/artwork/widescreen/` | Additional panoramas and preparation records |
| `config/` | Input checksums and jump tables |
| `tools/` | C generation, APK building and regression checks |
| `reference/` | External Git submodules and separately downloaded dlmalloc |

`generated/`, `original/`, `decoded_original/`, `build/`, `work/` and `pub/`
are created or populated locally and excluded from the source repository.
The shared launcher is an external dependency selected through `PORT_LAUNCHER`,
`android/launcher` or the sibling `../launcher` directory. See the
[build instructions](docs/BUILDING.md#external-dependencies) for dependency
setup and local patches.

## Status and limitations

Development checks covered startup and gameplay scenarios on ARM64/ARM32,
touch controls and LAN. A complete playthrough of every level and compatibility
with every GPU have not been verified. ELF libraries are built with 16 KiB
alignment; execution on a device with actual 16 KiB memory pages still needs
separate testing. Sustained 60 FPS on every device is not guaranteed.

## Licensing

Project-authored port code is licensed under **GPL-3.0-or-later**; see
[LICENSE](LICENSE). Third-party components retain their own terms:
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
The project license does not grant rights to the original game, its resources
or translations of the original machine code.
