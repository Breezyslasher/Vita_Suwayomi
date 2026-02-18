/**
 * VitaSuwayomi - Recycling Grid implementation
 * Uses lazy loading to prevent UI freezes on large libraries
 */

#include "view/recycling_grid.hpp"
#include "view/media_item_cell.hpp"
#include "view/long_press_gesture.hpp"

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
    setupGrid();
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
    int maxInitialRows = 6;
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
    int bufferRows = 3;
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

    // For simplicity, if we're removing items, do a full rebuild
    // This ensures proper row layout recalculation
    // In the future, we could optimize this to remove cells in place
    std::vector<Manga> filteredItems;
    for (size_t i = 0; i < m_items.size(); i++) {
        if (idsToRemove.find(m_items[i].id) == idsToRemove.end()) {
            filteredItems.push_back(m_items[i]);
        }
    }

    brls::Logger::debug("RecyclingGrid: removeItems - filtered from {} to {} items",
                       m_items.size(), filteredItems.size());
    setDataSource(filteredItems);
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

void RecyclingGrid::setOnSelectionChanged(std::function<void(int count)> callback) {
    m_onSelectionChanged = callback;
}

void RecyclingGrid::clearViews() {
    m_incrementalBuildActive = false;  // Cancel any pending incremental build
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

    // Clear existing views
    m_contentBox->clearViews();
    m_rows.clear();
    m_cells.clear();

    if (m_items.empty()) return;

    m_totalRowsNeeded = (m_items.size() + m_columns - 1) / m_columns;

    // On PS Vita's 444MHz CPU, creating 98+ MangaItemCell objects (each with ~11
    // sub-views) at once causes a multi-second freeze. Build incrementally:
    // - Create first 2 rows immediately (visible content appears fast)
    // - Build remaining rows in batches of 2 per frame via brls::sync()
    int immediateRows = std::min(2, m_totalRowsNeeded);

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
        int maxInitialRows = 6;
        int bufferRows = 3;
        int preloadUpToRow = std::min(maxInitialRows + bufferRows, m_totalRowsNeeded);
        int startCell = std::min(maxInitialRows * m_columns, (int)m_cells.size());
        int endCell = std::min(preloadUpToRow * m_columns, (int)m_cells.size());
        for (int i = startCell; i < endCell; i++) {
            m_cells[i]->loadThumbnailIfNeeded();
        }
    }
}

void RecyclingGrid::createRowRange(int startRow, int endRow) {
    int maxInitialRows = 6;

    for (int row = startRow; row < endRow; row++) {
        auto* rowBox = new brls::Box();
        rowBox->setAxis(brls::Axis::ROW);
        rowBox->setJustifyContent(brls::JustifyContent::FLEX_START);
        rowBox->setMarginBottom(m_rowMargin);

        // Create cells for this row
        int startIdx = row * m_columns;
        int endIdx = std::min(startIdx + m_columns, (int)m_items.size());

        // For list mode with auto-size, calculate row height based on title length
        int rowHeight = m_cellHeight;
        if (m_listMode && m_listRowSize == 3) {  // Auto mode
            int maxTitleLen = 0;
            for (int i = startIdx; i < endIdx; i++) {
                int titleLen = static_cast<int>(m_items[i].title.length());
                if (titleLen > maxTitleLen) {
                    maxTitleLen = titleLen;
                }
            }
            int lines = (maxTitleLen + 44) / 45;
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
                cell->setListRowSize(m_listRowSize);
            } else if (m_compactMode) {
                cell->setCompactMode(true);
            }

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
            cell->registerClickAction([this, index](brls::View* view) {
                if (m_longPressTriggered) {
                    m_longPressTriggered = false;
                    return true;
                }
                onItemClicked(index);
                return true;
            });
            cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

            cell->addGestureRecognizer(new LongPressGestureRecognizer(
                cell,
                [this, index](LongPressGestureStatus status) {
                    if (status.state == brls::GestureState::START) {
                        m_longPressTriggered = true;
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

    // Build 2 rows per frame (~12 cells) to keep frames responsive
    int batchSize = 2;
    int endRow = std::min(m_incrementalBuildRow + batchSize, m_totalRowsNeeded);

    createRowRange(m_incrementalBuildRow, endRow);
    m_incrementalBuildRow = endRow;

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
        m_incrementalBuildActive = false;
        int maxInitialRows = 6;
        int bufferRows = 3;
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

    // Load thumbnails for rows around the focused cell: 2 rows above, 6 rows below
    int loadFromRow = std::max(0, focusedRow - 2);
    int loadToRow = std::min(totalRows, focusedRow + 7);  // 6 rows below (visible area + buffer)

    int startCell = loadFromRow * m_columns;
    int endCell = std::min(loadToRow * m_columns, static_cast<int>(m_cells.size()));

    // loadThumbnailIfNeeded() checks m_thumbnailLoaded internally,
    // so calling it on already-loaded cells is a no-op (just a bool check).
    // This avoids gaps when the user jumps to a distant row.
    for (int i = startCell; i < endCell; i++) {
        if (m_cells[i]) {
            m_cells[i]->loadThumbnailIfNeeded();
        }
    }
}

void RecyclingGrid::updateVisibleCells() {
    // Load all remaining thumbnails
    for (auto* cell : m_cells) {
        cell->loadThumbnailIfNeeded();
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
        m_cellHeight = static_cast<int>(m_cellWidth * 1.3);  // Normal height with title
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
        // List mode: 1 column, larger cells
        m_columns = 1;
        m_cellWidth = 900;
        // Apply list row size setting
        switch (m_listRowSize) {
            case 0:  // Small
                m_cellHeight = 60;
                break;
            case 1:  // Medium (default)
                m_cellHeight = 80;
                break;
            case 2:  // Large
                m_cellHeight = 100;
                break;
            case 3:  // Auto - will be handled per-cell in setupGrid
                m_cellHeight = 0;  // Dynamic height
                break;
            default:
                m_cellHeight = 80;
                break;
        }
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
    if (m_listRowSize == rowSize) return;
    m_listRowSize = rowSize;

    // If currently in list mode, update dimensions and rebuild
    if (m_listMode) {
        switch (m_listRowSize) {
            case 0:  // Small
                m_cellHeight = 60;
                break;
            case 1:  // Medium (default)
                m_cellHeight = 80;
                break;
            case 2:  // Large
                m_cellHeight = 100;
                break;
            case 3:  // Auto - dynamic height
                m_cellHeight = 0;
                break;
            default:
                m_cellHeight = 80;
                break;
        }

        // Rebuild grid if we have items
        if (!m_items.empty()) {
            setupGrid();
        }
    }
}

void RecyclingGrid::setShowLibraryBadge(bool show) {
    if (m_showLibraryBadge == show) return;
    m_showLibraryBadge = show;

    // Update existing cells
    for (auto* cell : m_cells) {
        cell->setShowLibraryBadge(show);
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
    } else if (!m_cells.empty()) {
        // Fall back to first cell if index is out of range
        brls::Application::giveFocus(m_cells[0]);
        m_focusedIndex = 0;
    }
}

} // namespace vitasuwayomi
