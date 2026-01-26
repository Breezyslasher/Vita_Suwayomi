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
 * A cell for displaying tracking search results with cover image and title
 * Similar to MangaItemCell but for TrackSearchResult
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

    TrackSearchResult m_result;
    std::string m_originalTitle;
    bool m_coverLoaded = false;

    brls::Image* m_coverImage = nullptr;
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_subtitleLabel = nullptr;
};

/**
 * A view for displaying tracking search results in a grid
 * Similar to browse tab search results
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
    brls::Box* m_gridBox = nullptr;
    std::vector<TrackingSearchResultCell*> m_cells;

    int m_columns = 5;
    int m_cellWidth = 160;
    int m_cellHeight = 220;
    int m_cellMargin = 12;
};

} // namespace vitasuwayomi
