/**
 * VitaSuwayomi - History Tab
 * Shows reading history with date/time and quick resume
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include <set>
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

class HistoryTab : public brls::Box {
public:
    HistoryTab();
    ~HistoryTab() override;

    void onFocusGained() override;
    void refresh();

private:
    void loadHistory();
    void loadMoreHistory();
    void onHistoryItemSelected(const ReadingHistoryItem& item);
    void showHistoryItemMenu(const ReadingHistoryItem& item, int index);
    void markChapterUnread(const ReadingHistoryItem& item);
    std::string formatTimestamp(int64_t timestamp);
    std::string formatRelativeTime(int64_t timestamp);
    void rebuildHistoryList();
    void appendHistoryItems(const std::vector<ReadingHistoryItem>& items, size_t startIndex);
    brls::Box* createHistoryItemRow(const ReadingHistoryItem& item, int index);

    // UI Components
    brls::Label* m_titleLabel = nullptr;
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;
    brls::Box* m_emptyStateBox = nullptr;
    brls::Label* m_loadingLabel = nullptr;
    brls::Button* m_refreshBtn = nullptr;  // Header refresh button for focus management
    std::vector<brls::Box*> m_itemRows;  // Track item rows for focus management
    int m_focusIndexAfterRebuild = -1;  // Index to focus after rebuilding list

    // Infinite scroll
    bool m_isLoadingMore = false;  // Prevent multiple simultaneous loads
    std::set<int> m_itemsWithScrollListeners;  // Track which item indices have listeners
    void setupInfiniteScroll();  // Set up focus listeners for infinite scroll

    // Data
    std::vector<ReadingHistoryItem> m_historyItems;
    bool m_loaded = false;
    bool m_hasMoreItems = true;
    int m_currentOffset = 0;
    static const int ITEMS_PER_PAGE = 20;

    // Shared pointer to track if this object is still alive
    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
