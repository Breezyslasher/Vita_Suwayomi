---
name: Testing Checklist V3 - Complete
about: Complete consolidated feature testing checklist for VitaSuwayomi (combines v1 + v2)
title: "[Testing] Complete Feature Checklist V3"
labels: testing, v3
assignees: ''
---

## Status Legend
| Symbol | Status |
|--------|--------|
| ✓ | Working |
| X | Broken |
| _(unchecked)_ | Untested |

---

## Authentication & Connection

### Login & Auto-Detection
- [ ] Connect to Suwayomi server with URL
- [ ] Auth mode auto-detected on connect (no manual selection needed)
- [ ] Credentials persist after app restart
- [ ] Status label shows "Checking server..." then "Detecting auth mode..." during connect
- [ ] Status shows detected auth mode on success: "Connected! (UI Login (JWT))"

### Error Messages
- [ ] Empty URL shows: "Please enter server URL"
- [ ] Server offline/wrong URL shows: "Server offline or wrong URL"
- [ ] Auth required but no credentials shows: "Server requires auth - enter username & password"
- [ ] Wrong credentials shows: "Wrong username or password"
- [ ] Connection test (Test button) detects server reachability and auth type

### Auto-Detection Flow
- [ ] No-auth server: auto-detects None, connects without credentials
- [ ] Basic Auth server: auto-detects Basic Auth, validates credentials
- [ ] JWT server (UI Login): auto-detects UI Login, login + validates protected query
- [ ] JWT server (Simple Login): tries UI Login first, falls back to Simple Login
- [ ] Auth mode label updates to show detected mode after connect
- [ ] Manual auth mode override still works (tap to cycle)

### JWT Authentication (Simple Login / UI Login)
- [ ] JWT login sends username/password and receives tokens
- [ ] Access token stored and used for API calls
- [ ] Protected query validation after login (categories query)
- [ ] If wrong JWT mode selected, auto-switches to correct mode
- [ ] Refresh token stored for token renewal
- [ ] JWT tokens persist after restart

### Connection Restore (App Restart)
- [ ] Saved JWT tokens refreshed on startup
- [ ] Auth validated with protected query (not just public aboutServer)
- [ ] If saved auth mode wrong, auto-tries all modes (UI Login, Simple Login, Basic Auth)
- [ ] Correct mode saved after auto-detection on restore
- [ ] Falls back to login screen if all auth modes fail

### URL Configuration
- [ ] Local server URL (LAN) works
- [ ] Remote server URL (WAN) works
- [ ] Toggle between local/remote connections
- [ ] Auto-switch on connection failure
- [ ] Switch notification shows when URL changes
- [ ] Connection timeout configuration works

---

## Library Browsing

### Category Navigation
- [ ] All user categories appear as tabs
- [ ] Category tabs scroll horizontally (touch drag)
- [ ] Selected category is highlighted
- [ ] Empty categories are hidden
- [ ] Hidden categories (user-configured) don't show
- [ ] L button navigates to previous category
- [ ] R button navigates to next category
- [ ] D-pad left/right navigates between tabs
- [ ] scrollToView() keeps selected tab visible

### Manga Display
- [ ] Manga covers load properly
- [ ] Manga titles display correctly
- [ ] Unread badge shows on items with unread chapters
- [ ] Gold star badge shows on library items (when browsing)
- [ ] Scroll through manga grid works smoothly
- [ ] Clicking manga opens detail view

### Button Hints
- [ ] Button hint icons display correctly sized
- [ ] L/R category hints show in header
- [ ] Start button hint shows on focused manga
- [ ] Triangle (Y) button hint for sort shows
- [ ] Select button hint for update shows

### Controller Actions
- [ ] Start button opens context menu on focused manga
- [ ] Triangle (Y) button opens sort options
- [ ] Select button triggers category update
- [ ] D-pad navigation through manga grid works

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
- [ ] Sort by Recently Added works (Newest)
- [ ] Sort by Recently Added works (Oldest)
- [ ] Sort Last Read
- [ ] Sort Date Updated (Newest)
- [ ] Sort Date Updated (Oldest)
- [ ] Sort Total Chapters
- [ ] Sort Downloads Only
- [ ] Sort mode persists after restart
- [ ] Sort setting saved per category
- [ ] Different categories can have different sort modes
- [ ] Switching categories restores that category's sort
- [ ] Default sort mode applies to categories without a custom sort

### Selection Mode & Batch Operations
- [ ] Enter selection mode (long press / Start button)
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

## Library Grouping Modes (Square Button)

### Mode Switching
- [ ] Square button opens grouping mode dropdown
- [ ] "By Category" mode shows category tabs with L/R hints
- [ ] "By Source" mode groups manga by source with tabs
- [ ] "No Grouping" mode shows all manga in one flat grid
- [ ] Selected grouping mode persists after restart
- [ ] Switching modes preserves sort settings

### By Category Mode
- [ ] Category tabs populate from server categories
- [ ] Selecting a category loads its manga
- [ ] L/R bumper buttons cycle between categories
- [ ] Category-specific sort is remembered per category

### By Source Mode
- [ ] Source tabs populate from manga sources (alphabetical)
- [ ] Selecting a source tab filters to that source's manga
- [ ] L/R bumper buttons cycle between sources
- [ ] Empty sources still show tab

### No Grouping Mode
- [ ] All library manga displayed in single grid
- [ ] Category tabs row is fully hidden
- [ ] L/R bumper hint icons are hidden
- [ ] L/R bumper buttons are disabled (no action)
- [ ] Sort applies across all manga

### Focus Safety on Mode Switch
- [ ] Switching from By Category to No Grouping doesn't crash
- [ ] Switching from By Source to By Category doesn't crash
- [ ] Switching from By Source to No Grouping doesn't crash
- [ ] Focus transfers to content grid after mode switch
- [ ] Pressing UP from grid in No Grouping skips hidden tabs row

---

## Library Caching

### Data Caching
- [ ] "Cache Library Data" toggle works in settings
- [ ] When enabled, categories load from cache on startup
- [ ] When enabled, manga list loads from cache instantly
- [ ] Cache updates after fresh server fetch
- [ ] Cache invalidates when manga moves between categories
- [ ] Disabling cache forces server fetch each time

### Cover Image Caching
- [ ] "Cache Cover Images" toggle works in settings
- [ ] When enabled, cover images save to disk
- [ ] Cached covers load without network on next open
- [ ] Cover cache can be cleared from storage management
- [ ] New covers download and cache on first view

---

## Incremental UI Updates

- [ ] Manga cells update incrementally (no full grid rebuild)
- [ ] Unread count changes reflect without reloading entire list
- [ ] Download status updates live on manga cells
- [ ] Moving manga between categories removes it from current view without full refresh
- [ ] Focus position preserved after incremental update

---

## Pull-to-Refresh Library Update

- [ ] Pulling down on grid triggers server-side library update
- [ ] Pulling down from category tabs row triggers library update
- [ ] Update notification shows "Checking for new chapters..."
- [ ] Works in By Category mode (updates specific category)
- [ ] Works in No Grouping mode (updates all manga)
- [ ] Works in By Source mode (updates all manga)
- [ ] Select/Back button on grid also triggers update + reload

---

## List View Row Size

- [ ] List row size setting appears in settings
- [ ] Small rows (compact, 60px) work
- [ ] Medium rows (standard, 80px) work
- [ ] Large rows (100px) work
- [ ] Auto row size (fits title text) works
- [ ] Row size setting persists after restart
- [ ] Row size applies in List display mode only

---

## Long Press Gesture (Library)

- [ ] Long press on manga opens context menu (touch)
- [ ] 400ms hold duration triggers gesture
- [ ] Moving finger more than 15px cancels long press
- [ ] Sound plays on long press trigger
- [ ] Long press enters selection mode
- [ ] Long press works alongside tap and swipe gestures

---

## Hidden Categories

- [ ] Hide/show categories configurable in settings
- [ ] Hidden categories don't appear in library tabs
- [ ] Hidden category IDs persist in settings
- [ ] Unhiding a category makes it reappear

---

## Unread Badge Toggle

- [ ] "Show Unread Badge" toggle in settings works
- [ ] When disabled, unread count badges hidden on manga
- [ ] When enabled, badges show as normal
- [ ] Setting persists after restart

---

## Category Management

- [ ] Categories load from server
- [ ] Categories sorted by order
- [ ] Update button triggers category-specific update
- [ ] Update notification shows correctly
- [ ] Refresh reloads categories and manga

---

## Browse Sources

### Source List
- [ ] Sources tab shows installed sources
- [ ] Source icons load properly
- [ ] Source names display correctly
- [ ] Clicking source opens source browser
- [ ] Filter sources by language works
- [ ] NSFW sources toggle works

### Source Browser
- [ ] Popular manga tab loads correctly
- [ ] Latest manga tab loads correctly
- [ ] Search within source works
- [ ] Manga grid displays with covers
- [ ] Infinite scroll loads next page when near end
- [ ] Focus moves to first new item after load
- [ ] Back button returns to source list

### Navigation
- [ ] D-pad navigation through manga works
- [ ] UP from first row navigates to header/tabs
- [ ] B button goes back to source list
- [ ] Touch support for manga selection works

---

## Search

### Search Input
- [ ] Search tab opens correctly
- [ ] Keyboard input works
- [ ] Search executes on submit

### Source Search
- [ ] Search returns results from current source
- [ ] Search results show covers and titles
- [ ] Clicking search result opens detail view
- [ ] Infinite scroll for search results works
- [ ] Focus moves to first result after search
- [ ] Back button returns to source browser

### Global Search
- [ ] Global search across all sources works
- [ ] Results grouped by source
- [ ] Source headers show in results
- [ ] Navigate between source result groups

### Search History
- [ ] Searches automatically saved to history
- [ ] History button appears in search header
- [ ] History dialog shows recent searches
- [ ] Tapping history item re-executes search
- [ ] Clear search history works
- [ ] Max history limit configurable (default 20)
- [ ] History persists after restart

---

## Source Language Filtering

- [ ] Language filter dialog opens from settings
- [ ] Multiple languages can be selected
- [ ] Empty selection means all languages shown
- [ ] Filter applies to source list display
- [ ] Language filter persists after restart
- [ ] Language filter text updates in settings cell

---

## Manga Detail View

### Display
- [ ] Cover image displays
- [ ] Title displays correctly
- [ ] Author/Artist displays correctly
- [ ] Status shows (Ongoing/Completed/etc.)
- [ ] Genre tags display
- [ ] Description/summary displays
- [ ] Description loads from server (not just cache)
- [ ] Expandable description works
- [ ] Chapter count displays
- [ ] Source information displays

### Library Actions
- [ ] Add to library button works
- [ ] Remove from library button works
- [ ] Library status updates immediately

### Chapter List
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
- [ ] Range selection works (L1 button)

### Context Menu (Start Button)
- [ ] Start button opens context menu
- [ ] Continue Reading option works
- [ ] Add/Remove from Library option works
- [ ] Download options work
- [ ] Track option opens tracking dialog
- [ ] Single tracker auto-selects (skips list if only one)

### Navigation
- [ ] D-pad navigation works
- [ ] L button doesn't conflict with other actions
- [ ] B button returns to previous view

---

## Chapter Loading Performance

### Incremental Chapter Building
- [ ] Chapter list builds incrementally (not all at once)
- [ ] First chapters appear immediately
- [ ] Remaining chapters build in batches per frame via brls::sync()
- [ ] No long UI freeze on large chapter lists (300+ chapters)
- [ ] D-pad navigation set up after all chapters are built
- [ ] Incremental build cancels on view destruction (no dangling callbacks)
- [ ] Re-calling populateChaptersList cancels previous incremental build

### Browser Append Without Rebuild
- [ ] RecyclingGrid::appendItems() adds items without destroying existing cells
- [ ] Existing manga thumbnails preserved during load-more
- [ ] Partial last row rebuilt correctly when appending
- [ ] Focus maintained on first new item after append
- [ ] No visible flicker or full grid rebuild on load-more

---

## Live Download Progress (Detail View)

- [ ] Download progress updates live on chapter rows
- [ ] Progress percentage shows during active download
- [ ] Download state icon updates (queued/downloading/done)
- [ ] Progress updates throttled (~500ms)
- [ ] Page-level progress tracking works
- [ ] Multiple simultaneous chapter downloads show progress

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

---

## Reader - Pinch-to-Zoom

- [ ] Two-finger pinch zooms in/out
- [ ] Zoom updates live during pinch (not just on release)
- [ ] Zoom centers on midpoint between fingers
- [ ] Zoom range: 0.5x to 4x
- [ ] Near-1x zoom snaps back to fit on release
- [ ] Page swipe suppressed during active pinch
- [ ] Page turn doesn't trigger when releasing pinch
- [ ] Pinch works while already zoomed (combines with existing zoom)

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

---

## Reader - Background Color

- [ ] Black background works
- [ ] White background works
- [ ] Gray background works
- [ ] Background color applies to page margins
- [ ] Background color setting persists

---

## Reader - Error Handling

- [ ] Failed page load shows error overlay
- [ ] Error overlay displays message text
- [ ] Retry button appears on error overlay
- [ ] Retry button reloads the failed page
- [ ] Stale error overlays don't show for already-loaded pages
- [ ] Error overlay hides when page loads successfully

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

## Reader - Per-Manga Settings

- [ ] Reader settings saved per manga (keyed by manga ID)
- [ ] Custom reading mode per manga works
- [ ] Custom page scale per manga works
- [ ] Custom rotation per manga works
- [ ] Custom crop borders per manga works
- [ ] Custom webtoon side padding per manga works
- [ ] Manga without custom settings use global defaults
- [ ] Per-manga settings sync to server meta keys

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

## Reader - Continue Reading Improvements

- [ ] Clicking a chapter resumes from lastPageRead (not page 0)
- [ ] Also checks DownloadsManager for more recent offline progress
- [ ] Reader saves state (chapter ID, page, read status) to ReaderResult on close
- [ ] MangaDetailView::willAppear consumes ReaderResult to update UI immediately
- [ ] Continue reading button reflects just-finished session without server refresh
- [ ] Offline continue reading path merges local progress from DownloadsManager
- [ ] Continue reading button updates correctly when offline
- [ ] onRead falls back to DownloadsManager progress when offline

---

## Reader - Webtoon Mode

### Auto-Detection
- [ ] Auto-detect toggle in settings works
- [ ] Detected webtoons use vertical/webtoon mode
- [ ] Detected webtoons use fit-width scaling
- [ ] isWebtoonFormat flag saved per manga
- [ ] Manual override of detected settings works
- [ ] Disabling auto-detect uses user's default settings for all manga

### Continuous Scroll
- [ ] Seamless vertical scrolling between pages
- [ ] Momentum scrolling with friction works
- [ ] Progressive image loading as user scrolls
- [ ] Side padding setting applies (0-20%)
- [ ] Tap callback toggles controls in webtoon mode
- [ ] Page progress tracks correctly during scroll
- [ ] Rotation applies in webtoon mode

### Tall Image Segmentation (Quality Fix)
- [ ] Tall images (height > 2048px, width <= 2048) auto-segment
- [ ] Segment 0 loaded at full width quality (not crushed)
- [ ] Additional segments created and loaded for remaining portions
- [ ] All segments stacked vertically seamlessly (no gaps)
- [ ] Page height calculated from original image dimensions
- [ ] Segments unloaded when page scrolls out of range (memory management)
- [ ] Works for WebP images
- [ ] Works for JPEG images
- [ ] Works for PNG images
- [ ] Short images (height <= 2048) still load normally (no regression)
- [ ] Segment metadata (totalSegments, originalWidth, originalHeight) set correctly

### Webtoon Settings
- [ ] Crop borders toggle works
- [ ] Side padding selector works (None/5%/10%/15%/20%)
- [ ] Webtoon settings persist after restart
- [ ] Webtoon settings save per-manga

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

### Queue & Progress
- [ ] Queue chapter download locally works
- [ ] Local download progress displays
- [ ] Downloaded page count updates (e.g., 5/20 pages)
- [ ] Local download completes with all pages

### State Management
- [ ] QUEUED state shows correctly
- [ ] DOWNLOADING state shows correctly
- [ ] PAUSED state shows correctly
- [ ] COMPLETED state shows correctly
- [ ] FAILED state shows correctly

### Offline Reading
- [ ] Locally downloaded chapters readable offline
- [ ] Local page paths resolve correctly
- [ ] Reading progress saved for local chapters
- [ ] Last read timestamp tracked

---

## Downloads Tab UI

### Queue Display
- [ ] Server queue section shows when items exist
- [ ] Local queue section shows when items exist
- [ ] Empty sections are hidden
- [ ] Queue items show manga title
- [ ] Queue items show chapter name
- [ ] Queue items show download progress

### Control Buttons
- [ ] Start button starts/resumes downloads
- [ ] Stop button stops downloads
- [ ] Pause button pauses downloads
- [ ] Clear button clears completed downloads
- [ ] Buttons work with controller (A button)
- [ ] Buttons work with touch input

### Navigation
- [ ] D-pad navigation through queue items works
- [ ] Navigation between server and local queues works
- [ ] UP from first queue item goes to control buttons
- [ ] DOWN from control buttons goes to first queue item

### Status Display
- [ ] Download status shows correctly (Downloading, Paused, etc.)
- [ ] Progress percentage shows for active downloads
- [ ] Queue count shows in section headers

---

## Downloads - Settings & Options

### Download Mode
- [ ] Server Only mode queues to server
- [ ] Local Only mode downloads to device
- [ ] Both mode downloads to server and device
- [ ] Download mode setting persists
- [ ] Download mode display string shows correctly

### Other Settings
- [ ] Concurrent downloads setting (1-4) works
- [ ] WiFi-only downloads toggle works
- [ ] Auto-download new chapters toggle works
- [ ] Auto-resume downloads on startup works
- [ ] Delete after reading toggle works
- [ ] Paused downloads stay paused on restart
- [ ] Failed downloads don't auto-resume

---

## Downloads - Storage Management

- [ ] Storage view accessible from settings
- [ ] Total storage usage displays
- [ ] Cache size displays separately
- [ ] Per-manga storage breakdown shows
- [ ] File sizes formatted correctly (KB/MB/GB)
- [ ] Delete specific manga downloads works
- [ ] Delete only read chapters option works
- [ ] Clear all downloads option works
- [ ] Clear cache option works

---

## History Tab

### Display & Loading
- [ ] History tab displays correctly
- [ ] Reading history loads on tab open
- [ ] Loading indicator shows while fetching
- [ ] History count shows in title (e.g., "Reading History (25)")
- [ ] Empty state shows when no history

### History Items
- [ ] Items grouped by date (Today, Yesterday, day name, or date)
- [ ] Each item shows manga cover thumbnail
- [ ] Each item shows manga title (truncated if long)
- [ ] Each item shows chapter name or number
- [ ] Each item shows page progress (e.g., "Page 5/20")
- [ ] Relative timestamps display ("2 mins ago", "3 hours ago")
- [ ] Resume indicator arrow shows on each item

### Navigation & Controls
- [ ] D-pad navigation through history items works
- [ ] UP from first item focuses refresh button
- [ ] DOWN from refresh button focuses first item
- [ ] Triangle button triggers refresh
- [ ] Refresh button in header works (touch)
- [ ] Click/A button resumes reading from last page
- [ ] Start button opens context menu

### Context Menu Options
- [ ] Continue Reading option works
- [ ] View Manga Details option works
- [ ] Mark as Unread option works (removes from history)

### Infinite Scroll
- [ ] Auto-loads more items when focus reaches near end
- [ ] Loading guard prevents concurrent loads
- [ ] New items appear seamlessly
- [ ] No "Load More" button visible

### Refresh
- [ ] Refresh clears and reloads history
- [ ] Focus moves to refresh button during refresh (prevents crash)
- [ ] History reloads from server

---

## Tracking Integration

### Tracker Setup
- [ ] MyAnimeList login works
- [ ] AniList login works
- [ ] OAuth authentication completes
- [ ] Tracker credentials persist

### Tracker Login in Settings
- [ ] "Tracking" section appears in settings
- [ ] "Tracker Accounts" cell opens tracker list
- [ ] All trackers fetched from server and listed
- [ ] Login status shown per tracker (logged in / not logged in)
- [ ] Not-logged-in tracker: opens username/password dialogs
- [ ] Credential login sends to server
- [ ] Success: shows notification and refreshes tracker list
- [ ] Failure: shows error notification
- [ ] Logged-in tracker: offers Logout option
- [ ] Logout sends logoutTracker to server

### Tracker Features
- [ ] Search tracker database works
- [ ] Bind manga to tracker entry works
- [ ] Tracker search shows covers and details
- [ ] Reading progress syncs automatically
- [ ] Update reading status works (Reading, Completed, etc.)
- [ ] Set scores/ratings works
- [ ] Set start date works
- [ ] Set finish date works
- [ ] View tracked items works
- [ ] Unbind tracker works

### Tracker Action Dropdown
- [ ] Tap tracked manga shows action dropdown
- [ ] "View Details" shows tracking info (status, progress, score)
- [ ] "Remove Tracking" option opens remove dialog

### Remove Tracking Dialog
- [ ] "Remove from app only" option works
- [ ] "Remove from [tracker] too" option works
- [ ] Confirmation dialog shows before removing
- [ ] Cancel button aborts removal

---

## Manga Migration

- [ ] Migration option in context menu opens search view
- [ ] Full-screen search for alternate sources
- [ ] Results grouped by source
- [ ] Source filtering in migration view
- [ ] Migration confirmation before executing
- [ ] Reading progress preserved after migration

---

## Reading Statistics

- [ ] Statistics display in settings/stats view
- [ ] Total chapters read counter increments
- [ ] Total manga completed counter increments
- [ ] Current reading streak calculates correctly (days)
- [ ] Longest reading streak tracks all-time max
- [ ] Last read date updates on chapter read
- [ ] Total reading time accumulates (estimated)
- [ ] Statistics persist after restart
- [ ] Reset statistics works
- [ ] Statistics sync from server works

---

## Backup & Restore

- [ ] Export library backup works
- [ ] Import library from backup works
- [ ] Backup file validation works
- [ ] Automatic backup naming with timestamps

---

## Infinite Scroll

### Browse Tab (SearchTab)
- [ ] No "Load More" button visible
- [ ] Auto-loads next page when scrolling near end of grid
- [ ] Loading guard prevents concurrent page loads
- [ ] Works for Popular mode
- [ ] Works for Latest mode
- [ ] Works for Search Results mode
- [ ] New items appended without full grid rebuild (appendItems)

### RecyclingGrid onEndReached
- [ ] Callback fires when focus is within 2 rows of end
- [ ] End-reached flag prevents repeated firing
- [ ] Flag resets on setDataSource() or appendItems()

---

## Offline Mode - Login & Reconnect

### Offline Button on Login Page
- [ ] "Offline" button visible on login page
- [ ] Tapping Offline saves entered server URL / credentials
- [ ] Tapping Offline marks app as disconnected
- [ ] App navigates to main activity in offline mode
- [ ] Login page pre-fills saved connection details on next launch

### Reconnect in Settings
- [ ] Connection status indicator shows "Connected" or "Offline"
- [ ] "Reconnect" button appears in settings
- [ ] Reconnect attempts to connect to saved server URL asynchronously
- [ ] Status label updates on successful reconnect
- [ ] Reconnect failure doesn't crash app

---

## Offline Mode - Reader

### Offline Reading
- [ ] Locally downloaded chapters load from disk when offline
- [ ] No timeout error overlay for locally downloaded pages
- [ ] Chapter progress saved to DownloadsManager when offline (not server)
- [ ] Reader shows "App is offline" on retry button when offline
- [ ] Per-manga reader settings load from local cache when offline
- [ ] Skip server API calls (fetchMangaMeta, fetchManga, etc.) when offline
- [ ] Skip markChapterAsRead when offline

### Chapter ID Matching
- [ ] Downloaded chapters found by both chapterIndex and chapterId
- [ ] isChapterDownloaded matches on both identifiers
- [ ] getChapterPages matches on both identifiers
- [ ] getPagePath matches on both identifiers
- [ ] queueChapterDownload duplicate check uses both identifiers

---

## Offline Mode - Library

### Library Offline Behavior
- [ ] Library falls back to cached data when server unreachable
- [ ] Connection failure detected and isConnected set to false
- [ ] No cascading retry loops when offline
- [ ] Skip network fetch when offline and cached data available

### Library Grouping Offline
- [ ] "No Grouping" mode aggregates all cached category manga when offline
- [ ] "By Source" mode groups cached manga by source name when offline
- [ ] "By Category" loads categories from cache when offline
- [ ] Manga deduplicated across categories
- [ ] sourceName field cached in manga serialization
- [ ] By Source shows correct source names (not "Unknown") offline

### Category Visibility Offline
- [ ] Empty categories hidden when offline in downloads-only mode
- [ ] Categories with no locally downloaded manga hidden when offline
- [ ] Categories with no cached data hidden when offline

---

## Offline Mode - Browse & Extensions

### Browse Tab Offline
- [ ] Shows "App is offline" message instead of attempting server fetch
- [ ] No crash when navigating browse tab offline

### Extensions Tab Offline
- [ ] Shows inline "App is offline" message on extensions page
- [ ] Recycler hidden, message shown directly in content area

---

## Offline Mode - UI Element Hiding

### Library View When Offline
- [ ] Update/refresh button hidden when offline
- [ ] Select button hint icon hidden when offline
- [ ] Update button hidden at construction time if starting offline
- [ ] Update button and hint restore when back online (onFocusGained)
- [ ] triggerLibraryUpdate() shows notification and returns early when offline
- [ ] START button context menu blocked entirely when offline
- [ ] Long-press context menu blocked entirely when offline
- [ ] Select button action disabled when offline

### Book Detail View When Offline
- [ ] "Add/Remove from Library" button hidden when offline
- [ ] "Tracking" button hidden when offline
- [ ] Download all, Download unread, Cancel downloading, Reset cover hidden offline
- [ ] "Remove all chapters" still visible offline
- [ ] Chapter "Mark Read/Unread" hidden when offline
- [ ] Chapter "Download" hidden when offline
- [ ] Chapter "Delete Download" still visible for downloaded chapters offline
- [ ] Action-ID mapping ensures menu dispatch works with hidden items

---

## Offline Progress Sync

### Save Progress Offline
- [ ] updateProgress() saves to DownloadsManager when offline (not server)
- [ ] Reading progress persists to disk via DownloadsManager
- [ ] Chapter lastPageRead and lastReadAt stored locally

### Sync on Reconnect
- [ ] App boot: syncs all local reading progress to server when connection restored
- [ ] Bidirectional sync: compares local vs server progress, takes more advanced position
- [ ] Local-ahead progress pushed to server
- [ ] Server-ahead progress pulled to local downloads
- [ ] Chapters marked read on server when user reached last page offline
- [ ] Uses chapterId (not chapterIndex) for API calls

### Settings Sync
- [ ] "Sync Progress Now" button in settings runs async (non-blocking)
- [ ] Uses bidirectional syncProgressFromServer
- [ ] Notification shows when sync completes

### Per-Book Sync
- [ ] Opening a book online triggers progress comparison
- [ ] Local-ahead progress pushed to server per chapter
- [ ] Server-ahead progress pulled to local per chapter

---

## Chapter Caching & Downloads-Only Mode

### Chapter Caching
- [ ] Chapter list cached after fetch from server
- [ ] Chapter list loads from cache before server request (faster display)
- [ ] Cached chapters available when offline
- [ ] Cache updates after fresh server fetch

### Downloads-Only Mode
- [ ] "Downloads Only Mode" toggle in library settings works
- [ ] When enabled + offline, library shows only manga with local downloads
- [ ] When enabled + offline, chapter list filters to downloaded chapters
- [ ] When online, all manga and chapters show normally (setting ignored)
- [ ] Setting persists across restarts

---

## Settings - Connection

- [ ] Server URL setting persists
- [ ] Username setting persists
- [ ] Password setting persists
- [ ] Test connection button works
- [ ] Clear cache option works
- [ ] Local URL configuration works
- [ ] Remote URL configuration works
- [ ] Connection timeout configurable (in seconds)

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

## Settings - Library

- [ ] Display mode setting persists
- [ ] Grid size setting persists
- [ ] Cache library data toggle works
- [ ] Cache cover images toggle works
- [ ] Default sort mode setting works
- [ ] Chapter sort order setting works
- [ ] Library update on start toggle works
- [ ] WiFi only restriction applies to startup update
- [ ] Update runs in background without blocking UI

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

## Smoke Test (Quick Validation)

### Core Functionality
- [ ] App launches successfully
- [ ] Connect to Suwayomi server
- [ ] Library categories load and display
- [ ] Select a category and view manga
- [ ] Open manga detail view
- [ ] Load chapter list
- [ ] Open reader and view pages
- [ ] Navigate pages with buttons
- [ ] Exit reader, progress saved
- [ ] Browse a source
- [ ] Search for manga
- [ ] Check extensions tab
- [ ] Check downloads tab
- [ ] Check history tab

### Reader Settings
- [ ] Open reader settings in chapter
- [ ] Change reading direction, verify it applies
- [ ] Change rotation, verify it applies
- [ ] Close reader and reopen same manga - settings preserved
- [ ] Open different manga - uses default settings
- [ ] Swipe to change pages - verify animation works

### Webtoon Detection
- [ ] Open a known webtoon/manhwa manga
- [ ] Verify auto-detection applies vertical mode + fit-width
- [ ] Open a regular manga - uses default RTL mode
- [ ] Toggle "Auto-Detect Webtoon" off
- [ ] Open webtoon again - now uses user defaults

### Webtoon Tall Image Quality
- [ ] Open webtoon with tall pages (e.g., full-strip WebP)
- [ ] Verify images look sharp at full width (not blurry/crushed)
- [ ] Short pages still display correctly
- [ ] Scroll through segmented pages - no gaps or visual artifacts

### Library Grouping Quick Test
- [ ] Open library, press Square to open grouping menu
- [ ] Switch to "No Grouping" - verify flat grid, no tabs, no L/R icons
- [ ] Switch to "By Source" - verify source tabs appear
- [ ] Switch to "By Category" - verify category tabs return
- [ ] No crash during any mode switch

### Pinch-to-Zoom Quick Test
- [ ] Open reader, place two fingers on screen
- [ ] Spread fingers apart - zoom increases live
- [ ] Pinch fingers together - zoom decreases live
- [ ] Release near 1x - snaps back to fit
- [ ] No accidental page turn after pinching

### Pull-to-Refresh Quick Test
- [ ] In library, pull down from category tabs
- [ ] Verify "Checking for new chapters" notification appears
- [ ] In library, pull down from grid (when at top)
- [ ] Verify update notification appears

### Caching Quick Test
- [ ] Enable cache in settings
- [ ] Load library, close and reopen app
- [ ] Verify library loads instantly from cache
- [ ] Disable cache, reopen - verify fresh server fetch

### Offline Mode Quick Test
- [ ] From login page, tap Offline - app enters main screen
- [ ] Library loads cached data, update button is hidden
- [ ] Open a downloaded manga - chapter list loads from cache
- [ ] Read a downloaded chapter - pages load from disk, no errors
- [ ] Go to settings, tap Reconnect - status changes to Connected
- [ ] Library update button reappears after reconnect

### Infinite Scroll Quick Test
- [ ] Browse a source, scroll to bottom - more manga loads automatically
- [ ] No "Load More" button visible
- [ ] Existing covers not reloaded during append
- [ ] Open History tab, scroll to bottom - more items load automatically

### Chapter Loading Quick Test
- [ ] Open a manga with 100+ chapters
- [ ] First chapters appear within 1-2 seconds
- [ ] Remaining chapters populate progressively (no long freeze)
- [ ] Can scroll and interact while chapters are still building

---

## Regression Tests (Edge Cases)

### Swipe Navigation Edge Cases
- [ ] Swipe at first page - no previous page preview, snaps back
- [ ] Swipe at last page - shows next chapter or end indicator
- [ ] Quick successive swipes don't break animation
- [ ] Swipe during page load - handles gracefully
- [ ] Swipe with rotation - directions adjusted correctly

### Settings Priority Edge Cases
- [ ] Server has settings, local cache has different settings - server wins
- [ ] Server unreachable - falls back to local cache
- [ ] No server settings, no local cache - uses global defaults
- [ ] Webtoon detected but has server settings - server settings win

### Chapter Navigation Edge Cases
- [ ] Last chapter, swipe to next - shows end indicator or loops
- [ ] First chapter, go to previous - shows start indicator
- [ ] Chapter with single page - navigation still works
- [ ] Chapter with 100+ pages - performance acceptable

### Download Edge Cases
- [ ] Download interrupted - resumes correctly
- [ ] Download while reading - doesn't affect reader
- [ ] Multiple concurrent downloads work
- [ ] Low storage handling

### Tracking Edge Cases
- [ ] Tracker auth expires - handles gracefully
- [ ] Tracker API error - shows user-friendly message
- [ ] Sync progress with no tracker bound - no crash
- [ ] JWT token expired - handles refresh or re-login

### Grouping Mode Edge Cases
- [ ] Switch grouping mode with empty library - no crash
- [ ] Switch from By Source to By Category with focus on source tab - focus transfers safely
- [ ] By Source mode with manga from only 1 source - single tab works
- [ ] Rapid grouping mode switches - no crash or stale data

### Pinch-to-Zoom Edge Cases
- [ ] Pinch while page is loading - no crash
- [ ] Pinch then immediately swipe - no page turn
- [ ] Pinch to 4x max zoom - clamped correctly
- [ ] Pinch to 0.5x min zoom - clamped correctly
- [ ] Pinch after double-tap zoom - combines correctly

### Cache Edge Cases
- [ ] Cache with 100+ manga - performance acceptable
- [ ] Cache invalidation when manga removed from library
- [ ] Cover cache with low storage - handles gracefully
- [ ] Switching categories fast with cache - no stale data

### Offline Mode Edge Cases
- [ ] Start offline, browse library, then reconnect - UI restores all buttons
- [ ] Start offline with no cached data - no crash, shows empty state
- [ ] Read chapter offline, close reader, reopen same chapter - progress preserved
- [ ] Sync progress with conflicting server data - bidirectional merge works
- [ ] Downloads-only mode with zero local downloads - empty library, no crash
- [ ] Rapidly toggle online/offline (reconnect/disconnect) - no crash
- [ ] Open manga detail offline, then reconnect mid-view - buttons remain hidden until next focus

### Infinite Scroll Edge Cases
- [ ] Scroll to end with no more pages (hasNextPage=false) - no extra load triggered
- [ ] Navigate away and back during load-more - no duplicate items
- [ ] Load-more with exactly a full last row - append starts new row
- [ ] Load-more with partial last row - partial row rebuilt correctly
- [ ] Rapid scroll past end - only one load-more fires (guard flag works)

### Chapter Loading Edge Cases
- [ ] Open manga with 0 chapters - no crash, empty list
- [ ] Open manga with 1 chapter - builds immediately, navigation works
- [ ] Open manga with 500+ chapters - builds incrementally, no freeze
- [ ] Navigate away during incremental build - build cancels safely
- [ ] Re-sort chapters during incremental build - old build cancels, new one starts
- [ ] Filter chapters during incremental build - restarts correctly
- [ ] D-pad navigation works after incremental build completes

### Webtoon Segmentation Edge Cases
- [ ] Very tall image (e.g., 800x15000) segments correctly into multiple tiles
- [ ] Normal height image (e.g., 800x1200) loads as single image (no regression)
- [ ] Image wider than 2048px still scales down appropriately
- [ ] Segmented page unloads all segments when scrolled out of range
- [ ] Background color and rotation apply to all segments
- [ ] Append/prepend chapters preserves segment data for existing pages
