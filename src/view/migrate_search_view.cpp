/**
 * VitaSuwayomi - Migrate Search View implementation
 * Searches all sources for a manga and displays results like the browser tab
 */

#include "view/migrate_search_view.hpp"
#include "view/manga_item_cell.hpp"
#include "view/media_detail_view.hpp"
#include "view/horizontal_scroll_row.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/async.hpp"
#include "utils/image_loader.hpp"
#include "utils/library_cache.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <set>

namespace vitasuwayomi {

MigrateSearchView::MigrateSearchView(const Manga& sourceManga)
    : m_sourceManga(sourceManga)
    , m_alive(std::make_shared<bool>(true))
{
    buildUi("Migrate: " + sourceManga.title);
    loadSourcesAndSearch();
}

// Find mode: the title comes from outside any source (a MangaBrain
// recommendation), so there is nothing to migrate FROM — every source is
// searched and picking a result opens its detail view.
MigrateSearchView::MigrateSearchView(const std::string& searchTitle)
    : m_findOnly(true)
    , m_alive(std::make_shared<bool>(true))
{
    m_sourceManga.title = searchTitle;   // performSearch() queries this
    buildUi("Find: " + searchTitle);
    loadSourcesAndSearch();
}

MigrateSearchView::MigrateSearchView(const std::string& searchTitle,
                                     const std::map<std::string, std::vector<Manga>>& prefetched)
    : m_findOnly(true)
    , m_alive(std::make_shared<bool>(true))
{
    m_sourceManga.title = searchTitle;
    buildUi("Find: " + searchTitle);
    m_resultsBySource = prefetched;
    size_t total = 0;
    for (const auto& kv : prefetched) total += kv.second.size();
    m_statusLabel->setText("No exact match - pick one of " + std::to_string(total) +
                           " results from " + std::to_string(prefetched.size()) + " sources");
    populateResults();
}

void MigrateSearchView::buildUi(const std::string& heading) {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(heading);
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

// The user's Browse settings: NSFW sources and the source-language filter.
bool MigrateSearchView::sourceAllowed(const Source& src) {
    const AppSettings& settings = Application::getInstance().getSettings();
    if (src.isNsfw && !settings.showNsfwSources) return false;

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
        if (!langMatch && src.lang != "multi" && src.lang != "all") return false;
    }
    return true;
}

void MigrateSearchView::filterSources(const std::vector<Source>& allSources) {
    m_filteredSources.clear();

    for (const auto& src : allSources) {
        // Migrating: skip the manga's current source. Find mode has no current
        // source — and the stub's sourceId 0 is the Local source's real id.
        if (!m_findOnly && src.id == m_sourceManga.sourceId) continue;
        if (!sourceAllowed(src)) continue;
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
    sourceLabel->setTextColor(Application::getInstance().getHeaderTextColor());
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
    if (m_findOnly) {
        // Nothing to migrate — just open the picked result.
        auto* detailView = new MangaDetailView(newManga);
        brls::Application::pushActivity(new brls::Activity(detailView));
        return;
    }

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

// ── Direct open for titles from outside the sources ─────────────────────────

namespace {

// Titles differ in punctuation and case between AniList and the sources
// ("Kaguya-sama: Love is War" vs "Kaguya-sama - Love Is War"), so compare
// on letters and digits only. Non-ASCII bytes are kept as-is, so Japanese,
// Korean and Chinese titles still compare exactly.
std::string normTitle(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (c >= 0x80) out += (char)c;
        else if (std::isalnum(c)) out += (char)std::tolower(c);
    }
    return out;
}

// One lookup at a time: a second tap while the first is still searching
// would otherwise open two detail views.
std::atomic<bool> s_resolving{false};

// Enough to cover the sources a user actually reads from without the tap
// turning into a minute-long wait; the picker covers everything else.
constexpr size_t kMaxSources = 8;
constexpr int    kBudgetSeconds = 20;

} // namespace

void MigrateSearchView::openTitle(std::vector<std::string> titles, int64_t preferredSourceId) {
    titles.erase(std::remove_if(titles.begin(), titles.end(),
                                [](const std::string& t) { return normTitle(t).empty(); }),
                 titles.end());
    if (titles.empty()) return;

    if (s_resolving.exchange(true)) {
        brls::Application::notify("Still opening the previous title...");
        return;
    }
    brls::Application::notify("Opening " + titles[0] + "...");

    asyncRun([titles, preferredSourceId]() {
        std::set<std::string> wanted;
        for (const auto& t : titles) wanted.insert(normTitle(t));
        auto isMatch = [&wanted](const Manga& m) { return wanted.count(normTitle(m.title)) > 0; };

        auto openManga = [](const Manga& m) {
            brls::sync([m]() {
                s_resolving.store(false);
                auto* detailView = new MangaDetailView(m);
                brls::Application::pushActivity(new brls::Activity(detailView));
            });
        };

        SuwayomiClient& client = SuwayomiClient::getInstance();

        // 1. Already in the library: open it, no search at all.
        std::vector<Manga> library;
        if (!LibraryCache::getInstance().loadAllLibraryManga(library) || library.empty())
            client.fetchLibraryManga(library);
        for (const auto& m : library) {
            if (isMatch(m)) { openManga(m); return; }
        }

        // 2. Silent search, stopping at the first exact title match.
        std::vector<Source> all;
        if (!client.fetchSourceList(all) || all.empty()) {
            brls::sync([]() {
                s_resolving.store(false);
                brls::Application::notify("Couldn't load your sources");
            });
            return;
        }
        std::vector<Source> sources;
        for (const auto& src : all) if (sourceAllowed(src)) sources.push_back(src);
        // The source the user is reading from is the likeliest to carry a
        // similar title, so it goes first.
        std::stable_partition(sources.begin(), sources.end(),
            [preferredSourceId](const Source& src) { return src.id == preferredSourceId; });
        if (sources.size() > kMaxSources) sources.resize(kMaxSources);

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(kBudgetSeconds);
        std::map<std::string, std::vector<Manga>> found;

        for (size_t i = 0; i < sources.size(); ++i) {
            const Source& src = sources[i];
            // Romaji is the query everywhere; in the preferred source also try
            // the English title, since many sources index by English name.
            std::vector<std::string> queries{titles[0]};
            if (i == 0 && src.id == preferredSourceId && titles.size() > 1 &&
                normTitle(titles[1]) != normTitle(titles[0]))
                queries.push_back(titles[1]);

            for (const auto& q : queries) {
                std::vector<Manga> results;
                bool hasNext = false;
                if (!client.searchManga(src.id, q, 1, results, hasNext)) continue;
                for (auto& m : results) {
                    m.sourceName = src.name;
                    if (isMatch(m)) { openManga(m); return; }
                }
                auto& bucket = found[src.name];
                bucket.insert(bucket.end(), results.begin(), results.end());
                if (bucket.empty()) found.erase(src.name);
            }
            if (std::chrono::steady_clock::now() > deadline) break;
        }

        // 3. No exact match: let the user pick from what was found, without
        //    searching again.
        const std::string heading = titles[0];
        brls::sync([heading, found]() {
            s_resolving.store(false);
            if (found.empty()) {
                brls::Application::notify("None of your sources has " + heading);
                return;
            }
            auto* view = new MigrateSearchView(heading, found);
            brls::Application::pushActivity(new brls::Activity(view));
        });
    });
}

} // namespace vitasuwayomi
