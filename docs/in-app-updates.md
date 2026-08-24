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
| Linux AppImage | `-{x86_64,aarch64}.AppImage` | self-replace over `$APPIMAGE` + relaunch (the one Linux format that self-updates) |
| Linux deb | `-Linux_{amd64,arm64}.deb` | hand to `xdg-open` |
| Linux AUR | `-Linux.pkg.tar.zst` | `pacman -U` note / AUR helper |
| Linux Flatpak / other | — | browser / Flathub (no self-update asset) |
| macOS | `-macOS-{Silicon,Intel}.dmg` | detached `/bin/sh` swaps the `.app`, reopens |
| Android | `-{arm64-v8a,armeabi-v7a}.apk` | `PlatformUtils.installApk` over JNI — see below |
| PS4 | `-ps4.pkg` | bundled updater helper (`VSWY00003`) installs it via BGFT — see below |

After the download every platform **hands off and closes on its own** — no
confirmation dialog of ours. Quitting is mandatory, not cosmetic: a package /
bundle / title manager can't replace the binary under a live process (dpkg,
the Vita promoter's `0x80101114` "in use", a running `.app`/`.exe`), so an app
that stays open leaves the old build running. Switch chain-loads the new NRO
via `envSetNextLoad` where hbloader supports it; Windows/macOS/AppImage relaunch
themselves; deb/AUR and PS4 need the user to reopen after the system installer
finishes. Android is the exception — the OS restarts the app itself.

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

## PS4 updater helper

Same shape as the Vita stub, for the same reason: BGFT installs into the locked
`/user/app/VSWY00002`, and uninstalling the running title kills this process. So
a second title (`VSWY00003`) ships **inside** our own pkg as `/app0/updater.pkg`
(`cmake/bundle_updater_pkg.cmake`) and does the work while we're closed.

At update time (`utils/ps4_install`):

1. download `/data/VitaSuwayomi/update.pkg`;
2. if the helper isn't installed, `installUpdaterApp()` copies the bundled pkg
   out and registers it with BGFT, then poll **`isUpdaterInstalled()`** — never
   probe by launching, since launching a not-yet-ready title pops `CE-40841-7`
   on every attempt;
3. `launchUpdater()` and exit with **`std::_Exit(0)`**, not `brls::quit()`: an
   orderly quit leaves HTTP threads running, and uninstalling a title whose
   process is still alive is reported as a crash (`CE-36329-3`).

The helper (`src/updater_ps4/main.cpp`) uninstalls `VSWY00002` and hands the pkg
to BGFT, which shows its own system progress. The main app removes the helper on
the next boot (`ps4::removeUpdaterApp()`) — a title that uninstalls its own
running self is killed mid-call.

## Android install permission

`PlatformUtils.installApk` streams the APK to the system installer through
`ApkProvider`, a minimal in-process ContentProvider (no AndroidX dependency)
that serves exactly one path — `file://` URIs throw `FileUriExposedException` on
API 24+ and the download sits in internal storage the installer can't read.

Two traps, both handled:

- **API 26+ needs a per-app grant.** `REQUEST_INSTALL_PACKAGES` in the manifest
  is necessary but *not* sufficient. Older Android auto-prompted when the intent
  launched; newer Android — Android TV especially — does not, so the installer
  just shows "Staging app… (Unknown)" and vanishes. `installApk` checks
  `canRequestPackageInstalls()` first and routes the user to
  `ACTION_MANAGE_UNKNOWN_APP_SOURCES` (falling back to the global
  unknown-sources and then security screens, which some TV builds need). It
  returns `false` so the app can say "grant it, then choose Update again".
- **Never self-kill after launching the installer.** It reads the APK from our
  in-process provider while staging; killing mid-stage tears the provider down
  and aborts the install. Android force-stops the package itself once the
  update commits.

## Testing

The platform branches are `#ifdef`-gated, so a desktop host build only exercises
the Linux path, and CI only proves each target **compiles and packages**. Verify
against a real published release — "builds green" is not "the swap works" —
especially the console paths: the Vita promote + relaunch, the PS4 BGFT hand-off,
and the Android TV permission flow.

To force an offer on every platform at once, lower the base version in the
`VERSION` file below the newest release; CI's `resolve-version` feeds it to both
the package version and the in-app one, so every build then reads as older.
