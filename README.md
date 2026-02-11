# VitaSuwayomi

A native Suwayomi client for PlayStation Vita. Read manga from your Suwayomi server directly on your Vita with full controller and touch support.

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

## Requirements

- PlayStation Vita with HENkaku/Enso
- Suwayomi-Server running and accessible
- Network connectivity between Vita and server

## Building

```bash
# Requires VitaSDK
mkdir build && cd build
cmake ..
make
```

## Installation

1. Build or download the VPK file
2. Transfer to Vita via USB or FTP
3. Install using VitaShell
4. Configure your Suwayomi server URL in settings

## Testing

Use the [Testing Checklist](.github/ISSUE_TEMPLATE/testing-checklist.md) issue template for comprehensive feature testing.

## License

[Add license information here]
