/**
 * VitaSuwayomi - Source Browse Tab
 * Browse manga from a specific source (popular, latest, search)
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitasuwayomi {

class SourceBrowseTab : public brls::Box {
public:
    SourceBrowseTab(const Source& source);

    void onFocusGained() override;

private:
    enum class BrowseMode {
        POPULAR,
        LATEST,
        SEARCH
    };

    void loadPopular();
    void loadLatest();
    void loadSearch(const std::string& query);
    void loadNextPage();
    void loadManga();
    void updateGrid();
    void updateModeButtons();
    void updateLoadMoreButton();
    void showSearchDialog();
    void onMangaSelected(const Manga& manga);

    Source m_source;
    BrowseMode m_browseMode = BrowseMode::POPULAR;
    std::string m_searchQuery;
    int m_currentPage = 1;
    bool m_hasNextPage = false;
    std::vector<Manga> m_mangaList;

    brls::Label* m_titleLabel = nullptr;
    brls::Button* m_popularBtn = nullptr;
    brls::Button* m_latestBtn = nullptr;
    brls::Button* m_searchBtn = nullptr;
    brls::Button* m_loadMoreBtn = nullptr;
    RecyclingGrid* m_contentGrid = nullptr;
};

} // namespace vitasuwayomi
