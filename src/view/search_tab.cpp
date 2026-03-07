/**
 * VitaSuwayomi - Search/Browse Tab implementation
 * Search manga across sources and browse source catalogs
 */

#include "view/search_tab.hpp"
#include "view/manga_item_cell.hpp"
#include "view/manga_detail_view.hpp"
#include "view/horizontal_scroll_row.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/async.hpp"
#include "utils/image_loader.hpp"
#include <algorithm>

namespace vitasuwayomi {

SearchTab::SearchTab() {
    m_alive = std::make_shared<bool>(true);

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

    // Button container for history and search buttons
    m_buttonContainer = new brls::Box();
    m_buttonContainer->setAxis(brls::Axis::ROW);
    m_buttonContainer->setAlignItems(brls::AlignItems::FLEX_END);

    // Search history button with Select icon above
    auto* historyContainer = new brls::Box();
    historyContainer->setAxis(brls::Axis::COLUMN);
    historyContainer->setAlignItems(brls::AlignItems::CENTER);
    historyContainer->setMarginRight(10);

    auto* selectButtonIcon = new brls::Image();
    selectButtonIcon->setWidth(48);
    selectButtonIcon->setHeight(16);
    selectButtonIcon->setScalingType(brls::ImageScalingType::FIT);
    selectButtonIcon->setImageFromFile("app0:resources/images/select_button.png");
    selectButtonIcon->setMarginBottom(2);
    historyContainer->addView(selectButtonIcon);

    m_historyBtn = new brls::Button();
    m_historyBtn->setWidth(44);
    m_historyBtn->setHeight(44);
    m_historyBtn->setCornerRadius(8);
    m_historyBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_historyBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* historyIcon = new brls::Image();
    historyIcon->setWidth(24);
    historyIcon->setHeight(24);
    historyIcon->setScalingType(brls::ImageScalingType::FIT);
    historyIcon->setImageFromFile("app0:resources/icons/history.png");
    m_historyBtn->addView(historyIcon);

    m_historyBtn->registerClickAction([this](brls::View* view) {
        brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
            auto a = aliveWeak.lock(); if (!a || !*a) return;
            showSearchHistoryDialog();
        });
        return true;
    });
    m_historyBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_historyBtn));
    // B button on History goes back when not on sources list
    m_historyBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        if (m_browseMode != BrowseMode::SOURCES) {
            brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() { auto a = aliveWeak.lock(); if (!a || !*a) return; handleBackNavigation(); });
            return true;
        }
        return false;
    }, true);
    historyContainer->addView(m_historyBtn);
    m_buttonContainer->addView(historyContainer);

    // Global search button with Start icon above
    auto* searchContainer = new brls::Box();
    searchContainer->setAxis(brls::Axis::COLUMN);
    searchContainer->setAlignItems(brls::AlignItems::CENTER);

    // Start button icon - use actual image dimensions (64x16)
    auto* startButtonIcon = new brls::Image();
    startButtonIcon->setWidth(64);
    startButtonIcon->setHeight(16);
    startButtonIcon->setScalingType(brls::ImageScalingType::FIT);
    startButtonIcon->setImageFromFile("app0:resources/images/start_button.png");
    startButtonIcon->setMarginBottom(2);
    searchContainer->addView(startButtonIcon);

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
        brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
            auto a = aliveWeak.lock(); if (!a || !*a) return;
            if (m_currentSourceId != 0 && (m_browseMode == BrowseMode::POPULAR || m_browseMode == BrowseMode::LATEST)) {
                showSourceSearchDialog();
            } else {
                showGlobalSearchDialog();
            }
        });
        return true;
    });
    m_globalSearchBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_globalSearchBtn));
    // B button on Search goes back when not on sources list
    m_globalSearchBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        if (m_browseMode != BrowseMode::SOURCES) {
            brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() { auto a = aliveWeak.lock(); if (!a || !*a) return; handleBackNavigation(); });
            return true;
        }
        return false;
    }, true);
    searchContainer->addView(m_globalSearchBtn);
    m_buttonContainer->addView(searchContainer);

    m_headerBox->addView(m_buttonContainer);

    this->addView(m_headerBox);

    // Register Start button to open search dialog
    // When browsing a source, search within that source; otherwise global search
    this->registerAction("Search", brls::ControllerButton::BUTTON_START, [this](brls::View* view) {
        // Ignore when search/history buttons are disabled (e.g. during global search results)
        if (m_browseMode == BrowseMode::SEARCH_RESULTS) return true;
        brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
            auto a = aliveWeak.lock(); if (!a || !*a) return;
            if (m_currentSourceId != 0 && (m_browseMode == BrowseMode::POPULAR || m_browseMode == BrowseMode::LATEST)) {
                showSourceSearchDialog();
            } else {
                showGlobalSearchDialog();
            }
        });
        return true;
    });

    // Register Select button to open search history
    this->registerAction("History", brls::ControllerButton::BUTTON_BACK, [this](brls::View* view) {
        // Ignore when search/history buttons are disabled (e.g. during global search results)
        if (m_browseMode == BrowseMode::SEARCH_RESULTS) return true;
        brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
            auto a = aliveWeak.lock(); if (!a || !*a) return;
            showSearchHistoryDialog();
        });
        return true;
    });

    // Register Circle button (B) to go back in search/browse modes
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        // Only handle if we're not on the sources list
        if (m_browseMode != BrowseMode::SOURCES) {
            brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
                auto a = aliveWeak.lock(); if (!a || !*a) return;
                handleBackNavigation();
            });
            return true;
        }
        return false;  // Let default handling occur (exit tab)
    });

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
    m_popularBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_popularBtn));
    // B button on Popular goes back
    m_popularBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() { auto a = aliveWeak.lock(); if (!a || !*a) return; handleBackNavigation(); });
        return true;
    }, true);
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
    m_latestBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_latestBtn));
    // B button on Latest goes back
    m_latestBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() { auto a = aliveWeak.lock(); if (!a || !*a) return; handleBackNavigation(); });
        return true;
    }, true);
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
    // B button on Filter goes back
    m_filterBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() { auto a = aliveWeak.lock(); if (!a || !*a) return; handleBackNavigation(); });
        return true;
    }, true);
    m_modeBox->addView(m_filterBtn);

    // Back button
    m_backBtn = new brls::Button();
    m_backBtn->setText("< Back");
    m_backBtn->setVisibility(brls::Visibility::GONE);
    m_backBtn->registerClickAction([this](brls::View* view) {
        handleBackNavigation();
        return true;
    });
    m_backBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_backBtn));
    // B button on Back also goes back
    m_backBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() { auto a = aliveWeak.lock(); if (!a || !*a) return; handleBackNavigation(); });
        return true;
    }, true);
    m_modeBox->addView(m_backBtn);

    this->addView(m_modeBox);

    // Results label
    m_resultsLabel = new brls::Label();
    m_resultsLabel->setText("");
    m_resultsLabel->setFontSize(18);
    m_resultsLabel->setMarginBottom(10);
    this->addView(m_resultsLabel);

    // Loading indicator label (hidden by default)
    m_loadingLabel = new brls::Label();
    m_loadingLabel->setText("");
    m_loadingLabel->setFontSize(20);
    m_loadingLabel->setTextColor(Application::getInstance().getAccentColor());
    m_loadingLabel->setMarginBottom(10);
    m_loadingLabel->setVisibility(brls::Visibility::GONE);
    this->addView(m_loadingLabel);

    // Content grid
    m_contentGrid = new RecyclingGrid();
    m_contentGrid->setGrow(1.0f);
    m_contentGrid->setShowLibraryBadge(true);  // Show star for library items in browser/search

    // Apply display mode and grid size from settings (same as library)
    const auto& settings = Application::getInstance().getSettings();
    switch (settings.libraryDisplayMode) {
        case LibraryDisplayMode::GRID_NORMAL:
            m_contentGrid->setCompactMode(false);
            m_contentGrid->setListMode(false);
            break;
        case LibraryDisplayMode::GRID_COMPACT:
            m_contentGrid->setCompactMode(true);
            break;
        case LibraryDisplayMode::LIST:
            m_contentGrid->setListMode(true);
            break;
    }
    switch (settings.libraryGridSize) {
        case LibraryGridSize::SMALL:
            m_contentGrid->setGridSize(4);
            break;
        case LibraryGridSize::MEDIUM:
            m_contentGrid->setGridSize(6);
            break;
        case LibraryGridSize::LARGE:
            m_contentGrid->setGridSize(8);
            break;
    }

    m_contentGrid->setOnItemSelected([this](const Manga& manga) {
        onMangaSelected(manga);
    });

    // Set up B button callback for back navigation from grid cells
    m_contentGrid->setOnBackPressed([this]() {
        if (m_browseMode != BrowseMode::SOURCES) {
            brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
                auto a = aliveWeak.lock(); if (!a || !*a) return;
                handleBackNavigation();
            });
            return true;
        }
        return false;
    });

    // Infinite scroll: auto-load next page when nearing the end
    m_contentGrid->setOnEndReached([this]() {
        if (m_hasNextPage && !m_isLoadingPage) {
            loadNextPage();
        }
    });

    this->addView(m_contentGrid);

    // Load sources initially
    loadSources();
}

SearchTab::~SearchTab() {
    if (m_alive) *m_alive = false;
    if (m_sourceIconsAlive) *m_sourceIconsAlive = false;
}

void SearchTab::willDisappear(bool resetState) {
    brls::Box::willDisappear(resetState);

    // Invalidate alive flags BEFORE destruction so all pending async callbacks
    // (image loader, brls::sync from asyncRun, deferred button handlers) bail out
    if (m_alive) *m_alive = false;
    if (m_sourceIconsAlive) *m_sourceIconsAlive = false;

    // Invalidate load generation so any in-flight async results are ignored
    m_loadGeneration++;

    // Cancel pending image loads to free up worker threads and network bandwidth
    ImageLoader::cancelAll();
    m_isLoadingPage = false;
}

void SearchTab::showLoadingIndicator(const std::string& message) {
    // Clear the results label so we don't show duplicate loading text
    // (m_resultsLabel is white, m_loadingLabel is accent-colored)
    if (m_resultsLabel) {
        m_resultsLabel->setText("");
    }
    m_isLoading = true;
    m_loadingDotCount = 0;
    m_loadingTimer = 0.0f;
    if (m_loadingLabel) {
        m_loadingLabel->setText(message + "...");
        m_loadingLabel->setVisibility(brls::Visibility::VISIBLE);
    }
}

void SearchTab::hideLoadingIndicator() {
    m_isLoading = false;
    if (m_loadingLabel) {
        m_loadingLabel->setVisibility(brls::Visibility::GONE);
    }
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

    // Show offline message if not connected
    if (!Application::getInstance().isConnected()) {
        if (m_resultsLabel) {
            m_resultsLabel->setText("App is offline - connect to a server to browse sources");
        }
        return;
    }

    showLoadingIndicator("Loading sources");

    asyncRun([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Source> sources;

        if (client.fetchSourceList(sources)) {
            brls::Logger::info("SearchTab: Got {} sources", sources.size());

            brls::sync([this, sources, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                hideLoadingIndicator();
                m_sources = sources;
                showSources();
            });
        } else {
            brls::Logger::error("SearchTab: Failed to fetch sources");
            Application::getInstance().setConnected(false);
            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                hideLoadingIndicator();
                m_resultsLabel->setText("App is offline - connect to a server to browse sources");
                // Focus on history button so user can still navigate
                brls::Application::giveFocus(m_historyBtn);
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
    m_loadGeneration++;  // Invalidate in-flight loads so they don't steal focus from IME
    brls::Application::getImeManager()->openForText([this](std::string text) {
        if (text.empty()) return;

        m_searchQuery = text;
        m_titleLabel->setText("Search: " + text);
        addToSearchHistory(text);
        performSearch(text);
    }, "Global Search", "Search across all sources", 256, m_searchQuery);
}

void SearchTab::showSourceSearchDialog() {
    m_loadGeneration++;  // Invalidate in-flight loads so they don't steal focus from IME
    brls::Application::getImeManager()->openForText([this](std::string text) {
        if (text.empty()) return;

        m_searchQuery = text;
        m_titleLabel->setText(m_currentSourceName + " - Search: " + text);
        addToSearchHistory(text);
        m_isGlobalSearch = false;
        performSourceSearch(m_currentSourceId, text);
    }, "Search " + m_currentSourceName, "Search within this source", 256, m_searchQuery);
}

void SearchTab::showSearchHistoryDialog() {
    m_loadGeneration++;  // Invalidate in-flight loads so they don't steal focus from dialog
    auto& settings = Application::getInstance().getSettings();
    auto& history = settings.searchHistory;

    if (history.empty()) {
        brls::Application::notify("No search history");
        return;
    }

    std::vector<std::string> options;
    for (const auto& query : history) {
        options.push_back(query);
    }
    options.push_back("Clear History");

    brls::Dropdown* dropdown = new brls::Dropdown(
        "Search History", options,
        [this, historySize = history.size()](int selected) {
            if (selected < 0) return;

            // Wrap in brls::sync to ensure proper focus after dropdown closes
            brls::sync([this, selected, historySize]() {
                if (selected == static_cast<int>(historySize)) {
                    // Clear history
                    clearSearchHistory();
                } else {
                    // Use selected search query
                    auto& history = Application::getInstance().getSettings().searchHistory;
                    if (selected < static_cast<int>(history.size())) {
                        std::string query = history[selected];
                        m_searchQuery = query;
                        m_titleLabel->setText("Search: " + query);
                        performSearch(query);
                    }
                }
            });
        }, 0);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void SearchTab::addToSearchHistory(const std::string& query) {
    if (query.empty()) return;

    auto& settings = Application::getInstance().getSettings();
    auto& history = settings.searchHistory;

    // Remove if already exists (to move to front)
    auto it = std::find(history.begin(), history.end(), query);
    if (it != history.end()) {
        history.erase(it);
    }

    // Add to front
    history.insert(history.begin(), query);

    // Limit history size
    while (history.size() > static_cast<size_t>(settings.maxSearchHistory)) {
        history.pop_back();
    }

    Application::getInstance().saveSettings();
}

void SearchTab::clearSearchHistory() {
    auto& settings = Application::getInstance().getSettings();
    settings.searchHistory.clear();
    Application::getInstance().saveSettings();
    brls::Application::notify("Search history cleared");
}

void SearchTab::showSources() {
    m_loadGeneration++;  // Invalidate any in-flight async callbacks
    hideLoadingIndicator();  // Cancel any visible loading text from prior browse/search
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
    // Restore search/history buttons when returning to source list
    m_buttonContainer->setVisibility(brls::Visibility::VISIBLE);
    m_historyBtn->setFocusable(true);
    m_globalSearchBtn->setFocusable(true);

    // CRITICAL: Move focus to a safe target before clearing any views.
    // Focus may be on a grid cell or search result row that will be deleted.
    brls::Application::giveFocus(m_historyBtn);

    // Clear manga grid, results by source, and hide search results view
    m_mangaList.clear();
    m_resultsBySource.clear();
    m_contentGrid->setDataSource(m_mangaList);
    m_hasNextPage = false;
    if (m_searchResultsScrollView) {
        m_searchResultsScrollView->setVisibility(brls::Visibility::GONE);
    }
    // Clear search results (safe now - focus moved above)
    if (m_searchResultsBox) {
        m_searchResultsBox->clearViews();
    }

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
    // Invalidate the old source-icon alive flag before destroying the icons.
    // Pending ImageLoader callbacks for previous source icons will see the
    // flag is false and skip the setImageFromMem() call on freed pointers.
    if (m_sourceIconsAlive) *m_sourceIconsAlive = false;
    m_sourceIconsAlive = std::make_shared<bool>(true);

    m_sourceListBox->clearViews();

    // Group sources by language
    std::map<std::string, std::vector<Source>> sourcesByLang;
    for (const auto& source : m_filteredSources) {
        sourcesByLang[source.lang].push_back(source);
    }

    // Track first source row for focus transfer and navigation
    brls::Box* firstSourceRow = nullptr;
    brls::Box* previousSourceRow = nullptr;

    // Create source rows with icons
    for (const auto& [lang, sources] : sourcesByLang) {
        // Language header
        auto* langHeader = new brls::Label();
        std::string langName = lang.empty() ? "Unknown" : lang;
        // Comprehensive language code to display name mapping
        static const std::map<std::string, std::string> languageNames = {
            {"all", "All Languages"}, {"en", "English"}, {"ja", "Japanese"},
            {"ko", "Korean"}, {"zh", "Chinese"}, {"zh-Hans", "Chinese (Simplified)"},
            {"zh-Hant", "Chinese (Traditional)"}, {"es", "Spanish"},
            {"es-419", "Spanish (Latin America)"}, {"pt", "Portuguese"},
            {"pt-BR", "Portuguese (Brazil)"}, {"fr", "French"}, {"de", "German"},
            {"it", "Italian"}, {"ru", "Russian"}, {"ar", "Arabic"},
            {"id", "Indonesian"}, {"th", "Thai"}, {"vi", "Vietnamese"},
            {"pl", "Polish"}, {"tr", "Turkish"}, {"nl", "Dutch"},
            {"uk", "Ukrainian"}, {"cs", "Czech"}, {"ro", "Romanian"},
            {"bg", "Bulgarian"}, {"hu", "Hungarian"}, {"el", "Greek"},
            {"he", "Hebrew"}, {"fa", "Persian"}, {"hi", "Hindi"},
            {"bn", "Bengali"}, {"ms", "Malay"}, {"fil", "Filipino"},
            {"my", "Burmese"}, {"multi", "Multi-language"}
        };
        auto langIt = languageNames.find(lang);
        if (langIt != languageNames.end()) {
            langName = langIt->second;
        } else if (!lang.empty()) {
            langName[0] = std::toupper(langName[0]);
        }

        langHeader->setText(langName + " (" + std::to_string(sources.size()) + ")");
        langHeader->setFontSize(18);
        langHeader->setMarginTop(10);
        langHeader->setMarginBottom(5);
        langHeader->setTextColor(Application::getInstance().getHeaderTextColor());
        m_sourceListBox->addView(langHeader);

        // Source buttons
        for (const auto& source : sources) {
            auto* sourceRow = new brls::Box();
            sourceRow->setAxis(brls::Axis::ROW);
            sourceRow->setAlignItems(brls::AlignItems::CENTER);
            sourceRow->setMarginBottom(8);
            sourceRow->setPadding(8);
            sourceRow->setCornerRadius(8);
            sourceRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());
            sourceRow->setFocusable(true);

            // Track first source row for focus transfer
            if (!firstSourceRow) {
                firstSourceRow = sourceRow;
            }

            // Source icon
            auto* sourceIcon = new brls::Image();
            sourceIcon->setWidth(32);
            sourceIcon->setHeight(32);
            sourceIcon->setMarginRight(12);
            sourceIcon->setScalingType(brls::ImageScalingType::FIT);
            if (!source.iconUrl.empty()) {
                // Load icon asynchronously from server
                std::string iconUrl = Application::getInstance().getServerUrl() + source.iconUrl;
                ImageLoader::loadAsync(iconUrl, [](brls::Image* img) {}, sourceIcon, m_sourceIconsAlive);
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

            // Register B button on source row to go back (exit tab in SOURCES mode)
            // This ensures B button works when focus is on source rows
            sourceRow->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
                // In SOURCES mode, let default handling exit the tab
                return false;
            }, true);  // hidden action

            // Set up navigation from first source row to header buttons
            if (sourceRow == firstSourceRow) {
                // First source row - set custom UP navigation to history button
                sourceRow->setCustomNavigationRoute(brls::FocusDirection::UP, m_historyBtn);
            }

            previousSourceRow = sourceRow;
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

    // Set up navigation from header buttons down to first source row
    if (firstSourceRow) {
        m_historyBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, firstSourceRow);
        m_globalSearchBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, firstSourceRow);
    } else {
        // No source rows - clear navigation routes to prevent crash
        m_historyBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, nullptr);
        m_globalSearchBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, nullptr);
    }

    // Transfer focus to first source row
    if (firstSourceRow) {
        brls::Application::giveFocus(firstSourceRow);
    }
}

void SearchTab::showSourceBrowser(const Source& source) {
    // Invalidate any in-flight async results from previous browse/search so
    // stale callbacks don't overwrite the new state.
    m_loadGeneration++;

    m_currentSourceId = source.id;
    m_currentSourceName = source.name;

    m_titleLabel->setText(source.name);

    // Show source-specific buttons
    m_popularBtn->setVisibility(brls::Visibility::VISIBLE);
    m_latestBtn->setVisibility(source.supportsLatest ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    // Filter button hidden until source filters are implemented
    m_filterBtn->setVisibility(brls::Visibility::GONE);
    m_backBtn->setVisibility(brls::Visibility::VISIBLE);

    // CRITICAL: Move focus away from source list BEFORE clearing views.
    // The currently focused view may be a source row that clearViews() will delete.
    // If we clear first, borealis holds a dangling focus pointer and crashes
    // when giveFocus tries to call onFocusLost() on the freed view.
    brls::Application::giveFocus(m_popularBtn);

    // Hide source list and search results, show content grid
    if (m_sourceScrollView) {
        m_sourceScrollView->setVisibility(brls::Visibility::GONE);
    }
    // Invalidate source icon alive flag BEFORE clearing - pending ImageLoader
    // callbacks for source icons would otherwise write to freed brls::Image
    // pointers (m_alive stays true since SearchTab is alive, so the stale
    // callbacks pass the alive check and crash with a wild pointer).
    if (m_sourceIconsAlive) *m_sourceIconsAlive = false;
    // Cancel ALL pending image loads (source icons still being fetched/decoded
    // by worker threads).  Without this, up to 85 source-icon downloads and
    // their decode pipelines (libwebp → FFmpeg → stb_image) keep running in
    // the background, burning memory while the 12 new manga thumbnail loads
    // are queued — the combined allocation pressure crashes the Vita.
    ImageLoader::cancelAll();
    // Clear source list to prevent ghost focus targets (safe now - focus moved above)
    if (m_sourceListBox) {
        m_sourceListBox->clearViews();
    }
    if (m_searchResultsScrollView) {
        m_searchResultsScrollView->setVisibility(brls::Visibility::GONE);
    }
    // Clear search results to prevent ghost focus targets
    if (m_searchResultsBox) {
        m_searchResultsBox->clearViews();
    }
    m_contentGrid->setVisibility(brls::Visibility::VISIBLE);

    // Set up navigation from header buttons down to mode buttons (source list is now hidden)
    m_historyBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, m_backBtn);
    m_globalSearchBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, m_backBtn);

    // Load popular manga by default
    m_browseMode = BrowseMode::POPULAR;
    loadPopularManga(source.id);
    updateModeButtons();
}

void SearchTab::showFilterDialog() {
    // Source filter dialog not yet implemented
    // Would show configurable filters like genres, status, etc.
    brls::Application::notify("Source filters coming soon");
}

void SearchTab::loadPopularManga(int64_t sourceId) {
    brls::Logger::debug("SearchTab: Loading popular manga from source {}", sourceId);
    m_resultsLabel->setText("Loading popular manga...");
    showLoadingIndicator("Loading popular manga");
    m_currentPage = 1;
    int gen = ++m_loadGeneration;

    asyncRun([this, sourceId, gen, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;
        bool hasNextPage = false;

        if (client.fetchPopularManga(sourceId, 1, manga, hasNextPage)) {
            brls::Logger::info("SearchTab: Got {} popular manga", manga.size());

            brls::sync([this, manga, hasNextPage, gen, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (gen != m_loadGeneration) return;  // Stale callback, user navigated away
                hideLoadingIndicator();
                m_mangaList = manga;
                m_hasNextPage = hasNextPage;
                m_contentGrid->setDataSource(m_mangaList);
                m_resultsLabel->setText(std::to_string(manga.size()) + " manga found");

                // Transfer focus to first cell via focusIndex which handles
                // pending focus during incremental grid builds
                if (!manga.empty()) {
                    m_contentGrid->focusIndex(0);
                }
            });
        } else {
            brls::Logger::error("SearchTab: Failed to fetch popular manga");
            brls::sync([this, gen, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (gen != m_loadGeneration) return;
                hideLoadingIndicator();
                m_resultsLabel->setText("Failed to load popular manga");
                // Focus on Popular button so user can retry
                brls::Application::giveFocus(m_popularBtn);
            });
        }
    });
}

void SearchTab::loadLatestManga(int64_t sourceId) {
    brls::Logger::debug("SearchTab: Loading latest manga from source {}", sourceId);
    m_resultsLabel->setText("Loading latest manga...");
    showLoadingIndicator("Loading latest manga");
    m_currentPage = 1;
    int gen = ++m_loadGeneration;

    asyncRun([this, sourceId, gen, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;
        bool hasNextPage = false;

        if (client.fetchLatestManga(sourceId, 1, manga, hasNextPage)) {
            brls::Logger::info("SearchTab: Got {} latest manga", manga.size());

            brls::sync([this, manga, hasNextPage, gen, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (gen != m_loadGeneration) return;
                hideLoadingIndicator();
                m_mangaList = manga;
                m_hasNextPage = hasNextPage;
                m_contentGrid->setDataSource(m_mangaList);
                m_resultsLabel->setText(std::to_string(manga.size()) + " manga found");

                // Transfer focus to first cell via focusIndex which handles
                // pending focus during incremental grid builds
                if (!manga.empty()) {
                    m_contentGrid->focusIndex(0);
                }
            });
        } else {
            brls::Logger::error("SearchTab: Failed to fetch latest manga");
            brls::sync([this, gen, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (gen != m_loadGeneration) return;
                hideLoadingIndicator();
                m_resultsLabel->setText("Failed to load latest manga");
                // Focus on Latest button so user can retry
                brls::Application::giveFocus(m_latestBtn);
            });
        }
    });
}

void SearchTab::performSearch(const std::string& query) {
    if (query.empty()) {
        m_resultsLabel->setText("");
        m_mangaList.clear();
        m_resultsBySource.clear();
        m_contentGrid->setDataSource(m_mangaList);
        return;
    }

    // If we have a current source, search within that source
    if (m_currentSourceId != 0 && m_browseMode != BrowseMode::SOURCES) {
        m_isGlobalSearch = false;
        performSourceSearch(m_currentSourceId, query);
        return;
    }

    // This is a global search
    m_isGlobalSearch = true;

    // Filter sources by language setting first
    filterSourcesByLanguage();

    // Update UI for search mode
    m_browseMode = BrowseMode::SEARCH_RESULTS;
    m_popularBtn->setVisibility(brls::Visibility::GONE);
    m_latestBtn->setVisibility(brls::Visibility::GONE);
    m_filterBtn->setVisibility(brls::Visibility::GONE);
    m_backBtn->setVisibility(brls::Visibility::VISIBLE);
    // Hide search/history buttons during global search
    m_buttonContainer->setVisibility(brls::Visibility::GONE);
    m_historyBtn->setFocusable(false);
    m_globalSearchBtn->setFocusable(false);
    m_resultsLabel->setText("Searching " + std::to_string(m_filteredSources.size()) + " sources...");
    showLoadingIndicator("Searching sources");

    // CRITICAL: Move focus before clearing views to prevent use-after-free
    brls::Application::giveFocus(m_backBtn);

    // Invalidate source icon alive flag BEFORE clearing - pending ImageLoader
    // callbacks for source icons would otherwise write to freed brls::Image
    // pointers (the stale callbacks pass the alive check and crash).
    if (m_sourceIconsAlive) *m_sourceIconsAlive = false;

    // Cancel all pending image loads from previous search results to prevent
    // combined memory pressure from old + new thumbnail downloads crashing the Vita.
    ImageLoader::cancelAll();

    // Hide source list and grid, will show grouped results
    if (m_sourceScrollView) {
        m_sourceScrollView->setVisibility(brls::Visibility::GONE);
    }
    // Clear source list (safe now - focus moved above)
    if (m_sourceListBox) {
        m_sourceListBox->clearViews();
    }
    // Clear old search results to free memory before new search
    if (m_searchResultsBox) {
        m_searchResultsBox->clearViews();
    }
    m_contentGrid->setVisibility(brls::Visibility::GONE);

    // Copy filtered sources for async use
    std::vector<Source> sourcesToSearch = m_filteredSources;
    int gen = ++m_loadGeneration;

    asyncRun([this, query, sourcesToSearch, gen, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        // Map to group results by source
        std::map<std::string, std::vector<Manga>> resultsBySource;
        int totalResults = 0;
        int failedSources = 0;
        int searchedSources = 0;

        // Search each filtered source
        for (const auto& source : sourcesToSearch) {
            std::vector<Manga> results;
            bool hasNextPage = false;
            searchedSources++;

            // Use searchManga which uses GraphQL API
            if (client.searchManga(source.id, query, 1, results, hasNextPage)) {
                if (!results.empty()) {
                    for (auto& manga : results) {
                        manga.sourceName = source.name;
                    }
                    resultsBySource[source.name] = results;
                    totalResults += results.size();
                }
            } else {
                failedSources++;
                brls::Logger::warning("SearchTab: Search failed for source '{}'", source.name);
            }

            // Limit to prevent too many requests on constrained hardware
            if (totalResults >= 100) {
                brls::Logger::info("SearchTab: Hit 100-result limit after {} of {} sources",
                                   searchedSources, sourcesToSearch.size());
                break;
            }
        }

        brls::Logger::info("SearchTab: Found {} results from {} sources for '{}' ({} failed)",
                           totalResults, resultsBySource.size(), query, failedSources);

        // Store all results for flat list backup
        std::vector<Manga> allResults;
        for (const auto& [sourceName, mangas] : resultsBySource) {
            for (const auto& manga : mangas) {
                allResults.push_back(manga);
            }
        }

        int totalSourceCount = static_cast<int>(sourcesToSearch.size());
        bool wasTruncated = (totalResults >= 100 && searchedSources < totalSourceCount);

        brls::sync([this, allResults, resultsBySource, failedSources, searchedSources,
                     totalSourceCount, wasTruncated, gen, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            if (gen != m_loadGeneration) return;  // Stale callback, user navigated away
            hideLoadingIndicator();
            m_mangaList = allResults;
            m_resultsBySource = resultsBySource;

            // Show result count with source breakdown
            if (resultsBySource.empty()) {
                std::string resultText = "No results found";
                if (failedSources > 0) {
                    resultText += " (" + std::to_string(failedSources) + " source" +
                                  (failedSources > 1 ? "s" : "") + " failed)";
                }
                m_resultsLabel->setText(resultText);
                m_contentGrid->setDataSource(m_mangaList);
                m_contentGrid->setVisibility(brls::Visibility::VISIBLE);
                // Focus on back button when no results found
                brls::Application::giveFocus(m_backBtn);
            } else {
                std::string resultText = std::to_string(allResults.size()) + " results from " +
                                        std::to_string(resultsBySource.size()) + "/" +
                                        std::to_string(totalSourceCount) + " sources";
                if (wasTruncated) {
                    resultText += " (limited)";
                }
                if (failedSources > 0) {
                    resultText += " (" + std::to_string(failedSources) + " failed)";
                }
                m_resultsLabel->setText(resultText);

                // Display results grouped by source in horizontal rows
                populateSearchResultsBySource();
            }
        });
    });
}

void SearchTab::performSourceSearch(int64_t sourceId, const std::string& query) {
    brls::Logger::debug("SearchTab: Searching '{}' in source {}", query, sourceId);
    m_resultsLabel->setText("Searching...");
    showLoadingIndicator("Searching");
    m_browseMode = BrowseMode::SEARCH_RESULTS;
    m_currentPage = 1;
    int gen = ++m_loadGeneration;

    asyncRun([this, sourceId, query, gen, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;
        bool hasNextPage = false;

        if (client.searchManga(sourceId, query, 1, manga, hasNextPage)) {
            brls::Logger::info("SearchTab: Found {} results for '{}'", manga.size(), query);

            brls::sync([this, manga, hasNextPage, gen, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (gen != m_loadGeneration) return;
                hideLoadingIndicator();
                m_mangaList = manga;
                m_hasNextPage = hasNextPage;
                m_contentGrid->setDataSource(m_mangaList);
                m_resultsLabel->setText(std::to_string(manga.size()) + " results");

                // Transfer focus to first cell via focusIndex which handles
                // pending focus during incremental grid builds
                if (!manga.empty()) {
                    m_contentGrid->focusIndex(0);
                } else {
                    // No results - focus on back button
                    brls::Application::giveFocus(m_backBtn);
                }
            });
        } else {
            brls::Logger::error("SearchTab: Failed to search manga");
            brls::sync([this, gen, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (gen != m_loadGeneration) return;
                hideLoadingIndicator();
                m_resultsLabel->setText("Search failed");
                // Focus on back button so user can go back
                brls::Application::giveFocus(m_backBtn);
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

void SearchTab::populateSearchResultsBySource() {
    // Create or clear the search results scroll view
    if (!m_searchResultsScrollView) {
        m_searchResultsScrollView = new brls::ScrollingFrame();
        m_searchResultsScrollView->setGrow(1.0f);
        // Use centered scrolling to keep focused items visible
        m_searchResultsScrollView->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

        m_searchResultsBox = new brls::Box();
        m_searchResultsBox->setAxis(brls::Axis::COLUMN);
        m_searchResultsBox->setJustifyContent(brls::JustifyContent::FLEX_START);
        m_searchResultsBox->setAlignItems(brls::AlignItems::STRETCH);
        m_searchResultsBox->setPadding(10);

        m_searchResultsScrollView->setContentView(m_searchResultsBox);

        // Register B button on search results scroll view to handle back navigation
        m_searchResultsScrollView->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
            if (m_browseMode != BrowseMode::SOURCES) {
                brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
                    auto a = aliveWeak.lock(); if (!a || !*a) return;
                    handleBackNavigation();
                });
                return true;
            }
            return false;
        }, true);  // hidden action
    }
    // Move focus to safe target before clearing search result views
    brls::Application::giveFocus(m_backBtn);
    m_searchResultsBox->clearViews();

    // Track first cell for focus transfer
    brls::View* firstCell = nullptr;

    // Create a horizontal row for each source
    for (const auto& [sourceName, manga] : m_resultsBySource) {
        if (!manga.empty()) {
            brls::View* cell = createSourceRow(sourceName, manga);
            if (!firstCell && cell) {
                firstCell = cell;
            }
        }
    }

    // Show search results view, hide others
    if (m_sourceScrollView) {
        m_sourceScrollView->setVisibility(brls::Visibility::GONE);
    }
    m_contentGrid->setVisibility(brls::Visibility::GONE);
    m_searchResultsScrollView->setVisibility(brls::Visibility::VISIBLE);

    // Add scroll view if not already added
    if (m_searchResultsScrollView->getParent() == nullptr) {
        this->addView(m_searchResultsScrollView);
    }

    // Transfer focus to first manga cell in search results
    if (firstCell) {
        brls::Application::giveFocus(firstCell);
    }
}

brls::View* SearchTab::createSourceRow(const std::string& sourceName, const std::vector<Manga>& manga) {
    // Source header label
    auto* sourceLabel = new brls::Label();
    sourceLabel->setText(sourceName + " (" + std::to_string(manga.size()) + ")");
    sourceLabel->setFontSize(18);
    sourceLabel->setMarginTop(10);
    sourceLabel->setMarginBottom(8);
    sourceLabel->setTextColor(Application::getInstance().getHeaderTextColor());
    m_searchResultsBox->addView(sourceLabel);

    // Calculate cell dimensions based on library display settings (same as grid)
    const auto& settings = Application::getInstance().getSettings();
    int columns = 6;  // default medium
    switch (settings.libraryGridSize) {
        case LibraryGridSize::SMALL:  columns = 4; break;
        case LibraryGridSize::MEDIUM: columns = 6; break;
        case LibraryGridSize::LARGE:  columns = 8; break;
    }
    int cellMargin = 12;
    int availableWidth = 920;
    int cellWidth = (availableWidth - (columns - 1) * cellMargin) / columns;
    int cellHeight = static_cast<int>(cellWidth * 1.4);
    bool compactMode = (settings.libraryDisplayMode == LibraryDisplayMode::GRID_COMPACT);
    bool listMode = (settings.libraryDisplayMode == LibraryDisplayMode::LIST);

    if (listMode) {
        cellWidth = 900;
        cellHeight = 80;
    }

    // Create horizontal scrolling row for cells
    auto* rowBox = new HorizontalScrollRow();
    rowBox->setHeight(cellHeight + 10);
    rowBox->setMarginBottom(10);

    brls::View* firstCell = nullptr;

    // Create manga cells for each result
    for (size_t i = 0; i < manga.size(); i++) {
        auto* cell = new MangaItemCell();
        cell->setShowLibraryBadge(true);  // Show star for library items in search results
        if (compactMode) {
            cell->setCompactMode(true);
        } else if (listMode) {
            cell->setListMode(true);
        }
        cell->setGridColumns(columns);
        cell->setManga(manga[i]);
        cell->setWidth(cellWidth);
        cell->setHeight(cellHeight);
        cell->setMarginRight(10);

        Manga mangaCopy = manga[i];
        cell->registerClickAction([this, mangaCopy](brls::View* view) {
            onMangaSelected(mangaCopy);
            return true;
        });
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

        // Register B button on cell to handle back navigation
        cell->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
            if (m_browseMode != BrowseMode::SOURCES) {
                brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
                    auto a = aliveWeak.lock(); if (!a || !*a) return;
                    handleBackNavigation();
                });
                return true;
            }
            return false;
        }, true);  // hidden action

        // Track first cell for focus transfer
        if (i == 0) {
            firstCell = cell;
        }

        rowBox->addView(cell);
    }

    m_searchResultsBox->addView(rowBox);
    return firstCell;
}

void SearchTab::loadNextPage() {
    if (!m_hasNextPage || m_isLoadingPage) return;

    m_isLoadingPage = true;
    m_currentPage++;
    brls::Logger::debug("SearchTab: Loading page {}", m_currentPage);
    showLoadingIndicator("Loading page " + std::to_string(m_currentPage));

    // Remember the index of first new item (current list size)
    int firstNewItemIndex = static_cast<int>(m_mangaList.size());

    int gen = m_loadGeneration;  // Don't increment - loadNextPage appends, not a new navigation

    // Capture member values by value for safe background thread access
    auto browseMode = m_browseMode;
    auto sourceId = m_currentSourceId;
    auto page = m_currentPage;
    auto query = m_searchQuery;

    asyncRun([this, firstNewItemIndex, gen, aliveWeak = std::weak_ptr<bool>(m_alive),
              browseMode, sourceId, page, query]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;
        bool hasNextPage = false;

        bool success = false;
        if (browseMode == BrowseMode::POPULAR) {
            success = client.fetchPopularManga(sourceId, page, manga, hasNextPage);
        } else if (browseMode == BrowseMode::LATEST) {
            success = client.fetchLatestManga(sourceId, page, manga, hasNextPage);
        } else if (browseMode == BrowseMode::SEARCH_RESULTS && !query.empty()) {
            success = client.searchManga(sourceId, query, page, manga, hasNextPage);
        }

        if (success) {
            brls::sync([this, manga, hasNextPage, firstNewItemIndex, gen, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (gen != m_loadGeneration) return;
                // Append to existing list
                for (const auto& m : manga) {
                    m_mangaList.push_back(m);
                }
                m_hasNextPage = hasNextPage;
                m_contentGrid->appendItems(manga);
                m_resultsLabel->setText(std::to_string(m_mangaList.size()) + " manga");
                m_isLoadingPage = false;
                hideLoadingIndicator();
            });
        } else {
            brls::sync([this, gen, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (gen != m_loadGeneration) return;
                m_isLoadingPage = false;
                hideLoadingIndicator();
            });
        }
    });
}

void SearchTab::updateModeButtons() {
    if (m_browseMode == BrowseMode::SOURCES) {
        m_popularBtn->setVisibility(brls::Visibility::GONE);
        m_latestBtn->setVisibility(brls::Visibility::GONE);
        m_backBtn->setVisibility(brls::Visibility::GONE);
    } else {
        m_popularBtn->setVisibility(brls::Visibility::VISIBLE);
        m_backBtn->setVisibility(brls::Visibility::VISIBLE);

        // Show latest button if source supports it
        for (const auto& source : m_sources) {
            if (source.id == m_currentSourceId && source.supportsLatest) {
                m_latestBtn->setVisibility(brls::Visibility::VISIBLE);
                break;
            }
        }

        // Highlight the active mode button with accent color
        NVGcolor accentColor = Application::getInstance().getAccentColor();
        NVGcolor defaultColor = nvgRGBA(0, 0, 0, 0);  // transparent

        m_popularBtn->setBackgroundColor(m_browseMode == BrowseMode::POPULAR ? accentColor : defaultColor);
        m_latestBtn->setBackgroundColor(m_browseMode == BrowseMode::LATEST ? accentColor : defaultColor);
    }
}

void SearchTab::handleBackNavigation() {
    if (m_isNavigatingBack) return;  // Prevent double back-press
    if (m_browseMode == BrowseMode::SOURCES) return;  // Already on sources, nothing to do
    m_isNavigatingBack = true;
    m_loadGeneration++;  // Invalidate any in-flight async callbacks
    hideLoadingIndicator();  // Hide any loading text left from cancelled async loads
    // Invalidate source icon alive flag before clearing to prevent stale
    // texture uploads to freed brls::Image pointers.
    if (m_sourceIconsAlive) *m_sourceIconsAlive = false;
    // Cancel pending image loads (source icons or manga thumbnails) to free
    // worker threads and memory before navigating to a new view state.
    ImageLoader::cancelAll();
    if (m_browseMode == BrowseMode::SEARCH_RESULTS) {
        if (m_isGlobalSearch) {
            // Global search: go back to sources list
            showSources();
            m_isNavigatingBack = false;
        } else {
            // Source-specific search: go back to source's main page (Popular)
            // Find the current source and show its browser again
            for (const auto& source : m_sources) {
                if (source.id == m_currentSourceId) {
                    m_browseMode = BrowseMode::POPULAR;
                    m_titleLabel->setText(source.name);
                    m_searchLabel->setVisibility(brls::Visibility::GONE);

                    // Update buttons for source browsing mode
                    m_popularBtn->setVisibility(brls::Visibility::VISIBLE);
                    m_latestBtn->setVisibility(source.supportsLatest ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
                    m_backBtn->setVisibility(brls::Visibility::VISIBLE);

                    // CRITICAL: Move focus before clearing search result views
                    brls::Application::giveFocus(m_popularBtn);

                    // Hide search results, show content grid
                    if (m_searchResultsScrollView) {
                        m_searchResultsScrollView->setVisibility(brls::Visibility::GONE);
                    }
                    // Clear search results (safe now - focus moved above)
                    if (m_searchResultsBox) {
                        m_searchResultsBox->clearViews();
                    }
                    m_contentGrid->setVisibility(brls::Visibility::VISIBLE);

                    // Load popular manga
                    loadPopularManga(m_currentSourceId);
                    m_isNavigatingBack = false;  // Allow back navigation again
                    return;
                }
            }
            // Fallback: go to sources if source not found
            showSources();
            m_isNavigatingBack = false;
        }
    } else {
        // For POPULAR/LATEST modes: go back to sources list
        showSources();
        m_isNavigatingBack = false;
    }
}

} // namespace vitasuwayomi
