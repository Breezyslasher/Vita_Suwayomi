/**
 * VitaSuwayomi - Recycling Grid implementation
 * Uses lazy loading to prevent UI freezes on large libraries
 */

#include "view/recycling_grid.hpp"
#include "view/media_item_cell.hpp"
#include "view/long_press_gesture.hpp"
#include "utils/perf_overlay.hpp"
#include "app/application.hpp"
#include <cmath>

namespace vitasuwayomi {

RecyclingGrid::RecyclingGrid() {
    m_alive = std::make_shared<bool>(true);
    this->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    // Content box to hold all rows
    m_contentBox = new brls::Box();
    m_contentBox->setAxis(brls::Axis::COLUMN);
    m_contentBox->setPadding(10);
    this->setContentView(m_contentBox);

    // PS Vita screen: 960x544, use 6 columns
    m_columns = 6;

    // Register action for pull-to-refresh when at top
    // When user is at top and presses up on D-pad, trigger refresh
    this->registerAction("Refresh", brls::ControllerButton::BUTTON_BACK, [this](brls::View*) {
        // Back/Select button triggers refresh when in this view
        if (m_onPullToRefresh) {
            m_onPullToRefresh();
        }
        return true;
    });

    // Add swipe-down gesture for pull-to-refresh (touchscreen support)
    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
            static brls::Point touchStart;
            static bool isValidPull = false;

            if (status.state == brls::GestureState::START) {
                touchStart = status.position;
                isValidPull = false;
                m_isPulling = false;
                m_pullDistance = 0.0f;
            } else if (status.state == brls::GestureState::STAY) {
                float dx = status.position.x - touchStart.x;
                float dy = status.position.y - touchStart.y;

                // Only consider vertical swipes downward when near top of scroll
                float scrollY = this->getContentOffsetY();
                if (dy > 0 && std::abs(dy) > std::abs(dx) * 1.5f && scrollY <= 5.0f) {
                    isValidPull = true;
                    m_isPulling = true;
                    m_pullDistance = dy;
                }
            } else if (status.state == brls::GestureState::END) {
                float dy = status.position.y - touchStart.y;

                // Trigger refresh if pulled down past threshold
                if (isValidPull && dy > PULL_THRESHOLD && m_onPullToRefresh) {
                    m_onPullToRefresh();
                }

                // Reset state
                m_isPulling = false;
                m_pullDistance = 0.0f;
                isValidPull = false;
            }
        },
        brls::PanAxis::VERTICAL));
}

RecyclingGrid::~RecyclingGrid() {
    if (m_alive) {
        *m_alive = false;
    }
    m_incrementalBuildActive = false;
}

void RecyclingGrid::setDataSource(const std::vector<Manga>& items) {
    brls::Logger::debug("RecyclingGrid: setDataSource with {} items", items.size());
    m_items = items;
    m_endReachedFired = false;  // Allow end-reached to fire again with new data
    setupGrid();
}

void RecyclingGrid::appendItems(const std::vector<Manga>& newItems) {
    if (newItems.empty()) return;

    // Cancel any ongoing incremental build before appending
    m_incrementalBuildActive = false;

    int oldCellCount = static_cast<int>(m_cells.size());
    int oldRowCount = static_cast<int>(m_rows.size());

    // Append to items
    for (const auto& item : newItems) {
        m_items.push_back(item);
    }

    m_endReachedFired = false;  // Allow end-reached to fire again

    int newTotalRows = (m_items.size() + m_columns - 1) / m_columns;
    m_totalRowsNeeded = newTotalRows;

    // If the last existing row was partial, remove and rebuild it with new items
    // Use m_cells.size() (not m_items.size()) to compute the actual number of cells
    // in the last row, since incremental build may not have created all rows yet.
    int startRow;
    if (oldCellCount > 0 && oldRowCount > 0) {
        int cellsInLastRow = oldCellCount - (oldRowCount - 1) * m_columns;

        // Sanity check: cellsInLastRow must be valid
        if (cellsInLastRow <= 0 || cellsInLastRow > m_columns) {
            // State is inconsistent - fall back to full rebuild from existing rows
            startRow = oldRowCount;
        } else if (cellsInLastRow < m_columns) {
            // Partial last row - remove and rebuild it with new items

            // Move focus away if it's on a cell in the partial row being removed
            int partialStart = oldCellCount - cellsInLastRow;
            for (int i = partialStart; i < oldCellCount; i++) {
                if (m_cells[i] && m_cells[i]->isFocused()) {
                    brls::Application::giveFocus(m_contentBox);
                    break;
                }
            }

            // Remove cell pointers for the partial row (view memory freed by removeView)
            for (int i = 0; i < cellsInLastRow; i++) {
                m_cells.pop_back();
            }

            // Remove last row view (deletes the row box and its children)
            brls::Box* lastRow = m_rows.back();
            m_rows.pop_back();
            m_contentBox->removeView(lastRow);

            startRow = oldRowCount - 1;
        } else {
            // Last row is full, just append new rows
            startRow = oldRowCount;
        }
    } else {
        startRow = oldRowCount;
    }

    // Create only the new rows
    createRowRange(startRow, newTotalRows);

    brls::Logger::info("RecyclingGrid: appendItems - added {} items, now {} total ({} rows)",
                        newItems.size(), m_items.size(), newTotalRows);
}

void RecyclingGrid::updateDataOrder(const std::vector<Manga>& items) {
    // Update cell data in place without rebuilding the grid structure
    // This is much more efficient for sorting operations
    if (items.size() != m_items.size()) {
        // Size changed, need full rebuild
        brls::Logger::debug("RecyclingGrid: updateDataOrder size mismatch ({} vs {}), doing full rebuild",
                           items.size(), m_items.size());
        setDataSource(items);
        return;
    }

    if (m_cells.empty()) {
        // No cells exist, need full rebuild
        setDataSource(items);
        return;
    }

    brls::Logger::debug("RecyclingGrid: updateDataOrder - updating {} cells in place", items.size());

    // Save old items to detect which cells actually changed manga
    std::vector<Manga> oldItems = std::move(m_items);

    // Update internal data
    m_items = items;

    // Update each cell with the new manga data at its position
    // This preserves the grid structure and just updates the displayed content
    int maxInitialRows = 1;  // Keep initial thumbnail work minimal during reorder
    int cellsInInitialRows = maxInitialRows * m_columns;

    for (size_t i = 0; i < m_cells.size() && i < items.size(); i++) {
        MangaItemCell* cell = m_cells[i];
        if (cell) {
            // If same manga at same position, only update metadata (preserves loaded thumbnail)
            if (i < oldItems.size() && oldItems[i].id == items[i].id) {
                cell->updateMangaData(items[i]);
            } else if (static_cast<int>(i) < cellsInInitialRows) {
                // Different manga - reload with thumbnail
                cell->setManga(items[i]);
            } else {
                cell->setMangaDeferred(items[i]);
            }
        }
    }

    // Only load thumbnails for a buffer beyond visible rows (not all remaining)
    int bufferRows = 2;  // Reduced from 3
    int preloadUpToCell = std::min((maxInitialRows + bufferRows) * m_columns, static_cast<int>(m_cells.size()));

    if (static_cast<int>(m_cells.size()) > cellsInInitialRows) {
        for (int i = cellsInInitialRows; i < preloadUpToCell; i++) {
            if (m_cells[i]) {
                m_cells[i]->loadThumbnailIfNeeded();
            }
        }
    }

    brls::Logger::debug("RecyclingGrid: updateDataOrder complete");
}

void RecyclingGrid::updateCellData(const std::vector<Manga>& items) {
    // Update cell metadata in place without reloading thumbnails
    // Used for incremental updates when only counts/metadata change (like downloads tab)
    if (items.size() != m_items.size()) {
        brls::Logger::debug("RecyclingGrid: updateCellData size mismatch, doing full rebuild");
        setDataSource(items);
        return;
    }

    if (m_cells.empty()) {
        setDataSource(items);
        return;
    }

    brls::Logger::debug("RecyclingGrid: updateCellData - updating {} cells metadata in place", items.size());

    // Update internal data
    m_items = items;

    // Update each cell's data without reloading thumbnails
    for (size_t i = 0; i < m_cells.size() && i < items.size(); i++) {
        MangaItemCell* cell = m_cells[i];
        if (cell) {
            cell->updateMangaData(items[i]);
        }
    }

    brls::Logger::debug("RecyclingGrid: updateCellData complete");
}

void RecyclingGrid::removeItems(const std::vector<int>& mangaIdsToRemove) {
    // Remove specific items without full rebuild
    // This is used for filter operations like DOWNLOADED_ONLY
    if (mangaIdsToRemove.empty()) return;

    brls::Logger::debug("RecyclingGrid: removeItems - removing {} items", mangaIdsToRemove.size());

    // Create a set for fast lookup
    std::set<int> idsToRemove(mangaIdsToRemove.begin(), mangaIdsToRemove.end());

    // Find indices to remove (in reverse order to avoid index shifting issues)
    std::vector<size_t> indicesToRemove;
    for (size_t i = 0; i < m_items.size(); i++) {
        if (idsToRemove.count(m_items[i].id)) {
            indicesToRemove.push_back(i);
        }
    }

    if (indicesToRemove.empty()) return;

    // Save focused index before rebuild so we can restore focus to a valid cell.
    // Without this, setupGrid() destroys all cells and borealis may try to
    // restore focus to a freed cell pointer → vtable crash.
    int savedFocus = m_focusedIndex;

    // For simplicity, if we're removing items, do a full rebuild
    // This ensures proper row layout recalculation
    std::vector<Manga> filteredItems;
    for (size_t i = 0; i < m_items.size(); i++) {
        if (idsToRemove.find(m_items[i].id) == idsToRemove.end()) {
            filteredItems.push_back(m_items[i]);
        }
    }

    brls::Logger::debug("RecyclingGrid: removeItems - filtered from {} to {} items",
                       m_items.size(), filteredItems.size());
    setDataSource(filteredItems);

    // Restore focus to a valid cell after rebuild. Clamp to last item if the
    // previously-focused index is now out of range (e.g. last item was removed).
    if (!filteredItems.empty()) {
        int clampedIndex = std::min(savedFocus, static_cast<int>(filteredItems.size()) - 1);
        if (clampedIndex < 0) clampedIndex = 0;
        focusIndex(clampedIndex);
    }
}

void RecyclingGrid::setOnItemSelected(std::function<void(const Manga&)> callback) {
    m_onItemSelected = callback;
}

void RecyclingGrid::setOnItemLongPressed(std::function<void(const Manga&, int index)> callback) {
    m_onItemLongPressed = callback;
}

void RecyclingGrid::setOnPullToRefresh(std::function<void()> callback) {
    m_onPullToRefresh = callback;
}

void RecyclingGrid::setOnBackPressed(std::function<bool()> callback) {
    m_onBackPressed = callback;
}

void RecyclingGrid::setOnEndReached(std::function<void()> callback) {
    m_onEndReached = callback;
}

void RecyclingGrid::setOnSelectionChanged(std::function<void(int count)> callback) {
    m_onSelectionChanged = callback;
}

void RecyclingGrid::clearViews() {
    m_incrementalBuildActive = false;  // Cancel any pending incremental build

    // Move focus away from cells before deleting them (same reason as setupGrid)
    for (auto* cell : m_cells) {
        if (cell && cell->isFocused()) {
            if (m_contentBox) {
                brls::Application::giveFocus(m_contentBox);
            }
            break;
        }
    }

    m_items.clear();
    m_rows.clear();
    m_cells.clear();
    if (m_contentBox) {
        m_contentBox->clearViews();
    }
}

void RecyclingGrid::setupGrid() {
    // Cancel any ongoing incremental build
    m_incrementalBuildActive = false;
    m_pendingFocusIndex = -1;

    // Reset scroll-based loading state
    m_lastScrollLoadY = 0.0f;

    // CRITICAL: Move focus away from cells before deleting them.
    // clearViews() will destroy all cells, but brls may still hold
    // a pointer to the currently-focused cell. Without this, brls
    // accesses freed memory on the next frame → vtable crash.
    for (auto* cell : m_cells) {
        if (cell && cell->isFocused()) {
            brls::Application::giveFocus(m_contentBox);
            break;
        }
    }

    // Clear existing views
    m_contentBox->clearViews();
    m_rows.clear();
    m_cells.clear();

    if (m_items.empty()) return;

    m_totalRowsNeeded = (m_items.size() + m_columns - 1) / m_columns;

    // On PS Vita/PS4, category-load FPS can tank if we create too many cells in
    // a single frame. Build very conservatively:
    // - Create only the first row immediately (content appears quickly)
    // - Build remaining rows one row per frame via brls::sync()
    int immediateRows = std::min(1, m_totalRowsNeeded);

    brls::Logger::info("RecyclingGrid: Building {} rows for {} items (first {} immediate)",
                        m_totalRowsNeeded, m_items.size(), immediateRows);

    createRowRange(0, immediateRows);

    // Schedule remaining rows for incremental building
    if (immediateRows < m_totalRowsNeeded) {
        m_incrementalBuildRow = immediateRows;
        m_incrementalBuildActive = true;
        std::weak_ptr<bool> aliveWeak = m_alive;
        brls::sync([this, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            buildNextRowBatch();
        });
    } else {
        // Small library - all rows built, trigger buffer preload
        int maxInitialRows = 1;
        int bufferRows = 2;  // Reduced from 3 to prevent queue flooding
        int preloadUpToRow = std::min(maxInitialRows + bufferRows, m_totalRowsNeeded);
        int startCell = std::min(maxInitialRows * m_columns, (int)m_cells.size());
        int endCell = std::min(preloadUpToRow * m_columns, (int)m_cells.size());
        for (int i = startCell; i < endCell; i++) {
            m_cells[i]->loadThumbnailIfNeeded();
        }
    }
}

void RecyclingGrid::createRowRange(int startRow, int endRow) {
    int maxInitialRows = 1;  // Keep category-entry thumbnail burst very small

    for (int row = startRow; row < endRow; row++) {
        auto* rowBox = new brls::Box();
        rowBox->setAxis(brls::Axis::ROW);
        rowBox->setJustifyContent(brls::JustifyContent::FLEX_START);
        rowBox->setMarginBottom(m_rowMargin);

        // Create cells for this row
        int startIdx = row * m_columns;
        int endIdx = std::min(startIdx + m_columns, (int)m_items.size());

        // For list mode, auto-adapt row height based on title width
        int rowHeight = m_cellHeight;
        if (m_listMode) {
            int maxWidthUnits = 0;
            for (int i = startIdx; i < endIdx; i++) {
                // Estimate visual width in "width units" (1 unit ≈ 1 Latin char width).
                // CJK / wide characters (multi-byte UTF-8) count as 2 units since
                // they render roughly twice as wide as Latin characters.
                const std::string& title = m_items[i].title;
                int widthUnits = 0;
                for (size_t j = 0; j < title.size(); ) {
                    unsigned char c = static_cast<unsigned char>(title[j]);
                    if (c < 0x80) {
                        // ASCII: 1 byte, 1 width unit
                        widthUnits += 1;
                        j += 1;
                    } else if (c < 0xC0) {
                        // Continuation byte (shouldn't appear here), skip
                        j += 1;
                    } else if (c < 0xE0) {
                        // 2-byte sequence (Latin extended, etc): 1 width unit
                        widthUnits += 1;
                        j += 2;
                    } else if (c < 0xF0) {
                        // 3-byte sequence (CJK, Japanese, Korean, etc): 2 width units
                        widthUnits += 2;
                        j += 3;
                    } else {
                        // 4-byte sequence (emoji, etc): 2 width units
                        widthUnits += 2;
                        j += 4;
                    }
                }
                if (widthUnits > maxWidthUnits) {
                    maxWidthUnits = widthUnits;
                }
            }
            // At font 14 on the Vita (900px cell, ~32px padding), roughly 108
            // Latin-width characters fit per line.
            int charsPerLine = 100;  // Slightly conservative
            int lines = (maxWidthUnits + charsPerLine - 1) / charsPerLine;
            if (lines < 1) lines = 1;
            if (lines > 3) lines = 3;
            rowHeight = 40 + (lines * 20);
            if (rowHeight < 60) rowHeight = 60;
            if (rowHeight > 120) rowHeight = 120;
        }

        rowBox->setHeight(rowHeight);

        for (int i = startIdx; i < endIdx; i++) {
            auto* cell = new MangaItemCell();
            cell->setWidth(m_cellWidth);
            cell->setHeight(rowHeight);
            cell->setMarginRight(m_cellMargin);

            // Apply display mode to cell
            if (m_listMode) {
                cell->setListMode(true);
            } else if (m_compactMode) {
                cell->setCompactMode(true);
            }

            // Pass grid column count so cell can adapt title font/truncation
            cell->setGridColumns(m_columns);

            // Apply library badge setting (for browser/search tabs)
            if (m_showLibraryBadge) {
                cell->setShowLibraryBadge(true);
            }

            // Load first rows immediately, defer rest for staggered loading
            if (row < maxInitialRows) {
                cell->setManga(m_items[i]);
            } else {
                cell->setMangaDeferred(m_items[i]);
            }

            int index = i;
            cell->registerClickAction([this, cell, index](brls::View* view) {
                if (m_longPressTriggered) {
                    m_longPressTriggered = false;
                    return true;
                }
                // Transfer focus to the tapped cell (touch support)
                if (!cell->isFocused()) {
                    brls::Application::giveFocus(cell);
                }
                onItemClicked(index);
                return true;
            });
            cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

            cell->addGestureRecognizer(new LongPressGestureRecognizer(
                cell,
                [this, cell, index](LongPressGestureStatus status) {
                    if (status.state == brls::GestureState::START) {
                        m_longPressTriggered = true;
                        // Clear press visual since context menu is opening
                        cell->setPressed(false);
                        if (index >= 0 && index < (int)m_items.size() && m_onItemLongPressed) {
                            m_onItemLongPressed(m_items[index], index);
                        }
                    }
                },
                400
            ));

            cell->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
                if (m_onBackPressed) {
                    return m_onBackPressed();
                }
                return false;
            }, true);

            cell->getFocusEvent()->subscribe([this, index](brls::View*) {
                m_focusedIndex = index;
                loadThumbnailsNearIndex(index);

                // Fire onEndReached when focus is within 2 rows of the end
                if (m_onEndReached && !m_endReachedFired) {
                    int threshold = m_columns * 2;
                    if (index >= static_cast<int>(m_items.size()) - threshold) {
                        m_endReachedFired = true;
                        m_onEndReached();
                    }
                }
            });

            rowBox->addView(cell);
            m_cells.push_back(cell);
        }

        m_contentBox->addView(rowBox);
        m_rows.push_back(rowBox);
    }
}

void RecyclingGrid::buildNextRowBatch() {
    if (!m_incrementalBuildActive) return;
    if (m_incrementalBuildRow >= m_totalRowsNeeded) {
        m_incrementalBuildActive = false;
        return;
    }

    // Build only 1 row per frame to avoid frame-time spikes while loading a
    // category with many items on constrained hardware.
    int batchSize = 1;
    int endRow = std::min(m_incrementalBuildRow + batchSize, m_totalRowsNeeded);

    createRowRange(m_incrementalBuildRow, endRow);
    m_incrementalBuildRow = endRow;

    // Apply pending focus if the target cell was just created
    if (m_pendingFocusIndex >= 0 && m_pendingFocusIndex < static_cast<int>(m_cells.size())) {
        brls::Application::giveFocus(m_cells[m_pendingFocusIndex]);
        m_focusedIndex = m_pendingFocusIndex;
        m_pendingFocusIndex = -1;
    }

    if (m_incrementalBuildRow < m_totalRowsNeeded) {
        // More rows to build - schedule next batch
        std::weak_ptr<bool> aliveWeak = m_alive;
        brls::sync([this, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            buildNextRowBatch();
        });
    } else {
        // All rows built - trigger thumbnail preloading for buffer rows
        // Load visible rows (0-2) + 1 buffer row = 18 cells max, prevents queue buildup
        m_incrementalBuildActive = false;
        int maxInitialRows = 1;
        int bufferRows = 1;  // Reduced from 2: only preload next row to avoid queue flooding
        int preloadUpToRow = std::min(maxInitialRows + bufferRows, m_totalRowsNeeded);
        int startCell = std::min(maxInitialRows * m_columns, (int)m_cells.size());
        int endCell = std::min(preloadUpToRow * m_columns, (int)m_cells.size());
        for (int i = startCell; i < endCell; i++) {
            m_cells[i]->loadThumbnailIfNeeded();
        }
        brls::Logger::info("RecyclingGrid: Incremental build complete - {} cells in {} rows",
                            m_cells.size(), m_totalRowsNeeded);
    }
}

void RecyclingGrid::loadThumbnailsNearIndex(int index) {
    if (m_cells.empty() || m_columns <= 0) return;

    int focusedRow = index / m_columns;
    int totalRows = (static_cast<int>(m_cells.size()) + m_columns - 1) / m_columns;

    // Load thumbnails for rows around the focused cell: 1 row above, 2 rows below
    // Reduced from 1+3 to 1+2 to prevent queue flooding: ~18 cells instead of 30
    int loadFromRow = std::max(0, focusedRow - 1);
    int loadToRow = std::min(totalRows, focusedRow + 3);

    int startCell = loadFromRow * m_columns;
    int endCell = std::min(loadToRow * m_columns, static_cast<int>(m_cells.size()));

    // loadThumbnailIfNeeded() checks m_thumbnailLoaded internally,
    // so calling it on already-loaded cells is a no-op (just a bool check).
    // This avoids gaps when the user jumps to a distant row.
    int loadBudget = 18;
    for (int i = startCell; i < endCell && loadBudget > 0; i++) {
        if (m_cells[i]) {
            m_cells[i]->loadThumbnailIfNeeded();
            loadBudget--;
        }
    }

    // Texture recycling: unload thumbnails far from visible area to free GPU memory
    // On PS Vita with 128MB RAM, keeping 100+ cover textures loaded wastes VRAM.
    // Unload cells more than 12 rows away from focus - they'll reload from
    // ImageLoader's LRU memory cache when scrolled back (fast, no network hit).
    static constexpr int UNLOAD_DISTANCE_ROWS = 8;  // Reduced from 12 to free GPU memory faster
    int unloadAboveRow = focusedRow - UNLOAD_DISTANCE_ROWS;
    int unloadBelowRow = focusedRow + UNLOAD_DISTANCE_ROWS;

    // Unload cells far above
    constexpr int UNLOAD_BUDGET_PER_CALL = 24;
    int unloadBudgetAbove = UNLOAD_BUDGET_PER_CALL / 2;
    int unloadBudgetBelow = UNLOAD_BUDGET_PER_CALL - unloadBudgetAbove;

    // IMPORTANT PERF FIX:
    // On large libraries, scanning from index 0 -> N every focus move can cost
    // several milliseconds per frame on Vita/PS4. Cap how many cells we inspect
    // per side so unload work stays O(1) per call instead of O(total_cells).
    constexpr int MAX_UNLOAD_SCAN_PER_SIDE = 72;

    if (unloadAboveRow > 0) {
        int unloadEnd = std::min(unloadAboveRow * m_columns, static_cast<int>(m_cells.size()));
        int scanned = 0;
        // Scan backwards from the boundary because recently-visible rows near the
        // boundary are more likely to still have loaded thumbnails.
        for (int i = unloadEnd - 1; i >= 0 && unloadBudgetAbove > 0 && scanned < MAX_UNLOAD_SCAN_PER_SIDE; i--, scanned++) {
            if (m_cells[i] && m_cells[i]->isThumbnailLoaded()) {
                m_cells[i]->unloadThumbnail();
                unloadBudgetAbove--;
            }
        }
    }

    // Unload cells far below
    if (unloadBelowRow < totalRows) {
        int unloadStart = std::max(0, unloadBelowRow * m_columns);
        int scanned = 0;
        for (int i = unloadStart; i < static_cast<int>(m_cells.size()) && unloadBudgetBelow > 0 &&
                        scanned < MAX_UNLOAD_SCAN_PER_SIDE; i++, scanned++) {
            if (m_cells[i] && m_cells[i]->isThumbnailLoaded()) {
                m_cells[i]->unloadThumbnail();
                unloadBudgetBelow--;
            }
        }
    }
}

void RecyclingGrid::updateVisibleCells() {
    // Load all remaining thumbnails
    for (auto* cell : m_cells) {
        cell->loadThumbnailIfNeeded();
    }
}

void RecyclingGrid::resetThumbnailLoadStates() {
    // Reset m_thumbnailLoaded on all cells so they can reload via loadThumbnailIfNeeded().
    // Called after ImageLoader::cancelAll() which may cancel pending thumbnail loads,
    // leaving cells with m_thumbnailLoaded=true but no actual thumbnail applied.
    // Uses resetThumbnailLoadState() (not unloadThumbnail) to keep the old image visible
    // until the new one loads from cache, avoiding a visual flash.
    for (auto* cell : m_cells) {
        if (cell) {
            cell->resetThumbnailLoadState();
        }
    }
    brls::Logger::debug("RecyclingGrid: Reset thumbnail load states for {} cells", m_cells.size());
}

void RecyclingGrid::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    auto& perf = PerfOverlay::getInstance();
    perf.endFrame();   // End previous frame timing
    perf.beginFrame(); // Start this frame timing

    // Visibility culling: hide off-screen rows so ScrollingFrame::draw() skips them.
    // Without this, ALL rows (including off-screen) get full NanoVG draw calls issued,
    // wasting ~80% of CPU frame time on path generation for invisible content.
    // INVISIBLE preserves layout space (scroll height stays correct) but skips draw.
    // Optimization: only update rows at the boundaries of the visible range, not all rows.
    if (!m_rows.empty() && m_cellHeight > 0) {
        float scrollY = this->getContentOffsetY();
        float viewH = this->getHeight();
        float rowH = static_cast<float>(m_cellHeight + m_rowMargin);
        int firstVisible = std::max(0, static_cast<int>(scrollY / rowH) - 1);
        int lastVisible = std::min(static_cast<int>(m_rows.size()),
                                    static_cast<int>((scrollY + viewH) / rowH) + 2);

        // Only update rows if visible range changed (avoid O(rows) iteration every frame)
        if (firstVisible != m_cachedFirstVisible || lastVisible != m_cachedLastVisible) {
            // Hide rows that are now invisible
            if (m_cachedFirstVisible >= 0) {
                // Hide rows above the new range
                for (int i = m_cachedFirstVisible; i < firstVisible; i++) {
                    if (i >= 0 && i < static_cast<int>(m_rows.size())) {
                        m_rows[i]->setVisibility(brls::Visibility::INVISIBLE);
                    }
                }
                // Hide rows below the new range
                for (int i = lastVisible; i < m_cachedLastVisible; i++) {
                    if (i >= 0 && i < static_cast<int>(m_rows.size())) {
                        m_rows[i]->setVisibility(brls::Visibility::INVISIBLE);
                    }
                }
            } else {
                // First call: set all visibility in one pass
                for (int i = 0; i < static_cast<int>(m_rows.size()); i++) {
                    brls::Visibility desired = (i >= firstVisible && i < lastVisible)
                        ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE;
                    m_rows[i]->setVisibility(desired);
                }
            }

            // Show rows that are now visible
            for (int i = firstVisible; i < lastVisible; i++) {
                if (i >= 0 && i < static_cast<int>(m_rows.size())) {
                    m_rows[i]->setVisibility(brls::Visibility::VISIBLE);
                }
            }

            m_cachedFirstVisible = firstVisible;
            m_cachedLastVisible = lastVisible;
        }
    }

    PERF_BEGIN("grid_draw");
    // Call parent draw - now only visible rows are rendered
    brls::ScrollingFrame::draw(vg, x, y, width, height, style, ctx);
    PERF_END("grid_draw");

    // Check scroll position for thumbnail loading during touch scrolling.
    // The focus-based loading (loadThumbnailsNearIndex) handles D-pad navigation,
    // but during touch scrolling the focus index doesn't change.
    // Throttled: require full row of scroll AND 10-frame cooldown to prevent
    // queue flooding that causes FPS drops to <10 on PS Vita.
    if (m_scrollLoadCooldown > 0) {
        m_scrollLoadCooldown--;
    }
    if (!m_cells.empty() && m_columns > 0 && m_scrollLoadCooldown == 0) {
        float scrollY = this->getContentOffsetY();
        float scrollDelta = std::abs(scrollY - m_lastScrollLoadY);
        float rowHeight = static_cast<float>(m_cellHeight + m_rowMargin);

        // Trigger when scrolled more than a full row since last load (was 0.5 row)
        if (scrollDelta > rowHeight * 1.5f) {
            m_lastScrollLoadY = scrollY;
            m_scrollLoadCooldown = 10;  // Wait 10 frames before next load trigger
            loadThumbnailsForScrollPosition();
        }
    }

    // Draw performance overlay on top (uses screen coordinates, ignores scroll)
    // Reset scissor so overlay draws over everything
    nvgResetScissor(vg);
    perf.draw(vg, 960.0f, 544.0f);
}

void RecyclingGrid::loadThumbnailsForScrollPosition() {
    if (m_cells.empty() || m_columns <= 0 || m_cellHeight <= 0) return;

    float scrollY = this->getContentOffsetY();
    float viewHeight = this->getHeight();
    float rowHeight = static_cast<float>(m_cellHeight + m_rowMargin);
    int totalRows = (static_cast<int>(m_cells.size()) + m_columns - 1) / m_columns;

    // Calculate which rows are currently visible based on scroll position
    int firstVisibleRow = std::max(0, static_cast<int>(scrollY / rowHeight));
    int lastVisibleRow = std::min(totalRows, static_cast<int>((scrollY + viewHeight) / rowHeight) + 1);

    // Load 1 row above and 2 rows below the visible area as buffer
    // Reduced from 2+3 to 1+2 to prevent queue flooding during fast scrolling
    int loadFromRow = std::max(0, firstVisibleRow - 1);
    int loadToRow = std::min(totalRows, lastVisibleRow + 2);

    int startCell = loadFromRow * m_columns;
    int endCell = std::min(loadToRow * m_columns, static_cast<int>(m_cells.size()));

    int loadBudget = 18;
    for (int i = startCell; i < endCell && loadBudget > 0; i++) {
        if (m_cells[i]) {
            m_cells[i]->loadThumbnailIfNeeded();
            loadBudget--;
        }
    }

    // Also apply texture recycling for scroll-based movement (same as focus-based)
    // Only iterate through actual unload ranges, not all cells
    static constexpr int SCROLL_UNLOAD_DISTANCE_ROWS = 8;  // Reduced from 12
    int centerRow = (firstVisibleRow + lastVisibleRow) / 2;
    int unloadAboveRow = centerRow - SCROLL_UNLOAD_DISTANCE_ROWS;
    int unloadBelowRow = centerRow + SCROLL_UNLOAD_DISTANCE_ROWS;

    constexpr int UNLOAD_BUDGET_PER_CALL = 24;
    int unloadBudgetAbove = UNLOAD_BUDGET_PER_CALL / 2;
    int unloadBudgetBelow = UNLOAD_BUDGET_PER_CALL - unloadBudgetAbove;

    constexpr int MAX_UNLOAD_SCAN_PER_SIDE = 72;

    if (unloadAboveRow > 0) {
        // Only iterate cells in the range [0, unloadAboveRow)
        int unloadEnd = std::min(unloadAboveRow * m_columns, static_cast<int>(m_cells.size()));
        int scanned = 0;
        for (int i = unloadEnd - 1; i >= 0 && unloadBudgetAbove > 0 &&
                        scanned < MAX_UNLOAD_SCAN_PER_SIDE; i--, scanned++) {
            if (m_cells[i] && m_cells[i]->isThumbnailLoaded()) {
                m_cells[i]->unloadThumbnail();
                unloadBudgetAbove--;
            }
        }
    }
    if (unloadBelowRow < totalRows) {
        // Only iterate cells in the range [unloadBelowRow, end)
        int unloadStart = std::max(0, unloadBelowRow * m_columns);
        int unloadEnd = static_cast<int>(m_cells.size());
        int scanned = 0;
        for (int i = unloadStart; i < unloadEnd && unloadBudgetBelow > 0 &&
                        scanned < MAX_UNLOAD_SCAN_PER_SIDE; i++, scanned++) {
            if (m_cells[i] && m_cells[i]->isThumbnailLoaded()) {
                m_cells[i]->unloadThumbnail();
                unloadBudgetBelow--;
            }
        }
    }
}

void RecyclingGrid::onItemClicked(int index) {
    brls::Logger::debug("RecyclingGrid::onItemClicked index={}", index);
    if (index >= 0 && index < (int)m_items.size()) {
        if (m_selectionMode) {
            toggleSelection(index);
        } else if (m_onItemSelected) {
            m_onItemSelected(m_items[index]);
        }
    }
}

void RecyclingGrid::setSelectionMode(bool enabled) {
    m_selectionMode = enabled;
    if (!enabled) {
        clearSelection();
    }
}

void RecyclingGrid::toggleSelection(int index) {
    if (index < 0 || index >= (int)m_cells.size()) return;

    if (m_selectedIndices.count(index)) {
        m_selectedIndices.erase(index);
        m_cells[index]->setSelected(false);
    } else {
        m_selectedIndices.insert(index);
        m_cells[index]->setSelected(true);
    }

    // Notify selection change
    if (m_onSelectionChanged) {
        m_onSelectionChanged(static_cast<int>(m_selectedIndices.size()));
    }
}

void RecyclingGrid::clearSelection() {
    for (int idx : m_selectedIndices) {
        if (idx >= 0 && idx < (int)m_cells.size()) {
            m_cells[idx]->setSelected(false);
        }
    }
    m_selectedIndices.clear();
}

std::vector<int> RecyclingGrid::getSelectedIndices() const {
    return std::vector<int>(m_selectedIndices.begin(), m_selectedIndices.end());
}

bool RecyclingGrid::isIndexSelected(int index) const {
    return m_selectedIndices.count(index) > 0;
}

std::vector<Manga> RecyclingGrid::getSelectedManga() const {
    std::vector<Manga> result;
    for (int idx : m_selectedIndices) {
        if (idx >= 0 && idx < (int)m_items.size()) {
            result.push_back(m_items[idx]);
        }
    }
    return result;
}

int RecyclingGrid::getSelectionCount() const {
    return static_cast<int>(m_selectedIndices.size());
}

const Manga* RecyclingGrid::getItem(int index) const {
    if (index >= 0 && index < (int)m_items.size()) {
        return &m_items[index];
    }
    return nullptr;
}

bool RecyclingGrid::hasCellFocus() {
    for (auto* cell : m_cells) {
        if (cell && cell->isFocused()) {
            return true;
        }
    }
    return false;
}

brls::View* RecyclingGrid::create() {
    return new RecyclingGrid();
}

void RecyclingGrid::setGridSize(int columns) {
    if (columns < 3) columns = 3;
    if (columns > 10) columns = 10;

    m_columns = columns;

    // Adjust cell dimensions based on column count
    // PS Vita screen: 960x544, with padding
    int availableWidth = 920;  // Account for padding
    m_cellWidth = (availableWidth - (m_columns - 1) * m_cellMargin) / m_columns;

    // Adjust height proportionally (cover ratio ~0.7)
    if (m_listMode) {
        m_cellHeight = 80;  // Fixed height for list mode
    } else if (m_compactMode) {
        m_cellHeight = static_cast<int>(m_cellWidth * 1.4);  // Taller for compact (cover only)
    } else {
        m_cellHeight = static_cast<int>(m_cellWidth * 1.4);  // Normal height with 2-line title
    }

    brls::Logger::info("RecyclingGrid: Grid size set to {} columns, cell {}x{}",
                       m_columns, m_cellWidth, m_cellHeight);

    // Rebuild grid if we have items
    if (!m_items.empty()) {
        setupGrid();
    }
}

void RecyclingGrid::setCompactMode(bool compact) {
    if (m_compactMode == compact) return;
    m_compactMode = compact;
    m_listMode = false;  // Disable list mode if enabling compact

    // Recalculate dimensions
    setGridSize(m_columns);
}

void RecyclingGrid::setListMode(bool listMode) {
    if (m_listMode == listMode) return;
    m_listMode = listMode;
    m_compactMode = false;  // Disable compact mode if enabling list

    if (m_listMode) {
        // List mode: 1 column, auto-adapt row height to title length
        m_columns = 1;
        m_cellWidth = 900;
        m_cellHeight = 0;  // Dynamic height, calculated per-row in createRowRange
        m_rowMargin = 5;
    } else {
        // Reset to default grid
        m_columns = 6;
        m_rowMargin = 10;
        setGridSize(m_columns);
    }

    // Rebuild grid if we have items
    if (!m_items.empty()) {
        setupGrid();
    }
}

void RecyclingGrid::setListRowSize(int rowSize) {
    // No-op: list mode always auto-adapts row height to title length
    (void)rowSize;
}

void RecyclingGrid::setShowLibraryBadge(bool show) {
    if (m_showLibraryBadge == show) return;
    m_showLibraryBadge = show;

    // Update existing cells
    for (auto* cell : m_cells) {
        cell->setShowLibraryBadge(show);
    }
}

void RecyclingGrid::refreshLibraryBadges() {
    for (auto* cell : m_cells) {
        cell->refreshLibraryBadge();
    }
}

brls::View* RecyclingGrid::getFirstCell() const {
    if (m_cells.empty()) {
        return nullptr;
    }
    return m_cells[0];
}

void RecyclingGrid::focusIndex(int index) {
    if (index >= 0 && index < static_cast<int>(m_cells.size())) {
        brls::Application::giveFocus(m_cells[index]);
        m_focusedIndex = index;
        m_pendingFocusIndex = -1;
    } else if (index >= 0 && m_incrementalBuildActive) {
        // Cell doesn't exist yet (still being built incrementally).
        // Store as pending - buildNextRowBatch will apply it once the cell is created.
        m_pendingFocusIndex = index;
    } else if (!m_cells.empty()) {
        // Fall back to first cell if index is out of range
        brls::Application::giveFocus(m_cells[0]);
        m_focusedIndex = 0;
        m_pendingFocusIndex = -1;
    }
}

} // namespace vitasuwayomi
