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
    void appendItems(const std::vector<Manga>& items);  // Add more items without clearing
    void setOnItemSelected(std::function<void(const Manga&)> callback);
    void setOnLoadMore(std::function<void()> callback);  // Called when "Load More" is pressed
    void setHasMorePages(bool hasMore);  // Show/hide load more button
    void setLoading(bool loading);  // Show loading state
    void clearViews();

    static brls::View* create();

private:
    void setupGrid();
    void appendToGrid(const std::vector<Manga>& newItems);  // Add items to existing grid
    void updateVisibleCells();
    void onItemClicked(int index);
    void updateLoadMoreButton();

    std::vector<Manga> m_items;
    std::function<void(const Manga&)> m_onItemSelected;
    std::function<void()> m_onLoadMore;
    bool m_hasMorePages = false;
    bool m_isLoading = false;

    brls::Button* m_loadMoreBtn = nullptr;

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
