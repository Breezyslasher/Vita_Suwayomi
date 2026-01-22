/**
 * VitaSuwayomi - Search/Browse Tab implementation
 * Search manga across sources and browse source catalogs
 */

#include "view/search_tab.hpp"
#include "view/manga_item_cell.hpp"
#include "view/manga_detail_view.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/async.hpp"

namespace vitasuwayomi {

SearchTab::SearchTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Header row with title and search icon
    m_headerBox = new brls::Box();
    m_headerBox->setAxis(brls::Axis::ROW);
    m_headerBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    m_headerBox->setAlignItems(brls::AlignItems::CENTER);
    m_headerBox->setMarginBottom(15);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Browse");
    m_titleLabel->setFontSize(28);
    m_headerBox->addView(m_titleLabel);

    // Global search button with search icon
    m_globalSearchBtn = new brls::Button();
    m_globalSearchBtn->setWidth(44);
    m_globalSearchBtn->setHeight(44);
    m_globalSearchBtn->setCornerRadius(8);
    m_globalSearchBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_globalSearchBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* searchIcon = new brls::Image();
    searchIcon->setWidth(24);
    searchIcon->setHeight(24);
    searchIcon->setScalingType(brls::ImageScalingType::FIT);
    searchIcon->setImageFromFile("app0:resources/icons/search.png");
    m_globalSearchBtn->addView(searchIcon);

    m_globalSearchBtn->registerClickAction([this](brls::View* view) {
        showGlobalSearchDialog();
        return true;
    });
    m_globalSearchBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_globalSearchBtn));
    m_headerBox->addView(m_globalSearchBtn);

    this->addView(m_headerBox);

    // Hidden search label (used for source-specific search)
    m_searchLabel = new brls::Label();
    m_searchLabel->setText("");
    m_searchLabel->setFontSize(16);
    m_searchLabel->setMarginBottom(10);
    m_searchLabel->setVisibility(brls::Visibility::GONE);
    this->addView(m_searchLabel);

    // Mode selector buttons
    m_modeBox = new brls::Box();
    m_modeBox->setAxis(brls::Axis::ROW);
    m_modeBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_modeBox->setAlignItems(brls::AlignItems::CENTER);
    m_modeBox->setMarginBottom(10);

    // Sources button
    m_sourcesBtn = new brls::Button();
    m_sourcesBtn->setText("Sources");
    m_sourcesBtn->setMarginRight(10);
    m_sourcesBtn->registerClickAction([this](brls::View* view) {
        showSources();
        return true;
    });
    m_modeBox->addView(m_sourcesBtn);

    // Popular button
    m_popularBtn = new brls::Button();
    m_popularBtn->setText("Popular");
    m_popularBtn->setMarginRight(10);
    m_popularBtn->setVisibility(brls::Visibility::GONE);
    m_popularBtn->registerClickAction([this](brls::View* view) {
        m_browseMode = BrowseMode::POPULAR;
        loadPopularManga(m_currentSourceId);
        updateModeButtons();
        return true;
    });
    m_modeBox->addView(m_popularBtn);

    // Latest button
    m_latestBtn = new brls::Button();
    m_latestBtn->setText("Latest");
    m_latestBtn->setMarginRight(10);
    m_latestBtn->setVisibility(brls::Visibility::GONE);
    m_latestBtn->registerClickAction([this](brls::View* view) {
        m_browseMode = BrowseMode::LATEST;
        loadLatestManga(m_currentSourceId);
        updateModeButtons();
        return true;
    });
    m_modeBox->addView(m_latestBtn);

    // Filter button with tag icon
    m_filterBtn = new brls::Button();
    m_filterBtn->setWidth(44);
    m_filterBtn->setHeight(40);
    m_filterBtn->setCornerRadius(8);
    m_filterBtn->setMarginRight(10);
    m_filterBtn->setVisibility(brls::Visibility::GONE);
    m_filterBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_filterBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* filterIcon = new brls::Image();
    filterIcon->setWidth(20);
    filterIcon->setHeight(20);
    filterIcon->setScalingType(brls::ImageScalingType::FIT);
    filterIcon->setImageFromFile("app0:resources/icons/tag.png");
    m_filterBtn->addView(filterIcon);

    m_filterBtn->registerClickAction([this](brls::View* view) {
        showFilterDialog();
        return true;
    });
    m_filterBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_filterBtn));
    m_modeBox->addView(m_filterBtn);

    // Back button
    m_backBtn = new brls::Button();
    m_backBtn->setText("< Back");
    m_backBtn->setVisibility(brls::Visibility::GONE);
    m_backBtn->registerClickAction([this](brls::View* view) {
        showSources();
        return true;
    });
    m_modeBox->addView(m_backBtn);

    this->addView(m_modeBox);

    // Results label
    m_resultsLabel = new brls::Label();
    m_resultsLabel->setText("");
    m_resultsLabel->setFontSize(18);
    m_resultsLabel->setMarginBottom(10);
    this->addView(m_resultsLabel);

    // Content grid
    m_contentGrid = new RecyclingGrid();
    m_contentGrid->setGrow(1.0f);
    m_contentGrid->setOnItemSelected([this](const Manga& manga) {
        onMangaSelected(manga);
    });
    m_contentGrid->setOnLoadMore([this]() {
        loadNextPage();
    });
    this->addView(m_contentGrid);

    // Load sources initially
    loadSources();
}

void SearchTab::onFocusGained() {
    brls::Box::onFocusGained();

    if (m_sources.empty()) {
        loadSources();
    }
}

void SearchTab::loadSources() {
    brls::Logger::debug("SearchTab: Loading sources");
    m_browseMode = BrowseMode::SOURCES;

    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Source> sources;

        if (client.fetchSourceList(sources)) {
            brls::Logger::info("SearchTab: Got {} sources", sources.size());

            brls::sync([this, sources]() {
                m_sources = sources;
                showSources();
            });
        } else {
            brls::Logger::error("SearchTab: Failed to fetch sources");
            brls::sync([this]() {
                m_resultsLabel->setText("Failed to load sources");
            });
        }
    });
}

void SearchTab::filterSourcesByLanguage() {
    const AppSettings& settings = Application::getInstance().getSettings();
    m_filteredSources.clear();

    for (const auto& source : m_sources) {
        // Filter by NSFW setting
        if (source.isNsfw && !settings.showNsfwSources) {
            continue;
        }

        // Filter by language setting
        if (!settings.enabledSourceLanguages.empty()) {
            bool languageMatch = false;

            // Check exact match first
            if (settings.enabledSourceLanguages.count(source.lang) > 0) {
                languageMatch = true;
            }
            // Check if base language is enabled (e.g., "zh" matches "zh-Hans", "zh-Hant")
            else {
                // Get base language code (before any dash)
                std::string baseLang = source.lang;
                size_t dashPos = baseLang.find('-');
                if (dashPos != std::string::npos) {
                    baseLang = baseLang.substr(0, dashPos);
                }

                if (settings.enabledSourceLanguages.count(baseLang) > 0) {
                    languageMatch = true;
                }
            }

            // Always show multi-language sources if "multi" or "all" is selected
            if (source.lang == "multi" || source.lang == "all") {
                languageMatch = true;
            }

            if (!languageMatch) {
                continue;
            }
        }

        m_filteredSources.push_back(source);
    }

    brls::Logger::debug("SearchTab: Filtered {} -> {} sources", m_sources.size(), m_filteredSources.size());
}

void SearchTab::showGlobalSearchDialog() {
    brls::Application::getImeManager()->openForText([this](std::string text) {
        if (text.empty()) return;

        m_searchQuery = text;
        m_titleLabel->setText("Search: " + text);
        performSearch(text);
    }, "Global Search", "Search across all sources", 256, m_searchQuery);
}

void SearchTab::showSources() {
    m_browseMode = BrowseMode::SOURCES;
    m_titleLabel->setText("Browse");
    m_searchLabel->setVisibility(brls::Visibility::GONE);

    // Filter sources by language setting
    filterSourcesByLanguage();
    m_resultsLabel->setText(std::to_string(m_filteredSources.size()) + " sources");

    // Hide source-specific buttons
    m_popularBtn->setVisibility(brls::Visibility::GONE);
    m_latestBtn->setVisibility(brls::Visibility::GONE);
    m_filterBtn->setVisibility(brls::Visibility::GONE);
    m_backBtn->setVisibility(brls::Visibility::GONE);
    m_sourcesBtn->setVisibility(brls::Visibility::VISIBLE);

    // Hide search results scroll view
    if (m_searchResultsScroll) {
        m_searchResultsScroll->setVisibility(brls::Visibility::GONE);
    }

    // Clear manga grid and show source list
    m_mangaList.clear();
    m_contentGrid->setDataSource(m_mangaList);

    // Create or clear source list box
    if (!m_sourceScrollView) {
        m_sourceScrollView = new brls::ScrollingFrame();
        m_sourceScrollView->setGrow(1.0f);

        m_sourceListBox = new brls::Box();
        m_sourceListBox->setAxis(brls::Axis::COLUMN);
        m_sourceListBox->setJustifyContent(brls::JustifyContent::FLEX_START);
        m_sourceListBox->setPadding(10);

        m_sourceScrollView->setContentView(m_sourceListBox);
    }
    m_sourceListBox->clearViews();

    // Group sources by language
    std::map<std::string, std::vector<Source>> sourcesByLang;
    for (const auto& source : m_filteredSources) {
        sourcesByLang[source.lang].push_back(source);
    }

    // Create source rows with icons
    for (const auto& [lang, sources] : sourcesByLang) {
        // Language header
        auto* langHeader = new brls::Label();
        std::string langName = lang.empty() ? "Unknown" : lang;
        if (langName == "en") langName = "English";
        else if (langName == "multi") langName = "Multi-language";
        else if (langName == "ja") langName = "Japanese";
        else if (langName == "ko") langName = "Korean";
        else if (langName == "zh") langName = "Chinese";

        langHeader->setText(langName + " (" + std::to_string(sources.size()) + ")");
        langHeader->setFontSize(18);
        langHeader->setMarginTop(10);
        langHeader->setMarginBottom(5);
        langHeader->setTextColor(nvgRGB(100, 180, 255));
        m_sourceListBox->addView(langHeader);

        // Source buttons
        for (const auto& source : sources) {
            auto* sourceRow = new brls::Box();
            sourceRow->setAxis(brls::Axis::ROW);
            sourceRow->setAlignItems(brls::AlignItems::CENTER);
            sourceRow->setMarginBottom(8);
            sourceRow->setPadding(8);
            sourceRow->setCornerRadius(8);
            sourceRow->setBackgroundColor(nvgRGBA(40, 40, 40, 200));
            sourceRow->setFocusable(true);

            // Source icon
            auto* sourceIcon = new brls::Image();
            sourceIcon->setWidth(32);
            sourceIcon->setHeight(32);
            sourceIcon->setMarginRight(12);
            sourceIcon->setScalingType(brls::ImageScalingType::FIT);
            if (!source.iconUrl.empty()) {
                // Load icon from server
                std::string iconUrl = Application::getInstance().getServerUrl() + source.iconUrl;
                sourceIcon->setImageFromFile(iconUrl);
            }
            sourceRow->addView(sourceIcon);

            // Source name
            auto* nameLabel = new brls::Label();
            nameLabel->setText(source.name);
            nameLabel->setFontSize(16);
            nameLabel->setGrow(1.0f);
            sourceRow->addView(nameLabel);

            // Click to browse source
            Source sourceCopy = source;
            sourceRow->registerClickAction([this, sourceCopy](brls::View* view) {
                onSourceSelected(sourceCopy);
                return true;
            });
            sourceRow->addGestureRecognizer(new brls::TapGestureRecognizer(sourceRow));

            m_sourceListBox->addView(sourceRow);
        }
    }

    // Show source scroll view, hide content grid
    m_contentGrid->setVisibility(brls::Visibility::GONE);
    m_sourceScrollView->setVisibility(brls::Visibility::VISIBLE);

    // Add scroll view if not already added
    if (m_sourceScrollView->getParent() == nullptr) {
        this->addView(m_sourceScrollView);
    }
}

void SearchTab::showSourceBrowser(const Source& source) {
    m_currentSourceId = source.id;
    m_currentSourceName = source.name;

    m_titleLabel->setText(source.name);

    // Show source-specific buttons
    m_sourcesBtn->setVisibility(brls::Visibility::GONE);
    m_popularBtn->setVisibility(brls::Visibility::VISIBLE);
    m_latestBtn->setVisibility(source.supportsLatest ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    m_filterBtn->setVisibility(source.isConfigurable ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    m_backBtn->setVisibility(brls::Visibility::VISIBLE);

    // Hide source list and search results, show content grid
    if (m_sourceScrollView) {
        m_sourceScrollView->setVisibility(brls::Visibility::GONE);
    }
    if (m_searchResultsScroll) {
        m_searchResultsScroll->setVisibility(brls::Visibility::GONE);
    }
    m_contentGrid->setVisibility(brls::Visibility::VISIBLE);

    // Load popular manga by default
    m_browseMode = BrowseMode::POPULAR;
    loadPopularManga(source.id);
    updateModeButtons();
}

void SearchTab::showFilterDialog() {
    // TODO: Implement source filter dialog
    // This would show the source's configurable filters (genres, status, etc)
    brls::Application::notify("Filters not yet implemented");
}

void SearchTab::loadPopularManga(int64_t sourceId) {
    brls::Logger::debug("SearchTab: Loading popular manga from source {}", sourceId);
    m_resultsLabel->setText("Loading popular manga...");
    m_currentPage = 1;
    m_contentGrid->setHasMorePages(false);

    asyncRun([this, sourceId]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;
        bool hasNextPage = false;

        if (client.fetchPopularManga(sourceId, 1, manga, hasNextPage)) {
            brls::Logger::info("SearchTab: Got {} popular manga (hasNext: {})", manga.size(), hasNextPage);

            brls::sync([this, manga, hasNextPage]() {
                m_mangaList = manga;
                m_hasNextPage = hasNextPage;
                m_contentGrid->setDataSource(m_mangaList);
                m_contentGrid->setHasMorePages(hasNextPage);
                m_resultsLabel->setText(std::to_string(manga.size()) + " manga");
            });
        } else {
            brls::Logger::error("SearchTab: Failed to fetch popular manga");
            brls::sync([this]() {
                m_resultsLabel->setText("Failed to load popular manga");
            });
        }
    });
}

void SearchTab::loadLatestManga(int64_t sourceId) {
    brls::Logger::debug("SearchTab: Loading latest manga from source {}", sourceId);
    m_resultsLabel->setText("Loading latest manga...");
    m_currentPage = 1;
    m_contentGrid->setHasMorePages(false);

    asyncRun([this, sourceId]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;
        bool hasNextPage = false;

        if (client.fetchLatestManga(sourceId, 1, manga, hasNextPage)) {
            brls::Logger::info("SearchTab: Got {} latest manga (hasNext: {})", manga.size(), hasNextPage);

            brls::sync([this, manga, hasNextPage]() {
                m_mangaList = manga;
                m_hasNextPage = hasNextPage;
                m_contentGrid->setDataSource(m_mangaList);
                m_contentGrid->setHasMorePages(hasNextPage);
                m_resultsLabel->setText(std::to_string(manga.size()) + " manga");
            });
        } else {
            brls::Logger::error("SearchTab: Failed to fetch latest manga");
            brls::sync([this]() {
                m_resultsLabel->setText("Failed to load latest manga");
            });
        }
    });
}

void SearchTab::performSearch(const std::string& query) {
    if (query.empty()) {
        m_resultsLabel->setText("");
        m_mangaList.clear();
        m_contentGrid->setDataSource(m_mangaList);
        return;
    }

    // If we have a current source, search within that source
    if (m_currentSourceId != 0 && m_browseMode != BrowseMode::SOURCES) {
        performSourceSearch(m_currentSourceId, query);
        return;
    }

    // Filter sources by language setting first
    filterSourcesByLanguage();

    // Hide source list and grid, will show search results rows
    if (m_sourceScrollView) {
        m_sourceScrollView->setVisibility(brls::Visibility::GONE);
    }
    m_contentGrid->setVisibility(brls::Visibility::GONE);

    // Update UI for search mode
    m_browseMode = BrowseMode::SEARCH_RESULTS;
    m_currentSourceId = 0;  // Reset so back button returns to sources
    m_sourcesBtn->setVisibility(brls::Visibility::GONE);
    m_popularBtn->setVisibility(brls::Visibility::GONE);
    m_latestBtn->setVisibility(brls::Visibility::GONE);
    m_filterBtn->setVisibility(brls::Visibility::GONE);
    m_backBtn->setVisibility(brls::Visibility::VISIBLE);
    m_resultsLabel->setText("Searching " + std::to_string(m_filteredSources.size()) + " sources...");

    // Copy filtered sources for async use
    std::vector<Source> sourcesToSearch = m_filteredSources;

    asyncRun([this, query, sourcesToSearch]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        // Map to group results by source (use ordered map to preserve insertion order)
        std::map<std::string, std::vector<Manga>> resultsBySource;
        int totalResults = 0;

        // Search each filtered source
        for (const auto& source : sourcesToSearch) {
            std::vector<Manga> results;
            bool hasNextPage = false;

            // Use searchManga which uses GraphQL API
            if (client.searchManga(source.id, query, 1, results, hasNextPage)) {
                if (!results.empty()) {
                    for (auto& manga : results) {
                        manga.sourceName = source.name;
                    }
                    resultsBySource[source.name] = results;
                    totalResults += results.size();
                }
            }

            // Limit to prevent too many requests
            if (totalResults >= 150) break;
        }

        brls::Logger::info("SearchTab: Found {} results from {} sources for '{}'",
                           totalResults, resultsBySource.size(), query);

        // Flatten results for m_mangaList (used by other functions)
        std::vector<Manga> allResults;
        for (const auto& [sourceName, mangas] : resultsBySource) {
            for (const auto& manga : mangas) {
                allResults.push_back(manga);
            }
        }

        brls::sync([this, allResults, resultsBySource]() {
            m_mangaList = allResults;

            // Display results in horizontal rows by source
            populateSearchResultsRows(resultsBySource);

            // Show result count with source breakdown
            if (resultsBySource.empty()) {
                m_resultsLabel->setText("No results found");
            } else {
                std::string resultText = std::to_string(allResults.size()) + " results from " +
                                        std::to_string(resultsBySource.size()) + " sources";
                m_resultsLabel->setText(resultText);
            }
        });
    });
}

void SearchTab::performSourceSearch(int64_t sourceId, const std::string& query) {
    brls::Logger::debug("SearchTab: Searching '{}' in source {}", query, sourceId);
    m_resultsLabel->setText("Searching...");
    m_browseMode = BrowseMode::SEARCH_RESULTS;
    m_currentPage = 1;

    asyncRun([this, sourceId, query]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;
        bool hasNextPage = false;

        if (client.searchManga(sourceId, query, 1, manga, hasNextPage)) {
            brls::Logger::info("SearchTab: Found {} results for '{}'", manga.size(), query);

            brls::sync([this, manga, hasNextPage]() {
                m_mangaList = manga;
                m_hasNextPage = hasNextPage;
                m_contentGrid->setDataSource(m_mangaList);
                m_resultsLabel->setText(std::to_string(manga.size()) + " results");
            });
        } else {
            brls::Logger::error("SearchTab: Failed to search manga");
            brls::sync([this]() {
                m_resultsLabel->setText("Search failed");
            });
        }
    });
}

void SearchTab::onSourceSelected(const Source& source) {
    brls::Logger::debug("SearchTab: Selected source '{}' ({})", source.name, source.id);
    showSourceBrowser(source);
}

void SearchTab::onMangaSelected(const Manga& manga) {
    brls::Logger::debug("SearchTab: Selected manga '{}' id={}", manga.title, manga.id);

    // Push manga detail view
    auto* detailView = new MangaDetailView(manga);
    brls::Application::pushActivity(new brls::Activity(detailView));
}

void SearchTab::loadNextPage() {
    if (!m_hasNextPage) return;

    m_currentPage++;
    brls::Logger::debug("SearchTab: Loading page {}", m_currentPage);

    // Show loading state
    m_contentGrid->setLoading(true);

    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;
        bool hasNextPage = false;

        bool success = false;
        if (m_browseMode == BrowseMode::POPULAR) {
            success = client.fetchPopularManga(m_currentSourceId, m_currentPage, manga, hasNextPage);
        } else if (m_browseMode == BrowseMode::LATEST) {
            success = client.fetchLatestManga(m_currentSourceId, m_currentPage, manga, hasNextPage);
        } else if (m_browseMode == BrowseMode::SEARCH_RESULTS && !m_searchQuery.empty()) {
            success = client.searchManga(m_currentSourceId, m_searchQuery, m_currentPage, manga, hasNextPage);
        }

        if (success && !manga.empty()) {
            brls::sync([this, manga, hasNextPage]() {
                // Append to existing list
                for (const auto& m : manga) {
                    m_mangaList.push_back(m);
                }
                m_hasNextPage = hasNextPage;

                // Use appendItems instead of setDataSource
                m_contentGrid->appendItems(manga);
                m_contentGrid->setLoading(false);
                m_contentGrid->setHasMorePages(hasNextPage);
                m_resultsLabel->setText(std::to_string(m_mangaList.size()) + " manga");

                brls::Logger::info("SearchTab: Loaded page {}, total {} manga (hasNext: {})",
                                  m_currentPage, m_mangaList.size(), hasNextPage);
            });
        } else {
            brls::sync([this]() {
                m_contentGrid->setLoading(false);
                m_contentGrid->setHasMorePages(false);
                if (m_mangaList.empty()) {
                    m_resultsLabel->setText("No more results");
                }
            });
        }
    });
}

void SearchTab::updateModeButtons() {
    // Highlight active mode button (in a real app, you'd change button style)
    // For now, just ensure visibility is correct
    if (m_browseMode == BrowseMode::SOURCES) {
        m_sourcesBtn->setVisibility(brls::Visibility::VISIBLE);
        m_popularBtn->setVisibility(brls::Visibility::GONE);
        m_latestBtn->setVisibility(brls::Visibility::GONE);
        m_backBtn->setVisibility(brls::Visibility::GONE);
    } else {
        m_sourcesBtn->setVisibility(brls::Visibility::GONE);
        m_popularBtn->setVisibility(brls::Visibility::VISIBLE);
        m_backBtn->setVisibility(brls::Visibility::VISIBLE);

        // Show latest button if source supports it
        for (const auto& source : m_sources) {
            if (source.id == m_currentSourceId && source.supportsLatest) {
                m_latestBtn->setVisibility(brls::Visibility::VISIBLE);
                break;
            }
        }
    }
}

void SearchTab::populateSearchResultsRows(const std::map<std::string, std::vector<Manga>>& resultsBySource) {
    // Create search results scroll view if not exists
    if (!m_searchResultsScroll) {
        m_searchResultsScroll = new brls::ScrollingFrame();
        m_searchResultsScroll->setGrow(1.0f);

        m_searchResultsBox = new brls::Box();
        m_searchResultsBox->setAxis(brls::Axis::COLUMN);
        m_searchResultsBox->setJustifyContent(brls::JustifyContent::FLEX_START);
        m_searchResultsBox->setPadding(10);

        m_searchResultsScroll->setContentView(m_searchResultsBox);
        this->addView(m_searchResultsScroll);
    }

    // Clear existing content
    m_searchResultsBox->clearViews();

    // Hide other views, show search results
    if (m_sourceScrollView) {
        m_sourceScrollView->setVisibility(brls::Visibility::GONE);
    }
    m_contentGrid->setVisibility(brls::Visibility::GONE);
    m_searchResultsScroll->setVisibility(brls::Visibility::VISIBLE);

    if (resultsBySource.empty()) {
        auto* emptyLabel = new brls::Label();
        emptyLabel->setText("No results found");
        emptyLabel->setFontSize(18);
        emptyLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        emptyLabel->setMarginTop(50);
        m_searchResultsBox->addView(emptyLabel);
        return;
    }

    // Create horizontal rows for each source
    for (const auto& [sourceName, mangas] : resultsBySource) {
        // Source header
        auto* headerBox = new brls::Box();
        headerBox->setAxis(brls::Axis::ROW);
        headerBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        headerBox->setAlignItems(brls::AlignItems::CENTER);
        headerBox->setMarginTop(15);
        headerBox->setMarginBottom(8);

        auto* sourceLabel = new brls::Label();
        sourceLabel->setText(sourceName);
        sourceLabel->setFontSize(18);
        sourceLabel->setTextColor(nvgRGB(100, 180, 255));
        headerBox->addView(sourceLabel);

        auto* countLabel = new brls::Label();
        countLabel->setText(std::to_string(mangas.size()) + " results");
        countLabel->setFontSize(14);
        countLabel->setTextColor(nvgRGBA(180, 180, 180, 255));
        headerBox->addView(countLabel);

        m_searchResultsBox->addView(headerBox);

        // Horizontal scroll view for manga items
        auto* rowScroll = new brls::ScrollingFrame();
        rowScroll->setHeight(200);  // Fixed height for row
        rowScroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

        auto* rowBox = new brls::Box();
        rowBox->setAxis(brls::Axis::ROW);
        rowBox->setJustifyContent(brls::JustifyContent::FLEX_START);
        rowBox->setAlignItems(brls::AlignItems::FLEX_START);
        rowBox->setPaddingRight(20);  // Extra padding at end

        // Create manga item cells for this row
        for (const auto& manga : mangas) {
            auto* cell = new MangaItemCell();
            cell->setWidth(140);
            cell->setHeight(190);
            cell->setMarginRight(12);
            cell->setFocusable(true);
            cell->setManga(manga);

            // Click handler to open manga detail
            Manga mangaCopy = manga;
            cell->registerClickAction([this, mangaCopy](brls::View* view) {
                onMangaSelected(mangaCopy);
                return true;
            });
            cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

            rowBox->addView(cell);
        }

        rowScroll->setContentView(rowBox);
        m_searchResultsBox->addView(rowScroll);
    }

    brls::Logger::info("SearchTab: Populated {} source rows with results", resultsBySource.size());
}

} // namespace vitasuwayomi
