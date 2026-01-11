/**
 * VitaABS - Podcast Search Tab Implementation
 * Search iTunes for podcasts and add them to the server
 */

#include "view/podcast_search_tab.hpp"
#include "app/audiobookshelf_client.hpp"
#include "app/application.hpp"
#include "utils/async.hpp"
#include "utils/image_loader.hpp"

namespace vitaabs {

class PodcastResultCell : public brls::Box {
public:
    PodcastResultCell(const PodcastSearchResult& podcast, std::function<void()> onSelect)
        : m_podcast(podcast), m_onSelect(onSelect) {

        this->setFocusable(true);
        this->setHeight(100);
        this->setMargins(5, 5, 5, 5);
        this->setPadding(10, 10, 10, 10);
        this->setAxis(brls::Axis::ROW);
        this->setBackgroundColor(nvgRGBA(50, 50, 50, 255));
        this->setCornerRadius(8);

        // Cover image
        m_coverImage = new brls::Image();
        m_coverImage->setWidth(80);
        m_coverImage->setHeight(80);
        m_coverImage->setScalingType(brls::ImageScalingType::FIT);
        m_coverImage->setCornerRadius(4);
        this->addView(m_coverImage);

        // Load cover
        if (!podcast.artworkUrl.empty()) {
            ImageLoader::loadAsync(podcast.artworkUrl, [](brls::Image*) {}, m_coverImage);
        }

        // Info container
        brls::Box* infoBox = new brls::Box();
        infoBox->setAxis(brls::Axis::COLUMN);
        infoBox->setGrow(1.0f);
        infoBox->setMarginLeft(15);
        infoBox->setJustifyContent(brls::JustifyContent::CENTER);

        // Title
        brls::Label* titleLabel = new brls::Label();
        titleLabel->setText(podcast.title);
        titleLabel->setFontSize(18);
        titleLabel->setTextColor(nvgRGB(255, 255, 255));
        infoBox->addView(titleLabel);

        // Author
        brls::Label* authorLabel = new brls::Label();
        authorLabel->setText(podcast.author);
        authorLabel->setFontSize(14);
        authorLabel->setTextColor(nvgRGB(180, 180, 180));
        authorLabel->setMarginTop(5);
        infoBox->addView(authorLabel);

        // Genre and episode count
        std::string infoText = podcast.genre;
        if (podcast.trackCount > 0) {
            infoText += " • " + std::to_string(podcast.trackCount) + " episodes";
        }
        brls::Label* infoLabel = new brls::Label();
        infoLabel->setText(infoText);
        infoLabel->setFontSize(12);
        infoLabel->setTextColor(nvgRGB(140, 140, 140));
        infoLabel->setMarginTop(5);
        infoBox->addView(infoLabel);

        this->addView(infoBox);

        // Add action for select
        this->registerAction("Add to Library", brls::ControllerButton::BUTTON_A, [this](brls::View*) {
            if (m_onSelect) m_onSelect();
            return true;
        });
    }

private:
    PodcastSearchResult m_podcast;
    std::function<void()> m_onSelect;
    brls::Image* m_coverImage = nullptr;
};

PodcastSearchTab::PodcastSearchTab(const std::string& libraryId)
    : m_libraryId(libraryId) {

    this->setAxis(brls::Axis::COLUMN);
    this->setGrow(1.0f);
    this->setPadding(20, 20, 20, 20);

    // Register back button action to pop activity
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Search Podcasts");
    m_titleLabel->setFontSize(24);
    m_titleLabel->setMarginBottom(15);
    this->addView(m_titleLabel);

    // Subtitle
    brls::Label* subtitleLabel = new brls::Label();
    subtitleLabel->setText("Search iTunes for podcasts to add to your library");
    subtitleLabel->setFontSize(14);
    subtitleLabel->setTextColor(nvgRGB(180, 180, 180));
    subtitleLabel->setMarginBottom(20);
    this->addView(subtitleLabel);

    // Search input (using Label as a clickable button to trigger keyboard)
    m_searchBox = new brls::Box();
    m_searchBox->setAxis(brls::Axis::ROW);
    m_searchBox->setAlignItems(brls::AlignItems::CENTER);
    m_searchBox->setPadding(15);
    m_searchBox->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
    m_searchBox->setCornerRadius(8);
    m_searchBox->setMarginBottom(20);
    m_searchBox->setFocusable(true);

    brls::Label* searchLabel = new brls::Label();
    searchLabel->setText("Tap to search for podcasts...");
    searchLabel->setFontSize(16);
    searchLabel->setTextColor(nvgRGB(180, 180, 180));
    m_searchBox->addView(searchLabel);

    m_searchBox->registerClickAction([this, searchLabel](brls::View*) {
        // Open on-screen keyboard for search
        brls::Application::getImeManager()->openForText([this, searchLabel](std::string query) {
            if (!query.empty()) {
                searchLabel->setText("Search: " + query);
                onSearch(query);
            }
        }, "Search Podcasts", "Enter podcast name", 100, "");
        return true;
    });
    this->addView(m_searchBox);

    // Results scroll container
    m_resultsScroll = new brls::ScrollingFrame();
    m_resultsScroll->setGrow(1.0f);

    m_resultsContainer = new brls::Box();
    m_resultsContainer->setAxis(brls::Axis::COLUMN);
    m_resultsContainer->setGrow(1.0f);

    m_resultsScroll->setContentView(m_resultsContainer);
    this->addView(m_resultsScroll);
}

void PodcastSearchTab::onFocusGained() {
    brls::Box::onFocusGained();
}

void PodcastSearchTab::onSearch(const std::string& query) {
    brls::Logger::info("PodcastSearchTab: Searching for '{}'", query);

    // Show loading
    m_resultsContainer->clearViews();
    brls::Label* loadingLabel = new brls::Label();
    loadingLabel->setText("Searching...");
    loadingLabel->setFontSize(16);
    loadingLabel->setTextColor(nvgRGB(180, 180, 180));
    m_resultsContainer->addView(loadingLabel);

    // Perform search asynchronously
    std::string searchQuery = query;
    asyncRun([this, searchQuery]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<PodcastSearchResult> results;
        client.searchPodcasts(searchQuery, results);

        // Update UI on main thread
        brls::sync([this, results]() {
            showSearchResults(results);
        });
    });
}

void PodcastSearchTab::showSearchResults(const std::vector<PodcastSearchResult>& results) {
    m_searchResults = results;
    m_resultsContainer->clearViews();

    if (results.empty()) {
        brls::Label* noResultsLabel = new brls::Label();
        noResultsLabel->setText("No podcasts found");
        noResultsLabel->setFontSize(16);
        noResultsLabel->setTextColor(nvgRGB(180, 180, 180));
        m_resultsContainer->addView(noResultsLabel);
        return;
    }

    // Add result header
    brls::Label* headerLabel = new brls::Label();
    headerLabel->setText("Found " + std::to_string(results.size()) + " podcasts:");
    headerLabel->setFontSize(16);
    headerLabel->setMarginBottom(10);
    m_resultsContainer->addView(headerLabel);

    // Add result cells
    for (const auto& podcast : results) {
        PodcastSearchResult p = podcast;
        PodcastResultCell* cell = new PodcastResultCell(podcast, [this, p]() {
            onPodcastSelected(p);
        });
        m_resultsContainer->addView(cell);
    }
}

void PodcastSearchTab::onPodcastSelected(const PodcastSearchResult& podcast) {
    brls::Logger::info("PodcastSearchTab: Selected podcast: {}", podcast.title);

    // Show confirmation dialog
    brls::Dialog* dialog = new brls::Dialog("Add \"" + podcast.title + "\" to your library?");
    dialog->addButton("Cancel", []() {});
    dialog->addButton("Add", [this, podcast]() {
        addPodcast(podcast);
    });
    dialog->open();
}

void PodcastSearchTab::addPodcast(const PodcastSearchResult& podcast) {
    brls::Logger::info("PodcastSearchTab: Adding podcast '{}' from feed: {}", podcast.title, podcast.feedUrl);

    asyncRun([this, podcast]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        bool success = client.addPodcastToLibrary(m_libraryId, podcast);

        brls::sync([this, success, podcast]() {
            if (success) {
                brls::Application::notify("Added \"" + podcast.title + "\" to library");
            } else {
                brls::Application::notify("Failed to add podcast");
            }
        });
    });
}

} // namespace vitaabs
