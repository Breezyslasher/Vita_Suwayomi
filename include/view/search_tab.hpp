/**
 * VitaSuwayomi - Search/Browse Tab
 * Search manga across sources and browse source catalogs
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitasuwayomi {

// Browse mode
enum class BrowseMode {
    SOURCES,        // Show list of available sources
    POPULAR,        // Browse popular manga from selected source
    LATEST,         // Browse latest manga from selected source
    SEARCH_RESULTS  // Show search results
};

class SearchTab : public brls::Box {
public:
    SearchTab();

    void onFocusGained() override;

private:
    void loadSources();
    void loadPopularManga(int64_t sourceId);
    void loadLatestManga(int64_t sourceId);
    void performSearch(const std::string& query);
    void performSourceSearch(int64_t sourceId, const std::string& query);
    void onSourceSelected(const Source& source);
    void onMangaSelected(const Manga& manga);
    void showSources();
    void showSourceBrowser(const Source& source);
    void loadNextPage();
    void updateModeButtons();

    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_searchLabel = nullptr;
    brls::Label* m_resultsLabel = nullptr;

    // Mode selector buttons
    brls::Box* m_modeBox = nullptr;
    brls::Button* m_sourcesBtn = nullptr;
    brls::Button* m_popularBtn = nullptr;
    brls::Button* m_latestBtn = nullptr;
    brls::Button* m_backBtn = nullptr;

    // Source selector
    brls::Box* m_sourceListBox = nullptr;

    // Main content grid
    RecyclingGrid* m_contentGrid = nullptr;

    // State
    BrowseMode m_browseMode = BrowseMode::SOURCES;
    int64_t m_currentSourceId = 0;
    std::string m_currentSourceName;
    std::string m_searchQuery;
    int m_currentPage = 1;
    bool m_hasNextPage = false;

    // Data
    std::vector<Source> m_sources;
    std::vector<Manga> m_mangaList;
};

} // namespace vitasuwayomi
