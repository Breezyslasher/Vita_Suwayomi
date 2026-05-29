# VitaSuwayomi

A native Suwayomi client for PlayStation Vita. Read manga from your Suwayomi server directly on your Vita with full controller and touch support.

> **Note:** Check the [Issues](../../issues) page for current status of app features and known bugs.

## Features

**Library Management**
- Browse your manga library with category tabs
- Multiple display modes: Grid, Compact Grid, List
- Sort by title, unread count, recently added, and more
- Batch operations for managing multiple manga

**Reader**
- Multiple reading modes: RTL, LTR, Vertical, Webtoon
- Touch gestures: swipe, pinch-to-zoom, double-tap
- Controller navigation with D-pad and shoulder buttons
- Auto-detect webtoon/manhwa for optimal settings
- Color filters and brightness adjustment
- Per-manga settings that sync with Suwayomi server

**Browse & Search**
- Browse sources for new manga
- Search within sources or globally
- Filter sources by language

**Downloads**
- Download chapters to server or locally
- Offline reading support
- Download queue management

**Tracking**
- MyAnimeList integration
- AniList integration
- Automatic progress sync

**Other**
- Reading history with quick resume
- Extension management
- Dual URL support (LAN/WAN) with auto-failover
- Android TV support with D-pad navigation
- Deep links for opening manga and chapters directly

## Deep Links (Android)

VitaSuwayomi supports deep links on Android to open manga or chapters directly from other apps, browsers, or automation tools.

**URL Scheme:** `vitasuwayomi://`

| Link | Action |
|------|--------|
| `vitasuwayomi://manga/{id}` | Open manga detail page |
| `vitasuwayomi://chapter/{mangaId}/{chapterId}` | Open chapter in reader |

**Examples:**
```
vitasuwayomi://manga/42
vitasuwayomi://chapter/42/157
```

**Usage from ADB:**
```bash
adb shell am start -a android.intent.action.VIEW -d "vitasuwayomi://manga/42"
```

The manga and chapter IDs match the Suwayomi server's internal IDs (visible in the server's API or web UI URLs).
## Mobile
 Platform | Status |
|:--|:--:|
| Android | ✅ [Supported](https://github.com/Breezyslasher/Vita_Suwayomi/issues/291) |
| PS Vita | ✅ [Supported](https://github.com/Breezyslasher/Vita_Suwayomi/issues/128) |

## Consoles
| Platform | Status |
|:--|:--:|
| PS4 | ✅ [Supported](https://github.com/Breezyslasher/Vita_Suwayomi/issues/284) |
| Android TV | ✅ [Supported](https://github.com/Breezyslasher/Vita_Suwayomi/issues/292) |
| Nintendo Switch | ✅ [Supported](https://github.com/Breezyslasher/Vita_Suwayomi/issues/290) |

## Desktop
| Platform | Status |
|:--|:--:|
| Windows | ✅ [Supported](https://github.com/Breezyslasher/Vita_Suwayomi/issues/283) |
| macOS | ❌ Untested |
| Linux | ✅ [Supported](https://github.com/Breezyslasher/Vita_Suwayomi/issues/232) *(Flatpak, Deb, AUR)* |

> [!NOTE]
> Some console platforms require modded/homebrew-enabled systems.

## Requirements

- Suwayomi-Server running and accessible
- Network connectivity between device and server

**PS Vita:** Requires HENkaku/Enso

## Installation

**PS Vita:**
1. Download the VPK file
2. Transfer to Vita via USB or FTP
3. Install using VitaShell
4. Configure your Suwayomi server URL in settings

**Android:** Install the APK and configure your server URL on first launch.

**Other platforms:** Download the appropriate build from [Releases](../../releases) and run the application.



## License

[Add license information here]
