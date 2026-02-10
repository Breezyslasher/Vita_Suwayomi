/**
 * VitaSuwayomi - Recycling Grid implementation
 * Uses lazy loading to prevent UI freezes on large libraries
 */

#include "view/recycling_grid.hpp"
#include "view/media_item_cell.hpp"

namespace vitasuwayomi {

RecyclingGrid::RecyclingGrid() {
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

void RecyclingGrid::setDataSource(const std::vector<Manga>& items) {
    brls::Logger::debug("RecyclingGrid: setDataSource with {} items", items.size());
    m_items = items;
    setupGrid();
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

void RecyclingGrid::clearViews() {
    m_items.clear();
    m_rows.clear();
    m_cells.clear();
    if (m_contentBox) {
        m_contentBox->clearViews();
    }
}

void RecyclingGrid::setupGrid() {
    // Clear existing views
    m_contentBox->clearViews();
    m_rows.clear();
    m_cells.clear();

    if (m_items.empty()) return;

    // Calculate number of rows needed
    int totalRows = (m_items.size() + m_columns - 1) / m_columns;

    brls::Logger::debug("RecyclingGrid: Creating {} rows for {} items", totalRows, m_items.size());

    // Create all rows and cells
    // Load first 6 rows immediately (visible on screen), then load rest quickly
    int maxInitialRows = 6;

    for (int row = 0; row < totalRows; row++) {
        auto* rowBox = new brls::Box();
        rowBox->setAxis(brls::Axis::ROW);
        rowBox->setJustifyContent(brls::JustifyContent::FLEX_START);
        rowBox->setMarginBottom(m_rowMargin);
        rowBox->setHeight(m_cellHeight);

        // Create cells for this row
        int startIdx = row * m_columns;
        int endIdx = std::min(startIdx + m_columns, (int)m_items.size());

        for (int i = startIdx; i < endIdx; i++) {
            auto* cell = new MangaItemCell();
            cell->setWidth(m_cellWidth);
            cell->setHeight(m_cellHeight);
            cell->setMarginRight(m_cellMargin);

            // Apply display mode to cell
            if (m_listMode) {
                cell->setListMode(true);
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
                onItemClicked(index);
                return true;
            });
            cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

            // Register B button on cell to handle back navigation
            cell->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
                if (m_onBackPressed) {
                    return m_onBackPressed();
                }
                return false;
            }, true);  // hidden action

            // Track focused index
            cell->getFocusEvent()->subscribe([this, index](brls::View*) {
                m_focusedIndex = index;
            });

            rowBox->addView(cell);
            m_cells.push_back(cell);
        }

        m_contentBox->addView(rowBox);
        m_rows.push_back(rowBox);
    }

    // Load remaining covers immediately - ImageLoader handles queuing
    // No artificial delays needed; the image loader's queue prevents overload
    if (totalRows > maxInitialRows) {
        int startCell = maxInitialRows * m_columns;
        // Queue all remaining thumbnails at once - ImageLoader handles concurrency
        for (int i = startCell; i < (int)m_cells.size(); i++) {
            m_cells[i]->loadThumbnailIfNeeded();
        }
    }

    brls::Logger::debug("RecyclingGrid: Grid setup complete with {} cells", m_cells.size());
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
        m_cellHeight = 80;
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
