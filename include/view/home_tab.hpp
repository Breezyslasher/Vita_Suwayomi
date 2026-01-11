/**
 * VitaSuwayomi - Home Tab
 * Shows Continue Reading and Recent Chapter Updates
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

class MangaItemCell;  // Forward declaration

class HomeTab : public brls::Box {
public:
    HomeTab();
    ~HomeTab() override;

    void onFocusGained() override;
    void refresh();

private:
    void loadContent();
    void loadContinueReading();
    void loadRecentUpdates();
    void populateHorizontalRow(brls::Box* container, const std::vector<Manga>& items);
    void populateUpdatesRow(brls::Box* container, const std::vector<RecentUpdate>& updates);
    void onMangaSelected(const Manga& manga);
    void onUpdateSelected(const RecentUpdate& update);

    // Check if this tab is still valid (not destroyed)
    bool isValid() const { return m_alive && *m_alive; }

    brls::Label* m_titleLabel = nullptr;
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;

    // Continue Reading section (horizontal row of manga with reading progress)
    brls::Label* m_continueLabel = nullptr;
    brls::Box* m_continueBox = nullptr;
    std::vector<Manga> m_continueReading;

    // Recent Updates section (horizontal row of recent chapter updates)
    brls::Label* m_recentUpdatesLabel = nullptr;
    brls::Box* m_recentUpdatesBox = nullptr;
    std::vector<RecentUpdate> m_recentUpdates;

    bool m_loaded = false;

    // Shared pointer to track if this object is still alive
    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
