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

    // Source filter button with triangle hint above (only shown when browsing a source)
    m_filterBtnContainer = new brls::Box();
    m_filterBtnContainer->setAxis(brls::Axis::COLUMN);
    m_filterBtnContainer->setAlignItems(brls::AlignItems::CENTER);
    m_filterBtnContainer->setMarginRight(10);
    m_filterBtnContainer->setVisibility(brls::Visibility::GONE);  // Hidden on sources page

    auto* triangleHintIcon = new brls::Image();
    triangleHintIcon->setWidth(16);
    triangleHintIcon->setHeight(16);
    triangleHintIcon->setScalingType(brls::ImageScalingType::FIT);
    triangleHintIcon->setImageFromFile("app0:resources/images/triangle_button.png");
    triangleHintIcon->setMarginBottom(2);
    m_filterBtnContainer->addView(triangleHintIcon);

    m_tagFilterBtn = new brls::Button();
    m_tagFilterBtn->setWidth(44);
    m_tagFilterBtn->setHeight(44);
    m_tagFilterBtn->setCornerRadius(8);
    m_tagFilterBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_tagFilterBtn->setAlignItems(brls::AlignItems::CENTER);

    m_tagFilterIcon = new brls::Image();
    m_tagFilterIcon->setWidth(24);
    m_tagFilterIcon->setHeight(24);
    m_tagFilterIcon->setScalingType(brls::ImageScalingType::FIT);
    m_tagFilterIcon->setImageFromFile("app0:resources/icons/tag.png");
    m_tagFilterBtn->addView(m_tagFilterIcon);

    m_tagFilterBtn->registerClickAction([this](brls::View* view) {
        brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
            auto a = aliveWeak.lock(); if (!a || !*a) return;
            if (m_browseMode != BrowseMode::SOURCES && m_currentSourceId != 0) {
                showFilterDialog();
            }
        });
        return true;
    });
    m_tagFilterBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_tagFilterBtn));
    m_tagFilterBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        if (m_browseMode != BrowseMode::SOURCES) {
            brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() { auto a = aliveWeak.lock(); if (!a || !*a) return; handleBackNavigation(); });
            return true;
        }
        return false;
    }, true);
    m_filterBtnContainer->addView(m_tagFilterBtn);
    m_buttonContainer->addView(m_filterBtnContainer);

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

    // Register Y button (triangle) to open source filters when browsing
    this->registerAction("Filter", brls::ControllerButton::BUTTON_Y, [this](brls::View* view) {
        if (m_currentSourceId != 0 && (m_browseMode == BrowseMode::POPULAR || m_browseMode == BrowseMode::LATEST)) {
            brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
                auto a = aliveWeak.lock(); if (!a || !*a) return;
                showFilterDialog();
            });
            return true;
        }
        return false;
    });

    // Register Circle button (B) to go back in search/browse modes
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        // If a filter panel is open, close it first
        if (m_filterPanelType != FilterPanelType::NONE) {
            brls::sync([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
                auto a = aliveWeak.lock(); if (!a || !*a) return;
                hideFilterPanel();
            });
            return true;
        }
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
    m_popularBtn->setCornerRadius(6);
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
    m_latestBtn->setCornerRadius(6);
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

    // Content wrapper: ROW layout with main content (left) and filter panel (right)
    m_contentWrapper = new brls::Box();
    m_contentWrapper->setAxis(brls::Axis::ROW);
    m_contentWrapper->setGrow(1.0f);
    m_contentWrapper->setAlignItems(brls::AlignItems::STRETCH);

    // Main content column (left side, takes remaining space)
    m_mainContent = new brls::Box();
    m_mainContent->setAxis(brls::Axis::COLUMN);
    m_mainContent->setGrow(1.0f);
    m_mainContent->setShrink(1.0f);

    // Results label
    m_resultsLabel = new brls::Label();
    m_resultsLabel->setText("");
    m_resultsLabel->setFontSize(18);
    m_resultsLabel->setMarginBottom(10);
    m_mainContent->addView(m_resultsLabel);

    // Loading indicator label (hidden by default)
    m_loadingLabel = new brls::Label();
    m_loadingLabel->setText("");
    m_loadingLabel->setFontSize(20);
    m_loadingLabel->setTextColor(Application::getInstance().getAccentColor());
    m_loadingLabel->setMarginBottom(10);
    m_loadingLabel->setVisibility(brls::Visibility::GONE);
    m_mainContent->addView(m_loadingLabel);

    m_contentWrapper->addView(m_mainContent);

    // Inline filter panel (hidden by default, appears on right side when button pressed)
    m_filterPanel = new brls::Box();
    m_filterPanel->setAxis(brls::Axis::COLUMN);
    m_filterPanel->setVisibility(brls::Visibility::GONE);
    m_contentWrapper->addView(m_filterPanel);

    this->addView(m_contentWrapper);

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

    m_mainContent->addView(m_contentGrid);

    // Load sources initially
    loadSources();
}

SearchTab::~SearchTab() {
    if (m_alive) *m_alive = false;
    if (m_sourceIconsAlive) *m_sourceIconsAlive = false;
}

void SearchTab::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);

    // Refresh star badges on all cells to reflect library add/remove changes
    // made while this tab was covered by another activity (e.g. detail view)
    if (m_contentGrid) {
        m_contentGrid->refreshLibraryBadges();
    }

    // Also refresh star badges in search-results-by-source horizontal rows
    if (m_searchResultsBox) {
        for (auto* row : m_searchResultsBox->getChildren()) {
            auto* rowBox = dynamic_cast<brls::Box*>(row);
            if (!rowBox) continue;
            for (auto* child : rowBox->getChildren()) {
                auto* cell = dynamic_cast<MangaItemCell*>(child);
                if (cell) {
                    cell->refreshLibraryBadge();
                }
            }
        }
    }
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

    // Refresh star badges on visible cells to reflect library add/remove changes
    // made from the detail view (uses recent additions/removals tracking)
    if (m_contentGrid) {
        m_contentGrid->refreshLibraryBadges();
    }

    // Also refresh star badges in search-results-by-source horizontal rows
    if (m_searchResultsBox) {
        for (auto* row : m_searchResultsBox->getChildren()) {
            auto* rowBox = dynamic_cast<brls::Box*>(row);
            if (!rowBox) continue;
            for (auto* child : rowBox->getChildren()) {
                auto* cell = dynamic_cast<MangaItemCell*>(child);
                if (cell) {
                    cell->refreshLibraryBadge();
                }
            }
        }
    }
}

bool SearchTab::isFocusInPanel(brls::View* view) const {
    while (view) {
        if (view == m_filterPanel)
            return true;
        view = view->getParent();
    }
    return false;
}

brls::View* SearchTab::getNextFocus(brls::FocusDirection direction, brls::View* currentView) {
    // When the filter panel is visible, prevent d-pad focus from crossing
    // between the main content and the panel.  Focus should only enter/leave
    // the panel via explicit actions (Y to open, B to close).
    if (m_filterPanelType != FilterPanelType::NONE &&
        m_filterPanel && m_filterPanel->getVisibility() == brls::Visibility::VISIBLE) {

        brls::View* focused = brls::Application::getCurrentFocus();
        bool inPanel = isFocusInPanel(focused);

        // Block RIGHT from main content into the panel
        if (!inPanel && direction == brls::FocusDirection::RIGHT)
            return focused;  // stay where we are

        // Block LEFT from panel into main content
        if (inPanel && direction == brls::FocusDirection::LEFT)
            return focused;  // stay where we are
    }

    return brls::Box::getNextFocus(direction, currentView);
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

        hideFilterPanel();
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

        hideFilterPanel();
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
                        hideFilterPanel();
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
    hideFilterPanel();  // Close any open inline filter panel
    m_browseMode = BrowseMode::SOURCES;
    m_titleLabel->setText("Browse");
    m_searchLabel->setVisibility(brls::Visibility::GONE);

    // Filter sources by language and tags
    filterSourcesByLanguage();
    m_resultsLabel->setText(std::to_string(m_filteredSources.size()) + " sources");

    // Hide source-specific buttons
    m_popularBtn->setVisibility(brls::Visibility::GONE);
    m_latestBtn->setVisibility(brls::Visibility::GONE);
    m_backBtn->setVisibility(brls::Visibility::GONE);
    // Restore filter icon on header button
    if (m_tagFilterIcon) m_tagFilterIcon->setImageFromFile("app0:resources/icons/tag.png");
    m_tagFilterBtn->setBackgroundColor(Application::getInstance().getButtonColor());
    // Hide the filter button on the sources page (it does nothing here)
    m_filterBtnContainer->setVisibility(brls::Visibility::GONE);
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
        m_mainContent->addView(m_sourceScrollView);
    }

    // Set up navigation from header buttons down to first source row
    if (firstSourceRow) {
        m_tagFilterBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, firstSourceRow);
        m_historyBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, firstSourceRow);
        m_globalSearchBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, firstSourceRow);
    } else {
        // No source rows - clear navigation routes to prevent crash
        m_tagFilterBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, nullptr);
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
    m_backBtn->setVisibility(brls::Visibility::VISIBLE);

    // Reset filter state for new source
    m_sourceFilters.clear();
    m_filtersLoaded = false;
    m_filtersActive = false;
    // Reset filter button color for source browsing
    m_tagFilterBtn->setBackgroundColor(Application::getInstance().getButtonColor());
    // Show the filter button when browsing a source
    m_filterBtnContainer->setVisibility(brls::Visibility::VISIBLE);

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

    // Navigation chain: [< Back] → [Tag] → [History] → [Search]
    // RIGHT on Back button goes to Tag button
    m_backBtn->setCustomNavigationRoute(brls::FocusDirection::RIGHT, m_tagFilterBtn);
    // LEFT on Tag button goes to Back button
    m_tagFilterBtn->setCustomNavigationRoute(brls::FocusDirection::LEFT, m_backBtn);
    // LEFT on History button goes to Tag button
    m_historyBtn->setCustomNavigationRoute(brls::FocusDirection::LEFT, m_tagFilterBtn);

    // Load popular manga by default
    m_browseMode = BrowseMode::POPULAR;
    loadPopularManga(source.id);
    updateModeButtons();

    // Load source filters in background
    loadSourceFilters();
}

void SearchTab::loadSourceFilters() {
    if (m_filtersLoaded || m_currentSourceId == 0) return;

    asyncRun([this, sourceId = m_currentSourceId, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<SourceFilter> filters;

        if (client.fetchSourceFilters(sourceId, filters)) {
            brls::sync([this, filters, sourceId, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (m_currentSourceId != sourceId) return;  // User navigated away
                m_sourceFilters = filters;
                m_filtersLoaded = true;
                brls::Logger::info("Loaded {} filters for source", filters.size());
            });
        } else {
            brls::Logger::warning("Failed to load source filters");
        }
    });
}

void SearchTab::resetFilters() {
    for (auto& filter : m_sourceFilters) {
        switch (filter.type) {
            case FilterType::TEXT:
                filter.textState = filter.textDefault;
                break;
            case FilterType::CHECKBOX:
                filter.checkBoxState = filter.checkBoxDefault;
                break;
            case FilterType::TRISTATE:
                filter.triState = TriState::IGNORE;
                break;
            case FilterType::SELECT:
                filter.selectState = filter.selectDefault;
                break;
            case FilterType::SORT:
                filter.sortState = filter.sortDefault;
                break;
            case FilterType::GROUP:
                for (auto& child : filter.filters) {
                    switch (child.type) {
                        case FilterType::TRISTATE:
                            child.triState = TriState::IGNORE;
                            break;
                        case FilterType::CHECKBOX:
                            child.checkBoxState = child.checkBoxDefault;
                            break;
                        case FilterType::TEXT:
                            child.textState = child.textDefault;
                            break;
                        case FilterType::SELECT:
                            child.selectState = child.selectDefault;
                            break;
                        default: break;
                    }
                }
                break;
            default: break;
        }
    }
    m_filtersActive = false;
}

void SearchTab::applyFilters() {
    if (m_currentSourceId == 0) return;

    m_filtersActive = true;
    m_currentPage = 1;
    m_mangaList.clear();
    m_browseMode = BrowseMode::POPULAR;  // Treat filter search as browsing
    showLoadingIndicator("Applying filters");
    m_resultsLabel->setText("Searching with filters...");
    updateModeButtons();

    // Update header tag/filter button to show filters are active
    m_tagFilterBtn->setBackgroundColor(Application::getInstance().getActiveRowBackground());

    int gen = ++m_loadGeneration;

    asyncRun([this, gen, aliveWeak = std::weak_ptr<bool>(m_alive),
              sourceId = m_currentSourceId, filters = m_sourceFilters]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;
        bool hasNextPage = false;

        if (client.searchMangaWithFilters(sourceId, "", 1, filters, manga, hasNextPage)) {
            brls::sync([this, manga, hasNextPage, gen, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (gen != m_loadGeneration) return;
                hideLoadingIndicator();
                m_mangaList = manga;
                m_hasNextPage = hasNextPage;
                m_contentGrid->setDataSource(m_mangaList);
                m_resultsLabel->setText(std::to_string(manga.size()) + " results (filtered)");

                if (!manga.empty()) {
                    m_contentGrid->focusIndex(0);
                }
            });
        } else {
            brls::sync([this, gen, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (gen != m_loadGeneration) return;
                hideLoadingIndicator();
                m_resultsLabel->setText("Filter search failed");
                brls::Application::giveFocus(m_tagFilterBtn);
            });
        }
    });
}

void SearchTab::hideFilterPanel() {
    if (m_filterPanel) {
        // Clear highlighted row BEFORE clearViews to avoid use-after-free
        // (focus events could fire during view deletion and access freed row)
        m_lastHighlightedRow = nullptr;
        m_filterPanelType = FilterPanelType::NONE;
        m_filterPanel->clearViews();
        m_filterPanel->setVisibility(brls::Visibility::GONE);
        // Restore focus to the view that was focused before the panel opened.
        // The saved view could have been destroyed if the grid was rebuilt
        // while the panel was open (e.g. async callback calling setDataSource).
        // Validate the pointer is still in our view hierarchy before using it.
        if (m_prePanelFocusView) {
            // Check if the saved view is still a descendant of our content
            brls::View* check = m_prePanelFocusView;
            bool valid = false;
            while (check) {
                if (check == this) { valid = true; break; }
                check = check->getParent();
            }
            if (valid) {
                brls::Application::giveFocus(m_prePanelFocusView);
            } else {
                // Stale pointer - focus the content grid instead
                if (m_contentGrid) {
                    brls::Application::giveFocus(m_contentGrid);
                }
            }
            m_prePanelFocusView = nullptr;
        }
    }
}

void SearchTab::showFilterDialog() {
    if (m_sourceFilters.empty()) {
        if (!m_filtersLoaded) {
            brls::Application::notify("Loading filters...");
            loadSourceFilters();
        } else {
            brls::Application::notify("This source has no filters");
        }
        return;
    }

    // Toggle: if already showing source filters, hide and return
    if (m_filterPanelType == FilterPanelType::SOURCE_FILTER) {
        hideFilterPanel();
        return;
    }

    buildFilterPanel();
}

void SearchTab::buildFilterPanel() {
    m_loadGeneration++;  // Invalidate in-flight loads

    // Save current focus so we can restore it when closing
    m_prePanelFocusView = brls::Application::getCurrentFocus();
    m_lastHighlightedRow = nullptr;

    // Clear and repopulate the inline panel
    m_filterPanel->clearViews();
    m_filterPanelType = FilterPanelType::SOURCE_FILTER;

    m_filterPanel->setBackgroundColor(Application::getInstance().getDialogBackground());
    m_filterPanel->setCornerRadius(12);
    m_filterPanel->setPadding(10);
    m_filterPanel->setMarginLeft(10);
    m_filterPanel->setWidth(300);

    // Title
    auto* titleLabel = new brls::Label();
    titleLabel->setText("Source Filters");
    titleLabel->setFontSize(18);
    titleLabel->setSingleLine(true);
    titleLabel->setMarginBottom(6);
    m_filterPanel->addView(titleLabel);

    // Helper: create a label for a filter row showing its current state
    auto makeFilterLabel = [](const SourceFilter& f) -> std::string {
        switch (f.type) {
            case FilterType::CHECKBOX:
                return (f.checkBoxState ? "\u2713 " : "   ") + f.name;
            case FilterType::TRISTATE: {
                std::string prefix;
                switch (f.triState) {
                    case TriState::IGNORE:  prefix = "   "; break;
                    case TriState::INCLUDE: prefix = "[+] "; break;
                    case TriState::EXCLUDE: prefix = "[-] "; break;
                }
                return prefix + f.name;
            }
            case FilterType::TEXT:
                return f.name + ": " + (f.textState.empty() ? "(empty)" : f.textState);
            case FilterType::SELECT:
                if (!f.selectOptions.empty() && f.selectState >= 0 &&
                    f.selectState < static_cast<int>(f.selectOptions.size())) {
                    return f.name + ": " + f.selectOptions[f.selectState];
                }
                return f.name + ": (select)";
            case FilterType::SORT: {
                std::string label = f.name + ": ";
                if (!f.sortOptions.empty() && f.sortState.index >= 0 &&
                    f.sortState.index < static_cast<int>(f.sortOptions.size())) {
                    label += f.sortOptions[f.sortState.index];
                    label += (f.sortState.ascending ? " (Asc)" : " (Desc)");
                } else {
                    label += "(default)";
                }
                return label;
            }
            default:
                return f.name;
        }
    };

    // Main scrolling frame for the whole filter content
    auto* mainScroll = new brls::ScrollingFrame();
    mainScroll->setGrow(1.0f);

    auto* filterListBox = new brls::Box();
    filterListBox->setAxis(brls::Axis::COLUMN);

    // On first open, collapse all GROUP filters by default
    if (m_collapsedGroups.empty()) {
        for (size_t i = 0; i < m_sourceFilters.size(); i++) {
            if (m_sourceFilters[i].type == FilterType::GROUP) {
                m_collapsedGroups.insert(static_cast<int>(i));
            }
        }
    }

    // Build filter rows
    for (size_t i = 0; i < m_sourceFilters.size(); i++) {
        auto& filter = m_sourceFilters[i];

        if (filter.type == FilterType::HEADER) {
            auto* headerLabel = new brls::Label();
            headerLabel->setText("--- " + filter.name + " ---");
            headerLabel->setFontSize(14);
            headerLabel->setSingleLine(true);
            headerLabel->setMarginTop(6);
            headerLabel->setMarginBottom(3);
            headerLabel->setTextColor(Application::getInstance().getHeaderTextColor());
            filterListBox->addView(headerLabel);
            continue;
        }

        if (filter.type == FilterType::SEPARATOR) {
            auto* sepLabel = new brls::Label();
            sepLabel->setText("----------");
            sepLabel->setFontSize(12);
            sepLabel->setSingleLine(true);
            sepLabel->setMarginTop(3);
            sepLabel->setMarginBottom(3);
            sepLabel->setTextColor(Application::getInstance().getSubtitleColor());
            filterListBox->addView(sepLabel);
            continue;
        }

        if (filter.type == FilterType::GROUP) {
            bool collapsed = m_collapsedGroups.count(static_cast<int>(i)) > 0;

            // Group header (clickable to expand/collapse)
            auto* groupRow = new brls::Box();
            groupRow->setAxis(brls::Axis::ROW);
            groupRow->setFocusable(true);
            groupRow->setPadding(5, 8, 5, 8);
            groupRow->setMarginTop(3);
            groupRow->setMarginBottom(2);
            groupRow->setCornerRadius(6);
            groupRow->setAlignItems(brls::AlignItems::CENTER);
            groupRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());

            auto* arrowLabel = new brls::Label();
            arrowLabel->setFontSize(14);
            arrowLabel->setWidth(20);
            arrowLabel->setMarginRight(6);
            arrowLabel->setText(collapsed ? "\u25B6" : "\u25BC");
            groupRow->addView(arrowLabel);

            auto* groupNameLabel = new brls::Label();
            groupNameLabel->setText(filter.name + " (" + std::to_string(filter.filters.size()) + ")");
            groupNameLabel->setFontSize(14);
            groupNameLabel->setSingleLine(true);
            groupRow->addView(groupNameLabel);

            int groupIdx = static_cast<int>(i);
            // Hover highlight for group row
            groupRow->getFocusEvent()->subscribe([this, groupRow](brls::View*) {
                if (m_lastHighlightedRow && m_lastHighlightedRow != groupRow) {
                    m_lastHighlightedRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());
                }
                groupRow->setBackgroundColor(Application::getInstance().getActiveRowBackground());
                m_lastHighlightedRow = groupRow;
            });
            groupRow->addGestureRecognizer(new brls::TapGestureRecognizer(groupRow));

            filterListBox->addView(groupRow);

            // Container for child rows (plain Box, not ScrollingFrame to avoid nesting issues)
            auto* childrenBox = new brls::Box();
            childrenBox->setAxis(brls::Axis::COLUMN);
            childrenBox->setMarginLeft(12);
            childrenBox->setVisibility(collapsed ? brls::Visibility::GONE : brls::Visibility::VISIBLE);

            // Update toggle to use childrenBox directly
            groupRow->registerClickAction([this, groupIdx, childrenBox, arrowLabel, groupRow](brls::View*) {
                auto setChildrenFocusable = [](brls::Box* box, bool focusable) {
                    for (auto* child : box->getChildren()) {
                        auto* childBox = dynamic_cast<brls::Box*>(child);
                        if (childBox) {
                            childBox->setFocusable(focusable);
                            // Also handle nested option boxes (SELECT children)
                            for (auto* nested : childBox->getChildren()) {
                                auto* nestedBox = dynamic_cast<brls::Box*>(nested);
                                if (nestedBox) {
                                    nestedBox->setFocusable(focusable);
                                }
                            }
                        }
                    }
                };
                if (m_collapsedGroups.count(groupIdx)) {
                    m_collapsedGroups.erase(groupIdx);
                    childrenBox->setVisibility(brls::Visibility::VISIBLE);
                    setChildrenFocusable(childrenBox, true);
                    arrowLabel->setText("\u25BC");
                } else {
                    m_collapsedGroups.insert(groupIdx);
                    childrenBox->setVisibility(brls::Visibility::GONE);
                    setChildrenFocusable(childrenBox, false);
                    arrowLabel->setText("\u25B6");
                    // If focus was inside the collapsed group, move it to the group header
                    brls::View* focused = brls::Application::getCurrentFocus();
                    if (focused) {
                        brls::View* check = focused;
                        while (check) {
                            if (check == childrenBox) {
                                brls::Application::giveFocus(groupRow);
                                break;
                            }
                            check = check->getParent();
                        }
                    }
                }
                return true;
            });

            // Build child rows
            for (size_t j = 0; j < filter.filters.size(); j++) {
                auto& child = filter.filters[j];
                int fi = static_cast<int>(i);
                int ci = static_cast<int>(j);

                auto* childRow = new brls::Box();
                childRow->setAxis(brls::Axis::ROW);
                childRow->setFocusable(!collapsed);
                childRow->setPadding(4, 8, 4, 8);
                childRow->setMarginBottom(1);
                childRow->setCornerRadius(4);
                childRow->setAlignItems(brls::AlignItems::CENTER);
                childRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());

                // Add expand arrow for SELECT children
                brls::Label* childArrow = nullptr;
                if (child.type == FilterType::SELECT) {
                    childArrow = new brls::Label();
                    childArrow->setFontSize(12);
                    childArrow->setWidth(16);
                    childArrow->setMarginRight(4);
                    childArrow->setText("\u25B6");
                    childRow->addView(childArrow);
                }

                auto* childLabel = new brls::Label();
                childLabel->setText(makeFilterLabel(child));
                childLabel->setFontSize(13);
                childLabel->setSingleLine(true);
                childRow->addView(childLabel);

                // Hover highlight for child row
                childRow->getFocusEvent()->subscribe([this, childRow](brls::View*) {
                    if (m_lastHighlightedRow && m_lastHighlightedRow != childRow) {
                        m_lastHighlightedRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());
                    }
                    childRow->setBackgroundColor(Application::getInstance().getActiveRowBackground());
                    m_lastHighlightedRow = childRow;
                });

                // Create inline options container for SELECT children
                brls::Box* childOptionsBox = nullptr;
                if (child.type == FilterType::SELECT) {
                    childOptionsBox = new brls::Box();
                    childOptionsBox->setAxis(brls::Axis::COLUMN);
                    childOptionsBox->setMarginLeft(20);
                    childOptionsBox->setMarginBottom(1);
                    childOptionsBox->setVisibility(brls::Visibility::GONE);

                    for (int optIdx = 0; optIdx < static_cast<int>(child.selectOptions.size()); optIdx++) {
                        auto* optRow = new brls::Box();
                        optRow->setAxis(brls::Axis::ROW);
                        optRow->setFocusable(false);  // Hidden by default (GONE), set focusable when expanded
                        optRow->setPadding(3, 8, 3, 8);
                        optRow->setMarginBottom(1);
                        optRow->setCornerRadius(4);
                        optRow->setAlignItems(brls::AlignItems::CENTER);
                        optRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());

                        auto* checkMark = new brls::Label();
                        checkMark->setFontSize(12);
                        checkMark->setWidth(16);
                        checkMark->setMarginRight(4);
                        checkMark->setText(optIdx == child.selectState ? "\u2713" : " ");
                        if (optIdx == child.selectState)
                            checkMark->setTextColor(Application::getInstance().getCtaButtonColor());
                        optRow->addView(checkMark);

                        auto* optLabel = new brls::Label();
                        optLabel->setText(child.selectOptions[optIdx]);
                        optLabel->setFontSize(12);
                        optLabel->setSingleLine(true);
                        optRow->addView(optLabel);

                        optRow->registerClickAction([this, fi, ci, optIdx, childLabel, makeFilterLabel, childOptionsBox, childArrow, childRow](brls::View*) {
                            if (fi >= static_cast<int>(m_sourceFilters.size())) return true;
                            auto& parent = m_sourceFilters[fi];
                            if (ci >= static_cast<int>(parent.filters.size())) return true;
                            parent.filters[ci].selectState = optIdx;
                            childLabel->setText(makeFilterLabel(parent.filters[ci]));
                            // Update checkmarks
                            for (size_t k = 0; k < childOptionsBox->getChildren().size(); k++) {
                                auto* oc = dynamic_cast<brls::Box*>(childOptionsBox->getChildren()[k]);
                                if (oc && !oc->getChildren().empty()) {
                                    auto* cm = dynamic_cast<brls::Label*>(oc->getChildren()[0]);
                                    if (cm) {
                                        cm->setText(static_cast<int>(k) == optIdx ? "\u2713" : " ");
                                        if (static_cast<int>(k) == optIdx)
                                            cm->setTextColor(Application::getInstance().getCtaButtonColor());
                                    }
                                }
                            }
                            childOptionsBox->setVisibility(brls::Visibility::GONE);
                            for (auto* opt : childOptionsBox->getChildren()) {
                                auto* optBox = dynamic_cast<brls::Box*>(opt);
                                if (optBox) optBox->setFocusable(false);
                            }
                            if (childArrow) childArrow->setText("\u25B6");
                            // Move focus back to parent row since options are now hidden
                            brls::Application::giveFocus(childRow);
                            return true;
                        });
                        optRow->addGestureRecognizer(new brls::TapGestureRecognizer(optRow));

                        optRow->getFocusEvent()->subscribe([this, optRow](brls::View*) {
                            if (m_lastHighlightedRow && m_lastHighlightedRow != optRow) {
                                m_lastHighlightedRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());
                            }
                            optRow->setBackgroundColor(Application::getInstance().getActiveRowBackground());
                            m_lastHighlightedRow = optRow;
                        });

                        childOptionsBox->addView(optRow);
                    }
                }

                childRow->registerClickAction([this, fi, ci, childLabel, makeFilterLabel, childOptionsBox, childArrow](brls::View*) {
                    if (fi >= static_cast<int>(m_sourceFilters.size())) return true;
                    auto& parent = m_sourceFilters[fi];
                    if (ci >= static_cast<int>(parent.filters.size())) return true;
                    auto& child = parent.filters[ci];

                    switch (child.type) {
                        case FilterType::TRISTATE:
                            switch (child.triState) {
                                case TriState::IGNORE:  child.triState = TriState::INCLUDE; break;
                                case TriState::INCLUDE: child.triState = TriState::EXCLUDE; break;
                                case TriState::EXCLUDE: child.triState = TriState::IGNORE; break;
                            }
                            childLabel->setText(makeFilterLabel(child));
                            break;
                        case FilterType::CHECKBOX:
                            child.checkBoxState = !child.checkBoxState;
                            childLabel->setText(makeFilterLabel(child));
                            break;
                        case FilterType::TEXT:
                            brls::Application::getImeManager()->openForText(
                                [this, fi, ci, childLabel, makeFilterLabel](std::string text) {
                                    if (fi < static_cast<int>(m_sourceFilters.size()) &&
                                        ci < static_cast<int>(m_sourceFilters[fi].filters.size())) {
                                        m_sourceFilters[fi].filters[ci].textState = text;
                                        childLabel->setText(makeFilterLabel(m_sourceFilters[fi].filters[ci]));
                                    }
                                }, child.name, "", 256, child.textState);
                            break;
                        case FilterType::SELECT:
                            // Toggle inline options visibility and focusability
                            if (childOptionsBox) {
                                bool expanding = childOptionsBox->getVisibility() == brls::Visibility::GONE;
                                childOptionsBox->setVisibility(expanding ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
                                for (auto* opt : childOptionsBox->getChildren()) {
                                    auto* optBox = dynamic_cast<brls::Box*>(opt);
                                    if (optBox) optBox->setFocusable(expanding);
                                }
                                if (childArrow) childArrow->setText(expanding ? "\u25BC" : "\u25B6");
                            }
                            break;
                        default: break;
                    }
                    return true;
                });
                childRow->addGestureRecognizer(new brls::TapGestureRecognizer(childRow));

                childrenBox->addView(childRow);
                if (childOptionsBox) {
                    childrenBox->addView(childOptionsBox);
                }
            }

            filterListBox->addView(childrenBox);
            continue;
        }

        // Top-level non-group filter
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setFocusable(true);
        row->setPadding(5, 8, 5, 8);
        row->setMarginBottom(2);
        row->setCornerRadius(6);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setBackgroundColor(Application::getInstance().getInactiveRowBackground());

        // Add expand arrow for SELECT and SORT types
        brls::Label* arrowLabel = nullptr;
        if (filter.type == FilterType::SELECT || filter.type == FilterType::SORT) {
            arrowLabel = new brls::Label();
            arrowLabel->setFontSize(13);
            arrowLabel->setWidth(18);
            arrowLabel->setMarginRight(4);
            arrowLabel->setText("\u25B6");
            row->addView(arrowLabel);
        }

        auto* rowLabel = new brls::Label();
        rowLabel->setText(makeFilterLabel(filter));
        rowLabel->setFontSize(13);
        rowLabel->setSingleLine(true);
        row->addView(rowLabel);

        // Hover highlight for top-level row
        row->getFocusEvent()->subscribe([this, row](brls::View*) {
            if (m_lastHighlightedRow && m_lastHighlightedRow != row) {
                m_lastHighlightedRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());
            }
            row->setBackgroundColor(Application::getInstance().getActiveRowBackground());
            m_lastHighlightedRow = row;
        });

        // Create inline options container for SELECT and SORT types
        brls::Box* optionsBox = nullptr;
        if (filter.type == FilterType::SELECT || filter.type == FilterType::SORT) {
            optionsBox = new brls::Box();
            optionsBox->setAxis(brls::Axis::COLUMN);
            optionsBox->setMarginLeft(16);
            optionsBox->setMarginBottom(2);
            optionsBox->setVisibility(brls::Visibility::GONE);

            if (filter.type == FilterType::SELECT) {
                for (int optIdx = 0; optIdx < static_cast<int>(filter.selectOptions.size()); optIdx++) {
                    auto* optRow = new brls::Box();
                    optRow->setAxis(brls::Axis::ROW);
                    optRow->setFocusable(false);  // Hidden by default (GONE), set focusable when expanded
                    optRow->setPadding(4, 8, 4, 8);
                    optRow->setMarginBottom(1);
                    optRow->setCornerRadius(4);
                    optRow->setAlignItems(brls::AlignItems::CENTER);
                    optRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());

                    auto* checkMark = new brls::Label();
                    checkMark->setFontSize(12);
                    checkMark->setWidth(18);
                    checkMark->setMarginRight(4);
                    checkMark->setText(optIdx == filter.selectState ? "\u2713" : " ");
                    if (optIdx == filter.selectState)
                        checkMark->setTextColor(Application::getInstance().getCtaButtonColor());
                    optRow->addView(checkMark);

                    auto* optLabel = new brls::Label();
                    optLabel->setText(filter.selectOptions[optIdx]);
                    optLabel->setFontSize(12);
                    optLabel->setSingleLine(true);
                    optRow->addView(optLabel);

                    int filterIdx2 = static_cast<int>(i);
                    optRow->registerClickAction([this, filterIdx2, optIdx, rowLabel, makeFilterLabel, optionsBox, arrowLabel, row](brls::View*) {
                        if (filterIdx2 >= static_cast<int>(m_sourceFilters.size())) return true;
                        m_sourceFilters[filterIdx2].selectState = optIdx;
                        rowLabel->setText(makeFilterLabel(m_sourceFilters[filterIdx2]));
                        // Update checkmarks on all option rows
                        for (size_t k = 0; k < optionsBox->getChildren().size(); k++) {
                            auto* optChild = dynamic_cast<brls::Box*>(optionsBox->getChildren()[k]);
                            if (optChild && !optChild->getChildren().empty()) {
                                auto* cm = dynamic_cast<brls::Label*>(optChild->getChildren()[0]);
                                if (cm) {
                                    cm->setText(static_cast<int>(k) == optIdx ? "\u2713" : " ");
                                    if (static_cast<int>(k) == optIdx)
                                        cm->setTextColor(Application::getInstance().getCtaButtonColor());
                                }
                            }
                        }
                        // Collapse after selection
                        optionsBox->setVisibility(brls::Visibility::GONE);
                        for (auto* opt : optionsBox->getChildren()) {
                            auto* ob = dynamic_cast<brls::Box*>(opt);
                            if (ob) ob->setFocusable(false);
                        }
                        if (arrowLabel) arrowLabel->setText("\u25B6");
                        // Move focus back to parent row since options are now hidden
                        brls::Application::giveFocus(row);
                        return true;
                    });
                    optRow->addGestureRecognizer(new brls::TapGestureRecognizer(optRow));

                    // Hover highlight
                    optRow->getFocusEvent()->subscribe([this, optRow](brls::View*) {
                        if (m_lastHighlightedRow && m_lastHighlightedRow != optRow) {
                            m_lastHighlightedRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());
                        }
                        optRow->setBackgroundColor(Application::getInstance().getActiveRowBackground());
                        m_lastHighlightedRow = optRow;
                    });

                    optionsBox->addView(optRow);
                }
            } else if (filter.type == FilterType::SORT) {
                for (int sIdx = 0; sIdx < static_cast<int>(filter.sortOptions.size()); sIdx++) {
                    for (int dir = 0; dir < 2; dir++) {
                        bool isAsc = (dir == 0);
                        std::string optText = filter.sortOptions[sIdx] + (isAsc ? " (Ascending)" : " (Descending)");
                        bool isCurrent = (filter.sortState.index == sIdx && filter.sortState.ascending == isAsc);

                        auto* optRow = new brls::Box();
                        optRow->setAxis(brls::Axis::ROW);
                        optRow->setFocusable(false);  // Hidden by default (GONE), set focusable when expanded
                        optRow->setPadding(4, 8, 4, 8);
                        optRow->setMarginBottom(1);
                        optRow->setCornerRadius(4);
                        optRow->setAlignItems(brls::AlignItems::CENTER);
                        optRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());

                        auto* checkMark = new brls::Label();
                        checkMark->setFontSize(12);
                        checkMark->setWidth(18);
                        checkMark->setMarginRight(4);
                        checkMark->setText(isCurrent ? "\u2713" : " ");
                        if (isCurrent)
                            checkMark->setTextColor(Application::getInstance().getCtaButtonColor());
                        optRow->addView(checkMark);

                        auto* optLabel = new brls::Label();
                        optLabel->setText(optText);
                        optLabel->setFontSize(12);
                        optLabel->setSingleLine(true);
                        optRow->addView(optLabel);

                        int filterIdx2 = static_cast<int>(i);
                        optRow->registerClickAction([this, filterIdx2, sIdx, isAsc, rowLabel, makeFilterLabel, optionsBox, arrowLabel, row](brls::View*) {
                            if (filterIdx2 >= static_cast<int>(m_sourceFilters.size())) return true;
                            m_sourceFilters[filterIdx2].sortState.index = sIdx;
                            m_sourceFilters[filterIdx2].sortState.ascending = isAsc;
                            rowLabel->setText(makeFilterLabel(m_sourceFilters[filterIdx2]));
                            // Update checkmarks on all sort option rows
                            int optionIdx = 0;
                            for (size_t k = 0; k < optionsBox->getChildren().size(); k++) {
                                auto* optChild = dynamic_cast<brls::Box*>(optionsBox->getChildren()[k]);
                                if (optChild && !optChild->getChildren().empty()) {
                                    auto* cm = dynamic_cast<brls::Label*>(optChild->getChildren()[0]);
                                    if (cm) {
                                        int si = optionIdx / 2;
                                        bool ai = (optionIdx % 2 == 0);
                                        bool selected = (si == sIdx && ai == isAsc);
                                        cm->setText(selected ? "\u2713" : " ");
                                        if (selected)
                                            cm->setTextColor(Application::getInstance().getCtaButtonColor());
                                    }
                                }
                                optionIdx++;
                            }
                            // Collapse after selection
                            optionsBox->setVisibility(brls::Visibility::GONE);
                            for (auto* opt : optionsBox->getChildren()) {
                                auto* ob = dynamic_cast<brls::Box*>(opt);
                                if (ob) ob->setFocusable(false);
                            }
                            if (arrowLabel) arrowLabel->setText("\u25B6");
                            // Move focus back to parent row since options are now hidden
                            brls::Application::giveFocus(row);
                            return true;
                        });
                        optRow->addGestureRecognizer(new brls::TapGestureRecognizer(optRow));

                        // Hover highlight
                        optRow->getFocusEvent()->subscribe([this, optRow](brls::View*) {
                            if (m_lastHighlightedRow && m_lastHighlightedRow != optRow) {
                                m_lastHighlightedRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());
                            }
                            optRow->setBackgroundColor(Application::getInstance().getActiveRowBackground());
                            m_lastHighlightedRow = optRow;
                        });

                        optionsBox->addView(optRow);
                    }
                }
            }
        }

        int filterIdx = static_cast<int>(i);
        row->registerClickAction([this, filterIdx, rowLabel, makeFilterLabel, optionsBox, arrowLabel](brls::View*) {
            if (filterIdx >= static_cast<int>(m_sourceFilters.size())) return true;
            auto& filter = m_sourceFilters[filterIdx];

            switch (filter.type) {
                case FilterType::CHECKBOX:
                    filter.checkBoxState = !filter.checkBoxState;
                    rowLabel->setText(makeFilterLabel(filter));
                    break;
                case FilterType::TRISTATE:
                    switch (filter.triState) {
                        case TriState::IGNORE:  filter.triState = TriState::INCLUDE; break;
                        case TriState::INCLUDE: filter.triState = TriState::EXCLUDE; break;
                        case TriState::EXCLUDE: filter.triState = TriState::IGNORE; break;
                    }
                    rowLabel->setText(makeFilterLabel(filter));
                    break;
                case FilterType::TEXT:
                    brls::Application::getImeManager()->openForText(
                        [this, filterIdx, rowLabel, makeFilterLabel](std::string text) {
                            if (filterIdx < static_cast<int>(m_sourceFilters.size())) {
                                m_sourceFilters[filterIdx].textState = text;
                                rowLabel->setText(makeFilterLabel(m_sourceFilters[filterIdx]));
                            }
                        }, filter.name, "", 256, filter.textState);
                    break;
                case FilterType::SELECT:
                case FilterType::SORT:
                    // Toggle inline options visibility and focusability
                    if (optionsBox) {
                        bool expanding = optionsBox->getVisibility() == brls::Visibility::GONE;
                        optionsBox->setVisibility(expanding ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
                        for (auto* opt : optionsBox->getChildren()) {
                            auto* optBox = dynamic_cast<brls::Box*>(opt);
                            if (optBox) optBox->setFocusable(expanding);
                        }
                        if (arrowLabel) arrowLabel->setText(expanding ? "\u25BC" : "\u25B6");
                    }
                    break;
                default: break;
            }
            return true;
        });
        row->addGestureRecognizer(new brls::TapGestureRecognizer(row));

        filterListBox->addView(row);
        if (optionsBox) {
            filterListBox->addView(optionsBox);
        }
    }

    mainScroll->setContentView(filterListBox);
    // B button (circle) on any filter row closes the panel
    filterListBox->registerAction("Close", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        hideFilterPanel();
        return true;
    }, true);
    m_filterPanel->addView(mainScroll);

    // Button row
    auto* buttonRow = new brls::Box();
    buttonRow->setAxis(brls::Axis::ROW);
    buttonRow->setJustifyContent(brls::JustifyContent::FLEX_START);
    buttonRow->setMarginTop(6);

    auto* applyBtn = new brls::Button();
    applyBtn->setText("Apply");
    applyBtn->setMarginRight(8);
    applyBtn->registerClickAction([this](brls::View*) {
        hideFilterPanel();
        applyFilters();
        return true;
    });
    applyBtn->addGestureRecognizer(new brls::TapGestureRecognizer(applyBtn));

    auto* resetBtn = new brls::Button();
    resetBtn->setText("Reset");
    resetBtn->registerClickAction([this](brls::View*) {
        hideFilterPanel();
        resetFilters();
        m_tagFilterBtn->setBackgroundColor(Application::getInstance().getButtonColor());
        brls::Application::notify("Filters reset");
        loadPopularManga(m_currentSourceId);
        return true;
    });
    resetBtn->addGestureRecognizer(new brls::TapGestureRecognizer(resetBtn));

    buttonRow->addView(applyBtn);
    buttonRow->addView(resetBtn);
    m_filterPanel->addView(buttonRow);

    // B button on panel buttons to close panel
    applyBtn->registerAction("Close", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        hideFilterPanel();
        return true;
    }, true);
    resetBtn->registerAction("Close", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        hideFilterPanel();
        return true;
    }, true);

    m_filterPanel->setVisibility(brls::Visibility::VISIBLE);

    // Focus first focusable item
    brls::Application::giveFocus(filterListBox);
}

void SearchTab::collectAllTags(std::set<std::string>& allTags) {
    const auto& settings = Application::getInstance().getSettings();
    for (const auto& [sourceId, tags] : settings.sourceTags) {
        for (const auto& tag : tags) {
            allTags.insert(tag);
        }
    }
}

void SearchTab::showTagFilterDialog() {
    std::set<std::string> allTags;
    collectAllTags(allTags);

    if (allTags.empty()) {
        brls::Application::notify("No tags yet - long press a source to add tags");
        return;
    }

    // Toggle: if already showing tag filter, hide and return
    if (m_filterPanelType == FilterPanelType::TAG_FILTER) {
        hideFilterPanel();
        return;
    }

    buildTagFilterPanel();
}

void SearchTab::buildTagFilterPanel() {
    // Save current focus so we can restore it when closing
    m_prePanelFocusView = brls::Application::getCurrentFocus();
    m_lastHighlightedRow = nullptr;

    m_filterPanel->clearViews();
    m_filterPanelType = FilterPanelType::TAG_FILTER;

    m_filterPanel->setBackgroundColor(Application::getInstance().getDialogBackground());
    m_filterPanel->setCornerRadius(12);
    m_filterPanel->setPadding(10);
    m_filterPanel->setMarginLeft(10);
    m_filterPanel->setWidth(300);

    auto& settings = Application::getInstance().getSettings();
    int activeCount = static_cast<int>(settings.selectedSourceTagFilters.size());
    std::set<std::string> allTags;
    collectAllTags(allTags);
    std::vector<std::string> tagList(allTags.begin(), allTags.end());

    auto checkLabels = std::make_shared<std::vector<brls::Label*>>();
    auto rows = std::make_shared<std::vector<brls::Box*>>();

    // Title
    auto* titleLabel = new brls::Label();
    std::string title = "Filter by Tag";
    if (activeCount > 0) {
        title += " (" + std::to_string(activeCount) + " active)";
    }
    titleLabel->setText(title);
    titleLabel->setFontSize(18);
    titleLabel->setSingleLine(true);
    titleLabel->setMarginBottom(6);
    m_filterPanel->addView(titleLabel);

    // Scrollable tag list
    auto* scrollView = new brls::ScrollingFrame();
    scrollView->setGrow(1.0f);

    auto* tagListBox = new brls::Box();
    tagListBox->setAxis(brls::Axis::COLUMN);

    for (size_t i = 0; i < tagList.size(); i++) {
        const auto& tag = tagList[i];
        bool active = settings.selectedSourceTagFilters.count(tag) > 0;

        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setFocusable(true);
        row->setPadding(5, 8, 5, 8);
        row->setMarginBottom(2);
        row->setCornerRadius(6);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setBackgroundColor(active ? Application::getInstance().getActiveRowBackground() : Application::getInstance().getInactiveRowBackground());

        auto* checkLabel = new brls::Label();
        checkLabel->setFontSize(13);
        checkLabel->setWidth(20);
        checkLabel->setMarginRight(6);
        if (active) {
            checkLabel->setText("\u2713");
            checkLabel->setTextColor(Application::getInstance().getCtaButtonColor());
        } else {
            checkLabel->setText("");
        }
        row->addView(checkLabel);
        checkLabels->push_back(checkLabel);

        auto* nameLabel = new brls::Label();
        nameLabel->setText(tag);
        nameLabel->setFontSize(13);
        nameLabel->setSingleLine(true);
        row->addView(nameLabel);

        rows->push_back(row);

        // Hover highlight for tag row
        row->getFocusEvent()->subscribe([this, row](brls::View*) {
            if (m_lastHighlightedRow && m_lastHighlightedRow != row) {
                m_lastHighlightedRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());
            }
            row->setBackgroundColor(Application::getInstance().getActiveRowBackground());
            m_lastHighlightedRow = row;
        });

        row->registerClickAction([this, tag, checkLabels, rows, i, titleLabel](brls::View*) {
            auto& s = Application::getInstance().getSettings();
            if (s.selectedSourceTagFilters.count(tag) > 0) {
                s.selectedSourceTagFilters.erase(tag);
                (*checkLabels)[i]->setText("");
            } else {
                s.selectedSourceTagFilters.insert(tag);
                (*checkLabels)[i]->setText("\u2713");
                (*checkLabels)[i]->setTextColor(Application::getInstance().getCtaButtonColor());
            }
            int count = static_cast<int>(s.selectedSourceTagFilters.size());
            std::string t = "Filter by Tag";
            if (count > 0) t += " (" + std::to_string(count) + " active)";
            titleLabel->setText(t);
            return true;
        });
        row->addGestureRecognizer(new brls::TapGestureRecognizer(row));

        tagListBox->addView(row);
    }

    scrollView->setContentView(tagListBox);
    // B button (circle) on any tag filter row closes the panel
    tagListBox->registerAction("Close", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        hideFilterPanel();
        return true;
    }, true);
    m_filterPanel->addView(scrollView);

    // Button row
    auto* buttonRow = new brls::Box();
    buttonRow->setAxis(brls::Axis::ROW);
    buttonRow->setJustifyContent(brls::JustifyContent::FLEX_START);
    buttonRow->setMarginTop(6);

    auto* applyBtn = new brls::Button();
    applyBtn->setText("Apply");
    applyBtn->setMarginRight(8);
    applyBtn->registerClickAction([this](brls::View*) {
        Application::getInstance().saveSettings();
        hideFilterPanel();
        showSources();
        return true;
    });
    applyBtn->addGestureRecognizer(new brls::TapGestureRecognizer(applyBtn));

    auto* clearBtn = new brls::Button();
    clearBtn->setText("Clear");
    clearBtn->registerClickAction([this, checkLabels, rows, titleLabel](brls::View*) {
        auto& s = Application::getInstance().getSettings();
        s.selectedSourceTagFilters.clear();
        for (size_t i = 0; i < checkLabels->size(); i++) {
            (*checkLabels)[i]->setText("");
            (*rows)[i]->setBackgroundColor(Application::getInstance().getInactiveRowBackground());
        }
        titleLabel->setText("Filter by Tag");
        brls::Application::notify("Tag filters cleared");
        return true;
    });
    clearBtn->addGestureRecognizer(new brls::TapGestureRecognizer(clearBtn));

    // B button on panel buttons to close panel
    applyBtn->registerAction("Close", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        hideFilterPanel();
        return true;
    }, true);
    clearBtn->registerAction("Close", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        hideFilterPanel();
        return true;
    }, true);

    buttonRow->addView(applyBtn);
    buttonRow->addView(clearBtn);
    m_filterPanel->addView(buttonRow);

    m_filterPanel->setVisibility(brls::Visibility::VISIBLE);
    brls::Application::giveFocus(tagListBox);
}

void SearchTab::showTagManageDialog(const Source& source) {
    // Toggle: if already showing tag manage, hide and return
    if (m_filterPanelType == FilterPanelType::TAG_MANAGE) {
        hideFilterPanel();
        return;
    }

    buildTagManagePanel(source);
}

void SearchTab::buildTagManagePanel(const Source& source) {
    // Save current focus so we can restore it when closing
    if (m_filterPanelType == FilterPanelType::NONE) {
        m_prePanelFocusView = brls::Application::getCurrentFocus();
    }
    m_lastHighlightedRow = nullptr;

    m_filterPanel->clearViews();
    m_filterPanelType = FilterPanelType::TAG_MANAGE;

    m_filterPanel->setBackgroundColor(Application::getInstance().getDialogBackground());
    m_filterPanel->setCornerRadius(12);
    m_filterPanel->setPadding(10);
    m_filterPanel->setMarginLeft(10);
    m_filterPanel->setWidth(300);

    auto& settings = Application::getInstance().getSettings();
    std::string sourceIdStr = std::to_string(source.id);

    std::set<std::string> currentTags;
    auto it = settings.sourceTags.find(sourceIdStr);
    if (it != settings.sourceTags.end()) {
        currentTags = it->second;
    }

    std::set<std::string> allTags;
    collectAllTags(allTags);
    std::vector<std::string> tagList(allTags.begin(), allTags.end());

    auto checkLabels = std::make_shared<std::vector<brls::Label*>>();
    auto rowPtrs = std::make_shared<std::vector<brls::Box*>>();

    // Title
    auto* titleLabel = new brls::Label();
    titleLabel->setText("Tags: " + source.name);
    titleLabel->setFontSize(18);
    titleLabel->setSingleLine(true);
    titleLabel->setMarginBottom(6);
    m_filterPanel->addView(titleLabel);

    // Scrollable tag list
    auto* scrollView = new brls::ScrollingFrame();
    scrollView->setGrow(1.0f);

    auto* tagListBox = new brls::Box();
    tagListBox->setAxis(brls::Axis::COLUMN);

    for (size_t i = 0; i < tagList.size(); i++) {
        const auto& tag = tagList[i];
        bool has = currentTags.count(tag) > 0;

        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setFocusable(true);
        row->setPadding(5, 8, 5, 8);
        row->setMarginBottom(2);
        row->setCornerRadius(6);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setBackgroundColor(has ? Application::getInstance().getActiveRowBackground() : Application::getInstance().getInactiveRowBackground());

        auto* checkLabel = new brls::Label();
        checkLabel->setFontSize(13);
        checkLabel->setWidth(20);
        checkLabel->setMarginRight(6);
        if (has) {
            checkLabel->setText("\u2713");
            checkLabel->setTextColor(Application::getInstance().getCtaButtonColor());
        } else {
            checkLabel->setText("");
        }
        row->addView(checkLabel);
        checkLabels->push_back(checkLabel);

        auto* nameLabel = new brls::Label();
        nameLabel->setText(tag);
        nameLabel->setFontSize(13);
        nameLabel->setSingleLine(true);
        row->addView(nameLabel);

        rowPtrs->push_back(row);

        // Hover highlight for tag manage row
        row->getFocusEvent()->subscribe([this, row](brls::View*) {
            if (m_lastHighlightedRow && m_lastHighlightedRow != row) {
                m_lastHighlightedRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());
            }
            row->setBackgroundColor(Application::getInstance().getActiveRowBackground());
            m_lastHighlightedRow = row;
        });

        row->registerClickAction([this, sourceIdStr, tag, checkLabels, rowPtrs, i](brls::View*) {
            auto& s = Application::getInstance().getSettings();
            auto& sourceTags = s.sourceTags[sourceIdStr];
            if (sourceTags.count(tag) > 0) {
                sourceTags.erase(tag);
                if (sourceTags.empty()) s.sourceTags.erase(sourceIdStr);
                (*checkLabels)[i]->setText("");
            } else {
                sourceTags.insert(tag);
                (*checkLabels)[i]->setText("\u2713");
                (*checkLabels)[i]->setTextColor(Application::getInstance().getCtaButtonColor());
            }
            Application::getInstance().saveSettings();
            return true;
        });
        row->addGestureRecognizer(new brls::TapGestureRecognizer(row));

        tagListBox->addView(row);
    }

    scrollView->setContentView(tagListBox);
    // B button (circle) on any tag manage row closes the panel
    tagListBox->registerAction("Close", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        hideFilterPanel();
        return true;
    }, true);
    m_filterPanel->addView(scrollView);

    // Button row
    auto* buttonRow = new brls::Box();
    buttonRow->setAxis(brls::Axis::ROW);
    buttonRow->setJustifyContent(brls::JustifyContent::FLEX_START);
    buttonRow->setMarginTop(6);

    auto* addBtn = new brls::Button();
    addBtn->setText("+ Add");
    addBtn->setMarginRight(8);
    addBtn->registerClickAction([this, sourceIdStr, source](brls::View*) {
        brls::Application::getImeManager()->openForText([this, sourceIdStr, source](std::string text) {
            if (text.empty()) return;
            auto& s = Application::getInstance().getSettings();
            s.sourceTags[sourceIdStr].insert(text);
            Application::getInstance().saveSettings();
            brls::Application::notify("Tag '" + text + "' added");
            // Rebuild the panel to include new tag
            buildTagManagePanel(source);
        }, "New Tag", "Enter tag name", 32, "");
        return true;
    });
    addBtn->addGestureRecognizer(new brls::TapGestureRecognizer(addBtn));

    auto* doneBtn = new brls::Button();
    doneBtn->setText("Done");
    doneBtn->registerClickAction([this](brls::View*) {
        hideFilterPanel();
        showSources();
        return true;
    });
    doneBtn->addGestureRecognizer(new brls::TapGestureRecognizer(doneBtn));

    // B button on panel buttons to close panel
    addBtn->registerAction("Close", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        hideFilterPanel();
        return true;
    }, true);
    doneBtn->registerAction("Close", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        hideFilterPanel();
        return true;
    }, true);

    buttonRow->addView(addBtn);
    buttonRow->addView(doneBtn);
    m_filterPanel->addView(buttonRow);

    m_filterPanel->setVisibility(brls::Visibility::VISIBLE);
    brls::Application::giveFocus(tagListBox);
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
    // Remember which mode (Popular/Latest) the user was in before searching
    m_previousBrowseMode = m_browseMode;
    m_browseMode = BrowseMode::SEARCH_RESULTS;
    m_currentPage = 1;

    // Hide Popular/Latest buttons during source-specific search
    m_popularBtn->setVisibility(brls::Visibility::GONE);
    m_latestBtn->setVisibility(brls::Visibility::GONE);

    // Hide search and search history buttons
    m_buttonContainer->setVisibility(brls::Visibility::GONE);
    m_historyBtn->setFocusable(false);
    m_globalSearchBtn->setFocusable(false);

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
        m_mainContent->addView(m_searchResultsScrollView);
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
    auto filtersActive = m_filtersActive;
    auto filters = m_sourceFilters;

    asyncRun([this, firstNewItemIndex, gen, aliveWeak = std::weak_ptr<bool>(m_alive),
              browseMode, sourceId, page, query, filtersActive, filters]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;
        bool hasNextPage = false;

        bool success = false;
        if (filtersActive) {
            success = client.searchMangaWithFilters(sourceId, "", page, filters, manga, hasNextPage);
        } else if (browseMode == BrowseMode::POPULAR) {
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

        // Highlight the active mode button using same colors as category buttons
        NVGcolor activeColor = Application::getInstance().getActiveRowBackground();
        NVGcolor inactiveColor = Application::getInstance().getInactiveRowBackground();

        m_popularBtn->setBackgroundColor(m_browseMode == BrowseMode::POPULAR ? activeColor : inactiveColor);
        m_latestBtn->setBackgroundColor(m_browseMode == BrowseMode::LATEST ? activeColor : inactiveColor);
    }
}

void SearchTab::handleBackNavigation() {
    if (m_isNavigatingBack) return;  // Prevent double back-press
    if (m_browseMode == BrowseMode::SOURCES) return;  // Already on sources, nothing to do
    m_isNavigatingBack = true;
    m_loadGeneration++;  // Invalidate any in-flight async callbacks
    hideFilterPanel();  // Close any open inline filter panel
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
            // Source-specific search: go back to the mode the user was in before searching
            // Find the current source and show its browser again
            for (const auto& source : m_sources) {
                if (source.id == m_currentSourceId) {
                    m_browseMode = m_previousBrowseMode;
                    m_titleLabel->setText(source.name);
                    m_searchLabel->setVisibility(brls::Visibility::GONE);

                    // Update buttons for source browsing mode
                    m_popularBtn->setVisibility(brls::Visibility::VISIBLE);
                    m_latestBtn->setVisibility(source.supportsLatest ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
                    m_backBtn->setVisibility(brls::Visibility::VISIBLE);
                    // Restore filter icon on header button
                    if (m_tagFilterIcon) m_tagFilterIcon->setImageFromFile("app0:resources/icons/tag.png");

                    // Restore search/history buttons and filter button for source browsing
                    m_buttonContainer->setVisibility(brls::Visibility::VISIBLE);
                    m_filterBtnContainer->setVisibility(brls::Visibility::VISIBLE);
                    m_historyBtn->setFocusable(true);
                    m_globalSearchBtn->setFocusable(true);

                    // CRITICAL: Move focus before clearing search result views
                    brls::Application::giveFocus(m_browseMode == BrowseMode::LATEST ? m_latestBtn : m_popularBtn);

                    // Hide search results, show content grid
                    if (m_searchResultsScrollView) {
                        m_searchResultsScrollView->setVisibility(brls::Visibility::GONE);
                    }
                    // Clear search results (safe now - focus moved above)
                    if (m_searchResultsBox) {
                        m_searchResultsBox->clearViews();
                    }
                    m_contentGrid->setVisibility(brls::Visibility::VISIBLE);

                    // Reload the mode the user was previously browsing
                    updateModeButtons();
                    if (m_browseMode == BrowseMode::LATEST) {
                        loadLatestManga(m_currentSourceId);
                    } else {
                        loadPopularManga(m_currentSourceId);
                    }
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
        m_sourceFilters.clear();
        m_filtersLoaded = false;
        m_filtersActive = false;
        showSources();
        m_isNavigatingBack = false;
    }
}

} // namespace vitasuwayomi
