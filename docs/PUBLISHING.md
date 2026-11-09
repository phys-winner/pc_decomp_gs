# Publication metadata

- Repository URL: https://github.com/phys-winner/pc_decomp_gs
- Version: `1.0`
- Tag: `v1.0`
- GitHub description:

  Static recompilation of Grief Syndrome for Android with ARM64/ARM32, touch controls, widescreen and LAN co-op.

The local publication branch is `codex/release-1.0`. Its history starts with
the signed public source snapshot. Git submodules are pinned to external
upstream commits; dependency source trees are not stored in this repository.
The shared launcher is supplied as an external local directory.
`master` and `codex/local-before-publication`
retain private development history, including original game files.
Publish only the release branch and its tag; do not use `--all` or `--mirror`.

The remote URL and GitHub link in `app/port.json` point to this repository.
Use the description above for GitHub's About field. The public source branch
maps to remote `main`; the release uses the signed `v1.0` tag.

Original EXE/DLL/DAT, data ZIPs, signing keys, device logs, generated game C
and private history are excluded from this branch. Local copies remain intact.
APK and game-data ZIP publication are separate from source publication.

The `v1.0` release contains `grief-syndrome-1.0.apk` and
`grief-syndrome-widescreen-data.zip`. The latter contains only the optional
`gs_widescreen.dat`; original EXE/DAT game inputs are not release attachments.
These are the existing prebuilt version 1.0 artifacts, not a new APK build
from the publication commit. The APK's embedded development commit is `9826013a`.

The public commit and `v1.0` tag use Eugene's configured Git identity and
GPG key `5E9A5E5A9E8678AB`. Verify locally with `git verify-commit HEAD`
and `git verify-tag v1.0`.
