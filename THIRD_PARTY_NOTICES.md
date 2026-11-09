# Third-party notices

Project-authored port code and documentation are GPL-3.0-or-later; see
[LICENSE](LICENSE). Components and upstream-derived portions retain their
original terms. This file does not grant rights to original game code or data.

## External source dependencies

| Component | Version or provenance | License and retained notice |
| --- | --- | --- |
| pcrecomp | `904ac3129d0c9d05efbeec19a1dce6abbe7f5d26` | MIT; [LICENSE](reference/pcrecomp/LICENSE), copyright 2026 sp00nz |
| MojoShader | Git submodule at `cf2661bcb55d9a8732e86f79922592d7efdf5383`, with a separate port patch | Zlib; [LICENSE.txt](reference/mojoshader/LICENSE.txt), copyright 2008-2024 Ryan C. Gordon |
| dlmalloc | 2.8.6, relicensed in 2023 | MIT-0; full notice at the top of [malloc.c](reference/dlmalloc/malloc.c), copyright 2023 Doug Lea |
| MinGW-w64 DirectX headers | Git submodule at `9df86f2341616ef1888ae59919feaa6d4fad693d`, used by the optional Windows effect oracle | LGPL-2.1-or-later and individual file notices; [README](reference/dxvk/include/native/directx/README.md), [COPYING](reference/dxvk/include/native/directx/COPYING.MinGW-w64.txt) |
| AMD FidelityFX Super Resolution 1.0 | GLSL ES adaptation of EASU/RCAS, upstream `v1.20210629` | MIT; full AMD notice and adaptation details in [fsr1.h](app/native/host/fsr1.h), copyright 2021 Advanced Micro Devices, Inc. |
| NFSU2 x87 window algorithm | Adapted from patch 0014 by sistemas3cecasem | MIT-derived runtime algorithm; [original patch and attribution](patches/pcrecomp/README.md) |

The pcrecomp MIT notice also applies to derived portions of the lifter in
`tools/` and runtime in `app/native/runtime/`. Local adaptations of the host
and lifter were reused from this project's existing The Void and homm5 ports;
those external checkouts are not needed to build this repository.

MojoShader's local extensions include legacy shader instructions and effect
handling. These altered sources must not be represented as an unmodified
upstream release. Source notices remain in the external source trees.

The shared Android launcher is an external project dependency selected through
`PORT_LAUNCHER`, `android/launcher` or `../launcher`, following the sibling ports.
Its provenance and reference source digest are recorded in
[dependencies.json](dependencies.json). No launcher source is vendored here.

Initialize the submodules and supply dlmalloc as described in
[docs/BUILDING.md](docs/BUILDING.md#external-dependencies). The external license
links above resolve after dependency setup. Port changes are retained in
`patches/pcrecomp/port-fixes.patch` and `patches/mojoshader/port-fixes.patch`.

## Development tools

Python packages are installed separately using [requirements.txt](requirements.txt):
capstone (BSD-3-Clause), pefile (MIT), and Pillow (HPND). They are not bundled
into the Android APK. Android SDK/NDK, JDK and Microsoft DirectX development
tools are separately installed tools with their own terms. Android system
libraries are provided by the device. No DXVK, Turnip or adrenotools binaries
are included in this port's APK build.

## Original game inputs and derived material

Grief Syndrome, its original EXE and DAT containers, GSOnline, and original
artwork and scripts remain subject to their respective rights holders' terms.
Original binary inputs and resource archives are excluded from the public
source branch, as are generated translations of the EXE and GSOnline module.
Game-specific script adaptations and effect descriptions do not acquire
rights to the underlying game through this project's GPL grant.

Additional panorama source images and their preparation records are in
`app/artwork/widescreen/`. The original ACT input used to build the optional
panorama archive must be supplied locally; it is excluded from Git.
