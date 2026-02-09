# VitaSuwayomi Complete Feature Testing Checklist

## Status Legend

| Symbol | Status |
|--------|--------|
| ✓ | Working |
| X | Broken |
| _(no symbol)_ | Untested |

---

## Authentication & Connection

- [ ] Connect to Suwayomi server with URL
- [ ] Login with username/password (Basic Auth)
- [ ] Credentials persist after app restart
- [ ] App handles invalid credentials gracefully
- [ ] App handles server offline gracefully
- [ ] Connection status shows correctly
- [ ] Local server URL (LAN) works
- [ ] Remote server URL (WAN) works
- [ ] Toggle between local/remote connections
- [ ] Auto-switch on connection failure
- [ ] Connection timeout configuration works

---

## Home Tab

- [ ] Home tab displays correctly
- [ ] Continue Reading section shows recent manga
- [ ] Recent Chapter Updates section shows new releases
- [ ] Clicking manga opens detail view
- [ ] Clicking chapter opens reader directly

---

## Library Browsing

- [ ] All user categories appear as tabs
- [ ] Category tabs scroll horizontally
- [ ] Selected category is highlighted
- [ ] Empty categories are hidden
- [ ] Hidden categories (user-configured) don't show
- [ ] Manga covers load properly
- [ ] Manga titles display correctly
- [ ] Unread badge shows on items with unread chapters
- [ ] Downloaded badge shows on downloaded items
- [ ] Scroll through manga grid works smoothly
- [ ] Clicking manga opens detail view
- [ ] L/R buttons navigate between categories

### Library Display Options

- [ ] Grid View displays correctly (covers + titles)
- [ ] Compact Grid View works (covers only)
- [ ] List View works (detailed)
- [ ] Grid size 4 columns works
- [ ] Grid size 6 columns works
- [ ] Grid size 8 columns works
- [ ] Display mode persists after restart

### Library Sorting

- [ ] Sort by Title A-Z works
- [ ] Sort by Title Z-A works
- [ ] Sort by Unread Count (most first) works
- [ ] Sort by Unread Count (least first) works
- [ ] Sort by Recently Added works
- [ ] Sort mode persists after restart

### Selection Mode & Batch Operations

- [ ] Enter selection mode (long press/Start button)
- [ ] Multi-select manga works
- [ ] Selection count displays correctly
- [ ] Batch download chapters works
- [ ] Batch mark as read works
- [ ] Batch mark as unread works
- [ ] Batch remove from library works
- [ ] Download next N chapters from selected works
- [ ] Change categories for multiple manga works
- [ ] Exit selection mode works

---

## Category Management

- [ ] Categories load from server
- [ ] Categories sorted by order
- [ ] Update button triggers category-specific update
- [ ] Update notification shows correctly
- [ ] Refresh reloads categories and manga
- [ ] Hide/show categories in settings

---

## Browse Sources

- [ ] Sources tab shows installed sources
- [ ] Source icons load properly
- [ ] Clicking source opens source browser
- [ ] Popular manga loads for source
- [ ] Latest manga loads for source
- [ ] Search within source works
- [ ] Pagination works (load more)
- [ ] Filter sources by language works
- [ ] NSFW sources toggle works

---

## Search

- [ ] Search tab opens correctly
- [ ] Search returns results from sources
- [ ] Search results show covers and titles
- [ ] Clicking search result opens detail view
- [ ] Global search across sources works
- [ ] Results grouped by source
- [ ] Search history saves automatically
- [ ] Search history limit configurable
- [ ] Clear search history works
- [ ] Quick access to previous searches

---

## Manga Detail View

- [ ] Cover image displays
- [ ] Title displays correctly
- [ ] Author/Artist displays correctly
- [ ] Status shows (Ongoing/Completed/etc.)
- [ ] Genre tags display
- [ ] Description/summary displays
- [ ] Expandable description works
- [ ] Add to library button works
- [ ] Remove from library button works
- [ ] Chapter list loads and displays
- [ ] Chapter count displays
- [ ] Source information displays

---

## Chapter List

- [ ] Chapters load correctly
- [ ] Chapter names display properly
- [ ] Read chapters marked differently
- [ ] Chapter numbers display
- [ ] Scanlator shows if available
- [ ] Upload date shows
- [ ] Clicking chapter opens reader
- [ ] Mark as read works
- [ ] Mark as unread works
- [ ] Bookmark chapter works
- [ ] Download chapter works
- [ ] Delete downloaded chapter works

### Chapter List Filtering & Sorting

- [ ] Sort by newest first works
- [ ] Sort by oldest first works
- [ ] Filter by Downloaded status works
- [ ] Filter by Unread status works
- [ ] Filter by Bookmarked status works
- [ ] Filter by scanlator group works
- [ ] Multi-select chapters works
- [ ] Range selection works

---

## Reader - Basic Controls

- [ ] Pages load correctly
- [ ] Page counter shows current/total
- [ ] Tap anywhere to show/hide controls
- [ ] Exit reader returns to detail view
- [ ] Progress saves when exiting
- [ ] Top bar shows manga title
- [ ] Bottom bar shows chapter progress
- [ ] Page slider works
- [ ] Jump to page input works

---

## Reader - Navigation

- [ ] D-pad left/right changes pages
- [ ] L/R shoulder buttons change pages
- [ ] First page reached shows indicator
- [ ] Last page reached shows indicator
- [ ] Go to next chapter works
- [ ] Go to previous chapter works
- [ ] Page slider navigation works
- [ ] Resume from last read position works

---

## Reader - Touch Gestures

- [ ] Swipe left/right to change pages
- [ ] Swipe shows preview of next/previous page
- [ ] Swipe animation completes page turn
- [ ] Partial swipe snaps back to current page
- [ ] Swipe direction respects rotation setting
- [ ] Swipe works in RTL mode (swipe left = next page)
- [ ] Swipe works in LTR mode (swipe right = next page)
- [ ] Swipe works in vertical mode (swipe up = next page)
- [ ] Single tap toggles UI controls
- [ ] Double tap zooms in/out
- [ ] Pinch to zoom works
- [ ] Long press for context menu works

---

## Reader - Reading Modes

- [ ] Right-to-Left (RTL) mode works
- [ ] Left-to-Right (LTR) mode works
- [ ] Vertical scroll mode works
- [ ] Webtoon (continuous scroll) mode works

---

## Reader - Page Scaling

- [ ] Fit Screen works
- [ ] Fit Width works
- [ ] Fit Height works
- [ ] Original Size (1:1) works

---

## Reader - Image Controls

- [ ] Rotation 0° works
- [ ] Rotation 90° works
- [ ] Rotation 180° works
- [ ] Rotation 270° works
- [ ] Auto-crop borders works

---

## Reader - Color Filters

- [ ] No filter (default) works
- [ ] Sepia filter works
- [ ] Night mode filter works
- [ ] Blue light filter works
- [ ] Brightness adjustment (0-100%) works
- [ ] Filter intensity adjustment (0-100%) works
- [ ] Filter settings persist

---

## Reader - Settings Persistence

- [ ] Reader settings menu opens (Settings button)
- [ ] Reading direction changes persist
- [ ] Image rotation changes persist
- [ ] Scale mode changes persist
- [ ] Settings persist after closing reader
- [ ] Settings persist after app restart
- [ ] Per-manga settings override global defaults
- [ ] Different manga can have different settings

---

## Reader - Server Sync

- [ ] Settings save to Suwayomi server (meta keys)
- [ ] Settings load from server on chapter open
- [ ] Server settings take priority over local cache
- [ ] Settings sync works with Tachiyomi/Mihon clients
- [ ] Meta keys used: readerMode, rotation, scaleType, cropBorders, webtoonSidePadding

---

## Reader - Progress

- [ ] Last page read saves to server
- [ ] Resume at last read page works
- [ ] Finishing chapter marks as read
- [ ] Progress syncs correctly

---

## Reader - Auto-Chapter Advance

- [ ] Auto-advance toggle works
- [ ] Advance delay (0-10 seconds) configurable
- [ ] Countdown shows before advancing
- [ ] Auto-advance respects setting

---

## Reader - Webtoon Mode

- [ ] Webtoon auto-detection works (genre/source based)
- [ ] Auto-detected webtoons use vertical mode
- [ ] Auto-detected webtoons use fit-width scale
- [ ] Crop borders setting applies to pages
- [ ] Side padding applies correctly (0-20%)
- [ ] Manual override of auto-detected settings works
- [ ] Webtoon settings save per-manga
- [ ] Seamless page scrolling works
- [ ] Tall page splitting works

---

## Extensions Management

- [ ] Extensions tab shows available extensions
- [ ] Installed extensions marked
- [ ] Extension icons load
- [ ] Install extension works
- [ ] Update extension works
- [ ] Uninstall extension works
- [ ] Extensions filter by language
- [ ] Updates Available section shows update count
- [ ] Search extensions by name works
- [ ] Expandable/collapsible sections work

---

## Downloads - Server Downloads

- [ ] Queue chapter download to server works
- [ ] Download progress shows
- [ ] Download completes successfully
- [ ] Downloaded chapters marked
- [ ] Downloads tab shows queue
- [ ] Cancel download works
- [ ] Start/Stop download queue works
- [ ] Downloaded chapters read offline

---

## Downloads - Local Downloads

- [ ] Queue chapter download locally works
- [ ] Local download progress shows
- [ ] Local download completes successfully
- [ ] Locally downloaded chapters marked
- [ ] Read locally downloaded chapters offline
- [ ] Delete local downloads works

---

## Downloads - Settings & Options

- [ ] Download location: Server only
- [ ] Download location: Local only
- [ ] Download location: Both
- [ ] Concurrent downloads setting (1-4) works
- [ ] WiFi-only downloads toggle works
- [ ] Auto-download new chapters toggle works
- [ ] Auto-resume downloads on startup works
- [ ] Delete after reading toggle works

---

## Downloads - Storage Management

- [ ] View total storage usage
- [ ] Per-manga storage breakdown
- [ ] Delete specific manga downloads
- [ ] Delete only read chapters
- [ ] Clear all downloads
- [ ] Cache clearing works

---

## History Tab

- [ ] History tab displays correctly
- [ ] Reading history shows chronologically
- [ ] Relative timestamps display ("2 days ago")
- [ ] Quick resume from history works
- [ ] Remove individual history entry works
- [ ] Clear entire history works

---

## Tracking Integration

### Tracker Setup

- [ ] MyAnimeList login works
- [ ] AniList login works
- [ ] OAuth authentication completes
- [ ] Tracker credentials persist

### Tracker Features

- [ ] Search tracker database works
- [ ] Bind manga to tracker entry works
- [ ] Tracker search shows covers
- [ ] Tracker search shows title/description
- [ ] Reading progress syncs automatically
- [ ] Update reading status works (Reading, Completed, etc.)
- [ ] Set scores/ratings works
- [ ] Set start date works
- [ ] Set finish date works
- [ ] View tracked items works
- [ ] Unbind tracker works

---

## Manga Migration

- [ ] Migration option appears in context menu
- [ ] Search for manga across all sources works
- [ ] Compare source options works
- [ ] Migrate between sources works
- [ ] Reading progress preserved after migration

---

## Reading Statistics

- [ ] Statistics display correctly
- [ ] Total chapters read counter works
- [ ] Manga completed count works
- [ ] Current reading streak displays
- [ ] Longest reading streak displays
- [ ] Total reading time displays
- [ ] Reset statistics works

---

## Backup & Restore

- [ ] Export library backup works
- [ ] Import library from backup works
- [ ] Backup file validation works
- [ ] Automatic backup naming with timestamps

---

## Settings - Connection

- [ ] Server URL setting persists
- [ ] Username setting persists
- [ ] Password setting persists
- [ ] Test connection button works
- [ ] Clear cache option works
- [ ] Local URL configuration works
- [ ] Remote URL configuration works

---

## Settings - UI

- [ ] Theme: System works
- [ ] Theme: Light works
- [ ] Theme: Dark works
- [ ] Show Clock toggle works
- [ ] Enable Animations toggle works
- [ ] Debug Logging toggle works

---

## Settings - Reader Defaults

- [ ] Default reading direction setting works (RTL/LTR/Vertical/Webtoon)
- [ ] Default page scale mode setting works
- [ ] Default image rotation setting works (0/90/180/270)
- [ ] Reader background color setting works (Black/White/Gray)
- [ ] Keep screen on setting works
- [ ] Show page number setting works
- [ ] Tap to navigate setting works
- [ ] Default settings apply to new manga without custom settings

---

## Settings - Webtoon / Long Strip

- [ ] Crop borders toggle works
- [ ] Auto-detect webtoon toggle works
- [ ] Side padding selector works (None/5%/10%/15%/20%)
- [ ] Webtoon settings persist after restart
- [ ] Disabling auto-detect uses user defaults for all manga
- [ ] Enabling auto-detect applies webtoon defaults to detected webtoons

---

## Settings - Webtoon Detection

Webtoon auto-detection checks for:
- [ ] Genre tags: "Long Strip", "Webtoon", "Web Comic", "Manhwa", "Manhua", "Full Color"
- [ ] Sources: Webtoon, Tapas, Tappytoon, Lezhin, Toomics, Bilibili, Asura, Reaper, Flame

---

## Settings - Library

- [ ] Display mode setting persists
- [ ] Grid size setting persists
- [ ] Cache library data toggle works
- [ ] Cache cover images toggle works
- [ ] Default sort mode setting works
- [ ] Chapter sort order setting works

---

## Settings - Downloads

- [ ] Download location preference persists
- [ ] Auto-download new chapters setting works
- [ ] WiFi-only restriction setting works
- [ ] Max concurrent downloads setting works
- [ ] Delete after reading setting works
- [ ] Resume on startup setting works

---

## Settings - Browse

- [ ] Language filter configuration works
- [ ] NSFW source visibility toggle works

---

## Navigation & UI

- [ ] D-pad navigation works throughout app
- [ ] Tab switching works
- [ ] Back button returns to previous screen
- [ ] Scrolling works smoothly
- [ ] Focus indicators visible
- [ ] Loading indicators show during fetches
- [ ] Toast notifications display correctly
- [ ] Dialog boxes work correctly

---

## Error Handling

- [ ] Network errors show user-friendly message
- [ ] Empty states handled gracefully
- [ ] Failed image loads show placeholder
- [ ] Server errors reported appropriately

---

## Server Information

- [ ] View connected server details
- [ ] Server version displays
- [ ] Server build info displays
- [ ] GitHub link works
- [ ] Discord link works

---

## Smoke Test (Quick Validation ~10 minutes)

### Basic Functionality
1. [ ] App launches successfully
2. [ ] Connect to Suwayomi server
3. [ ] Library categories load and display
4. [ ] Select a category and view manga
5. [ ] Open manga detail view
6. [ ] Load chapter list
7. [ ] Open reader and view pages
8. [ ] Navigate pages with buttons
9. [ ] Exit reader, progress saved
10. [ ] Browse a source
11. [ ] Search for manga
12. [ ] Check extensions tab
13. [ ] Check downloads tab
14. [ ] Check history tab
15. [ ] Check home tab

### Reader Settings (New Features)
16. [ ] Open reader settings in chapter
17. [ ] Change reading direction, verify it applies
18. [ ] Change rotation, verify it applies
19. [ ] Close reader and reopen same manga - settings preserved
20. [ ] Open different manga - uses default settings
21. [ ] Swipe to change pages - verify animation works
22. [ ] Swipe in rotated view - direction is correct
23. [ ] Apply color filter and verify it works
24. [ ] Adjust brightness and verify it works

### Webtoon Detection (New Features)
25. [ ] Open a known webtoon/manhwa manga
26. [ ] Verify auto-detection applies vertical mode + fit-width
27. [ ] Open a regular manga - uses default RTL mode
28. [ ] Check Settings > Webtoon section exists
29. [ ] Toggle "Auto-Detect Webtoon" off
30. [ ] Open webtoon again - now uses user defaults

### Settings Persistence
31. [ ] Change default reader settings in Settings tab
32. [ ] Restart app - settings persist
33. [ ] Open new manga - default settings applied
34. [ ] Per-manga settings still preserved after restart

### Batch Operations
35. [ ] Enter selection mode in library
36. [ ] Select multiple manga
37. [ ] Perform batch action (e.g., mark as read)
38. [ ] Exit selection mode

### Tracking
39. [ ] Open manga detail view
40. [ ] Access tracking option
41. [ ] Bind to a tracker (if logged in)

---

## Regression Tests (Edge Cases)

### Swipe Navigation Edge Cases
- [ ] Swipe at first page - no previous page preview, snaps back
- [ ] Swipe at last page - shows next chapter or end indicator
- [ ] Quick successive swipes don't break animation
- [ ] Swipe during page load - handles gracefully
- [ ] Swipe with 90° rotation - horizontal swipe moves vertically
- [ ] Swipe with 180° rotation - directions inverted correctly
- [ ] Swipe with 270° rotation - horizontal swipe moves vertically (opposite)

### Settings Priority Edge Cases
- [ ] Server has settings, local cache has different settings - server wins
- [ ] Server unreachable - falls back to local cache
- [ ] No server settings, no local cache - uses global defaults
- [ ] Webtoon detected but has server settings - server settings win
- [ ] Clear local cache - server settings still load correctly

### Webtoon Detection Edge Cases
- [ ] Manga with "Manhwa" genre but from non-webtoon source - detected as webtoon
- [ ] Manga from "Webtoon" source but no genre tags - detected as webtoon
- [ ] Manga with "Full Color" tag only - detected as webtoon
- [ ] Regular manga with no webtoon indicators - uses normal defaults
- [ ] User manually changes detected webtoon settings - custom settings saved

### Chapter Navigation Edge Cases
- [ ] Last chapter, swipe to next - shows end indicator or loops
- [ ] First chapter, go to previous - shows start indicator
- [ ] Chapter with single page - navigation still works
- [ ] Chapter with 100+ pages - performance acceptable
- [ ] Navigate chapters rapidly - no crashes

### Settings Sync Edge Cases
- [ ] Change settings while offline - saves locally, syncs when online
- [ ] Multiple rapid setting changes - all saved correctly
- [ ] Server returns error on save - local cache preserved
- [ ] Corrupt server meta data - handled gracefully, uses defaults

### Download Edge Cases
- [ ] Download interrupted - resumes correctly
- [ ] Download while reading - doesn't affect reader
- [ ] Multiple concurrent downloads work
- [ ] Low storage handling

### Tracking Edge Cases
- [ ] Tracker auth expires - handles gracefully
- [ ] Tracker API error - shows user-friendly message
- [ ] Sync progress with no tracker bound - no crash

---

## Build Instructions

### Prerequisites

- VitaSDK installed and configured
- CMake 3.10+
- Git

### Building

```bash
mkdir build && cd build
cmake ..
make
```

### Installing on Vita

1. Build the VPK file
2. Transfer to Vita via USB/FTP
3. Install using VitaShell
4. Launch from LiveArea

---

## Server Requirements

- Suwayomi-Server running and accessible
- Network connectivity between Vita and server
- Basic Auth enabled if using authentication
- Recommended: Both local (LAN) and remote (WAN) URLs configured for failover
