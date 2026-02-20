---
name: Testing Checklist V2 - New Features
about: Testing checklist for new features added since v1
title: "[Testing] V2 New Features Checklist"
labels: testing, v2
assignees: ''
---

## Status Legend
| Symbol | Status |
|--------|--------|
| ✓ | Working |
| X | Broken |
| _(unchecked)_ | Untested |

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

## Per-Category Sort Modes

- [ ] Sort setting saved per category
- [ ] Different categories can have different sort modes
- [ ] Switching categories restores that category's sort
- [ ] Default sort mode applies to categories without a custom sort
- [ ] Per-category sort persists after restart

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

## Authentication Modes

### Auth Mode Selection
- [ ] Auth mode cycles through: None, Basic Auth, Simple Login, UI Login
- [ ] Selected auth mode displays on login screen
- [ ] Wrong auth mode shows helpful error message

### JWT Authentication (Simple Login / UI Login)
- [ ] JWT login sends username/password and receives tokens
- [ ] Access token stored and used for API calls
- [ ] Refresh token stored for token renewal
- [ ] JWT tokens persist after restart
- [ ] Server JWT support auto-detected
- [ ] Basic Auth selected but server uses JWT shows guidance

---

## Pinch-to-Zoom (Reader)

- [ ] Two-finger pinch zooms in/out
- [ ] Zoom updates live during pinch (not just on release)
- [ ] Zoom centers on midpoint between fingers
- [ ] Zoom range: 0.5x to 4x
- [ ] Near-1x zoom snaps back to fit on release
- [ ] Page swipe suppressed during active pinch
- [ ] Page turn doesn't trigger when releasing pinch
- [ ] Pinch works while already zoomed (combines with existing zoom)

---

## Reader Error Handling

- [ ] Failed page load shows error overlay
- [ ] Error overlay displays message text
- [ ] Retry button appears on error overlay
- [ ] Retry button reloads the failed page
- [ ] Stale error overlays don't show for already-loaded pages
- [ ] Error overlay hides when page loads successfully

---

## Reader Background Color

- [ ] Black background works
- [ ] White background works
- [ ] Gray background works
- [ ] Background color applies to page margins
- [ ] Background color setting persists

---

## Per-Manga Reader Settings

- [ ] Reader settings saved per manga (keyed by manga ID)
- [ ] Custom reading mode per manga works
- [ ] Custom page scale per manga works
- [ ] Custom rotation per manga works
- [ ] Custom crop borders per manga works
- [ ] Custom webtoon side padding per manga works
- [ ] Per-manga settings override global defaults
- [ ] Manga without custom settings use global defaults
- [ ] Per-manga settings sync to server meta keys

---

## Webtoon Auto-Detection

- [ ] Auto-detect toggle in settings works
- [ ] Detected webtoons use vertical/webtoon mode
- [ ] Detected webtoons use fit-width scaling
- [ ] isWebtoonFormat flag saved per manga
- [ ] Manual override of detected settings works
- [ ] Disabling auto-detect uses user's default settings for all manga

---

## Webtoon Continuous Scroll

- [ ] Seamless vertical scrolling between pages
- [ ] Momentum scrolling with friction works
- [ ] Progressive image loading as user scrolls
- [ ] Tall page splitting works
- [ ] Side padding setting applies (0-20%)
- [ ] Tap callback toggles controls in webtoon mode
- [ ] Page progress tracks correctly during scroll
- [ ] Rotation applies in webtoon mode

---

## Download Swipe-to-Delete

- [ ] Swipe left on download queue item reveals delete
- [ ] Swipe threshold prevents accidental deletes
- [ ] Visual feedback during swipe (row slides)
- [ ] Completing swipe removes item from queue
- [ ] Partial swipe snaps back
- [ ] Works for server download queue items
- [ ] Works for local download queue items

---

## Chapter Row Swipe Gesture (Detail View)

- [ ] Swipe left on chapter row for quick action
- [ ] Swipe right on chapter row for quick action
- [ ] Swipe state tracks correctly per row
- [ ] Visual feedback during swipe

---

## Live Download Progress (Detail View)

- [ ] Download progress updates live on chapter rows
- [ ] Progress percentage shows during active download
- [ ] Download state icon updates (queued/downloading/done)
- [ ] Progress updates throttled (not every byte, ~500ms)
- [ ] Page-level progress tracking works
- [ ] Multiple simultaneous chapter downloads show progress

---

## Storage Management View

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

## Local Downloads

### Queue & Progress
- [ ] Queue chapter for local download works
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

## Download Mode Setting

- [ ] Server Only mode queues to server
- [ ] Local Only mode downloads to device
- [ ] Both mode downloads to server and device
- [ ] Download mode setting persists
- [ ] Download mode display string shows correctly

---

## Auto-Resume Downloads

- [ ] Auto-resume toggle in settings works
- [ ] On app start, queued downloads auto-resume when enabled
- [ ] Paused downloads stay paused on restart
- [ ] Failed downloads don't auto-resume

---

## Library Update on Start

- [ ] "Update on Start" toggle works in settings
- [ ] When enabled, library update triggers on app launch
- [ ] "WiFi only" restriction applies to startup update
- [ ] Update runs in background without blocking UI

---

## Search History

- [ ] Searches automatically saved to history
- [ ] History button appears in search header
- [ ] History dialog shows recent searches
- [ ] Tapping history item re-executes search
- [ ] Clear search history works
- [ ] Max history limit configurable (default 20)
- [ ] History persists after restart

---

## Tracking - Advanced Features

### Tracker Action Dropdown
- [ ] Tap tracked manga shows action dropdown
- [ ] "View Details" shows tracking info (status, progress, score)
- [ ] "Remove Tracking" option opens remove dialog

### Remove Tracking Dialog
- [ ] "Remove from app only" option works
- [ ] "Remove from [tracker] too" option works
- [ ] Confirmation dialog shows before removing
- [ ] Cancel button aborts removal

### Tracker Search & Bind
- [ ] Tracker search input dialog opens
- [ ] Search results show with covers and details
- [ ] TrackingSearchResultCell displays title, description, status
- [ ] Selecting result binds manga to tracker entry

### Tracker Edit Dialog
- [ ] Edit tracking status works
- [ ] Edit reading progress works
- [ ] Edit score/rating works
- [ ] Single tracker auto-selects (skip tracker picker)

---

## Manga Migration

### Migration Search View
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

## Source Language Filtering

- [ ] Language filter dialog opens from settings
- [ ] Multiple languages can be selected
- [ ] Empty selection means all languages shown
- [ ] Filter applies to source list display
- [ ] Language filter persists after restart
- [ ] Language filter text updates in settings cell

---

## Horizontal Scroll for Category/Source Tabs

- [ ] Category tabs scroll horizontally via touch drag
- [ ] Source tabs scroll horizontally via touch drag
- [ ] D-pad left/right navigates between tabs
- [ ] scrollToView() keeps selected tab visible
- [ ] Scroll offset resets when tabs are rebuilt

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

## Network - URL Switching

### Local/Remote Toggle
- [ ] Local URL configuration works
- [ ] Remote URL configuration works
- [ ] Toggle between local and remote URLs
- [ ] Active URL indicator shows which is in use

### Auto-Switch on Failure
- [ ] Auto-switch toggle in settings works
- [ ] On connection failure, tries alternate URL
- [ ] Switch notification shows when URL changes
- [ ] tryAlternateUrl() returns correct result

### Connection Timeout
- [ ] Connection timeout configurable (in seconds)
- [ ] Timeout applies to API requests
- [ ] Timeout setting persists

---

## Offline Mode - Login & Reconnect

### Offline Button on Login Page
- [ ] "Offline" button visible on login page
- [ ] Tapping Offline saves entered server URL / credentials
- [ ] Tapping Offline marks app as disconnected
- [ ] App navigates to main activity in offline mode
- [ ] Login page pre-fills saved connection details on next launch (URL, username, auth mode)

### Reconnect in Settings
- [ ] Connection status indicator shows "Connected" or "Offline"
- [ ] "Reconnect" button appears in settings
- [ ] Reconnect attempts to connect to saved server URL asynchronously
- [ ] Status label updates on successful reconnect
- [ ] Reconnect failure doesn't crash app

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
- [ ] When enabled + offline, chapter list in detail view filters to downloaded chapters
- [ ] When online, all manga and chapters show normally (setting ignored)
- [ ] Setting persists across restarts

---

## Offline Mode - Reader

### Offline Reading
- [ ] Locally downloaded chapters load from disk when offline
- [ ] No 15s timeout error overlay for locally downloaded pages
- [ ] Chapter progress saved to DownloadsManager when offline (not server)
- [ ] Reader shows "App is offline" on retry button when offline
- [ ] Per-manga reader settings load from local cache when offline
- [ ] Skip fetchMangaMeta, fetchManga, fetchChapter, fetchChapters when offline
- [ ] Skip markChapterAsRead when offline

### Chapter ID Matching
- [ ] Downloaded chapters found by both chapterIndex and chapterId
- [ ] isChapterDownloaded matches on both identifiers
- [ ] getChapterPages matches on both identifiers
- [ ] getPagePath matches on both identifiers
- [ ] queueChapterDownload duplicate check uses both identifiers
- [ ] cancelChapterDownload uses both identifiers
- [ ] deleteChapterDownload uses both identifiers

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
- [ ] Manga deduplicated across categories (std::set)
- [ ] sourceName field cached in manga serialization (field 12)
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
- [ ] Select button hint icon hidden when offline (entire container)
- [ ] Update button hidden at construction time if starting offline
- [ ] Update button and hint restore when back online (onFocusGained)
- [ ] triggerLibraryUpdate() shows notification and returns early when offline
- [ ] START button context menu blocked entirely when offline
- [ ] Long-press context menu blocked entirely when offline
- [ ] Select button (BUTTON_BACK) action disabled when offline

### Book Detail View When Offline
- [ ] "Add/Remove from Library" button hidden when offline
- [ ] "Tracking" button hidden when offline
- [ ] Options menu: Download all, Download unread, Cancel downloading, Reset cover hidden offline
- [ ] Options menu: "Remove all chapters" still visible offline
- [ ] Chapter menu: "Mark Read/Unread" hidden when offline
- [ ] Chapter menu: "Download" hidden when offline
- [ ] Chapter menu: "Delete Download" still visible for downloaded chapters offline
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

## Continue Reading Improvements

### Chapter Progress Preservation
- [ ] Clicking a chapter resumes from lastPageRead (not page 0)
- [ ] Also checks DownloadsManager for more recent offline progress
- [ ] Reader saves state (chapter ID, page, read status) to ReaderResult on close
- [ ] MangaDetailView::willAppear consumes ReaderResult to update UI immediately
- [ ] Continue reading button reflects just-finished session without server refresh

### Offline Continue Reading
- [ ] loadChapters offline path merges local progress from DownloadsManager
- [ ] Continue reading button updates correctly when offline
- [ ] onRead falls back to DownloadsManager progress when m_chapters empty + offline

---

## Infinite Scroll

### Browse Tab (SearchTab)
- [ ] No "Load More" button visible
- [ ] Auto-loads next page when scrolling near end of grid
- [ ] m_isLoadingPage guard prevents concurrent page loads
- [ ] Works for Popular mode
- [ ] Works for Latest mode
- [ ] Works for Search Results mode
- [ ] New items appended without full grid rebuild (appendItems)

### History Tab
- [ ] No "Load More" button visible
- [ ] Auto-loads more items when focus reaches within 5 items of end
- [ ] m_isLoadingMore guard prevents concurrent loads
- [ ] New items appear seamlessly

### RecyclingGrid onEndReached
- [ ] Callback fires when focus is within 2 rows of end
- [ ] m_endReachedFired flag prevents repeated firing
- [ ] Flag resets on setDataSource() or appendItems()

---

## Tracker Login in Settings

### Tracking Section
- [ ] "Tracking" section appears in settings
- [ ] "Tracker Accounts" cell opens tracker list
- [ ] All trackers fetched from server and listed
- [ ] Login status shown per tracker (logged in / not logged in)

### Login Flow
- [ ] Not-logged-in tracker: opens username IME dialog
- [ ] After username: opens password IME dialog
- [ ] Credential login sends to server (loginTrackerCredentials)
- [ ] Success: shows notification and refreshes tracker list
- [ ] Failure: shows error notification

### Logout Flow
- [ ] Logged-in tracker: offers Logout option
- [ ] Logout sends logoutTracker to server
- [ ] Success: shows notification and refreshes list
- [ ] authUrl field fetched for each tracker (for OAuth trackers)

---

## Chapter Loading Performance

### Incremental Chapter Building
- [ ] Chapter list builds incrementally (not all at once)
- [ ] First 10 chapters appear immediately
- [ ] Remaining chapters build in batches of 10 per frame via brls::sync()
- [ ] No 30-40 second UI freeze on large chapter lists (300+ chapters)
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

## Smoke Test - V2 Features

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

## Regression Tests - V2 Edge Cases

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

### Authentication Edge Cases
- [ ] JWT token expired - handles refresh or re-login
- [ ] Switch auth mode while connected - reconnects
- [ ] Server changes auth requirement - detected on next request

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
- [ ] Load-more with exactly a full last row (e.g., 24 items, 6 columns) - append starts new row
- [ ] Load-more with partial last row (e.g., 22 items, 6 columns) - partial row rebuilt correctly
- [ ] Rapid scroll past end - only one load-more fires (guard flag works)

### Chapter Loading Edge Cases
- [ ] Open manga with 0 chapters - no crash, empty list
- [ ] Open manga with 1 chapter - builds immediately, navigation works
- [ ] Open manga with 500+ chapters - builds incrementally, no freeze
- [ ] Navigate away during incremental build - build cancels safely
- [ ] Re-sort chapters during incremental build - old build cancels, new one starts
- [ ] Filter chapters during incremental build - restarts correctly
- [ ] D-pad navigation works after incremental build completes
