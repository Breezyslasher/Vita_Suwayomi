# PS Vita Core Dump Analysis

**File:** `psp2core-1771826419-0x0024893519-eboot.bin.psp2dmp`
**App:** VitaSuwayomi (VSWY00001) v01.00
**Firmware:** 3.60 (`release_branches/release_03.600`)

---

## Crash Summary

The application crashed during a **previous chapter navigation** in the manga reader.
The coredump was triggered immediately after the user swiped backward from a chapter transition page.

## Crash Timeline (from TTY log)

```
00:59:50.078  MangaDetailView: Creating for 'A Brides Story' id=269
00:59:55.488  ReaderActivity: manga=269, chapter=31415, startPage=0
00:59:55.878  ReaderActivity: readingMode=1, pageScaleMode=0, rotation=90
00:59:55.992  ReaderActivity: loaded settings from server for manga 269
00:59:56.010  ReaderActivity: keepScreenOn enabled, disabled screen dimming
00:59:56.129  GraphQL: Fetched 6 pages for chapter 31415
00:59:56.284  loadPages: startPage=1 page 1/8 url=http://192.168.1.28:4568/api/v1/manga/269/chapter/98/page/0
00:59:56.296  Preloading next chapter id=31414
00:59:56.307  Preloading prev chapter id=31416
00:59:56.509  Next chapter preloaded: 25 pages
00:59:56.519  Prev chapter preloaded: 24 pages
01:00:05.986  NEXTPAGE: currentPage=1/8
01:00:08.305  PREVPAGE: currentPage=2/8
01:00:11.441  PREVPAGE: currentPage=1/8
01:00:15.261  TSWIPE START: page=0/8 url=__transition:prev
01:00:15.787  TSWIPE OOB: previewIdx=-1 wantNext=false swipeToChapter=true
01:00:16.496  TSWIPE END: swipeToChapter=true swipingToNext=false
01:00:16.506  COMPLETE: turnPage=true swipeToChapter=true swipingToNext=false
01:00:16.512  COMPLETE: taking CHAPTER path, swipingToNext=false
01:00:16.521  PREVCHAPTER: chapterPos=97/107 prevPreloaded=true    <-- LAST LOG
               [coredump] start ID=VSWY00001                      <-- CRASH
```

## Technical Details

### ELF Core Dump Structure
- **Format:** 32-bit ARM Little-Endian ELF Core (type 4, machine 40)
- **Program headers:** 113 (31 PT_NOTE segments, 82 PT_LOAD segments)
- **Decompressed size:** 10,563,568 bytes

### Crashing Thread
- **Thread UID:** 0x40010003 (main thread - "VSWY00001")
- **Exception Type:** 5
- **Stop Reason:** 1

### ARM Register State (at crash)
```
R0  = 0x81e00bc0   (user space - likely object pointer)
R1  = 0x00000000
R2  = 0xe003cb00   (kernel space)
R3  = 0xe003d224   (kernel space)
R4  = 0xe001d5a4   (kernel space)
R5  = 0x00000020   (32)
R6  = 0x00000021   (33)
R7  = 0x81e00c28   (user space)
R8  = 0x816382d0   (user space)
R9  = 0x000004e3   (1251)
R10 = 0x81e00bc0   (user space - same as R0)
R11 = 0xe000e295   (kernel space)
R12 = 0xe000a0c4   (kernel space)
SP  = 0x60070010   (likely CPSR: User mode, ZC flags set)
LR  = 0x03000000
PC  = 0x81619800   (eboot.bin + 0x619800)
```

Note: Several registers contain kernel-space addresses (0xe0xxxxxx), indicating
the crash occurred during or immediately after a kernel syscall/exception handler.

### Faulting Address
- **PC:** `0x81619800` - within the main eboot.bin module
- **Estimated eboot.bin base:** `0x81000000`
- **Offset in binary:** `0x619800` (~6.4 MB into the executable)

### Active Threads at Crash (18 total)
| Thread | Name |
|--------|------|
| 0x40010003 | VSWY00001 (main - **CRASHED**) |
| 0x400100c3 | SceCommonDialogWorker |
| 0x400100bf | callbackThread |
| 0x40010193 | SceGxmDisplayQueue |
| Multiple | pthread (image loader workers) |

### Loaded Modules
| Module | Text Segment | Size |
|--------|-------------|------|
| eboot.bin | 0x81000000 - ~0x81620000 | ~6.1 MB |
| SceAppUtil | 0x81620000 - 0x81629f64 | 39 KB |
| SceLibPgf | 0x816d0000 - 0x816d5cb4 | 23 KB |
| SceIme | 0x81684000 - 0x81686c28 | 11 KB |
| SceAvPlayer | 0x816a0000 - 0x816b49fc | 84 KB |
| SceShaccCg | 0x8df00000 - 0x8e1fe954 | 3 MB |
| PSMPatch | 0x81650000 - 0x816594dc | 38 KB |
| MusicNonStop | 0x81640000 - 0x816482cc | 33 KB |
| SceGxm | 0xe0065290 - 0xe0088f94 | 142 KB |
| SceLibHttp | 0xe064f1c0 - 0xe066c310 | 116 KB |
| SceLibSsl | 0xe068d140 - 0xe06f3fe4 | 411 KB |
| SceNet | 0xe0521170 - 0xe052d400 | 49 KB |

### Memory Layout
| Block | Address | Size |
|-------|---------|------|
| Newlib heap | 0x81f00000 | 192 MB |
| SceGpuUserHeap | 0x81638000 | 32 KB |
| SceLibsslGlobalHeap | 0x81b80000 | 32 KB |
| SceLibhttpGlobalHeap | 0x8e300000 | 128 KB |
| SceGxmDriver | 0x70000000 | 64 KB |
| GPU parameter buffer | 0x61000000 | 16 MB |
| GPU textures (gpumem) | Multiple | Multiple 128-256 KB blocks |

---

## Root Cause Analysis

### What happened
The crash occurred in `ReaderActivity::previousChapter()` at `reader_activity.cpp:1643-1667`.
The call path was:
```
completeSwipeAnimation() → previousChapter() → [CRASH]
```

### Most likely cause: Memory pressure during chapter transition

At the moment of crash, the application had significant memory allocations:
1. **Current chapter:** 6 pages loaded as GPU textures
2. **Next chapter preload:** 25 pages cached in memory
3. **Prev chapter preload:** 24 pages cached in memory
4. **Image cache:** LRU cache with up to 200 entries
5. **20 worker threads** for concurrent image loading

When `previousChapter()` is called, it:
1. Clears preloaded chapter data (`m_nextChapterPages.clear()`, `m_prevChapterPages.clear()`)
2. Clears current pages (`m_pages.clear()`)
3. Calls `loadPages()` which starts a new async HTTP fetch

The crash likely occurs during the memory reallocation caused by clearing and rebuilding
these large vectors, or during the `std::string` operations on chapter data, while the
system is under memory pressure from the accumulated texture/image cache.

### Key observation: `previousChapter()` does NOT use preloaded data

Unlike `nextChapter()` which efficiently reuses preloaded pages:
```cpp
// nextChapter() - uses preloaded data:
if (m_nextChapterLoaded && !m_nextChapterPages.empty()) {
    m_pages = std::move(m_nextChapterPages);  // Zero-copy!
    // ... immediate display
}
```

`previousChapter()` **always** discards preloaded data and re-fetches from scratch:
```cpp
// previousChapter() - wasteful:
m_prevChapterPages.clear();   // Throws away preloaded data!
m_pages.clear();
loadPages();                   // Re-fetches from server
```

This means during backward navigation:
- Peak memory = old pages + old preloads + new fetch buffers
- The large preloaded datasets are freed at the same time new allocations begin
- Memory fragmentation on PS Vita's limited RAM can cause allocation failures

### Secondary factors

1. **Thread safety with raw `this` capture:** `loadPages()` captures `this` directly in
   async callbacks. If a previous `loadPages()` callback fires during `previousChapter()`,
   it could modify `m_chapters`/`m_pages` concurrently.

2. **Large texture memory:** Each manga page at full resolution can be 1-6 MB as a GPU
   texture. With 55+ pages cached (6 + 25 + 24), that's potentially 50+ MB of texture data
   on a system with only 256 MB total user RAM.

3. **No memory limit on preloads:** Both `preloadNextChapter()` and `preloadPrevChapter()`
   preload the first/last 3 images at full size with no total memory budget.

---

## Recommended Fixes

### 1. Use preloaded data in `previousChapter()` (HIGH PRIORITY)
```cpp
void ReaderActivity::previousChapter() {
    if (m_chapterPosition > 0) {
        m_chapterPosition--;
        m_chapterIndex = m_chapters[m_chapterPosition].id;
        m_chapterName = m_chapters[m_chapterPosition].name;
        m_nextChapterLoaded = false;
        m_nextChapterPages.clear();
        m_currentPage = 0;

        // USE preloaded pages instead of re-fetching
        if (m_prevChapterLoaded && !m_prevChapterPages.empty()) {
            m_pages = std::move(m_prevChapterPages);
            m_prevChapterLoaded = false;
            // ... (same as nextChapter's preloaded path)
        } else {
            m_startPage = 0;
            m_pages.clear();
            loadPages();
        }
    }
}
```

### 2. Reduce memory pressure
- Limit the LRU image cache size (currently 200 entries)
- Only preload 1 image per adjacent chapter instead of 3
- Free current page textures before loading new chapter

### 3. Add bounds checking
```cpp
void ReaderActivity::previousChapter() {
    if (m_chapterPosition > 0 &&
        m_chapterPosition - 1 < static_cast<int>(m_chapters.size())) {
        // ... safe to access m_chapters[m_chapterPosition - 1]
    }
}
```

### 4. Guard async callbacks
Ensure `loadPages()` callbacks check that `m_chapters` hasn't been invalidated
before modifying shared state.

---

## How to Debug Further

To map `PC=0x81619800` to a specific source line, rebuild eboot.bin with debug symbols:
```bash
# In CMakeLists.txt, add:
set(CMAKE_BUILD_TYPE Debug)
# Or add -g flag to compile options

# Then use vita-parse-core or arm-vita-eabi-addr2line:
arm-vita-eabi-addr2line -e eboot.elf -f 0x619800
```

This will show the exact function and line number where the crash occurred.
