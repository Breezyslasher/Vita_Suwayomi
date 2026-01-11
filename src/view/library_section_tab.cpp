/**
 * VitaABS - Library Section Tab implementation
 */

#include "view/library_section_tab.hpp"
#include "view/podcast_search_tab.hpp"
#include "app/audiobookshelf_client.hpp"
#include "app/downloads_manager.hpp"
#include "view/media_item_cell.hpp"
#include "view/media_detail_view.hpp"
#include "app/application.hpp"
#include "utils/async.hpp"
#include <map>
#include <tuple>

namespace vitaabs {

LibrarySectionTab::LibrarySectionTab(const std::string& sectionKey, const std::string& title, const std::string& sectionType)
    : m_sectionKey(sectionKey), m_title(title), m_sectionType(sectionType) {

    // Create alive flag for async callback safety
    m_alive = std::make_shared<bool>(true);

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(title);
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(15);
    this->addView(m_titleLabel);

    const auto& settings = Application::getInstance().getSettings();

    // View mode selector (All / Collections / Categories)
    m_viewModeBox = new brls::Box();
    m_viewModeBox->setAxis(brls::Axis::ROW);
    m_viewModeBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_viewModeBox->setAlignItems(brls::AlignItems::CENTER);
    m_viewModeBox->setMarginBottom(15);

    // All Items button
    m_allBtn = new brls::Button();
    m_allBtn->setText("All");
    m_allBtn->setMarginRight(10);
    m_allBtn->registerClickAction([this](brls::View* view) {
        showAllItems();
        return true;
    });
    m_viewModeBox->addView(m_allBtn);

    // Collections button (only show if collections are enabled)
    if (settings.showCollections) {
        m_collectionsBtn = new brls::Button();
        m_collectionsBtn->setText("Collections");
        m_collectionsBtn->setMarginRight(10);
        m_collectionsBtn->registerClickAction([this](brls::View* view) {
            showCollections();
            return true;
        });
        m_viewModeBox->addView(m_collectionsBtn);
    }

    // Downloaded button - always show
    m_downloadedBtn = new brls::Button();
    m_downloadedBtn->setText("Downloaded");
    m_downloadedBtn->setMarginRight(10);
    m_downloadedBtn->registerClickAction([this](brls::View* view) {
        showDownloaded();
        return true;
    });
    m_viewModeBox->addView(m_downloadedBtn);

    // Note: Categories/Genres button removed - Audiobookshelf doesn't have a genre browsing API

    // Back button (hidden by default, shown in filtered view)
    m_backBtn = new brls::Button();
    m_backBtn->setText("< Back");
    m_backBtn->setVisibility(brls::Visibility::GONE);
    m_backBtn->registerClickAction([this](brls::View* view) {
        showAllItems();
        return true;
    });
    m_viewModeBox->addView(m_backBtn);

    // Find Podcasts button (only for podcast libraries)
    if (sectionType == "podcast") {
        m_findPodcastsBtn = new brls::Button();
        m_findPodcastsBtn->setText("+ Find Podcasts");
        m_findPodcastsBtn->setMarginLeft(20);
        m_findPodcastsBtn->registerClickAction([this](brls::View* view) {
            openPodcastSearch();
            return true;
        });
        m_viewModeBox->addView(m_findPodcastsBtn);

        // Check New Episodes button
        m_checkEpisodesBtn = new brls::Button();
        m_checkEpisodesBtn->setText("Check Episodes");
        m_checkEpisodesBtn->setMarginLeft(10);
        m_checkEpisodesBtn->registerClickAction([this](brls::View* view) {
            checkAllNewEpisodes();
            return true;
        });
        m_viewModeBox->addView(m_checkEpisodesBtn);
    }

    this->addView(m_viewModeBox);

    // Content grid
    m_contentGrid = new RecyclingGrid();
    m_contentGrid->setGrow(1.0f);
    m_contentGrid->setOnItemSelected([this](const MediaItem& item) {
        onItemSelected(item);
    });
    this->addView(m_contentGrid);

    // Load content immediately
    brls::Logger::debug("LibraryTab: Created for section {} ({}) type={}", m_sectionKey, m_title, m_sectionType);
    loadContent();
}

LibrarySectionTab::~LibrarySectionTab() {
    // Mark as no longer alive to prevent async callbacks from updating destroyed UI
    if (m_alive) {
        *m_alive = false;
    }
    brls::Logger::debug("LibrarySectionTab: Destroyed for section {}", m_sectionKey);
}

void LibrarySectionTab::onFocusGained() {
    brls::Box::onFocusGained();

    if (!m_loaded) {
        loadContent();
    }
}

void LibrarySectionTab::loadContent() {
    brls::Logger::debug("LibrarySectionTab::loadContent - section: {} (async)", m_sectionKey);

    const auto& settings = Application::getInstance().getSettings();
    std::string key = m_sectionKey;
    std::weak_ptr<bool> aliveWeak = m_alive;  // Capture weak_ptr for async safety

    // Always load downloaded items first (for filtering and offline mode)
    loadDownloadedItems();

    // If showOnlyDownloaded is enabled, just show downloaded items and hide navigation
    if (settings.showOnlyDownloaded) {
        brls::Logger::info("LibraryTab: Show only downloaded mode enabled");
        m_viewMode = LibraryViewMode::DOWNLOADED;
        m_loaded = true;
        // Hide all navigation buttons in downloaded-only mode
        hideNavigationButtons();
        return;
    }

    asyncRun([this, key, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;

        if (client.fetchLibraryItems(key, items)) {
            brls::Logger::info("LibraryTab: Got {} items for section {}", items.size(), key);

            brls::sync([this, items, aliveWeak]() {
                // Check if object is still alive before updating UI
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) {
                    brls::Logger::debug("LibraryTab: Tab destroyed, skipping UI update");
                    return;
                }

                m_items = items;
                // Only update grid if we're in ALL_ITEMS mode
                if (m_viewMode == LibraryViewMode::ALL_ITEMS) {
                    m_contentGrid->setDataSource(m_items);
                }
                m_loaded = true;
            });
        } else {
            brls::Logger::error("LibraryTab: Failed to load content for section {} - showing downloaded items", key);
            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                // Offline - show downloaded items instead and hide navigation
                // Only add "(Offline)" if not already in title (e.g. offline-mode tabs)
                if (m_title.find("(Offline)") == std::string::npos) {
                    m_titleLabel->setText(m_title + " (Offline)");
                }
                m_viewMode = LibraryViewMode::DOWNLOADED;
                m_contentGrid->setDataSource(m_downloadedItems);
                hideNavigationButtons();
                m_loaded = true;
            });
        }
    });

    // Preload collections for quick switching
    if (settings.showCollections) {
        loadCollections();
    }
    // Note: Genre preloading removed - Audiobookshelf doesn't have a genre browsing API
}

void LibrarySectionTab::loadCollections() {
    std::string key = m_sectionKey;
    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, key, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<Collection> collections;

        if (client.fetchLibraryCollections(key, collections)) {
            brls::Logger::info("LibrarySectionTab: Got {} collections for section {}", collections.size(), key);

            // Convert Collection to MediaItem for display
            std::vector<MediaItem> collectionItems;
            for (const auto& col : collections) {
                MediaItem item;
                item.id = col.id;
                item.title = col.name;
                item.description = col.description;
                item.coverPath = col.coverPath;
                item.type = "collection";
                item.mediaType = MediaType::UNKNOWN;
                collectionItems.push_back(item);
            }

            brls::sync([this, collectionItems, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_collections = collectionItems;
                m_collectionsLoaded = true;

                // Hide collections button if none available
                if (m_collections.empty() && m_collectionsBtn) {
                    m_collectionsBtn->setVisibility(brls::Visibility::GONE);
                }
            });
        } else {
            brls::Logger::debug("LibrarySectionTab: No collections for section {}", key);
            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_collectionsLoaded = true;
                if (m_collectionsBtn) {
                    m_collectionsBtn->setVisibility(brls::Visibility::GONE);
                }
            });
        }
    });
}

void LibrarySectionTab::loadGenres() {
    // Audiobookshelf doesn't have a dedicated genre browsing API like Plex
    // Genre filtering would require fetching all items and extracting unique genres
    // For now, we disable the genres feature
    brls::Logger::debug("LibrarySectionTab: Genre browsing not supported in Audiobookshelf");

    m_genresLoaded = true;
    m_genres.clear();

    if (m_categoriesBtn) {
        m_categoriesBtn->setVisibility(brls::Visibility::GONE);
    }
}

void LibrarySectionTab::loadDownloadedItems() {
    brls::Logger::debug("LibrarySectionTab::loadDownloadedItems - section: {}", m_sectionKey);

    std::weak_ptr<bool> aliveWeak = m_alive;
    std::string sectionType = m_sectionType;

    // Load downloaded items synchronously (they're already local)
    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.init();

    auto downloads = mgr.getDownloads();
    std::vector<MediaItem> downloadedItems;

    // For podcasts, group episodes by podcast
    // Map: podcastId -> vector of {episodeId, episodeTitle, coverPath}
    std::map<std::string, std::vector<std::tuple<std::string, std::string, std::string, std::string>>> podcastEpisodes;
    // Also track podcast name for each podcastId
    std::map<std::string, std::string> podcastNames;

    for (const auto& dl : downloads) {
        // Filter by media type to match this library's type
        bool matchesType = false;
        if (sectionType == "book" && (dl.mediaType == "book" || dl.mediaType.empty())) {
            matchesType = true;
        } else if (sectionType == "podcast" && (dl.mediaType == "podcast" || dl.mediaType == "episode")) {
            matchesType = true;
        }

        if (matchesType && dl.state == DownloadState::COMPLETED) {
            brls::Logger::debug("LibrarySectionTab: Downloaded item '{}' - localCoverPath='{}'",
                               dl.title, dl.localCoverPath);

            // For podcast episodes, group by podcast
            if (sectionType == "podcast" && !dl.episodeId.empty()) {
                // This is a podcast episode - group by podcastId
                std::string podcastId = dl.itemId;
                // Use parentTitle first, fall back to authorName, then "Unknown Podcast"
                std::string podcastName = !dl.parentTitle.empty() ? dl.parentTitle :
                                          (!dl.authorName.empty() ? dl.authorName : "Unknown Podcast");
                podcastNames[podcastId] = podcastName;
                // Store: episodeId, episodeTitle, coverPath, podcastName
                podcastEpisodes[podcastId].push_back(std::make_tuple(dl.episodeId, dl.title, dl.localCoverPath, podcastName));
            } else {
                // Audiobook or standalone item
                MediaItem item;
                item.id = dl.itemId;
                item.title = dl.title;
                item.authorName = dl.authorName;
                item.description = dl.description;
                item.duration = dl.duration;
                item.currentTime = dl.currentTime;
                item.coverPath = dl.localCoverPath;
                item.type = dl.mediaType;
                item.mediaType = MediaType::BOOK;

                // Mark as downloaded for UI purposes
                item.isDownloaded = true;

                downloadedItems.push_back(item);
            }
        }
    }

    // Convert podcast episodes to MediaItems
    for (const auto& [podcastId, episodes] : podcastEpisodes) {
        if (episodes.size() == 1) {
            // Single episode - show as individual episode that plays directly
            const auto& ep = episodes[0];
            MediaItem item;
            item.id = podcastId;  // Keep podcast ID for playback
            item.podcastId = podcastId;
            item.episodeId = std::get<0>(ep);  // Episode ID
            item.title = std::get<1>(ep);  // Episode title
            item.coverPath = std::get<2>(ep);  // Cover path
            item.authorName = std::get<3>(ep);  // Podcast name as author
            item.type = "episode";
            item.mediaType = MediaType::PODCAST_EPISODE;
            item.isDownloaded = true;

            downloadedItems.push_back(item);
        } else {
            // Multiple episodes - show as grouped podcast
            MediaItem item;
            item.id = podcastId;
            item.title = podcastNames[podcastId];  // Podcast name
            item.coverPath = std::get<2>(episodes[0]);  // Cover path from first episode
            int episodeCount = episodes.size();
            item.authorName = std::to_string(episodeCount) + " episodes downloaded";
            item.type = "podcast";
            item.mediaType = MediaType::PODCAST;
            item.isDownloaded = true;

            downloadedItems.push_back(item);
        }
    }

    brls::Logger::info("LibrarySectionTab: Found {} downloaded items for type {}", downloadedItems.size(), sectionType);

    m_downloadedItems = downloadedItems;
    m_downloadedLoaded = true;

    // If in DOWNLOADED view mode, update the grid
    if (m_viewMode == LibraryViewMode::DOWNLOADED) {
        m_titleLabel->setText(m_title + " - Downloaded");
        m_contentGrid->setDataSource(m_downloadedItems);
        updateViewModeButtons();
    }

    // Hide Downloaded button if no downloads
    if (m_downloadedBtn) {
        if (m_downloadedItems.empty()) {
            m_downloadedBtn->setVisibility(brls::Visibility::GONE);
        } else {
            m_downloadedBtn->setVisibility(brls::Visibility::VISIBLE);
        }
    }
}

void LibrarySectionTab::showDownloaded() {
    if (!m_downloadedLoaded) {
        loadDownloadedItems();
    }

    m_viewMode = LibraryViewMode::DOWNLOADED;
    m_titleLabel->setText(m_title + " - Downloaded");
    m_contentGrid->setDataSource(m_downloadedItems);
    updateViewModeButtons();
}

void LibrarySectionTab::filterByDownloaded() {
    // Filter the current items list to show only downloaded items
    if (m_items.empty()) return;

    DownloadsManager& mgr = DownloadsManager::getInstance();
    std::vector<MediaItem> filtered;

    for (const auto& item : m_items) {
        if (mgr.isDownloaded(item.id)) {
            filtered.push_back(item);
        }
    }

    m_contentGrid->setDataSource(filtered);
}

void LibrarySectionTab::showAllItems() {
    m_viewMode = LibraryViewMode::ALL_ITEMS;
    m_titleLabel->setText(m_title);
    m_contentGrid->setDataSource(m_items);
    updateViewModeButtons();
}

void LibrarySectionTab::showCollections() {
    if (!m_collectionsLoaded) {
        brls::Application::notify("Loading collections...");
        return;
    }

    if (m_collections.empty()) {
        brls::Application::notify("No collections available");
        return;
    }

    m_viewMode = LibraryViewMode::COLLECTIONS;
    m_titleLabel->setText(m_title + " - Collections");

    // Show collections in the grid
    m_contentGrid->setDataSource(m_collections);
    updateViewModeButtons();
}

void LibrarySectionTab::showCategories() {
    if (!m_genresLoaded) {
        brls::Application::notify("Loading categories...");
        return;
    }

    if (m_genres.empty()) {
        brls::Application::notify("No categories available");
        return;
    }

    m_viewMode = LibraryViewMode::CATEGORIES;
    m_titleLabel->setText(m_title + " - Categories");

    // Convert genres to MediaItem format for the grid
    std::vector<MediaItem> genreItems;
    for (const auto& genre : m_genres) {
        MediaItem item;
        item.title = genre.title;
        item.id = genre.id;  // Use genre key for filtering
        item.type = "genre";
        item.mediaType = MediaType::UNKNOWN;
        genreItems.push_back(item);
    }

    m_contentGrid->setDataSource(genreItems);
    updateViewModeButtons();
}

void LibrarySectionTab::updateViewModeButtons() {
    // Show/hide back button
    bool inFilteredView = (m_viewMode == LibraryViewMode::FILTERED);
    m_backBtn->setVisibility(inFilteredView ? brls::Visibility::VISIBLE : brls::Visibility::GONE);

    // Show/hide mode buttons
    bool showModeButtons = (m_viewMode != LibraryViewMode::FILTERED);
    if (m_allBtn) {
        m_allBtn->setVisibility(showModeButtons ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (m_collectionsBtn) {
        m_collectionsBtn->setVisibility(showModeButtons && !m_collections.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (m_categoriesBtn) {
        m_categoriesBtn->setVisibility(showModeButtons && !m_genres.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (m_downloadedBtn) {
        m_downloadedBtn->setVisibility(showModeButtons && !m_downloadedItems.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
}

void LibrarySectionTab::hideNavigationButtons() {
    // Hide all navigation buttons for offline/downloaded-only mode
    if (m_allBtn) {
        m_allBtn->setVisibility(brls::Visibility::GONE);
    }
    if (m_collectionsBtn) {
        m_collectionsBtn->setVisibility(brls::Visibility::GONE);
    }
    if (m_categoriesBtn) {
        m_categoriesBtn->setVisibility(brls::Visibility::GONE);
    }
    if (m_downloadedBtn) {
        m_downloadedBtn->setVisibility(brls::Visibility::GONE);
    }
    if (m_backBtn) {
        m_backBtn->setVisibility(brls::Visibility::GONE);
    }
    if (m_findPodcastsBtn) {
        m_findPodcastsBtn->setVisibility(brls::Visibility::GONE);
    }
    if (m_checkEpisodesBtn) {
        m_checkEpisodesBtn->setVisibility(brls::Visibility::GONE);
    }
}

void LibrarySectionTab::onItemSelected(const MediaItem& item) {
    brls::Logger::debug("LibrarySectionTab::onItemSelected - title='{}' id='{}' type='{}' viewMode={}",
                       item.title, item.id, item.type, static_cast<int>(m_viewMode));

    // Handle selection based on current view mode
    if (m_viewMode == LibraryViewMode::COLLECTIONS) {
        // Selected a collection - show its contents
        brls::Logger::debug("LibrarySectionTab: Opening collection");
        onCollectionSelected(item);
        return;
    }

    if (m_viewMode == LibraryViewMode::CATEGORIES) {
        // Selected a category/genre - filter by it
        brls::Logger::debug("LibrarySectionTab: Opening category");
        GenreItem genre;
        genre.title = item.title;
        genre.id = item.id;
        onGenreSelected(genre);
        return;
    }

    // Normal item selection - for Audiobookshelf, most items go to detail view
    // Podcast episodes can be played directly
    if (item.mediaType == MediaType::PODCAST_EPISODE) {
        brls::Logger::debug("LibrarySectionTab: Playing podcast episode");
        Application::getInstance().pushPlayerActivity(item.podcastId, item.episodeId);
        return;
    }

    // Show media detail view for books and other types
    brls::Logger::info("LibrarySectionTab: Opening detail view for '{}'", item.title);
    auto* detailView = new MediaDetailView(item);
    brls::Logger::debug("LibrarySectionTab: MediaDetailView created, pushing activity");
    brls::Application::pushActivity(new brls::Activity(detailView));
    brls::Logger::debug("LibrarySectionTab: Activity pushed successfully");
}

void LibrarySectionTab::onCollectionSelected(const MediaItem& collection) {
    brls::Logger::debug("LibrarySectionTab: Selected collection: {}", collection.title);

    m_filterTitle = collection.title;
    std::string collectionId = collection.id;
    std::string filterTitle = m_filterTitle;
    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, collectionId, filterTitle, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;

        if (client.fetchCollectionBooks(collectionId, items)) {
            brls::Logger::info("LibrarySectionTab: Got {} items in collection", items.size());

            brls::sync([this, items, filterTitle, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) {
                    brls::Logger::debug("LibrarySectionTab: Tab no longer alive, skipping collection display");
                    return;
                }

                brls::Logger::debug("LibrarySectionTab: Displaying {} collection items", items.size());
                m_viewMode = LibraryViewMode::FILTERED;
                brls::Logger::debug("LibrarySectionTab: Setting title");
                m_titleLabel->setText(m_title + " - " + filterTitle);
                brls::Logger::debug("LibrarySectionTab: Setting grid data source");
                m_contentGrid->setDataSource(items);
                brls::Logger::debug("LibrarySectionTab: Grid updated, updating buttons");
                updateViewModeButtons();
                brls::Logger::debug("LibrarySectionTab: Buttons updated");

                // Try to give focus to the grid
                brls::Logger::debug("LibrarySectionTab: About to request focus on grid");
                if (m_contentGrid) {
                    brls::Application::giveFocus(m_contentGrid);
                }
                brls::Logger::debug("LibrarySectionTab: Collection display complete");
            });
        } else {
            brls::Logger::error("LibrarySectionTab: Failed to load collection content");
            brls::sync([aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                brls::Application::notify("Failed to load collection");
            });
        }
    });
}

void LibrarySectionTab::onGenreSelected(const GenreItem& genre) {
    brls::Logger::debug("LibraryTab: Selected genre: {} (key: {})", genre.title, genre.id);

    m_filterTitle = genre.title;
    std::string key = m_sectionKey;
    std::string genreKey = genre.id;
    std::string genreTitle = genre.title;
    std::string filterTitle = m_filterTitle;
    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, key, genreKey, genreTitle, filterTitle, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> items;

        // Try with genre key first, fall back to title
        if (client.fetchByGenreKey(key, genreKey, items) || client.fetchByGenre(key, genreTitle, items)) {
            brls::Logger::info("LibraryTab: Got {} items for genre", items.size());

            brls::sync([this, items, filterTitle, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_viewMode = LibraryViewMode::FILTERED;
                m_titleLabel->setText(m_title + " - " + filterTitle);
                m_contentGrid->setDataSource(items);
                updateViewModeButtons();
            });
        } else {
            brls::Logger::error("LibraryTab: Failed to load genre content");
            brls::sync([aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                brls::Application::notify("Failed to load category");
            });
        }
    });
}

void LibrarySectionTab::openPodcastSearch() {
    brls::Logger::debug("LibrarySectionTab: Opening podcast search for library {}", m_sectionKey);

    auto* searchTab = new PodcastSearchTab(m_sectionKey);
    brls::Application::pushActivity(new brls::Activity(searchTab));
}

void LibrarySectionTab::checkAllNewEpisodes() {
    brls::Logger::debug("LibrarySectionTab: Checking for new episodes in all podcasts");

    brls::Application::notify("Checking for new episodes...");

    std::weak_ptr<bool> aliveWeak = m_alive;
    std::string libraryKey = m_sectionKey;

    asyncRun([this, libraryKey, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

        int totalNew = 0;
        for (const auto& item : m_items) {
            if (item.type == "podcast" || item.mediaType == MediaType::PODCAST) {
                std::vector<MediaItem> newEps;
                if (client.checkNewEpisodes(item.id, newEps)) {
                    if (!newEps.empty()) {
                        totalNew += newEps.size();
                        // Auto-download new episodes
                        client.downloadAllNewEpisodes(item.id);
                    }
                }
            }
        }

        brls::sync([totalNew, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            if (totalNew > 0) {
                brls::Application::notify("Found " + std::to_string(totalNew) + " new episode(s)");
            } else {
                brls::Application::notify("No new episodes found");
            }
        });
    });
}

} // namespace vitaabs
