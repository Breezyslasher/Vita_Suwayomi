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
                float scrollY = this->getScrollingY();
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
    // Load first 4 rows immediately, then stagger loading the rest
    int maxInitialRows = 4;

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

    // Schedule loading of remaining covers after a short delay
    if (totalRows > maxInitialRows) {
        brls::sync([this, maxInitialRows]() {
            // Load remaining covers in batches
            int batchSize = 6;  // One row at a time
            int startCell = maxInitialRows * m_columns;

            for (int i = startCell; i < (int)m_cells.size(); i += batchSize) {
                int batchEnd = std::min(i + batchSize, (int)m_cells.size());
                int delayMs = ((i - startCell) / batchSize) * 100;  // 100ms between batches

                brls::sync([this, i, batchEnd]() {
                    for (int j = i; j < batchEnd; j++) {
                        if (j < (int)m_cells.size()) {
                            m_cells[j]->loadThumbnailIfNeeded();
                        }
                    }
                });
            }
        });
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

} // namespace vitasuwayomi
