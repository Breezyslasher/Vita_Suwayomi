/**
 * VitaSuwayomi - Home Tab implementation
 * Shows Continue Reading and Recent Chapter Updates
 */

#include "view/home_tab.hpp"
#include "view/manga_item_cell.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/async.hpp"

namespace vitasuwayomi {

HomeTab::HomeTab() {
    // Create alive flag for async callback safety
    m_alive = std::make_shared<bool>(true);

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Home");
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(15);
    this->addView(m_titleLabel);

    // Scrolling view for content
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_contentBox = new brls::Box();
    m_contentBox->setAxis(brls::Axis::COLUMN);
    m_contentBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_contentBox->setAlignItems(brls::AlignItems::STRETCH);

    // Continue Reading section
    m_continueLabel = new brls::Label();
    m_continueLabel->setText("Continue Reading");
    m_continueLabel->setFontSize(22);
    m_continueLabel->setMarginBottom(10);
    m_continueLabel->setVisibility(brls::Visibility::GONE);
    m_contentBox->addView(m_continueLabel);

    // Horizontal box for Continue Reading
    m_continueBox = new brls::Box();
    m_continueBox->setAxis(brls::Axis::ROW);
    m_continueBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_continueBox->setHeight(200);
    m_continueBox->setVisibility(brls::Visibility::GONE);
    m_contentBox->addView(m_continueBox);

    // Recent Updates section
    m_recentUpdatesLabel = new brls::Label();
    m_recentUpdatesLabel->setText("Recent Updates");
    m_recentUpdatesLabel->setFontSize(22);
    m_recentUpdatesLabel->setMarginTop(20);
    m_recentUpdatesLabel->setMarginBottom(10);
    m_recentUpdatesLabel->setVisibility(brls::Visibility::GONE);
    m_contentBox->addView(m_recentUpdatesLabel);

    // Horizontal box for Recent Updates
    m_recentUpdatesBox = new brls::Box();
    m_recentUpdatesBox->setAxis(brls::Axis::ROW);
    m_recentUpdatesBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_recentUpdatesBox->setHeight(200);
    m_recentUpdatesBox->setVisibility(brls::Visibility::GONE);
    m_contentBox->addView(m_recentUpdatesBox);

    m_scrollView->setContentView(m_contentBox);
    this->addView(m_scrollView);

    brls::Logger::debug("HomeTab: Created, loading content...");
    loadContent();
}

HomeTab::~HomeTab() {
    if (m_alive) {
        *m_alive = false;
    }
    brls::Logger::debug("HomeTab: Destroyed");
}

void HomeTab::onFocusGained() {
    brls::Box::onFocusGained();

    if (!m_loaded) {
        loadContent();
    }
}

void HomeTab::refresh() {
    m_loaded = false;
    loadContent();
}

void HomeTab::loadContent() {
    if (m_loaded) return;

    brls::Logger::debug("HomeTab: Loading content");

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        std::vector<Manga> continueReading;
        std::vector<RecentUpdate> recentUpdates;

        // Get library manga with reading progress
        std::vector<Manga> libraryManga;
        if (client.fetchLibraryManga(libraryManga)) {
            brls::Logger::info("HomeTab: Got {} library manga", libraryManga.size());

            // Filter to manga with reading progress (some chapters read but not all)
            for (const auto& manga : libraryManga) {
                if (manga.lastChapterRead > 0 && manga.unreadCount > 0) {
                    continueReading.push_back(manga);
                }
            }

            // Sort by last read time (most recent first) - approximate by chapter count read
            std::sort(continueReading.begin(), continueReading.end(),
                      [](const Manga& a, const Manga& b) {
                          return a.lastChapterRead > b.lastChapterRead;
                      });

            // Limit to 10 items
            if (continueReading.size() > 10) {
                continueReading.resize(10);
            }

            brls::Logger::info("HomeTab: Found {} continue reading items", continueReading.size());
        } else {
            brls::Logger::error("HomeTab: Failed to fetch library manga");
        }

        // Get recent chapter updates
        if (client.fetchRecentUpdates(1, recentUpdates)) {
            brls::Logger::info("HomeTab: Got {} recent updates", recentUpdates.size());

            // Limit to 10 items
            if (recentUpdates.size() > 10) {
                recentUpdates.resize(10);
            }
        } else {
            brls::Logger::error("HomeTab: Failed to fetch recent updates");
        }

        // Update UI on main thread
        brls::sync([this, continueReading, recentUpdates, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            m_continueReading = continueReading;
            m_recentUpdates = recentUpdates;
            m_loaded = true;

            // Show Continue Reading section if we have items
            if (!m_continueReading.empty()) {
                m_continueLabel->setVisibility(brls::Visibility::VISIBLE);
                m_continueBox->setVisibility(brls::Visibility::VISIBLE);
                populateHorizontalRow(m_continueBox, m_continueReading);
            }

            // Show Recent Updates section if we have items
            if (!m_recentUpdates.empty()) {
                m_recentUpdatesLabel->setVisibility(brls::Visibility::VISIBLE);
                m_recentUpdatesBox->setVisibility(brls::Visibility::VISIBLE);
                populateUpdatesRow(m_recentUpdatesBox, m_recentUpdates);
            }

            // Show message if nothing to display
            if (m_continueReading.empty() && m_recentUpdates.empty()) {
                m_titleLabel->setText("Home - No recent activity");
            }

            brls::Logger::debug("HomeTab: Content loaded and displayed");
        });
    });
}

void HomeTab::populateHorizontalRow(brls::Box* container, const std::vector<Manga>& items) {
    if (!container) return;

    container->clearViews();

    float totalWidth = items.size() * 160.0f;
    container->setWidth(totalWidth);

    for (size_t i = 0; i < items.size(); i++) {
        auto* cell = new MangaItemCell();
        cell->setManga(items[i]);
        cell->setWidth(150);
        cell->setHeight(185);
        cell->setMarginRight(10);

        Manga mangaCopy = items[i];
        cell->registerClickAction([this, mangaCopy](brls::View* view) {
            onMangaSelected(mangaCopy);
            return true;
        });
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

        container->addView(cell);
    }

    brls::Logger::debug("HomeTab: Populated horizontal row with {} items", items.size());
}

void HomeTab::populateUpdatesRow(brls::Box* container, const std::vector<RecentUpdate>& updates) {
    if (!container) return;

    container->clearViews();

    float totalWidth = updates.size() * 160.0f;
    container->setWidth(totalWidth);

    for (size_t i = 0; i < updates.size(); i++) {
        auto* cell = new MangaItemCell();
        cell->setManga(updates[i].manga);
        cell->setWidth(150);
        cell->setHeight(185);
        cell->setMarginRight(10);

        RecentUpdate updateCopy = updates[i];
        cell->registerClickAction([this, updateCopy](brls::View* view) {
            onUpdateSelected(updateCopy);
            return true;
        });
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

        container->addView(cell);
    }

    brls::Logger::debug("HomeTab: Populated updates row with {} items", updates.size());
}

void HomeTab::onMangaSelected(const Manga& manga) {
    brls::Logger::debug("HomeTab: Selected manga: {}", manga.title);

    // Open reader at last read position
    if (manga.lastChapterRead > 0) {
        Application::getInstance().pushReaderActivity(manga.id, manga.lastChapterRead, manga.title);
    } else {
        // Start from first chapter
        Application::getInstance().pushReaderActivity(manga.id, 1, manga.title);
    }
}

void HomeTab::onUpdateSelected(const RecentUpdate& update) {
    brls::Logger::debug("HomeTab: Selected update: {} - {}", update.manga.title, update.chapter.name);

    // Open reader at the updated chapter - use chapter.id not index
    Application::getInstance().pushReaderActivity(update.manga.id, update.chapter.id, update.manga.title);
}

} // namespace vitasuwayomi
