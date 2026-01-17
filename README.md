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
- [ ] Page navigation works (left/right)
- [ ] Page counter shows current/total
- [ ] Tap to show/hide controls works
- [ ] Exit reader returns to detail view
- [ ] Progress saves when exiting

## Reader - Navigation

- [ ] Swipe to change pages (if supported)
- [ ] D-pad left/right changes pages
- [ ] First page reached shows indicator
- [ ] Last page reached shows indicator
- [ ] Go to next chapter works
- [ ] Go to previous chapter works

## Reader - Progress

- [ ] Last page read saves to server
- [ ] Resume at last read page works
- [ ] Finishing chapter marks as read
- [ ] Progress syncs correctly

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

## Settings

- [ ] Server URL setting persists
- [ ] Username setting persists
- [ ] Password setting persists
- [ ] Test connection button works
- [ ] Clear cache option works

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

## Smoke Test (Quick Validation ~5 minutes)

1. [ ] App launches successfully
2. [ ] Connect to Suwayomi server
3. [ ] Library categories load and display
4. [ ] Select a category and view manga
5. [ ] Open manga detail view
6. [ ] Load chapter list
7. [ ] Open reader and view pages
8. [ ] Navigate pages in reader
9. [ ] Exit reader, progress saved
10. [ ] Browse a source
11. [ ] Search for manga
12. [ ] Check extensions tab
13. [ ] Check downloads tab
14. [ ] Verify settings persist after restart

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
