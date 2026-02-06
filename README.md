# VitaSuwayomi

A feature-rich manga reader for the PlayStation Vita that connects to a [Suwayomi](https://github.com/Suwayomi/Suwayomi-Server) server. Read your manga library on the go with full offline support, tracking integration, and extensive customization options.

---

## Table of Contents

- [Features Overview](#features-overview)
- [Library Management](#library-management)
- [Reading Experience](#reading-experience)
- [Downloads & Offline Reading](#downloads--offline-reading)
- [Search & Browse](#search--browse)
- [Extensions & Sources](#extensions--sources)
- [Tracking Integration](#tracking-integration)
- [Settings & Customization](#settings--customization)
- [Additional Features](#additional-features)
- [Building from Source](#building-from-source)
- [Server Requirements](#server-requirements)

---

## Features Overview

### Main Navigation
The app is organized into intuitive tabs for easy navigation:

- **Home** - Continue reading recent manga and view new chapter updates
- **Library** - Browse your organized manga collection by categories
- **Search** - Find manga across all your installed sources
- **Extensions** - Manage Suwayomi extensions (install, update, remove)
- **Downloads** - Monitor and manage your download queue
- **History** - View and resume from your reading history
- **Settings** - Configure all aspects of the application

### Quick Highlights
- Full offline reading support with local downloads
- Multiple reading modes (RTL, LTR, Vertical, Webtoon)
- MyAnimeList and AniList tracking integration
- Batch operations for library management
- Auto-detection for webtoon/manhwa formats
- Customizable color filters and display options
- Per-manga reader settings

---

## Library Management

### Category System
- **Category Tabs** - Navigate between categories with horizontal scrolling tabs
- **Category Visibility** - Hide empty or unwanted categories from view
- **Category Sorting** - Categories sorted by custom order from server
- **Update by Category** - Trigger chapter updates for specific categories

### Library Display Options
- **Grid View** - Standard view with covers and titles
- **Compact Grid** - Covers only for maximum density
- **List View** - Detailed view with additional information
- **Grid Size** - Choose between 4, 6, or 8 columns

### Sorting Options
- Title (A-Z or Z-A)
- Unread Count (most or least first)
- Recently Added

### Badge Indicators
- **Unread Badge** - Shows number of unread chapters
- **Downloaded Badge** - Indicates locally available content

### Batch Operations (Selection Mode)
- Multi-select manga with selection mode
- Download chapters for multiple manga at once
- Mark multiple manga as read/unread
- Remove multiple manga from library
- Download next N chapters from selected manga
- Move multiple manga to different categories

---

## Reading Experience

### Reading Modes
- **Right-to-Left (RTL)** - Traditional manga reading direction
- **Left-to-Right (LTR)** - Western comic style
- **Vertical** - Top-to-bottom scrolling
- **Webtoon Mode** - Continuous vertical scroll for long-strip content

### Page Scaling
- **Fit Screen** - Display entire page within screen bounds
- **Fit Width** - Fill screen width, scroll vertically if needed
- **Fit Height** - Fill screen height, scroll horizontally if needed
- **Original Size** - Display at native resolution (1:1)

### Image Controls
- **Rotation** - Rotate pages (0°, 90°, 180°, 270°)
- **Auto-Crop Borders** - Remove white/black borders automatically
- **Zoom** - Double-tap or pinch-to-zoom for detailed viewing

### Color Filters
- **Filter Types** - None, Sepia, Night Mode, Blue Light Filter
- **Brightness Control** - Adjustable from 0-100%
- **Filter Intensity** - Fine-tune filter strength (0-100%)

### Navigation Methods
- **Touch Gestures**
  - Swipe left/right for page navigation with preview animation
  - Tap to toggle UI controls
  - Double-tap to zoom
  - Long-press for context menus
- **Button Controls**
  - D-pad left/right for pages
  - L/R shoulder buttons for pages
  - Quick chapter navigation
- **Page Slider** - Jump to any page quickly
- **Jump to Page** - Direct page number input

### Reader UI
- Collapsible top/bottom control bars
- Page counter with current/total display
- Chapter progress indicator
- Quick settings overlay panel
- Manga title and chapter name display

### Webtoon-Specific Features
- **Auto-Detection** - Automatically detects webtoon/manhwa based on genre tags and source
- **Seamless Scrolling** - Continuous page flow without breaks
- **Side Padding** - Adjustable padding (0-20%) for comfortable reading
- **Tall Page Splitting** - Long images split into manageable segments

### Chapter Navigation
- Previous/Next chapter buttons
- Auto-advance to next chapter (configurable delay)
- Resume from last read position
- Chapter completion marking

### Per-Manga Settings
Override global defaults for individual manga:
- Reading mode
- Page scale
- Image rotation
- Crop borders
- Webtoon settings

---

## Downloads & Offline Reading

### Download Locations
- **Server Downloads** - Store on Suwayomi server
- **Local Downloads** - Store on Vita for offline reading
- **Both** - Download to both locations

### Queue Management
- View active download queue
- Pause/Resume downloads
- Reorder queue items
- Cancel individual or all downloads
- Download progress tracking

### Download Options
- **Concurrent Downloads** - Set 1-4 simultaneous downloads
- **WiFi-Only** - Restrict downloads to WiFi connections
- **Auto-Download** - Automatically download new chapters
- **Auto-Resume** - Resume incomplete downloads on app start
- **Delete After Reading** - Automatically clean up read chapters

### Storage Management
- View total storage usage
- Per-manga storage breakdown
- Delete specific manga downloads
- Delete only read chapters
- Clear all downloads
- Cache management

---

## Search & Browse

### Source Search
- Search within specific sources
- Browse Popular manga
- Browse Latest updates
- Pagination with "Load More"

### Global Search
- Search across all installed sources simultaneously
- Results grouped by source
- Cover thumbnails for all results
- Quick access to manga details

### Search History
- Automatic history tracking
- Quick access to previous searches
- Configurable history limit (default: 20)
- Clear history option

### Browse Filters
- Filter sources by language
- NSFW content toggle
- Source-specific filters and preferences

---

## Extensions & Sources

### Extension Management
- **Install** - Add new sources from available extensions
- **Update** - Keep extensions up to date with update indicators
- **Uninstall** - Remove unwanted extensions
- **Search** - Find extensions by name

### Extension Browser
- Updates Available section with badges
- Installed extensions section
- Available extensions grouped by language
- Expandable/collapsible sections

### Source Configuration
- Per-source preferences and settings
- Multiple setting types (switches, text, lists, etc.)
- Source filter customization
- Language-based source filtering

---

## Tracking Integration

### Supported Trackers
- **MyAnimeList** - Full MAL integration
- **AniList** - Complete AniList support
- Additional trackers (extensible)

### Tracking Features
- OAuth authentication
- Search tracker databases
- Bind manga to tracker entries
- Sync reading progress automatically
- Update reading status (Reading, Completed, On Hold, Dropped, Plan to Read)
- Set scores and ratings
- Record start/finish dates
- View all tracked items

### Tracker Search
- Visual search results with covers
- Title and description display
- Status and publishing information
- Easy binding to library manga

---

## Settings & Customization

### Connection Settings
- Local server URL (LAN)
- Remote server URL (WAN)
- Toggle between local/remote connections
- Auto-switch on connection failure
- Connection timeout configuration
- Basic Auth support (username/password)

### UI Settings
- **Theme** - System, Light, or Dark mode
- **Show Clock** - Display system time
- **Enable Animations** - Toggle UI animations
- **Debug Logging** - Enable detailed logging

### Reader Defaults
- Default reading direction
- Default page scale mode
- Default image rotation
- Background color (Black, White, Gray)
- Keep screen on during reading
- Show page numbers
- Tap-to-navigate zones

### Auto-Chapter Advance
- Enable/disable auto-advance
- Configurable delay (0-10 seconds)
- Show countdown before advancing

### Webtoon Settings
- Crop borders toggle
- Auto-detect webtoon format
- Side padding adjustment (0-20%)

### Library Settings
- Display mode selection
- Grid size configuration
- Library data caching
- Cover image caching
- Default sort mode
- Chapter sort order

### Download Settings
- Download location preference
- Auto-download new chapters
- WiFi-only restriction
- Max concurrent downloads
- Delete after reading
- Resume on startup

### Browse Settings
- Language filter configuration
- NSFW source visibility

---

## Additional Features

### Manga Detail View
- Cover image display
- Full metadata (title, author, artist, status)
- Expandable description
- Genre tags
- Chapter count and source info
- Add/remove from library
- Full chapter list with filtering

### Chapter List Features
- Sort by newest/oldest
- Filter by status (Downloaded, Unread, Bookmarked)
- Filter by scanlator group
- Multi-select for batch operations
- Range selection
- Quick action icons

### Reading History
- Chronological reading history
- Relative timestamps ("2 days ago")
- Quick resume functionality
- Remove individual entries
- Clear entire history

### Reading Statistics
- Total chapters read
- Manga completed count
- Current reading streak
- Longest reading streak
- Total reading time
- Reset statistics option

### Backup & Restore
- Export library backup
- Import from backup file
- Automatic backup naming with timestamps

### Manga Migration
- Search for manga across all sources
- Compare source options
- Migrate between sources
- Preserve reading progress

### Server Information
- View connected server details
- Server version and build info
- GitHub and Discord links

---

## Building from Source

### Prerequisites
- VitaSDK installed and configured
- CMake 3.10+
- Git

### Build Steps

```bash
# Clone the repository
git clone https://github.com/YourUsername/VitaSuwayomi.git
cd VitaSuwayomi

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make
```

### Installing on Vita

1. Build produces a `.vpk` file
2. Transfer to Vita via USB or FTP
3. Install using VitaShell
4. Launch from LiveArea

---

## Server Requirements

- [Suwayomi-Server](https://github.com/Suwayomi/Suwayomi-Server) running and accessible
- Network connectivity between Vita and server
- Basic Auth enabled if using authentication
- Recommended: Both local (LAN) and remote (WAN) URLs configured for failover

---

## Controls Reference

### General Navigation
| Button | Action |
|--------|--------|
| D-Pad | Navigate UI |
| X | Select/Confirm |
| O | Back/Cancel |
| L/R | Switch tabs / Navigate categories |
| Start | Context menu |

### Reader Controls
| Button | Action |
|--------|--------|
| D-Pad Left/Right | Previous/Next page |
| L/R | Previous/Next page |
| X | Toggle UI |
| O | Exit reader |
| Triangle | Reader settings |

### Touch Gestures
| Gesture | Action |
|---------|--------|
| Swipe Left/Right | Navigate pages |
| Single Tap | Toggle UI controls |
| Double Tap | Zoom in/out |
| Pinch | Manual zoom |
| Long Press | Context menu |

---

## License

This project is open source. See the LICENSE file for details.

## Acknowledgments

- [Suwayomi](https://github.com/Suwayomi/Suwayomi-Server) - The server powering this app
- [Borealis](https://github.com/natinusala/borealis) - UI framework
- All extension developers and contributors
