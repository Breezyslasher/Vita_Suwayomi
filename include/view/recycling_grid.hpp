/**
 * VitaSuwayomi - Recycling Grid
 * Efficient grid view with lazy loading for displaying manga items
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include <functional>
#include <vector>
#include <set>

namespace vitasuwayomi {

class MangaItemCell;

class RecyclingGrid : public brls::ScrollingFrame {
public:
    RecyclingGrid();

    void setDataSource(const std::vector<Manga>& items);
    void setOnItemSelected(std::function<void(const Manga&)> callback);
    void setOnItemLongPressed(std::function<void(const Manga&, int index)> callback);
    void setOnPullToRefresh(std::function<void()> callback);
    void setOnBackPressed(std::function<bool()> callback);
    void clearViews();

    // Grid customization
    void setGridSize(int columns);  // 4, 6, or 8 columns
    void setCompactMode(bool compact);  // Compact mode (covers only, no titles)
    void setListMode(bool listMode);  // List view instead of grid
    int getGridColumns() const { return m_columns; }
    bool isCompactMode() const { return m_compactMode; }
    bool isListMode() const { return m_listMode; }

    // Selection mode
    void setSelectionMode(bool enabled);
    bool isSelectionMode() const { return m_selectionMode; }
    void toggleSelection(int index);
    void clearSelection();
    std::vector<int> getSelectedIndices() const;
    std::vector<Manga> getSelectedManga() const;
    int getSelectionCount() const;

    // Get item at index
    const Manga* getItem(int index) const;
    int getItemCount() const { return static_cast<int>(m_items.size()); }
    int getFocusedIndex() const { return m_focusedIndex; }

    // Get first cell for focus transfer
    brls::View* getFirstCell() const;

    static brls::View* create();

private:
    void setupGrid();
    void updateVisibleCells();
    void onItemClicked(int index);

    std::vector<Manga> m_items;
    std::function<void(const Manga&)> m_onItemSelected;
    std::function<void(const Manga&, int index)> m_onItemLongPressed;
    std::function<void()> m_onPullToRefresh;
    std::function<bool()> m_onBackPressed;

    // Pull-to-refresh state
    bool m_isPulling = false;
    float m_pullDistance = 0.0f;
    static constexpr float PULL_THRESHOLD = 80.0f;  // Pixels to pull before triggering refresh

    // Selection mode
    bool m_selectionMode = false;
    std::set<int> m_selectedIndices;
    int m_focusedIndex = -1;

    brls::Box* m_contentBox = nullptr;
    std::vector<brls::Box*> m_rows;
    std::vector<MangaItemCell*> m_cells;

    int m_columns = 6;
    int m_cellWidth = 140;
    int m_cellHeight = 180;
    int m_cellMargin = 12;
    int m_rowMargin = 10;
    bool m_compactMode = false;
    bool m_listMode = false;

    int m_visibleStartRow = 0;
    int m_lastScrollY = 0;
    bool m_needsUpdate = false;
};

} // namespace vitasuwayomi
