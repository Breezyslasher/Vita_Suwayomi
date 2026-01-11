/**
 * VitaSuwayomi - Search Tab implementation
 */

#include "view/search_tab.hpp"
#include "app/suwayomi_client.hpp"
#include "view/media_detail_view.hpp"
#include "view/media_item_cell.hpp"
#include "app/application.hpp"

namespace vitasuwayomi {

SearchTab::SearchTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Search");
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
        }, "Search", "Enter search query", 256, m_searchQuery);
        return true;
    });
    m_searchLabel->addGestureRecognizer(new brls::TapGestureRecognizer(m_searchLabel));

    this->addView(m_searchLabel);

    // Results label
    m_resultsLabel = new brls::Label();
    m_resultsLabel->setText("");
    m_resultsLabel->setFontSize(18);
    m_resultsLabel->setMarginBottom(10);
    this->addView(m_resultsLabel);

    // Scrollable content for results
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_scrollContent = new brls::Box();
    m_scrollContent->setAxis(brls::Axis::COLUMN);
    m_scrollContent->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_scrollContent->setAlignItems(brls::AlignItems::STRETCH);

    // Books row (replaces Movies for Audiobookshelf)
    auto* booksLabel = new brls::Label();
    booksLabel->setText("Books");
    booksLabel->setFontSize(20);
    booksLabel->setMarginBottom(10);
    booksLabel->setVisibility(brls::Visibility::GONE);
    m_scrollContent->addView(booksLabel);

    m_moviesRow = new brls::HScrollingFrame();
    m_moviesRow->setHeight(180);
    m_moviesRow->setMarginBottom(15);
    m_moviesRow->setVisibility(brls::Visibility::GONE);

    m_moviesContent = new brls::Box();
    m_moviesContent->setAxis(brls::Axis::ROW);
    m_moviesContent->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_moviesRow->setContentView(m_moviesContent);
    m_scrollContent->addView(m_moviesRow);

    // Podcasts row (replaces TV Shows for Audiobookshelf)
    auto* podcastsLabel = new brls::Label();
    podcastsLabel->setText("Podcasts");
    podcastsLabel->setFontSize(20);
    podcastsLabel->setMarginBottom(10);
    podcastsLabel->setVisibility(brls::Visibility::GONE);
    m_scrollContent->addView(podcastsLabel);

    m_showsRow = new brls::HScrollingFrame();
    m_showsRow->setHeight(180);
    m_showsRow->setMarginBottom(15);
    m_showsRow->setVisibility(brls::Visibility::GONE);

    m_showsContent = new brls::Box();
    m_showsContent->setAxis(brls::Axis::ROW);
    m_showsContent->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_showsRow->setContentView(m_showsContent);
    m_scrollContent->addView(m_showsRow);

    // Episodes row (podcast episodes)
    auto* episodesLabel = new brls::Label();
    episodesLabel->setText("Episodes");
    episodesLabel->setFontSize(20);
    episodesLabel->setMarginBottom(10);
    episodesLabel->setVisibility(brls::Visibility::GONE);
    m_scrollContent->addView(episodesLabel);

    m_episodesRow = new brls::HScrollingFrame();
    m_episodesRow->setHeight(180);
    m_episodesRow->setMarginBottom(15);
    m_episodesRow->setVisibility(brls::Visibility::GONE);

    m_episodesContent = new brls::Box();
    m_episodesContent->setAxis(brls::Axis::ROW);
    m_episodesContent->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_episodesRow->setContentView(m_episodesContent);
    m_scrollContent->addView(m_episodesRow);

    // Hide music row - not applicable for Audiobookshelf
    m_musicRow = nullptr;
    m_musicContent = nullptr;

    m_scrollView->setContentView(m_scrollContent);
    this->addView(m_scrollView);
}

void SearchTab::onFocusGained() {
    brls::Box::onFocusGained();

    // Focus search label
    if (m_searchLabel) {
        brls::Application::giveFocus(m_searchLabel);
    }
}

void SearchTab::populateRow(brls::Box* rowContent, const std::vector<MediaItem>& items) {
    if (!rowContent) return;

    rowContent->clearViews();

    for (const auto& item : items) {
        auto* cell = new MediaItemCell();
        cell->setItem(item);
        cell->setWidth(120);
        cell->setHeight(170);
        cell->setMarginRight(10);

        MediaItem capturedItem = item;
        cell->registerClickAction([this, capturedItem](brls::View* view) {
            onItemSelected(capturedItem);
            return true;
        });

        rowContent->addView(cell);
    }
}

void SearchTab::performSearch(const std::string& query) {
    if (query.empty()) {
        m_resultsLabel->setText("");
        m_results.clear();
        m_movies.clear();  // books
        m_shows.clear();   // podcasts
        m_episodes.clear();
        m_music.clear();

        // Hide all rows
        m_moviesRow->setVisibility(brls::Visibility::GONE);
        m_showsRow->setVisibility(brls::Visibility::GONE);
        m_episodesRow->setVisibility(brls::Visibility::GONE);

        // Hide labels
        auto& views = m_scrollContent->getChildren();
        for (size_t i = 0; i < views.size(); i++) {
            views[i]->setVisibility(brls::Visibility::GONE);
        }
        return;
    }

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    // Audiobookshelf search requires a library ID
    // Search across all libraries
    std::vector<Library> libraries;
    if (!client.fetchLibraries(libraries) || libraries.empty()) {
        m_resultsLabel->setText("No libraries available");
        return;
    }

    m_results.clear();
    m_movies.clear();  // books
    m_shows.clear();   // podcasts
    m_episodes.clear();

    // Search each library
    for (const auto& lib : libraries) {
        std::vector<MediaItem> libResults;
        if (client.search(lib.id, query, libResults)) {
            for (auto& item : libResults) {
                m_results.push_back(item);
            }
        }
    }

    if (!m_results.empty()) {
        m_resultsLabel->setText("Found " + std::to_string(m_results.size()) + " results");

        // Organize results by type for Audiobookshelf
        for (const auto& item : m_results) {
            if (item.mediaType == MediaType::BOOK) {
                m_movies.push_back(item);  // Using movies vector for books
            } else if (item.mediaType == MediaType::PODCAST) {
                m_shows.push_back(item);   // Using shows vector for podcasts
            } else if (item.mediaType == MediaType::PODCAST_EPISODE) {
                m_episodes.push_back(item);
            }
        }

        // Update rows visibility and content
        // Order: Books(0,1), Podcasts(2,3), Episodes(4,5)
        auto& views = m_scrollContent->getChildren();

        // Books (label at index 0, row at index 1)
        if (!m_movies.empty()) {
            views[0]->setVisibility(brls::Visibility::VISIBLE);
            m_moviesRow->setVisibility(brls::Visibility::VISIBLE);
            populateRow(m_moviesContent, m_movies);
        } else {
            views[0]->setVisibility(brls::Visibility::GONE);
            m_moviesRow->setVisibility(brls::Visibility::GONE);
        }

        // Podcasts (label at index 2, row at index 3)
        if (!m_shows.empty()) {
            views[2]->setVisibility(brls::Visibility::VISIBLE);
            m_showsRow->setVisibility(brls::Visibility::VISIBLE);
            populateRow(m_showsContent, m_shows);
        } else {
            views[2]->setVisibility(brls::Visibility::GONE);
            m_showsRow->setVisibility(brls::Visibility::GONE);
        }

        // Episodes (label at index 4, row at index 5)
        if (!m_episodes.empty()) {
            views[4]->setVisibility(brls::Visibility::VISIBLE);
            m_episodesRow->setVisibility(brls::Visibility::VISIBLE);
            populateRow(m_episodesContent, m_episodes);
        } else {
            views[4]->setVisibility(brls::Visibility::GONE);
            m_episodesRow->setVisibility(brls::Visibility::GONE);
        }

    } else {
        m_resultsLabel->setText("No results found");
    }
}

void SearchTab::onItemSelected(const MediaItem& item) {
    // For podcast episodes, play directly instead of showing detail view
    if (item.mediaType == MediaType::PODCAST_EPISODE) {
        Application::getInstance().pushPlayerActivity(item.podcastId, item.episodeId);
        return;
    }

    // Show media detail view for books and podcasts
    auto* detailView = new MediaDetailView(item);
    brls::Application::pushActivity(new brls::Activity(detailView));
}

} // namespace vitasuwayomi
