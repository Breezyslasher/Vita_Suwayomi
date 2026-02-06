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

## Library Browsing

- [ ] All user categories appear as tabs
- [ ] Category tabs scroll horizontally
- [ ] Selected category is highlighted
- [ ] Empty categories are hidden
- [ ] Manga covers load properly
- [ ] Manga titles display correctly
- [ ] Unread badge shows on items with unread chapters
- [ ] Scroll through manga grid works smoothly
- [ ] Clicking manga opens detail view

## Category Management

- [ ] Categories load from server
- [ ] Categories sorted by order
- [ ] Update button triggers category-specific update
- [ ] Update notification shows correctly
- [ ] Refresh reloads categories and manga

## Browse Sources

- [ ] Sources tab shows installed sources
- [ ] Source icons load properly
- [ ] Clicking source opens source browser
- [ ] Popular manga loads for source
- [ ] Latest manga loads for source
- [ ] Search within source works
- [ ] Pagination works (load more)

## Search

- [ ] Search tab opens correctly
- [ ] Search returns results from sources
- [ ] Search results show covers and titles
- [ ] Clicking search result opens detail view
- [ ] Global search across sources works

## Manga Detail View

- [ ] Cover image displays
- [ ] Title displays correctly
- [ ] Author/Artist displays correctly
- [ ] Status shows (Ongoing/Completed/etc.)
- [ ] Genre tags display
- [ ] Description/summary displays
- [ ] Add to library button works
- [ ] Remove from library button works
- [ ] Chapter list loads and displays

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
- [ ] Download chapter works

## Reader - Basic Controls

- [ ] Pages load correctly
- [ ] Page counter shows current/total
- [ ] Tap anywhere to show/hide controls
- [ ] Exit reader returns to detail view
- [ ] Progress saves when exiting
- [ ] Top bar shows manga title
- [ ] Bottom bar shows chapter progress

## Reader - Navigation

- [ ] D-pad left/right changes pages
- [ ] L/R shoulder buttons change pages
- [ ] First page reached shows indicator
- [ ] Last page reached shows indicator
- [ ] Go to next chapter works
- [ ] Go to previous chapter works
- [ ] Page slider navigation works

## Reader - Swipe Gestures

- [ ] Swipe left/right to change pages
- [ ] Swipe shows preview of next/previous page
- [ ] Swipe animation completes page turn
- [ ] Partial swipe snaps back to current page
- [ ] Swipe direction respects rotation setting
- [ ] Swipe works in RTL mode (swipe left = next page)
- [ ] Swipe works in LTR mode (swipe right = next page)
- [ ] Swipe works in vertical mode (swipe up = next page)

## Reader - Settings Persistence

- [ ] Reader settings menu opens (Settings button)
- [ ] Reading direction changes (RTL/LTR/Vertical)
- [ ] Image rotation changes (0/90/180/270 degrees)
- [ ] Scale mode changes (Fit Screen/Width/Height/Original)
- [ ] Settings persist after closing reader
- [ ] Settings persist after app restart
- [ ] Per-manga settings override global defaults
- [ ] Different manga can have different settings

## Reader - Server Sync

- [ ] Settings save to Suwayomi server (meta keys)
- [ ] Settings load from server on chapter open
- [ ] Server settings take priority over local cache
- [ ] Settings sync works with Tachiyomi/Mihon clients
- [ ] Meta keys used: readerMode, rotation, scaleType, cropBorders, webtoonSidePadding

## Reader - Progress

- [ ] Last page read saves to server
- [ ] Resume at last read page works
- [ ] Finishing chapter marks as read
- [ ] Progress syncs correctly

## Reader - Webtoon Mode

- [ ] Webtoon auto-detection works (genre/source based)
- [ ] Auto-detected webtoons use vertical mode
- [ ] Auto-detected webtoons use fit-width scale
- [ ] Crop borders setting applies to pages
- [ ] Side padding applies correctly
- [ ] Manual override of auto-detected settings works
- [ ] Webtoon settings save per-manga

## Extensions Management

- [ ] Extensions tab shows available extensions
- [ ] Installed extensions marked
- [ ] Extension icons load
- [ ] Install extension works
- [ ] Update extension works
- [ ] Uninstall extension works
- [ ] Extensions filter by language

## Downloads

- [ ] Queue chapter download works
- [ ] Download progress shows
- [ ] Download completes successfully
- [ ] Downloaded chapters marked
- [ ] Downloads tab shows queue
- [ ] Cancel download works
- [ ] Start/Stop download queue works
- [ ] Downloaded chapters read offline

## Settings - Connection

- [ ] Server URL setting persists
- [ ] Username setting persists
- [ ] Password setting persists
- [ ] Test connection button works
- [ ] Clear cache option works

## Settings - Reader Defaults

- [ ] Default reading direction setting works (RTL/LTR/Vertical/Webtoon)
- [ ] Default page scale mode setting works
- [ ] Default image rotation setting works (0/90/180/270)
- [ ] Reader background color setting works (Black/White/Gray)
- [ ] Keep screen on setting works
- [ ] Show page number setting works
- [ ] Tap to navigate setting works
- [ ] Default settings apply to new manga without custom settings

## Settings - Webtoon / Long Strip

- [ ] Crop borders toggle works
- [ ] Auto-detect webtoon toggle works
- [ ] Side padding selector works (None/5%/10%/15%/20%)
- [ ] Webtoon settings persist after restart
- [ ] Disabling auto-detect uses user defaults for all manga
- [ ] Enabling auto-detect applies webtoon defaults to detected webtoons

## Settings - Webtoon Detection

Webtoon auto-detection checks for:
- [ ] Genre tags: "Long Strip", "Webtoon", "Web Comic", "Manhwa", "Manhua", "Full Color"
- [ ] Sources: Webtoon, Tapas, Tappytoon, Lezhin, Toomics, Bilibili, Asura, Reaper, Flame

## Navigation & UI

- [ ] D-pad navigation works throughout app
- [ ] Tab switching works
- [ ] Back button returns to previous screen
- [ ] Scrolling works smoothly
- [ ] Focus indicators visible
- [ ] Loading indicators show during fetches

## Error Handling

- [ ] Network errors show user-friendly message
- [ ] Empty states handled gracefully
- [ ] Failed image loads show placeholder
- [ ] Server errors reported appropriately

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

### Reader Settings (New Features)
14. [ ] Open reader settings in chapter
15. [ ] Change reading direction, verify it applies
16. [ ] Change rotation, verify it applies
17. [ ] Close reader and reopen same manga - settings preserved
18. [ ] Open different manga - uses default settings
19. [ ] Swipe to change pages - verify animation works
20. [ ] Swipe in rotated view - direction is correct

### Webtoon Detection (New Features)
21. [ ] Open a known webtoon/manhwa manga
22. [ ] Verify auto-detection applies vertical mode + fit-width
23. [ ] Open a regular manga - uses default RTL mode
24. [ ] Check Settings > Webtoon section exists
25. [ ] Toggle "Auto-Detect Webtoon" off
26. [ ] Open webtoon again - now uses user defaults

### Settings Persistence
27. [ ] Change default reader settings in Settings tab
28. [ ] Restart app - settings persist
29. [ ] Open new manga - default settings applied
30. [ ] Per-manga settings still preserved after restart

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
