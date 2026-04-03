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

## Requirements

- PlayStation Vita with HENkaku/Enso
- Suwayomi-Server running and accessible
- Network connectivity between Vita and server



## Installation

1. Download the VPK file
2. Transfer to Vita via USB or FTP
3. Install using VitaShell
4. Configure your Suwayomi server URL in settings

## Building (PS Vita)

The PS Vita build now supports two renderer backends:

- **GXM** (`-DPSV_RENDERER=GXM`) — default and recommended for most users.
- **GLES2** (`-DPSV_RENDERER=GLES2`) — alternative backend (requires `vitaGL`, e.g. `vdpm vitagl`).

By default, CMake attempts to auto-install `vitaGL` for GLES2 when `vdpm` is available
(`-DPSV_AUTO_INSTALL_VITAGL=ON`). Disable this behavior with `-DPSV_AUTO_INSTALL_VITAGL=OFF`.

Example commands:

```bash
cmake -B build-vita-gxm -G Ninja -DPLATFORM_PSV=ON -DPSV_RENDERER=GXM
cmake --build build-vita-gxm

cmake -B build-vita-gles2 -G Ninja -DPLATFORM_PSV=ON -DPSV_RENDERER=GLES2
cmake --build build-vita-gles2
```

Each build outputs a renderer-specific VPK:

- `VitaSuwayomi-gxm.vpk`
- `VitaSuwayomi-gles2.vpk`


## License

[Add license information here]
