/**
 * VitaSuwayomi - Tracking Search View
 * Visual search results for trackers (MAL/AniList) with cover images and titles
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include <functional>
#include <vector>

namespace vitasuwayomi {

/**
 * A list item cell for displaying tracking search results
 * Shows cover image on left, title beside it, with description and status as subtext
 */
class TrackingSearchResultCell : public brls::Box {
public:
    TrackingSearchResultCell();

    void setResult(const TrackSearchResult& result);
    const TrackSearchResult& getResult() const { return m_result; }

    void onFocusGained() override;
    void onFocusLost() override;

    static brls::View* create();

private:
    void loadCoverImage();
    void updateDisplay();
    std::string truncateText(const std::string& text, size_t maxLen);

    TrackSearchResult m_result;
    bool m_coverLoaded = false;

    brls::Image* m_coverImage = nullptr;
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_descriptionLabel = nullptr;
    brls::Label* m_statusLabel = nullptr;
};

/**
 * A view for displaying tracking search results in a list
 * Shows cover, title, description, and status for each result
 */
class TrackingSearchView : public brls::Box {
public:
    TrackingSearchView(const std::string& trackerName, int trackerId, int mangaId,
                       const std::vector<TrackSearchResult>& results);

    void setOnResultSelected(std::function<void(const TrackSearchResult&)> callback);

private:
    void setupUI();
    void populateResults();
    void onResultSelected(const TrackSearchResult& result);

    std::string m_trackerName;
    int m_trackerId;
    int m_mangaId;
    std::vector<TrackSearchResult> m_results;
    std::function<void(const TrackSearchResult&)> m_onResultSelected;

    brls::Label* m_titleLabel = nullptr;
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_listBox = nullptr;
    std::vector<TrackingSearchResultCell*> m_cells;

    // List item dimensions
    int m_coverWidth = 80;
    int m_coverHeight = 120;
    int m_itemHeight = 140;
    int m_itemMargin = 10;
};

} // namespace vitasuwayomi
