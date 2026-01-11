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

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Browse");
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(20);
    this->addView(m_titleLabel);

    // Search input label (acts as button to open keyboard)
    m_searchLabel = new brls::Label();
    m_searchLabel->setText("Tap to search...");
    m_searchLabel->setFontSize(20);
    m_searchLabel->setMarginBottom(10);
    m_searchLabel->setFocusable(true);

    m_searchLabel->registerClickAction([this](brls::View* view) {
        brls::Application::getImeManager()->openForText([this](std::string text) {
            m_searchQuery = text;
            m_searchLabel->setText(std::string("Search: ") + text);
            performSearch(text);
        }, "Search", "Enter manga title", 256, m_searchQuery);
        return true;
    });
    m_searchLabel->addGestureRecognizer(new brls::TapGestureRecognizer(m_searchLabel));
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
    m_contentGrid->setOnMangaSelected([this](const Manga& manga) {
        onMangaSelected(manga);
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

void SearchTab::showSources() {
    m_browseMode = BrowseMode::SOURCES;
    m_titleLabel->setText("Browse - Sources");
    m_resultsLabel->setText(std::to_string(m_sources.size()) + " sources available");

    // Hide source-specific buttons
    m_popularBtn->setVisibility(brls::Visibility::GONE);
    m_latestBtn->setVisibility(brls::Visibility::GONE);
    m_backBtn->setVisibility(brls::Visibility::GONE);
    m_sourcesBtn->setVisibility(brls::Visibility::VISIBLE);

    // Create source list - we need to display sources as a list
    // For now, convert to Manga items to use the grid
    // In a real app, you'd have a source list view
    m_mangaList.clear();
    m_contentGrid->clearViews();

    // Add source items as buttons in the source list box
    // For simplicity, we'll create a simple list
    if (!m_sourceListBox) {
        m_sourceListBox = new brls::Box();
        m_sourceListBox->setAxis(brls::Axis::COLUMN);
        m_sourceListBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    }
    m_sourceListBox->clearViews();

    for (const auto& source : m_sources) {
        auto* sourceBtn = new brls::Button();
        std::string label = source.name;
        if (!source.lang.empty()) {
            label += " (" + source.lang + ")";
        }
        sourceBtn->setText(label);
        sourceBtn->setMarginBottom(5);

        Source sourceCopy = source;
        sourceBtn->registerClickAction([this, sourceCopy](brls::View* view) {
            onSourceSelected(sourceCopy);
            return true;
        });

        m_sourceListBox->addView(sourceBtn);
    }

    // Replace grid content with source list
    // For now just clear the grid since we're showing sources as buttons
    m_contentGrid->setMangaDataSource(m_mangaList);  // Empty list
}

void SearchTab::showSourceBrowser(const Source& source) {
    m_currentSourceId = source.id;
    m_currentSourceName = source.name;

    m_titleLabel->setText("Browse - " + source.name);

    // Show source-specific buttons
    m_sourcesBtn->setVisibility(brls::Visibility::GONE);
    m_popularBtn->setVisibility(brls::Visibility::VISIBLE);
    m_latestBtn->setVisibility(source.supportsLatest ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    m_backBtn->setVisibility(brls::Visibility::VISIBLE);

    // Load popular manga by default
    m_browseMode = BrowseMode::POPULAR;
    loadPopularManga(source.id);
    updateModeButtons();
}

void SearchTab::loadPopularManga(int64_t sourceId) {
    brls::Logger::debug("SearchTab: Loading popular manga from source {}", sourceId);
    m_resultsLabel->setText("Loading popular manga...");
    m_currentPage = 1;

    asyncRun([this, sourceId]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;
        bool hasNextPage = false;

        if (client.fetchPopularManga(sourceId, 1, manga, hasNextPage)) {
            brls::Logger::info("SearchTab: Got {} popular manga", manga.size());

            brls::sync([this, manga, hasNextPage]() {
                m_mangaList = manga;
                m_hasNextPage = hasNextPage;
                m_contentGrid->setMangaDataSource(m_mangaList);
                m_resultsLabel->setText(std::to_string(manga.size()) + " manga found");
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

    asyncRun([this, sourceId]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> manga;
        bool hasNextPage = false;

        if (client.fetchLatestManga(sourceId, 1, manga, hasNextPage)) {
            brls::Logger::info("SearchTab: Got {} latest manga", manga.size());

            brls::sync([this, manga, hasNextPage]() {
                m_mangaList = manga;
                m_hasNextPage = hasNextPage;
                m_contentGrid->setMangaDataSource(m_mangaList);
                m_resultsLabel->setText(std::to_string(manga.size()) + " manga found");
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
        m_contentGrid->setMangaDataSource(m_mangaList);
        return;
    }

    // If we have a current source, search within that source
    if (m_currentSourceId != 0) {
        performSourceSearch(m_currentSourceId, query);
        return;
    }

    // Otherwise, search across all sources
    m_resultsLabel->setText("Searching...");
    m_browseMode = BrowseMode::SEARCH_RESULTS;

    asyncRun([this, query]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> allResults;

        // Search each source
        for (const auto& source : m_sources) {
            std::vector<Manga> results;
            if (client.quickSearchManga(source.id, query, results)) {
                for (auto& manga : results) {
                    manga.sourceName = source.name;
                    allResults.push_back(manga);
                }
            }

            // Limit total results
            if (allResults.size() >= 50) break;
        }

        brls::Logger::info("SearchTab: Found {} total results for '{}'", allResults.size(), query);

        brls::sync([this, allResults]() {
            m_mangaList = allResults;
            m_contentGrid->setMangaDataSource(m_mangaList);
            m_resultsLabel->setText(std::to_string(allResults.size()) + " results");
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
                m_contentGrid->setMangaDataSource(m_mangaList);
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

        if (success) {
            brls::sync([this, manga, hasNextPage]() {
                // Append to existing list
                for (const auto& m : manga) {
                    m_mangaList.push_back(m);
                }
                m_hasNextPage = hasNextPage;
                m_contentGrid->setMangaDataSource(m_mangaList);
                m_resultsLabel->setText(std::to_string(m_mangaList.size()) + " manga");
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

} // namespace vitasuwayomi
