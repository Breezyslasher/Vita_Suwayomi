/**
 * VitaABS - Library Section Tab
 * Shows content for a single library section (for sidebar mode)
 * Collections, categories (genres) appear as browsable content within the tab
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include "app/audiobookshelf_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitaabs {

// View mode for the library section
enum class LibraryViewMode {
    ALL_ITEMS,      // Show all items in the library
    COLLECTIONS,    // Show collections as browsable items
    CATEGORIES,     // Show categories/genres as browsable items
    FILTERED,       // Showing items filtered by collection or category
    DOWNLOADED      // Show only downloaded items
};

class LibrarySectionTab : public brls::Box {
public:
    LibrarySectionTab(const std::string& sectionKey, const std::string& title, const std::string& sectionType = "");
    ~LibrarySectionTab() override;

    void onFocusGained() override;

private:
    void loadContent();
    void loadCollections();
    void loadGenres();
    void loadDownloadedItems();
    void showAllItems();
    void showCollections();
    void showCategories();
    void showDownloaded();
    void onItemSelected(const MediaItem& item);
    void onCollectionSelected(const MediaItem& collection);
    void onGenreSelected(const GenreItem& genre);
    void updateViewModeButtons();
    void filterByDownloaded();
    void hideNavigationButtons();  // Hide all nav buttons for offline/downloaded-only mode

    // Podcast management
    void openPodcastSearch();
    void checkAllNewEpisodes();

    // Check if this tab is still valid (not destroyed)
    bool isValid() const { return m_alive && *m_alive; }

    std::string m_sectionKey;
    std::string m_title;
    std::string m_sectionType;  // "movie", "show", "artist"

    brls::Label* m_titleLabel = nullptr;

    // View mode selector buttons
    brls::Box* m_viewModeBox = nullptr;
    brls::Button* m_allBtn = nullptr;
    brls::Button* m_collectionsBtn = nullptr;
    brls::Button* m_categoriesBtn = nullptr;
    brls::Button* m_downloadedBtn = nullptr;  // Downloaded items filter
    brls::Button* m_backBtn = nullptr;  // Back button when in filtered view

    // Podcast management buttons
    brls::Button* m_findPodcastsBtn = nullptr;
    brls::Button* m_checkEpisodesBtn = nullptr;

    // Main content grid
    RecyclingGrid* m_contentGrid = nullptr;

    // Data
    std::vector<MediaItem> m_items;
    std::vector<MediaItem> m_collections;
    std::vector<GenreItem> m_genres;
    std::vector<MediaItem> m_downloadedItems;  // Locally downloaded items

    LibraryViewMode m_viewMode = LibraryViewMode::ALL_ITEMS;
    std::string m_filterTitle;  // Title of current filter (collection/genre name)
    bool m_loaded = false;
    bool m_collectionsLoaded = false;
    bool m_genresLoaded = false;
    bool m_downloadedLoaded = false;

    // Shared pointer to track if this object is still alive
    // Used by async callbacks to check validity before updating UI
    std::shared_ptr<bool> m_alive;
};

} // namespace vitaabs
