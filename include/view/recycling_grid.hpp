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
#include <memory>

namespace vitasuwayomi {

class MangaItemCell;

class RecyclingGrid : public brls::ScrollingFrame {
public:
    RecyclingGrid();
    ~RecyclingGrid();

    void setDataSource(const std::vector<Manga>& items);
    void appendItems(const std::vector<Manga>& newItems);   // Append items without rebuilding existing cells
    void updateDataOrder(const std::vector<Manga>& items);  // Update cell data in place without rebuilding grid
    void updateCellData(const std::vector<Manga>& items);   // Update cell metadata in place (unread counts, etc.)
    void removeItems(const std::vector<int>& mangaIdsToRemove);  // Remove specific items without full rebuild
    void setOnItemSelected(std::function<void(const Manga&)> callback);
    void setOnItemLongPressed(std::function<void(const Manga&, int index)> callback);
    void setOnPullToRefresh(std::function<void()> callback);
    void setOnBackPressed(std::function<bool()> callback);
    void setOnEndReached(std::function<void()> callback);  // Fires when focus nears the last row
    void clearViews();

    // Grid customization
    void setGridSize(int columns);  // 4, 6, or 8 columns
    void setCompactMode(bool compact);  // Compact mode (covers only, no titles)
    void setListMode(bool listMode);  // List view instead of grid
    void setListRowSize(int rowSize);  // List row size: 0=small(60), 1=medium(80), 2=large(100), 3=auto
    void setShowLibraryBadge(bool show);  // Show star badge for library items (browser/search only)
    void refreshLibraryBadges();  // Re-evaluate star badge visibility on all cells
    int getGridColumns() const { return m_columns; }
    bool isCompactMode() const { return m_compactMode; }
    bool isListMode() const { return m_listMode; }
    int getListRowSize() const { return m_listRowSize; }

    // Selection mode
    void setSelectionMode(bool enabled);
    bool isSelectionMode() const { return m_selectionMode; }
    void toggleSelection(int index);
    void clearSelection();
    std::vector<int> getSelectedIndices() const;
    bool isIndexSelected(int index) const;
    std::vector<Manga> getSelectedManga() const;
    int getSelectionCount() const;
    void setOnSelectionChanged(std::function<void(int count)> callback);

    // Get item at index
    const Manga* getItem(int index) const;
    int getItemCount() const { return static_cast<int>(m_items.size()); }
    int getFocusedIndex() const { return m_focusedIndex; }
    bool hasCellFocus();  // Returns true if any cell in the grid currently has focus

    // Get first cell for focus transfer
    brls::View* getFirstCell() const;

    // Focus a specific cell by index
    void focusIndex(int index);

    // Load thumbnails around a specific cell index (called on focus change)
    void loadThumbnailsNearIndex(int index);

    // Reset m_thumbnailLoaded on all cells so they can reload.
    // Called after ImageLoader::cancelAll() to fix stale thumbnail states.
    void resetThumbnailLoadStates();

    // Override draw to check scroll position and load visible thumbnails
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;

    static brls::View* create();

private:
    void setupGrid();
    void createRowRange(int startRow, int endRow);  // Create rows [startRow, endRow)
    void buildNextRowBatch();  // Continue incremental grid building
    void updateVisibleCells();
    void loadThumbnailsForScrollPosition();  // Scroll-position-based thumbnail loading
    void onItemClicked(int index);

    std::vector<Manga> m_items;
    std::function<void(const Manga&)> m_onItemSelected;
    std::function<void(const Manga&, int index)> m_onItemLongPressed;
    std::function<void()> m_onPullToRefresh;
    std::function<bool()> m_onBackPressed;
    std::function<void()> m_onEndReached;
    bool m_endReachedFired = false;  // Prevent repeated firing until new data is loaded
    std::function<void(int count)> m_onSelectionChanged;

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
    bool m_showLibraryBadge = false;
    int m_listRowSize = 1;  // 0=small(60), 1=medium(80), 2=large(100), 3=auto

    int m_visibleStartRow = 0;
    int m_lastScrollY = 0;
    bool m_needsUpdate = false;
    float m_lastScrollLoadY = 0.0f;  // Last scroll Y where we triggered thumbnail loading
    int m_scrollLoadCooldown = 0;   // Frame cooldown to throttle scroll-based loading

    // Cached visible row range to avoid full iteration every frame
    int m_cachedFirstVisible = -1;
    int m_cachedLastVisible = -1;

    // Virtual row windowing: only keep a small window of rows attached to m_contentBox
    // to avoid O(totalRows) child iteration in borealis draw loop.
    // Rows outside the window are detached (not deleted) and spacers maintain scroll height.
    brls::Box* m_topSpacer = nullptr;
    brls::Box* m_bottomSpacer = nullptr;
    int m_windowFirstRow = -1;  // First row currently attached to m_contentBox
    int m_windowLastRow = -1;   // One past last row attached
    bool m_virtualScrollEnabled = false;
    void updateRowWindow(int firstVisible, int lastVisible);
    void enableVirtualScroll();   // Called after incremental build completes
    void disableVirtualScroll();  // Called before grid rebuild

    // Long-press tracking - when true, the next click should be skipped
    bool m_longPressTriggered = false;

    // Incremental grid building state - spreads cell creation across frames
    // to prevent multi-second freezes on large libraries (98+ books)
    std::shared_ptr<bool> m_alive;
    int m_incrementalBuildRow = 0;
    int m_totalRowsNeeded = 0;
    bool m_incrementalBuildActive = false;

    // Pending focus - when focusIndex is called during incremental build and the
    // target cell doesn't exist yet, store the index here. buildNextRowBatch
    // applies it once the cell is created, preventing focus from jumping to cell 0.
    int m_pendingFocusIndex = -1;
};

} // namespace vitasuwayomi
