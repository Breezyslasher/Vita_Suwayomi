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
    void willDisappear(bool resetState) override;
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
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
    bool m_isLoadingMore = false;  // Guard against concurrent load-more
    brls::Button* m_refreshBtn = nullptr;
    std::vector<brls::Box*> m_itemRows;
    int m_focusIndexAfterRebuild = -1;

    // Data
    std::vector<ReadingHistoryItem> m_historyItems;
    bool m_loaded = false;
    bool m_hasMoreItems = true;
    int m_currentOffset = 0;
    static const int ITEMS_PER_PAGE = 20;

    // Scroll-velocity tracking for deferring ImageLoader texture uploads
    // while the user is scrolling fast (each upload costs ~15-20ms on Vita).
    float m_prevScrollY = 0.0f;
    int m_scrollSettledFrames = 0;
    bool m_uploadsDeferred = false;

    // Shared pointer to track if this object is still alive
    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
