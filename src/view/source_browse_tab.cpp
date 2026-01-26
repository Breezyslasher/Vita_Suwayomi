/**
 * VitaSuwayomi - Source Browse Tab
 * Browse manga from a specific source
 */

#include "view/source_browse_tab.hpp"
#include "app/suwayomi_client.hpp"
#include "app/application.hpp"
#include "view/media_item_cell.hpp"
#include "utils/image_loader.hpp"

#include <borealis.hpp>

namespace vitasuwayomi {

SourceBrowseTab::SourceBrowseTab(const Source& source)
    : m_source(source)
    , m_currentPage(1)
    , m_hasNextPage(false)
    , m_browseMode(BrowseMode::POPULAR) {

    this->setAxis(brls::Axis::COLUMN);
    this->setPadding(20, 30, 20, 30);

    // Header with icon and title
    m_headerBox = new brls::Box();
    m_headerBox->setAxis(brls::Axis::ROW);
    m_headerBox->setAlignItems(brls::AlignItems::CENTER);
    m_headerBox->setMarginBottom(15);

    // Source icon
    m_sourceIcon = new brls::Image();
    m_sourceIcon->setWidth(32);
    m_sourceIcon->setHeight(32);
    m_sourceIcon->setMarginRight(12);
    m_sourceIcon->setScalingType(brls::ImageScalingType::FIT);
    m_headerBox->addView(m_sourceIcon);

    // Load icon asynchronously
    if (!source.iconUrl.empty()) {
        std::string iconUrl = Application::getInstance().getServerUrl() + source.iconUrl;
        ImageLoader::loadAsync(iconUrl, [](brls::Image* img) {}, m_sourceIcon);
    }

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(source.name);
    m_titleLabel->setFontSize(24);
    m_headerBox->addView(m_titleLabel);

    this->addView(m_headerBox);

    // Mode buttons
    auto* modeBox = new brls::Box();
    modeBox->setAxis(brls::Axis::ROW);
    modeBox->setMarginBottom(15);

    m_popularBtn = new brls::Button();
    m_popularBtn->setText("> Popular");
    m_popularBtn->setMarginRight(10);
    m_popularBtn->registerClickAction([this](brls::View*) {
        loadPopular();
        return true;
    });
    modeBox->addView(m_popularBtn);

    if (source.supportsLatest) {
        m_latestBtn = new brls::Button();
        m_latestBtn->setText("Latest");
        m_latestBtn->setMarginRight(10);
        m_latestBtn->registerClickAction([this](brls::View*) {
            loadLatest();
            return true;
        });
        modeBox->addView(m_latestBtn);
    }

    // Search button with Start icon above
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

    m_searchBtn = new brls::Button();
    m_searchBtn->setText("Search");
    m_searchBtn->registerClickAction([this](brls::View*) {
        showSearchDialog();
        return true;
    });
    searchContainer->addView(m_searchBtn);
    modeBox->addView(searchContainer);

    this->addView(modeBox);

    // Register Start button to open search dialog
    // Use brls::sync to defer IME opening to avoid crash during controller input handling
    this->registerAction("Search", brls::ControllerButton::BUTTON_START, [this](brls::View* view) {
        brls::sync([this]() {
            showSearchDialog();
        });
        return true;
    });

    // Content grid
    m_contentGrid = new RecyclingGrid();
    m_contentGrid->setGrow(1.0f);
    this->addView(m_contentGrid);

    // Load more button (hidden initially)
    m_loadMoreBtn = new brls::Button();
    m_loadMoreBtn->setText("Load More");
    m_loadMoreBtn->setMarginTop(15);
    m_loadMoreBtn->setVisibility(brls::Visibility::GONE);
    m_loadMoreBtn->registerClickAction([this](brls::View*) {
        loadNextPage();
        return true;
    });
    this->addView(m_loadMoreBtn);

    // Initial load
    loadPopular();
}

void SourceBrowseTab::onFocusGained() {
    brls::Box::onFocusGained();
}

void SourceBrowseTab::loadPopular() {
    m_browseMode = BrowseMode::POPULAR;
    m_currentPage = 1;
    m_mangaList.clear();
    updateModeButtons();
    loadManga();
}

void SourceBrowseTab::loadLatest() {
    m_browseMode = BrowseMode::LATEST;
    m_currentPage = 1;
    m_mangaList.clear();
    updateModeButtons();
    loadManga();
}

void SourceBrowseTab::loadSearch(const std::string& query) {
    m_browseMode = BrowseMode::SEARCH;
    m_searchQuery = query;
    m_currentPage = 1;
    m_mangaList.clear();
    updateModeButtons();
    loadManga();
}

void SourceBrowseTab::loadNextPage() {
    if (!m_hasNextPage) return;
    m_currentPage++;
    loadManga();
}

void SourceBrowseTab::loadManga() {
    brls::Logger::debug("Loading manga from source {} (page {})", m_source.name, m_currentPage);

    brls::async([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Manga> newManga;
        bool hasNext = false;
        bool success = false;

        switch (m_browseMode) {
            case BrowseMode::POPULAR:
                success = client.fetchPopularManga(m_source.id, m_currentPage, newManga, hasNext);
                break;
            case BrowseMode::LATEST:
                success = client.fetchLatestManga(m_source.id, m_currentPage, newManga, hasNext);
                break;
            case BrowseMode::SEARCH:
                success = client.searchManga(m_source.id, m_searchQuery, m_currentPage, newManga, hasNext);
                break;
        }

        brls::sync([this, success, newManga, hasNext]() {
            if (success) {
                // Append new manga to list
                for (const auto& manga : newManga) {
                    m_mangaList.push_back(manga);
                }
                m_hasNextPage = hasNext;
                updateGrid();
                updateLoadMoreButton();
            } else {
                brls::Application::notify("Failed to load manga");
            }
        });
    });
}

void SourceBrowseTab::updateGrid() {
    if (!m_contentGrid) return;

    m_contentGrid->clearViews();

    for (const auto& manga : m_mangaList) {
        auto* cell = new MangaItemCell();
        cell->setManga(manga);
        cell->registerClickAction([this, manga](brls::View*) {
            onMangaSelected(manga);
            return true;
        });
        m_contentGrid->addView(cell);
    }
}

void SourceBrowseTab::updateModeButtons() {
    // Use text prefix to indicate active state
    if (m_popularBtn) {
        m_popularBtn->setText(m_browseMode == BrowseMode::POPULAR ? "> Popular" : "Popular");
    }
    if (m_latestBtn) {
        m_latestBtn->setText(m_browseMode == BrowseMode::LATEST ? "> Latest" : "Latest");
    }
    if (m_searchBtn) {
        m_searchBtn->setText(m_browseMode == BrowseMode::SEARCH ? "> Search" : "Search");
    }
}

void SourceBrowseTab::updateLoadMoreButton() {
    if (m_loadMoreBtn) {
        m_loadMoreBtn->setVisibility(m_hasNextPage ?
            brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
}

void SourceBrowseTab::showSearchDialog() {
    brls::Application::getImeManager()->openForText([this](std::string query) {
        if (!query.empty()) {
            loadSearch(query);
        }
    }, "Search manga...", "", 100, "");
}

void SourceBrowseTab::onMangaSelected(const Manga& manga) {
    brls::Logger::info("Selected manga: {} (id: {})", manga.title, manga.id);

    // Push manga detail view
    // TODO: Implement navigation to MangaDetailView
}

} // namespace vitasuwayomi
