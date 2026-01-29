/**
 * VitaSuwayomi - Migrate Search View implementation
 * Searches all sources for a manga and displays results like the browser tab
 */

#include "view/migrate_search_view.hpp"
#include "view/manga_item_cell.hpp"
#include "view/horizontal_scroll_row.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/async.hpp"
#include "utils/image_loader.hpp"

namespace vitasuwayomi {

MigrateSearchView::MigrateSearchView(const Manga& sourceManga)
    : m_sourceManga(sourceManga)
    , m_alive(std::make_shared<bool>(true))
{
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Migrate: " + sourceManga.title);
    m_titleLabel->setFontSize(24);
    m_titleLabel->setMarginBottom(10);
    this->addView(m_titleLabel);

    // Status label
    m_statusLabel = new brls::Label();
    m_statusLabel->setText("Loading sources...");
    m_statusLabel->setFontSize(16);
    m_statusLabel->setMarginBottom(10);
    this->addView(m_statusLabel);

    // Scroll view for results
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);
    m_scrollView->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    m_resultsBox = new brls::Box();
    m_resultsBox->setAxis(brls::Axis::COLUMN);
    m_resultsBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_resultsBox->setAlignItems(brls::AlignItems::STRETCH);
    m_resultsBox->setPadding(10);

    m_scrollView->setContentView(m_resultsBox);
    this->addView(m_scrollView);

    // Register B button to go back
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });

    // Start loading
    loadSourcesAndSearch();
}

void MigrateSearchView::loadSourcesAndSearch() {
    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Source> sources;
        client.fetchSourceList(sources);

        brls::sync([this, sources, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            if (sources.empty()) {
                m_statusLabel->setText("No sources available");
                return;
            }

            filterSources(sources);

            if (m_filteredSources.empty()) {
                m_statusLabel->setText("No other sources available");
                return;
            }

            m_statusLabel->setText("Searching " + std::to_string(m_filteredSources.size()) + " sources...");
            performSearch();
        });
    });
}

void MigrateSearchView::filterSources(const std::vector<Source>& allSources) {
    const AppSettings& settings = Application::getInstance().getSettings();
    m_filteredSources.clear();

    for (const auto& src : allSources) {
        if (src.id == m_sourceManga.sourceId) continue;
        if (src.isNsfw && !settings.showNsfwSources) continue;

        if (!settings.enabledSourceLanguages.empty()) {
            bool langMatch = settings.enabledSourceLanguages.count(src.lang) > 0;
            if (!langMatch) {
                std::string baseLang = src.lang;
                size_t dashPos = baseLang.find('-');
                if (dashPos != std::string::npos) {
                    baseLang = baseLang.substr(0, dashPos);
                    langMatch = settings.enabledSourceLanguages.count(baseLang) > 0;
                }
            }
            if (!langMatch && src.lang != "multi" && src.lang != "all") continue;
        }

        m_filteredSources.push_back(src);
    }
}

void MigrateSearchView::performSearch() {
    std::weak_ptr<bool> aliveWeak = m_alive;
    std::string query = m_sourceManga.title;
    std::vector<Source> sourcesToSearch = m_filteredSources;

    asyncRun([this, query, sourcesToSearch, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::map<std::string, std::vector<Manga>> resultsBySource;
        int totalResults = 0;

        for (const auto& source : sourcesToSearch) {
            std::vector<Manga> results;
            bool hasNextPage = false;

            if (client.searchManga(source.id, query, 1, results, hasNextPage)) {
                if (!results.empty()) {
                    for (auto& manga : results) {
                        manga.sourceName = source.name;
                    }
                    resultsBySource[source.name] = results;
                    totalResults += results.size();
                }
            }

            if (totalResults >= 100) break;
        }

        brls::sync([this, resultsBySource, totalResults, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            m_resultsBySource = resultsBySource;

            if (resultsBySource.empty()) {
                m_statusLabel->setText("No results found");
            } else {
                m_statusLabel->setText(std::to_string(totalResults) + " results from " +
                                      std::to_string(resultsBySource.size()) + " sources");
                populateResults();
            }
        });
    });
}

void MigrateSearchView::populateResults() {
    m_resultsBox->clearViews();

    for (const auto& [sourceName, manga] : m_resultsBySource) {
        if (!manga.empty()) {
            createSourceRow(sourceName, manga);
        }
    }
}

void MigrateSearchView::createSourceRow(const std::string& sourceName, const std::vector<Manga>& manga) {
    // Source header
    auto* sourceLabel = new brls::Label();
    sourceLabel->setText(sourceName + " (" + std::to_string(manga.size()) + ")");
    sourceLabel->setFontSize(18);
    sourceLabel->setMarginTop(10);
    sourceLabel->setMarginBottom(8);
    sourceLabel->setTextColor(nvgRGB(100, 180, 255));
    m_resultsBox->addView(sourceLabel);

    // Horizontal scroll row with manga cells
    auto* rowBox = new HorizontalScrollRow();
    rowBox->setHeight(195);
    rowBox->setMarginBottom(10);

    for (size_t i = 0; i < manga.size(); i++) {
        auto* cell = new MangaItemCell();
        cell->setManga(manga[i]);
        cell->setWidth(150);
        cell->setHeight(185);
        cell->setMarginRight(10);

        Manga mangaCopy = manga[i];
        cell->registerClickAction([this, mangaCopy](brls::View* view) {
            onMangaSelected(mangaCopy);
            return true;
        });
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

        rowBox->addView(cell);
    }

    m_resultsBox->addView(rowBox);
}

void MigrateSearchView::onMangaSelected(const Manga& newManga) {
    // Confirm migration with a dropdown
    std::vector<std::string> options = {"Migrate to: " + newManga.title, "Cancel"};

    auto* dropdown = new brls::Dropdown(
        "Confirm Migration",
        options,
        [this, newManga](int selected) {
            if (selected == 0) {
                performMigration(newManga);
            }
        }
    );
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void MigrateSearchView::performMigration(const Manga& newManga) {
    brls::Application::notify("Migrating...");

    std::weak_ptr<bool> aliveWeak = m_alive;
    Manga oldManga = m_sourceManga;

    asyncRun([oldManga, newManga, aliveWeak]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        client.addMangaToLibrary(newManga.id);

        if (!oldManga.categoryIds.empty()) {
            client.setMangaCategories(newManga.id, oldManga.categoryIds);
        }

        client.removeMangaFromLibrary(oldManga.id);

        brls::sync([aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            brls::Application::notify("Migration complete");
            brls::Application::popActivity();
        });
    });
}

} // namespace vitasuwayomi
