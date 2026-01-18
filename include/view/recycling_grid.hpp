/**
 * VitaSuwayomi - Recycling Grid
 * Efficient grid view with lazy loading for displaying manga items
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include <functional>
#include <vector>

namespace vitasuwayomi {

class MangaItemCell;

class RecyclingGrid : public brls::ScrollingFrame {
public:
    RecyclingGrid();

    void setDataSource(const std::vector<Manga>& items);
    void setOnItemSelected(std::function<void(const Manga&)> callback);
    void clearViews();

    static brls::View* create();

private:
    void setupGrid();
    void updateVisibleCells();
    void onItemClicked(int index);

    std::vector<Manga> m_items;
    std::function<void(const Manga&)> m_onItemSelected;

    brls::Box* m_contentBox = nullptr;
    std::vector<brls::Box*> m_rows;
    std::vector<MangaItemCell*> m_cells;

    int m_columns = 6;
    int m_cellWidth = 140;
    int m_cellHeight = 180;
    int m_cellMargin = 12;
    int m_rowMargin = 10;

    int m_visibleStartRow = 0;
    int m_lastScrollY = 0;
    bool m_needsUpdate = false;
};

} // namespace vitasuwayomi
