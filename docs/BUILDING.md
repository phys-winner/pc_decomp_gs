# Building version 1.0

The current build scripts target **Windows with PowerShell**, Python 3.11+,
JDK 24, Android SDK platform 37, build-tools 37.0.0 (36.1.0 fallback) and
Android NDK **27.2.12479018**. Platform 36 is a compile-time fallback;
the manifest still targets API 37. No Gradle download is required.

Install the Python dependencies:

```powershell
python -m pip install -r requirements.txt
```

## External dependencies

Get the three Git submodules at the revisions recorded in this repository,
then apply the port's pcrecomp and MojoShader fixes once in those clean checkouts:

```powershell
git submodule update --init
git -C reference/pcrecomp apply ../../patches/pcrecomp/port-fixes.patch
git -C reference/mojoshader apply ../../patches/mojoshader/port-fixes.patch
```

`dependencies.json` records URLs, revisions and patch paths. The pcrecomp
patch preserves the FSINCOS fix and regression; the MojoShader patch preserves
the port's shader adaptations. The submodules deliberately ignore working-tree
changes in the parent status because these patches are applied locally.
Before updating upstream revisions, review and revalidate the patches.

Download dlmalloc separately and check its pinned SHA-256 before installing it:

```powershell
New-Item -ItemType Directory -Force build, reference/dlmalloc | Out-Null
Invoke-WebRequest 'https://gee.cs.oswego.edu/pub/misc/malloc.c' -OutFile build/malloc.c
$expectedHash = 'be7be56b40b4b31417e74e77687f9b96127ea6cae534cf236aee40a4c739508d'
if ((Get-FileHash build/malloc.c -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
    throw 'dlmalloc SHA-256 mismatch'
}
Copy-Item -LiteralPath build/malloc.c -Destination reference/dlmalloc/malloc.c
```

The shared launcher has no public Git URL. Supply the same external source
base used by the sibling homm5/ut3 ports. It is located in this order:
`PORT_LAUNCHER`, `android/launcher`, then `../launcher`; each candidate must
contain `tools/launcher.py`. To select it explicitly:

```powershell
$env:PORT_LAUNCHER = 'E:\static_recompilation\launcher'
```

The DirectX-header submodule is only needed by the optional Windows effect
oracle. It is not compiled into the Android APK. The existing generated effect
descriptions in the host do not need to be regenerated for a normal build.

## Original inputs

Supply these files locally; they are not in the repository:

| Local path | Input |
| --- | --- |
| `decoded_original/griefsyndrome-jp.exe` | Original Japanese v1.00 EXE |
| `original/japanese/griefsyndrome.exe` | The same Japanese EXE, used to prepare the installation ZIP |
| `original/russian/gs00.dat`, `gs01.dat`, `gs02.dat`, `gs03.dat` | Matching game-data containers |
| `original/russian/gso.dll` | GSOnline input required by the native LAN build |
| `app/artwork/widescreen/TitleBG.original.act` | Original `data/system/Title/TitleBG.act`, needed by the optional panorama builder and its tests |

`config/inputs.json` pins the EXE and DAT hashes. The GSOnline SHA-256 is
`86afc9823ed85f5c2cdaf6f7252db6c536fa50d1316d1e29a61e20d5248df0f4`;
the lifter rejects a mismatch. GSOnline is a build input; the installation
folder used by players does not need the DLL.

The installation-ZIP builder currently expects all four DAT containers,
including the Russian-language pair, even when building for Japanese play.
It reads the common `gs00.dat`/`gs01.dat` from `original/russian/`.
The generated panorama builder runs when all seven PNGs are present and
also requires the local ACT input above. The repository includes those PNGs.

## Toolchain and signing

Set paths for the installed SDK, NDK and JDK, and your own signing material:

```powershell
$env:ANDROID_SDK = 'C:\Android\Sdk'
$env:ANDROID_NDK = 'C:\Android\Sdk\ndk\27.2.12479018'
$env:JAVA_HOME = 'C:\Java\jdk-24'
$env:APKSIGNER_JAR = 'C:\Android\Sdk\build-tools\37.0.0\lib\apksigner.jar'
$env:SIGNER_KEY = 'C:\private\release.pk8'
$env:SIGNER_CERT = 'C:\private\release.x509.pem'
$env:SIGNER_CERT_SHA256 = 'YOUR_CERTIFICATE_SHA256_WITHOUT_COLONS'
```

The key must be an unencrypted PKCS#8 file suitable for `apksigner --key`.
`SIGNER_CERT_SHA256` is the expected SHA-256 certificate fingerprint, checked
after signing. Keep private keys outside the checkout. Defaults in
`tools/android_tools.py` retain the original development-machine setup;
they do not provide signing material. An APK signed with a different key
cannot update an existing installation under Android's signature checks.

## Generate and build

Run from the repository root:

```powershell
python -X utf8 tools/lift_griefsyndrome.py
python -X utf8 tools/lift_gso.py
python -X utf8 tools/build_apk.py
```

The first two commands regenerate game C and the combined dispatch table.
The APK builder compiles both ABIs, prepares the external launcher, builds
the external installation ZIP, signs the APK and checks the packaged native
hashes, ABI set, certificate, alignment and absence of original game files.
Outputs are in `pub/`; build receipts are in `build/`.

Use `--skip-native` only with existing matching native libraries;
`--arm64-only` omits ARM32. These flags do not skip game-data preparation.

## Checks

```powershell
python tools/test_launcher_path.py
python ../launcher/tools/launcher.py check --spec app/port.json
python ../launcher/tools/icons.py check app/res
python tools/build_widescreen.py --selftest
```

The launcher commands above use the sibling layout; substitute the selected
launcher path if using `PORT_LAUNCHER` or `android/launcher`.
The panorama self-test needs the local ACT input. For native graphics/input
regressions on a specifically selected Android device with ADB access:

```powershell
python tools/test_touch_graphics.py DEVICE_SERIAL
```

That command compiles both ABIs, copies test executables to the device and
runs supported variants. Further LAN/TV tests are in `tools/test_lan_reconnect.py`
and `tools/test_tv_native.py`; inspect their arguments before running them.
