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

void RecyclingGrid::setOnLoadMore(std::function<void()> callback) {
    m_onLoadMore = callback;
}

void RecyclingGrid::setHasMorePages(bool hasMore) {
    m_hasMorePages = hasMore;
    updateLoadMoreButton();
}

void RecyclingGrid::setLoading(bool loading) {
    m_isLoading = loading;
    updateLoadMoreButton();
}

void RecyclingGrid::updateLoadMoreButton() {
    if (!m_loadMoreBtn) return;

    if (m_hasMorePages && !m_isLoading) {
        m_loadMoreBtn->setText("Load More");
        m_loadMoreBtn->setVisibility(brls::Visibility::VISIBLE);
    } else if (m_isLoading) {
        m_loadMoreBtn->setText("Loading...");
        m_loadMoreBtn->setVisibility(brls::Visibility::VISIBLE);
    } else {
        m_loadMoreBtn->setVisibility(brls::Visibility::GONE);
    }
}

void RecyclingGrid::appendItems(const std::vector<Manga>& items) {
    if (items.empty()) return;

    brls::Logger::debug("RecyclingGrid: Appending {} items to existing {}", items.size(), m_items.size());

    // Append to data
    m_items.insert(m_items.end(), items.begin(), items.end());

    // Add new rows to grid
    appendToGrid(items);
}

void RecyclingGrid::appendToGrid(const std::vector<Manga>& newItems) {
    if (newItems.empty()) return;

    // Remove load more button temporarily
    if (m_loadMoreBtn && m_loadMoreBtn->getParent()) {
        m_contentBox->removeView(m_loadMoreBtn);
    }

    size_t startIndex = m_items.size() - newItems.size();

    // Calculate items in last row
    int itemsInLastRow = (startIndex > 0) ? (startIndex % m_columns) : 0;

    // If the last row isn't full, add to it first
    brls::Box* currentRow = nullptr;
    size_t newItemIndex = 0;

    if (itemsInLastRow > 0 && !m_rows.empty()) {
        currentRow = m_rows.back();
        while (itemsInLastRow < m_columns && newItemIndex < newItems.size()) {
            auto* cell = new MangaItemCell();
            cell->setWidth(m_cellWidth);
            cell->setHeight(m_cellHeight);
            cell->setMarginRight(m_cellMargin);
            cell->setManga(newItems[newItemIndex]);

            int index = startIndex + newItemIndex;
            cell->registerClickAction([this, index](brls::View* view) {
                onItemClicked(index);
                return true;
            });
            cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

            currentRow->addView(cell);
            m_cells.push_back(cell);

            itemsInLastRow++;
            newItemIndex++;
        }
    }

    // Create new rows for remaining items
    while (newItemIndex < newItems.size()) {
        auto* rowBox = new brls::Box();
        rowBox->setAxis(brls::Axis::ROW);
        rowBox->setJustifyContent(brls::JustifyContent::FLEX_START);
        rowBox->setMarginBottom(m_rowMargin);
        rowBox->setHeight(m_cellHeight);

        for (int col = 0; col < m_columns && newItemIndex < newItems.size(); col++) {
            auto* cell = new MangaItemCell();
            cell->setWidth(m_cellWidth);
            cell->setHeight(m_cellHeight);
            cell->setMarginRight(m_cellMargin);
            cell->setManga(newItems[newItemIndex]);

            int index = startIndex + newItemIndex;
            cell->registerClickAction([this, index](brls::View* view) {
                onItemClicked(index);
                return true;
            });
            cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

            rowBox->addView(cell);
            m_cells.push_back(cell);
            newItemIndex++;
        }

        m_contentBox->addView(rowBox);
        m_rows.push_back(rowBox);
    }

    // Re-add load more button at the end
    if (m_loadMoreBtn) {
        m_contentBox->addView(m_loadMoreBtn);
    }

    brls::Logger::debug("RecyclingGrid: Grid now has {} rows, {} cells", m_rows.size(), m_cells.size());
}

void RecyclingGrid::clearViews() {
    m_items.clear();
    m_rows.clear();
    m_cells.clear();
    m_hasMorePages = false;
    m_isLoading = false;
    if (m_contentBox) {
        m_contentBox->clearViews();
    }
    // Recreate load more button
    m_loadMoreBtn = nullptr;
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

    // Create "Load More" button at the bottom
    if (!m_loadMoreBtn) {
        m_loadMoreBtn = new brls::Button();
        m_loadMoreBtn->setText("Load More");
        m_loadMoreBtn->setMarginTop(15);
        m_loadMoreBtn->setMarginBottom(20);
        m_loadMoreBtn->setWidth(200);
        m_loadMoreBtn->setHeight(50);
        m_loadMoreBtn->setCornerRadius(8);
        m_loadMoreBtn->setVisibility(brls::Visibility::GONE);  // Hidden by default

        m_loadMoreBtn->registerClickAction([this](brls::View* view) {
            if (m_onLoadMore && !m_isLoading) {
                m_onLoadMore();
            }
            return true;
        });
        m_loadMoreBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_loadMoreBtn));
    }
    m_contentBox->addView(m_loadMoreBtn);
    updateLoadMoreButton();

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
        if (m_onItemSelected) {
            m_onItemSelected(m_items[index]);
        }
    }
}

brls::View* RecyclingGrid::create() {
    return new RecyclingGrid();
}

} // namespace vitasuwayomi
