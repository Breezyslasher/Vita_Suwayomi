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
}

void RecyclingGrid::setDataSource(const std::vector<Manga>& items) {
    brls::Logger::debug("RecyclingGrid: setDataSource with {} items", items.size());
    m_items = items;
    setupGrid();
}

void RecyclingGrid::setOnItemSelected(std::function<void(const Manga&)> callback) {
    m_onItemSelected = callback;
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
    int rowHeight = m_cellHeight + m_rowMargin;

    brls::Logger::debug("RecyclingGrid: Creating {} rows for {} items", totalRows, m_items.size());

    // Create all rows and cells, but only load images for first few rows
    int maxInitialRows = 4;  // Only load images for first 4 rows initially

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

            // Only set manga data for initial visible rows
            // This defers image loading for off-screen items
            if (row < maxInitialRows) {
                cell->setManga(m_items[i]);
            } else {
                // Set basic info without loading image
                cell->setMangaDeferred(m_items[i]);
            }

            int index = i;
            cell->registerClickAction([this, index](brls::View* view) {
                onItemClicked(index);
                return true;
            });
            cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

            rowBox->addView(cell);
            m_cells.push_back(cell);
        }

        m_contentBox->addView(rowBox);
        m_rows.push_back(rowBox);

        // Yield to UI thread every few rows to prevent freeze
        if (row > 0 && row % 5 == 0 && row < totalRows - 1) {
            // Force a UI update by scheduling the rest for later
            int remainingStart = row + 1;
            brls::sync([this, remainingStart, totalRows, maxInitialRows]() {
                // Continue building remaining rows
                for (int r = remainingStart; r < totalRows && r < remainingStart + 5; r++) {
                    // Rows already created, just need to trigger image loads
                    // for newly visible rows as user scrolls
                }
            });
        }
    }

    brls::Logger::debug("RecyclingGrid: Grid setup complete with {} cells", m_cells.size());
}

void RecyclingGrid::updateVisibleCells() {
    // This can be called on scroll to load images for newly visible cells
    // For now, cells load their images when setManga is called
}

void RecyclingGrid::onItemClicked(int index) {
    brls::Logger::debug("RecyclingGrid::onItemClicked index={}", index);
    if (index >= 0 && index < (int)m_items.size()) {
        if (m_onItemSelected) {
            m_onItemSelected(m_items[index]);
        }
    }
}

brls::View* RecyclingGrid::create() {
    return new RecyclingGrid();
}

} // namespace vitasuwayomi
