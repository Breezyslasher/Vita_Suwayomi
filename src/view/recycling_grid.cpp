/**
 * VitaSuwayomi - Recycling Grid implementation
 */

#include "view/recycling_grid.hpp"
#include "view/media_item_cell.hpp"

namespace vitasuwayomi {

RecyclingGrid::RecyclingGrid() {
    this->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    // Content box to hold all items
    m_contentBox = new brls::Box();
    m_contentBox->setAxis(brls::Axis::COLUMN);
    m_contentBox->setPadding(10);
    this->setContentView(m_contentBox);

    // PS Vita screen: 960x544, use 5 columns of 150px square items
    m_columns = 5;
    m_visibleRows = 3;
}

void RecyclingGrid::setDataSource(const std::vector<Manga>& items) {
    brls::Logger::debug("RecyclingGrid: setDataSource with {} items", items.size());
    m_items = items;
    rebuildGrid();
    brls::Logger::debug("RecyclingGrid: rebuildGrid completed");
}

void RecyclingGrid::setOnItemSelected(std::function<void(const Manga&)> callback) {
    m_onItemSelected = callback;
}

void RecyclingGrid::clearViews() {
    m_items.clear();
    if (m_contentBox) {
        m_contentBox->clearViews();
    }
}

void RecyclingGrid::rebuildGrid() {
    m_contentBox->clearViews();

    if (m_items.empty()) return;

    // Create rows
    brls::Box* currentRow = nullptr;
    int itemsInRow = 0;

    for (size_t i = 0; i < m_items.size(); i++) {
        if (itemsInRow == 0) {
            currentRow = new brls::Box();
            currentRow->setAxis(brls::Axis::ROW);
            currentRow->setJustifyContent(brls::JustifyContent::FLEX_START);
            currentRow->setMarginBottom(10);
            m_contentBox->addView(currentRow);
        }

        auto* cell = new MangaItemCell();
        cell->setManga(m_items[i]);
        cell->setWidth(150);
        cell->setHeight(185);  // Square cover (140) + labels (~45)
        cell->setMarginRight(10);

        int index = (int)i;
        cell->registerClickAction([this, index](brls::View* view) {
            onItemClicked(index);
            return true;
        });
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

        currentRow->addView(cell);

        itemsInRow++;
        if (itemsInRow >= m_columns) {
            itemsInRow = 0;
        }
    }
}

void RecyclingGrid::onItemClicked(int index) {
    brls::Logger::debug("RecyclingGrid::onItemClicked index={} items={}", index, m_items.size());
    if (index >= 0 && index < (int)m_items.size()) {
        if (m_onItemSelected) {
            brls::Logger::debug("RecyclingGrid: Calling onItemSelected for '{}'", m_items[index].title);
            m_onItemSelected(m_items[index]);
            brls::Logger::debug("RecyclingGrid: onItemSelected completed");
        } else {
            brls::Logger::warning("RecyclingGrid: No onItemSelected callback set");
        }
    } else {
        brls::Logger::error("RecyclingGrid: Invalid index {} (size={})", index, m_items.size());
    }
}

brls::View* RecyclingGrid::create() {
    return new RecyclingGrid();
}

} // namespace vitasuwayomi
