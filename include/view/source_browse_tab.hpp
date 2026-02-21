/**
 * VitaSuwayomi - Source Browse Tab
 * Browse manga from a specific source (popular, latest, search)
 *
 * NOTE: This is a standalone view component for browsing a single source.
 * Currently unused - SearchTab handles source browsing inline via showSourceBrowser().
 * This component is kept as an alternative implementation that can be pushed
 * as a separate activity if needed:
 *   auto* browseTab = new SourceBrowseTab(source);
 *   brls::Application::pushActivity(new brls::Activity(browseTab));
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include "app/application.hpp"
#include "view/recycling_grid.hpp"
#include <memory>

namespace vitasuwayomi {

class SourceBrowseTab : public brls::Box {
public:
    SourceBrowseTab(const Source& source);
    ~SourceBrowseTab() override;

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
    void loadManga(int focusIndexAfterLoad = -1);
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

    brls::Box* m_headerBox = nullptr;
    brls::Image* m_sourceIcon = nullptr;
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_loadingLabel = nullptr;
    brls::Button* m_popularBtn = nullptr;
    brls::Button* m_latestBtn = nullptr;
    brls::Button* m_searchBtn = nullptr;
    brls::Button* m_loadMoreBtn = nullptr;
    RecyclingGrid* m_contentGrid = nullptr;

    // Lifetime tracking for async operations
    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
