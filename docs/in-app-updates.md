# In-app updates

VitaSuwayomi checks GitHub releases for a newer build and installs it in place
(per platform), or opens the release page in a browser when in-place install
isn't possible.

## Public surface (`utils/app_update.hpp`)

```cpp
namespace app_update {
    void setSelfPath(const char* argv0);  // call in main() before borealis init
    void checkForUpdates(bool manual);     // false = silent startup, true = Settings cell
}
```

Wired in `src/main.cpp` (`setSelfPath` + silent `checkForUpdates(false)` at boot)
and the Settings → "Check for updates" cell (`checkForUpdates(true)`).

## Flow

1. On a worker thread, GET `https://api.github.com/repos/Breezyslasher/Vita_Suwayomi/releases?per_page=10`.
2. Parse the newest non-draft release (string-aware JSON helpers, no library).
   `isNewer()` compares the first three numeric runs of the tag and ignores the
   build number.
3. Match one asset by exact filename tail (`assetSuffix()`), matching the CI
   release naming in `.github/workflows/build-multi-platform.yml`.
4. Offer dialog → download with progress + cancel + one retry → per-platform
   install → relaunch/quit.

`AppSettings::skippedUpdateVersion` lets the silent check stay quiet on a
release the user dismissed with "Skip"; the manual check always shows it.

## Per-platform install

| Platform | Asset | Mechanism |
|---|---|---|
| PS Vita | `.vpk` | AutoPlugin2 stub (`VSWYUPD01`, bundled as `app0:updater.vpk`) promotes it — see below |
| Switch | `-nx-{opengl,deko3d}.nro` | unmount romfs, `rename` over the running NRO |
| Windows | `-windows-{x64,x86}.zip` | detached CRLF `.bat` unpacks over the install dir, relaunches |
| Linux deb | `-Linux_{amd64,arm64}.deb` | hand to `xdg-open` |
| Linux AUR | `-Linux.pkg.tar.zst` | `pacman -U` note / AUR helper |
| Linux Flatpak / other | — | browser / Flathub (no self-update asset) |
| macOS | `-macOS-{Silicon,Intel}.dmg` | detached `/bin/sh` swaps the `.app`, reopens |
| Android | `-{arm64-v8a,armeabi-v7a}.apk` | JNI `PlatformUtils.installApk` (system installer) |

## Vita AutoPlugin2 stub

A title can't promote over itself (`ScePromoterUtility` returns `0x80101114`
"in use"). So at update time VitaSuwayomi:

1. downloads `ux0:data/VitaSuwayomi/update.vpk` and writes `update_version.txt`;
2. installs the bundled stub (`app0:updater.vpk`, title `VSWYUPD01` — a *different*
   title, so promoting it is fine) via `vita::installVpk`;
3. launches the stub (`vita::launchTitle`) and quits.

With VitaSuwayomi closed, the stub (`src/updater_stub/main.cpp`) promotes
`update.vpk` using the shared `utils/vita_install` code (miniz unzip + forged
`sce_sys/package/head.bin` + `ScePromoterUtility`, the VitaShell technique) and
relaunches VitaSuwayomi (`VSWY00001`). The stub is a headless vita2d progress
screen, free of borealis/fmt so it links only the install code.

`vita_install` / `vita_head_bin.h` are ported from the VitaPlex updater (same
AutoPlugin2 lineage); the `head.bin` template originates from VitaShell.

## Testing

The platform branches are `#ifdef`-gated, so a desktop host build only exercises
the Linux path. Verify each target against a real published release — "builds
green" is not "the swap works", especially the Vita promote + relaunch.
