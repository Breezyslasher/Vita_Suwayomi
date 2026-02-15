/**
 * VitaSuwayomi - History Tab
 * Shows reading history with date/time and quick resume
 * Loads all data at once, caches to disk for instant display
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
    void loadFromCache();
    void fetchFromServer();
    void buildList();
    void onHistoryItemSelected(const ReadingHistoryItem& item);
    void showHistoryItemMenu(const ReadingHistoryItem& item, int index);
    void markChapterUnread(const ReadingHistoryItem& item);
    std::string formatTimestamp(int64_t timestamp);
    std::string formatRelativeTime(int64_t timestamp);
    brls::Box* createHistoryItemRow(const ReadingHistoryItem& item, int index);

    // UI Components
    brls::Label* m_titleLabel = nullptr;
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;
    brls::Box* m_emptyStateBox = nullptr;
    brls::Label* m_loadingLabel = nullptr;
    brls::Button* m_refreshBtn = nullptr;
    std::vector<brls::Box*> m_itemRows;

    // Data
    std::vector<ReadingHistoryItem> m_historyItems;
    bool m_loaded = false;
    bool m_isFetching = false;

    // Shared pointer to track if this object is still alive
    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
