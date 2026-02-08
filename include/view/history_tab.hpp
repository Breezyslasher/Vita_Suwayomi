/**
 * VitaSuwayomi - History Tab
 * Shows reading history with date/time and quick resume
 */

#pragma once

#include <borealis.hpp>
#include <memory>
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

    // UI Components
    brls::Label* m_titleLabel = nullptr;
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;
    brls::Box* m_emptyStateBox = nullptr;
    brls::Label* m_loadingLabel = nullptr;
    brls::Button* m_loadMoreBtn = nullptr;

    // Data
    std::vector<ReadingHistoryItem> m_historyItems;
    bool m_loaded = false;
    bool m_hasMoreItems = true;
    int m_currentOffset = 0;
    static const int ITEMS_PER_PAGE = 50;

    // Shared pointer to track if this object is still alive
    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
