/**
 * VitaABS - Podcast Search Tab
 * Search iTunes for podcasts and add them to the server
 */

#pragma once

#include <borealis.hpp>
#include "app/audiobookshelf_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitaabs {

class PodcastSearchTab : public brls::Box {
public:
    PodcastSearchTab(const std::string& libraryId);

    void onFocusGained() override;

private:
    void onSearch(const std::string& query);
    void showSearchResults(const std::vector<PodcastSearchResult>& results);
    void onPodcastSelected(const PodcastSearchResult& podcast);
    void addPodcast(const PodcastSearchResult& podcast);

    std::string m_libraryId;

    brls::Label* m_titleLabel = nullptr;
    brls::Box* m_searchBox = nullptr;
    brls::ScrollingFrame* m_resultsScroll = nullptr;
    brls::Box* m_resultsContainer = nullptr;

    std::vector<PodcastSearchResult> m_searchResults;
};

} // namespace vitaabs
