/**
 * VitaSuwayomi - Home Tab implementation
 */

#include "view/home_tab.hpp"
#include "view/media_detail_view.hpp"
#include "view/media_item_cell.hpp"
#include "app/application.hpp"
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

    // Continue Listening section
    m_continueLabel = new brls::Label();
    m_continueLabel->setText("Continue Listening");
    m_continueLabel->setFontSize(22);
    m_continueLabel->setMarginBottom(10);
    m_continueLabel->setVisibility(brls::Visibility::GONE);  // Hidden until loaded
    m_contentBox->addView(m_continueLabel);

    // Horizontal box for Continue Listening (no ScrollingFrame - use focus-based scrolling)
    m_continueBox = new brls::Box();
    m_continueBox->setAxis(brls::Axis::ROW);  // Horizontal layout
    m_continueBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_continueBox->setHeight(200);
    m_continueBox->setVisibility(brls::Visibility::GONE);
    m_contentBox->addView(m_continueBox);

    // Recently Added Episodes section
    m_recentEpisodesLabel = new brls::Label();
    m_recentEpisodesLabel->setText("Recently Added Episodes");
    m_recentEpisodesLabel->setFontSize(22);
    m_recentEpisodesLabel->setMarginTop(20);
    m_recentEpisodesLabel->setMarginBottom(10);
    m_recentEpisodesLabel->setVisibility(brls::Visibility::GONE);  // Hidden until loaded
    m_contentBox->addView(m_recentEpisodesLabel);

    // Horizontal box for Recently Added Episodes (no ScrollingFrame - use focus-based scrolling)
    m_recentEpisodesBox = new brls::Box();
    m_recentEpisodesBox->setAxis(brls::Axis::ROW);  // Horizontal layout
    m_recentEpisodesBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    m_recentEpisodesBox->setHeight(200);
    m_recentEpisodesBox->setVisibility(brls::Visibility::GONE);
    m_contentBox->addView(m_recentEpisodesBox);

    m_scrollView->setContentView(m_contentBox);
    this->addView(m_scrollView);

    // Load content immediately (since Home is the first tab)
    brls::Logger::debug("HomeTab: Created, loading content...");
    loadContent();
}

HomeTab::~HomeTab() {
    // Mark as no longer alive to prevent async callbacks
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

void HomeTab::loadContent() {
    if (m_loaded) return;

    brls::Logger::debug("HomeTab: Loading content");

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

        std::vector<MediaItem> continueItems;
        std::vector<MediaItem> recentEpisodes;

        // Get Continue Listening items using the direct API endpoint
        brls::Logger::info("HomeTab: Fetching items in progress...");
        if (client.fetchItemsInProgress(continueItems)) {
            brls::Logger::info("HomeTab: Got {} items in progress", continueItems.size());
        } else {
            brls::Logger::error("HomeTab: Failed to fetch items in progress");
        }

        // Get all libraries to fetch recent episodes from podcast libraries
        std::vector<Library> libraries;
        if (!client.fetchLibraries(libraries)) {
            brls::Logger::error("HomeTab: Failed to fetch libraries");
        } else {
            // Fetch recently added from podcast libraries for Recent Episodes
            for (const auto& lib : libraries) {
                if (lib.mediaType == "podcast") {
                    brls::Logger::debug("HomeTab: Fetching recent episodes from podcast library '{}'", lib.name);
                    std::vector<PersonalizedShelf> shelves;
                    if (client.fetchLibraryPersonalized(lib.id, shelves)) {
                        brls::Logger::debug("HomeTab: Got {} shelves from library '{}'", shelves.size(), lib.name);
                        for (const auto& shelf : shelves) {
                            std::string shelfId = shelf.id;
                            std::string label = shelf.label;

                            brls::Logger::debug("HomeTab: Checking shelf id='{}' label='{}' entities={}",
                                               shelfId, label, shelf.entities.size());

                            // Check for Recently Added Episodes - use shelf.id
                            if (shelfId == "recent-episodes" ||
                                shelfId == "newest-episodes" ||
                                shelfId == "episodes-recently-added" ||
                                shelfId == "recently-added" ||
                                label.find("Recent") != std::string::npos) {
                                brls::Logger::info("HomeTab: Found Recent Episodes shelf '{}' with {} items",
                                                  label, shelf.entities.size());
                                for (const auto& item : shelf.entities) {
                                    recentEpisodes.push_back(item);
                                }
                            }
                        }
                    } else {
                        brls::Logger::error("HomeTab: Failed to fetch personalized content for library '{}'", lib.name);
                    }
                }
            }
        }

        brls::Logger::info("HomeTab: Found {} continue items, {} recent episodes",
                          continueItems.size(), recentEpisodes.size());

        // Update UI on main thread
        brls::sync([this, continueItems, recentEpisodes, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            m_continueItems = continueItems;
            m_recentEpisodes = recentEpisodes;
            m_loaded = true;

            // Show Continue Listening section if we have items
            if (!m_continueItems.empty()) {
                m_continueLabel->setVisibility(brls::Visibility::VISIBLE);
                m_continueBox->setVisibility(brls::Visibility::VISIBLE);
                populateHorizontalRow(m_continueBox, m_continueItems);
            }

            // Show Recently Added Episodes section if we have items
            if (!m_recentEpisodes.empty()) {
                m_recentEpisodesLabel->setVisibility(brls::Visibility::VISIBLE);
                m_recentEpisodesBox->setVisibility(brls::Visibility::VISIBLE);
                populateHorizontalRow(m_recentEpisodesBox, m_recentEpisodes);
            }

            // Show message if nothing to display
            if (m_continueItems.empty() && m_recentEpisodes.empty()) {
                m_titleLabel->setText("Home - No items in progress");
            }

            brls::Logger::debug("HomeTab: Content loaded and displayed");
        });
    });
}

void HomeTab::populateHorizontalRow(brls::Box* container, const std::vector<MediaItem>& items) {
    if (!container) return;

    // Clear existing items
    container->clearViews();

    // Set container width to fit all items (160px per item: 150 width + 10 margin)
    float totalWidth = items.size() * 160.0f;
    container->setWidth(totalWidth);

    // Add cells for each item
    for (size_t i = 0; i < items.size(); i++) {
        auto* cell = new MediaItemCell();
        cell->setItem(items[i]);
        cell->setWidth(150);
        cell->setHeight(185);  // Square cover (140) + labels (~45)
        cell->setMarginRight(10);

        // Store the item for click handler
        MediaItem itemCopy = items[i];
        cell->registerClickAction([this, itemCopy](brls::View* view) {
            onItemSelected(itemCopy);
            return true;
        });
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

        container->addView(cell);
    }

    brls::Logger::debug("HomeTab: Populated horizontal row with {} items, width={}", items.size(), totalWidth);
}

void HomeTab::onItemSelected(const MediaItem& item) {
    brls::Logger::debug("HomeTab: Selected item: {} (type={})", item.title, item.type);

    // For podcast episodes, start playback directly
    if (item.mediaType == MediaType::PODCAST_EPISODE) {
        Application::getInstance().pushPlayerActivity(item.podcastId, item.episodeId);
        return;
    }

    // For books and other items, show detail view
    auto* detailView = new MediaDetailView(item);
    brls::Application::pushActivity(new brls::Activity(detailView));
}

} // namespace vitasuwayomi
