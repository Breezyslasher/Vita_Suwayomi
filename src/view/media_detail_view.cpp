/**
 * VitaSuwayomi - Manga Detail View implementation
 * NOBORU-style layout with cover on left, info and chapters on right
 */

#include "view/media_detail_view.hpp"
#include "view/tracking_search_view.hpp"
#include "app/suwayomi_client.hpp"
#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "utils/image_loader.hpp"
#include "utils/library_cache.hpp"
#include "utils/async.hpp"
#include <cmath>
#include <cstdio>
#include <ctime>
#include <set>
#include <limits>
#include <chrono>
#include <atomic>
#include <algorithm>

namespace vitasuwayomi {

MangaDetailView::MangaDetailView(const Manga& manga)
    : m_manga(manga), m_alive(std::make_shared<bool>(true)) {
    brls::Logger::info("MangaDetailView: Creating for '{}' id={}", manga.title, manga.id);
    m_lastProgressRefresh = std::chrono::steady_clock::now();

    // Try to load cached details if description is missing
    if (m_manga.description.empty()) {
        LibraryCache& cache = LibraryCache::getInstance();
        Manga cachedManga;
        if (cache.loadMangaDetails(m_manga.id, cachedManga)) {
            brls::Logger::info("MangaDetailView: Loaded details from cache for '{}'", m_manga.title);
            // Merge cached data with current manga (preserve non-cached fields)
            if (!cachedManga.description.empty()) m_manga.description = cachedManga.description;
            if (!cachedManga.author.empty() && m_manga.author.empty()) m_manga.author = cachedManga.author;
            if (!cachedManga.artist.empty() && m_manga.artist.empty()) m_manga.artist = cachedManga.artist;
            if (cachedManga.genre.size() > m_manga.genre.size()) m_manga.genre = cachedManga.genre;
            if (!cachedManga.sourceName.empty() && m_manga.sourceName.empty()) m_manga.sourceName = cachedManga.sourceName;
        }
    }

    // Komikku-style: horizontal layout with left panel (cover) and right panel (info)
    this->setAxis(brls::Axis::ROW);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);
    this->setBackgroundColor(nvgRGB(18, 18, 18)); // Komikku dark background

    // Register back button
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    }, false, false, brls::Sound::SOUND_BACK);

    // Load saved chapter sort order
    m_sortDescending = Application::getInstance().getSettings().chapterSortDescending;

    // Register R trigger for sort toggle
    this->registerAction("Sort", brls::ControllerButton::BUTTON_RB, [this](brls::View* view) {
        m_sortDescending = !m_sortDescending;
        updateSortIcon();
        populateChaptersList();

        // Persist chapter sort order
        auto& app = Application::getInstance();
        app.getSettings().chapterSortDescending = m_sortDescending;
        app.saveSettings();
        return true;
    });

    // Register Start button for options menu
    this->registerAction("Options", brls::ControllerButton::BUTTON_START, [this](brls::View* view) {
        showMangaMenu();
        return true;
    });

    // Register Y button for chapter filter
    this->registerAction("Filter", brls::ControllerButton::BUTTON_Y, [this](brls::View* view) {
        // Show filter options dialog
        std::vector<std::string> options = {"Toggle Downloaded", "Toggle Unread", "Toggle Bookmarked", "Clear Filters"};
        brls::Dropdown* dropdown = new brls::Dropdown(
            "Chapter Filters", options,
            [this](int selected) {
                if (selected < 0) return;
                switch (selected) {
                    case 0:
                        m_filterDownloaded = !m_filterDownloaded;
                        brls::Application::notify(m_filterDownloaded ? "Filtering: Downloaded" : "Filter removed");
                        break;
                    case 1:
                        m_filterUnread = !m_filterUnread;
                        brls::Application::notify(m_filterUnread ? "Filtering: Unread" : "Filter removed");
                        break;
                    case 2:
                        m_filterBookmarked = !m_filterBookmarked;
                        brls::Application::notify(m_filterBookmarked ? "Filtering: Bookmarked" : "Filter removed");
                        break;
                    case 3:
                        m_filterDownloaded = false;
                        m_filterUnread = false;
                        m_filterBookmarked = false;
                        m_filterScanlator.clear();
                        brls::Application::notify("Filters cleared");
                        break;
                }
                populateChaptersList();
            }, 0);
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return true;
    });

    // ========== LEFT PANEL: Cover + Action Buttons (260px) ==========
    auto* leftPanel = new brls::Box();
    leftPanel->setAxis(brls::Axis::COLUMN);
    leftPanel->setWidth(260);
    leftPanel->setPadding(20);
    leftPanel->setBackgroundColor(nvgRGB(28, 28, 28)); // Komikku side panel

    // Cover image container (centered)
    auto* coverContainer = new brls::Box();
    coverContainer->setAxis(brls::Axis::ROW);
    coverContainer->setJustifyContent(brls::JustifyContent::CENTER);
    coverContainer->setMarginBottom(15);

    m_coverImage = new brls::Image();
    m_coverImage->setSize(brls::Size(180, 260));
    m_coverImage->setScalingType(brls::ImageScalingType::FIT);
    m_coverImage->setCornerRadius(12);  // Komikku-style rounded corners
    coverContainer->addView(m_coverImage);
    leftPanel->addView(coverContainer);

    // Continue Reading button (Komikku teal accent) with Select button icon above
    auto* readButtonContainer = new brls::Box();
    readButtonContainer->setAxis(brls::Axis::COLUMN);
    readButtonContainer->setAlignItems(brls::AlignItems::FLEX_START);
    readButtonContainer->setMarginBottom(10);

    // Select button icon on top (64x16 original, scale to 80x20)
    auto* selectIcon = new brls::Image();
    selectIcon->setWidth(80);
    selectIcon->setHeight(20);
    selectIcon->setScalingType(brls::ImageScalingType::FIT);
    selectIcon->setImageFromFile("app0:resources/images/select_button.png");
    selectIcon->setMarginBottom(2);
    readButtonContainer->addView(selectIcon);

    m_readButton = new brls::Button();
    m_readButton->setWidth(190);
    m_readButton->setHeight(44);
    m_readButton->setCornerRadius(22);  // Pill-shaped button
    m_readButton->setBackgroundColor(nvgRGBA(0, 150, 136, 255)); // Teal

    std::string readText = "Start Reading";
    if (m_manga.lastChapterRead > 0) {
        readText = "Continue Ch. " + std::to_string(m_manga.lastChapterRead);
    }
    m_readButton->setText(readText);
    m_readButton->registerClickAction([this](brls::View* view) {
        onRead();
        return true;
    });
    m_readButton->addGestureRecognizer(new brls::TapGestureRecognizer(m_readButton));
    readButtonContainer->addView(m_readButton);
    leftPanel->addView(readButtonContainer);

    // Add to Library button (only shown if not already in library)
    if (!m_manga.inLibrary) {
        m_libraryButton = new brls::Button();
        m_libraryButton->setWidth(220);
        m_libraryButton->setHeight(44);
        m_libraryButton->setMarginBottom(10);
        m_libraryButton->setCornerRadius(22);  // Pill-shaped
        m_libraryButton->setBackgroundColor(nvgRGBA(66, 66, 66, 255));
        m_libraryButton->setText("Add to Library");
        m_libraryButton->registerClickAction([this](brls::View* view) {
            onAddToLibrary();
            return true;
        });
        m_libraryButton->addGestureRecognizer(new brls::TapGestureRecognizer(m_libraryButton));
        leftPanel->addView(m_libraryButton);
    }

    // Tracking button (for MAL, AniList, etc.)
    m_trackingButton = new brls::Button();
    m_trackingButton->setWidth(220);
    m_trackingButton->setHeight(44);
    m_trackingButton->setMarginBottom(10);
    m_trackingButton->setCornerRadius(22);  // Pill-shaped
    m_trackingButton->setBackgroundColor(nvgRGBA(103, 58, 183, 255));  // Purple for tracking
    m_trackingButton->setText("Tracking");
    m_trackingButton->registerClickAction([this](brls::View* view) {
        showTrackingDialog();
        return true;
    });
    m_trackingButton->addGestureRecognizer(new brls::TapGestureRecognizer(m_trackingButton));
    leftPanel->addView(m_trackingButton);

    this->addView(leftPanel);

    // ========== RIGHT PANEL: Info + Chapters (grows to fill) ==========
    auto* rightPanel = new brls::Box();
    rightPanel->setAxis(brls::Axis::COLUMN);
    rightPanel->setGrow(1.0f);
    rightPanel->setPadding(20, 20, 10, 0);

    // Title row
    auto* titleRow = new brls::Box();
    titleRow->setAxis(brls::Axis::ROW);
    titleRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    titleRow->setAlignItems(brls::AlignItems::FLEX_START);
    titleRow->setMarginBottom(10);

    auto* titleBox = new brls::Box();
    titleBox->setAxis(brls::Axis::COLUMN);
    titleBox->setGrow(1.0f);
    titleBox->setMarginRight(20);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(m_manga.title);
    m_titleLabel->setFontSize(22);
    m_titleLabel->setTextColor(nvgRGB(255, 255, 255));
    m_titleLabel->setMarginBottom(4);
    titleBox->addView(m_titleLabel);

    // Source
    if (!m_manga.sourceName.empty()) {
        m_sourceLabel = new brls::Label();
        m_sourceLabel->setText(m_manga.sourceName);
        m_sourceLabel->setFontSize(13);
        m_sourceLabel->setTextColor(nvgRGB(128, 128, 128));
        m_sourceLabel->setMarginBottom(8);
        titleBox->addView(m_sourceLabel);
    }

    // Stats row: Author | Status | Chapters
    auto* statsRow = new brls::Box();
    statsRow->setAxis(brls::Axis::ROW);
    statsRow->setMarginBottom(8);

    if (!m_manga.author.empty()) {
        m_authorLabel = new brls::Label();
        m_authorLabel->setText(m_manga.author);
        m_authorLabel->setFontSize(12);
        m_authorLabel->setTextColor(nvgRGB(160, 160, 160));
        m_authorLabel->setMarginRight(15);
        statsRow->addView(m_authorLabel);
    }

    m_statusLabel = new brls::Label();
    m_statusLabel->setText(m_manga.getStatusString());
    m_statusLabel->setFontSize(12);
    m_statusLabel->setTextColor(nvgRGB(74, 159, 255)); // Blue accent
    m_statusLabel->setMarginRight(15);
    statsRow->addView(m_statusLabel);

    m_chapterCountLabel = new brls::Label();
    std::string chapterInfo = std::to_string(m_manga.chapterCount) + " chapters";
    if (m_manga.unreadCount > 0) {
        chapterInfo += " (" + std::to_string(m_manga.unreadCount) + " unread)";
    }
    m_chapterCountLabel->setText(chapterInfo);
    m_chapterCountLabel->setFontSize(12);
    m_chapterCountLabel->setTextColor(nvgRGB(160, 160, 160));
    statsRow->addView(m_chapterCountLabel);

    titleBox->addView(statsRow);
    titleRow->addView(titleBox);
    rightPanel->addView(titleRow);

    // Genre tags
    if (!m_manga.genre.empty()) {
        m_genreBox = new brls::Box();
        m_genreBox->setAxis(brls::Axis::ROW);
        m_genreBox->setMarginBottom(10);

        for (size_t i = 0; i < m_manga.genre.size() && i < 6; i++) {
            auto* genreLabel = new brls::Label();
            genreLabel->setText(m_manga.genre[i]);
            genreLabel->setFontSize(11);
            genreLabel->setTextColor(nvgRGB(74, 159, 255));
            genreLabel->setMarginRight(12);
            m_genreBox->addView(genreLabel);
        }
        rightPanel->addView(m_genreBox);
    }

    // Description (collapsible - shows 2 lines by default, L to expand)
    brls::Logger::debug("MangaDetailView: Description length = {}", m_manga.description.length());
    m_descriptionLabel = new brls::Label();
    m_descriptionLabel->setFontSize(12);
    m_descriptionLabel->setTextColor(nvgRGB(192, 192, 192));
    m_descriptionLabel->setMarginBottom(15);

    if (!m_manga.description.empty()) {
        m_fullDescription = m_manga.description;
        m_descriptionExpanded = false;

        // Show first ~80 chars (approximately 2 lines) when collapsed
        std::string truncatedDesc = m_fullDescription;
        if (truncatedDesc.length() > 80) {
            truncatedDesc = truncatedDesc.substr(0, 77) + "... [L]";
        }
        m_descriptionLabel->setText(truncatedDesc);
    } else {
        m_fullDescription = "";
        m_descriptionLabel->setText("No description available [L]");
    }
    rightPanel->addView(m_descriptionLabel);

    // Register L trigger for description toggle
    this->registerAction("Summary", brls::ControllerButton::BUTTON_LB, [this](brls::View* view) {
        toggleDescription();
        return true;
    });

    // Chapters header
    auto* chaptersHeader = new brls::Box();
    chaptersHeader->setAxis(brls::Axis::ROW);
    chaptersHeader->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    chaptersHeader->setAlignItems(brls::AlignItems::CENTER);
    chaptersHeader->setMarginBottom(8);
    chaptersHeader->setPaddingBottom(8);

    m_chaptersLabel = new brls::Label();
    m_chaptersLabel->setText("Chapters");
    m_chaptersLabel->setFontSize(16);
    m_chaptersLabel->setTextColor(nvgRGB(255, 255, 255));
    chaptersHeader->addView(m_chaptersLabel);

    // Actions row: Sort (R) and Menu (Start) buttons with icons
    auto* chaptersActions = new brls::Box();
    chaptersActions->setAxis(brls::Axis::ROW);
    chaptersActions->setAlignItems(brls::AlignItems::CENTER);

    // Sort button with R icon above
    auto* sortContainer = new brls::Box();
    sortContainer->setAxis(brls::Axis::COLUMN);
    sortContainer->setAlignItems(brls::AlignItems::CENTER);
    sortContainer->setMarginRight(10);

    // R button icon (24x16 original, scale to 36x24)
    auto* rButtonIcon = new brls::Image();
    rButtonIcon->setWidth(36);
    rButtonIcon->setHeight(24);
    rButtonIcon->setScalingType(brls::ImageScalingType::FIT);
    rButtonIcon->setImageFromFile("app0:resources/images/r_button.png");
    rButtonIcon->setMarginBottom(2);
    sortContainer->addView(rButtonIcon);

    m_sortBtn = new brls::Button();
    m_sortBtn->setWidth(44);
    m_sortBtn->setHeight(40);
    m_sortBtn->setCornerRadius(8);

    m_sortIcon = new brls::Image();
    m_sortIcon->setWidth(24);
    m_sortIcon->setHeight(24);
    m_sortIcon->setScalingType(brls::ImageScalingType::FIT);
    updateSortIcon();
    m_sortBtn->addView(m_sortIcon);

    m_sortBtn->registerClickAction([this](brls::View* view) {
        m_sortDescending = !m_sortDescending;
        updateSortIcon();
        populateChaptersList();
        return true;
    });
    m_sortBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_sortBtn));
    sortContainer->addView(m_sortBtn);
    chaptersActions->addView(sortContainer);

    // Filter button with Y (triangle) icon above
    auto* filterContainer = new brls::Box();
    filterContainer->setAxis(brls::Axis::COLUMN);
    filterContainer->setAlignItems(brls::AlignItems::CENTER);
    filterContainer->setMarginRight(10);

    auto* yButtonIcon = new brls::Image();
    yButtonIcon->setWidth(24);
    yButtonIcon->setHeight(24);
    yButtonIcon->setScalingType(brls::ImageScalingType::FIT);
    yButtonIcon->setImageFromFile("app0:resources/images/triangle_button.png");
    yButtonIcon->setMarginBottom(2);
    filterContainer->addView(yButtonIcon);

    m_filterBtn = new brls::Button();
    m_filterBtn->setWidth(44);
    m_filterBtn->setHeight(40);
    m_filterBtn->setCornerRadius(8);
    m_filterBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_filterBtn->setAlignItems(brls::AlignItems::CENTER);

    // Add filter icon
    auto* filterIcon = new brls::Image();
    filterIcon->setWidth(24);
    filterIcon->setHeight(24);
    filterIcon->setScalingType(brls::ImageScalingType::FIT);
    filterIcon->setImageFromFile("app0:resources/icons/filter-menu-outline.png");
    m_filterBtn->addView(filterIcon);

    m_filterBtn->registerClickAction([this](brls::View* view) {
        // Show filter options dialog
        std::vector<std::string> options = {"Toggle Downloaded", "Toggle Unread", "Toggle Bookmarked", "Clear Filters"};
        brls::Dropdown* dropdown = new brls::Dropdown(
            "Chapter Filters", options,
            [this](int selected) {
                if (selected < 0) return;
                switch (selected) {
                    case 0:
                        m_filterDownloaded = !m_filterDownloaded;
                        brls::Application::notify(m_filterDownloaded ? "Filtering: Downloaded" : "Filter removed");
                        break;
                    case 1:
                        m_filterUnread = !m_filterUnread;
                        brls::Application::notify(m_filterUnread ? "Filtering: Unread" : "Filter removed");
                        break;
                    case 2:
                        m_filterBookmarked = !m_filterBookmarked;
                        brls::Application::notify(m_filterBookmarked ? "Filtering: Bookmarked" : "Filter removed");
                        break;
                    case 3:
                        m_filterDownloaded = false;
                        m_filterUnread = false;
                        m_filterBookmarked = false;
                        m_filterScanlator.clear();
                        brls::Application::notify("Filters cleared");
                        break;
                }
                populateChaptersList();
            }, 0);
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return true;
    });
    m_filterBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_filterBtn));
    filterContainer->addView(m_filterBtn);
    chaptersActions->addView(filterContainer);

    // Menu button with Start icon above
    auto* menuContainer = new brls::Box();
    menuContainer->setAxis(brls::Axis::COLUMN);
    menuContainer->setAlignItems(brls::AlignItems::CENTER);

    // Start button icon (64x16 original, scale to 80x20)
    auto* startButtonIcon = new brls::Image();
    startButtonIcon->setWidth(80);
    startButtonIcon->setHeight(20);
    startButtonIcon->setScalingType(brls::ImageScalingType::FIT);
    startButtonIcon->setImageFromFile("app0:resources/images/start_button.png");
    startButtonIcon->setMarginBottom(2);
    menuContainer->addView(startButtonIcon);

    m_menuBtn = new brls::Button();
    m_menuBtn->setWidth(44);
    m_menuBtn->setHeight(40);
    m_menuBtn->setCornerRadius(8);
    m_menuBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_menuBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* menuIcon = new brls::Image();
    menuIcon->setWidth(24);
    menuIcon->setHeight(24);
    menuIcon->setScalingType(brls::ImageScalingType::FIT);
    menuIcon->setImageFromFile("app0:resources/icons/menu.png");
    m_menuBtn->addView(menuIcon);

    m_menuBtn->registerClickAction([this](brls::View* view) {
        showMangaMenu();
        return true;
    });
    m_menuBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_menuBtn));
    menuContainer->addView(m_menuBtn);
    chaptersActions->addView(menuContainer);

    chaptersHeader->addView(chaptersActions);
    rightPanel->addView(chaptersHeader);

    // Chapters list (scrollable)
    m_chaptersScroll = new brls::ScrollingFrame();
    m_chaptersScroll->setGrow(1.0f);

    m_chaptersBox = new brls::Box();
    m_chaptersBox->setAxis(brls::Axis::COLUMN);

    m_chaptersScroll->setContentView(m_chaptersBox);
    rightPanel->addView(m_chaptersScroll);

    this->addView(rightPanel);

    // Load full details
    loadDetails();
}

brls::View* MangaDetailView::create() {
    return nullptr;
}

void MangaDetailView::refresh() {
    loadDetails();
}

void MangaDetailView::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);

    // Restore alive flag (cleared by willDisappear to cancel in-flight async ops)
    *m_alive = true;

    // Register progress callback for live download updates
    m_progressCallbackActive.store(true);
    m_lastProgressRefresh = std::chrono::steady_clock::now();

    std::weak_ptr<bool> aliveWeak = m_alive;
    int mangaId = m_manga.id;

    DownloadsManager& dm = DownloadsManager::getInstance();

    dm.setProgressCallback([this, aliveWeak, mangaId](int downloadedPages, int totalPages) {
        auto alive = aliveWeak.lock();
        if (!alive || !*alive || !m_progressCallbackActive.load()) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastProgressRefresh).count();

        // Update every 200ms for smooth progress, or immediately for completion
        const int PROGRESS_INTERVAL_MS = 200;
        if (elapsed >= PROGRESS_INTERVAL_MS || downloadedPages == totalPages) {
            m_lastProgressRefresh = now;
            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive || !m_progressCallbackActive.load()) {
                    return;
                }
                updateChapterDownloadStates();
            });
        }
    });

    dm.setChapterCompletionCallback([this, aliveWeak, mangaId](int completedMangaId, int chapterIndex, bool success) {
        auto alive = aliveWeak.lock();
        if (!alive || !*alive || !m_progressCallbackActive.load()) {
            return;
        }

        // Only refresh if the completed chapter belongs to this manga
        if (completedMangaId == mangaId) {
            brls::sync([this, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive || !m_progressCallbackActive.load()) {
                    return;
                }
                updateChapterDownloadStates();
            });
        }
    });

    // On first appearance, chapters are loaded via loadDetails().
    // On subsequent appearances (returning from reader), just update
    // download states in-place rather than doing a full reload.
    if (!m_chapters.empty()) {
        updateChapterDownloadStates();
        updateReadButtonText();
    } else {
        loadChapters();
    }
}

void MangaDetailView::willDisappear(bool resetState) {
    // CRITICAL: Clear callbacks FIRST to prevent any async updates after destruction
    DownloadsManager& dm = DownloadsManager::getInstance();
    dm.setProgressCallback(nullptr);
    dm.setChapterCompletionCallback(nullptr);

    // Stop receiving progress updates when not visible
    m_progressCallbackActive.store(false);

    // Mark view as no longer alive to prevent stale callback execution
    *m_alive = false;

    brls::Box::willDisappear(resetState);
}

void MangaDetailView::loadDetails() {
    brls::Logger::debug("MangaDetailView: Loading details for manga {}", m_manga.id);

    // Load cover
    loadCover();

    // Load chapters
    loadChapters();

    // Load tracking data
    loadTrackingData();

    // Fetch full manga details from API if description is empty
    if (m_manga.description.empty()) {
        brls::Logger::info("MangaDetailView: Description is empty, fetching from API...");
        asyncRun([this]() {
            SuwayomiClient& client = SuwayomiClient::getInstance();
            Manga updatedManga;

            if (client.fetchManga(m_manga.id, updatedManga)) {
                brls::sync([this, updatedManga, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
                    auto alive = aliveWeak.lock();
                    if (!alive || !*alive) {
                        return;
                    }

                    bool needsUpdate = false;

                    // Update description if we got one
                    if (!updatedManga.description.empty()) {
                        m_manga.description = updatedManga.description;
                        m_fullDescription = updatedManga.description;
                        m_descriptionExpanded = false;
                        needsUpdate = true;

                        // Update the description label
                        if (m_descriptionLabel) {
                            std::string truncatedDesc = m_fullDescription;
                            if (truncatedDesc.length() > 80) {
                                truncatedDesc = truncatedDesc.substr(0, 77) + "... [L]";
                            }
                            m_descriptionLabel->setText(truncatedDesc);
                        }
                    }

                    // Update other fields from API response
                    if (!updatedManga.author.empty() && m_manga.author.empty()) {
                        m_manga.author = updatedManga.author;
                        if (m_authorLabel) m_authorLabel->setText(m_manga.author);
                        needsUpdate = true;
                    }
                    if (!updatedManga.artist.empty() && m_manga.artist.empty()) {
                        m_manga.artist = updatedManga.artist;
                        needsUpdate = true;
                    }
                    if (updatedManga.genre.size() > m_manga.genre.size()) {
                        m_manga.genre = updatedManga.genre;
                        needsUpdate = true;
                    }
                    if (!updatedManga.sourceName.empty() && m_manga.sourceName.empty()) {
                        m_manga.sourceName = updatedManga.sourceName;
                        if (m_sourceLabel) m_sourceLabel->setText(m_manga.sourceName);
                        needsUpdate = true;
                    }

                    // Save updated manga details to cache
                    if (needsUpdate) {
                        LibraryCache::getInstance().saveMangaDetails(m_manga);
                        brls::Logger::info("MangaDetailView: Updated and cached manga details from API");
                    }
                });
            }
        });
    }
}

void MangaDetailView::loadCover() {
    if (!m_coverImage) return;

    SuwayomiClient& client = SuwayomiClient::getInstance();
    std::string coverUrl = client.getMangaThumbnailUrl(m_manga.id);

    if (!coverUrl.empty()) {
        ImageLoader::loadAsync(coverUrl, [this](brls::Image* image) {
            brls::Logger::debug("MangaDetailView: Cover loaded");
        }, m_coverImage);
    }
}

void MangaDetailView::loadChapters() {
    brls::Logger::debug("MangaDetailView: Loading chapters for manga {}", m_manga.id);

    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Chapter> chapters;

        if (client.fetchChapters(m_manga.id, chapters)) {
            brls::Logger::info("MangaDetailView: Got {} chapters", chapters.size());

            // Check if autoDownloadChapters is enabled
            bool autoDownload = Application::getInstance().getSettings().autoDownloadChapters;
            std::vector<int> chaptersToDownload;

            if (autoDownload) {
                // Find unread chapters that are not yet downloaded
                for (const auto& ch : chapters) {
                    if (!ch.read && !ch.downloaded) {
                        chaptersToDownload.push_back(ch.id);
                    }
                }

                // Queue chapters for download if any found
                if (!chaptersToDownload.empty()) {
                    brls::Logger::info("MangaDetailView: autoDownloadChapters - queuing {} unread chapters",
                                      chaptersToDownload.size());
                    client.queueChapterDownloads(chaptersToDownload);
                }
            }

            brls::sync([this, chapters, chaptersToDownload, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) {
                    return;
                }

                m_chapters = chapters;
                populateChaptersList();

                // Update chapter count label
                if (m_chapterCountLabel) {
                    std::string info = std::to_string(m_chapters.size()) + " chapters";
                    int unread = 0;
                    for (const auto& ch : m_chapters) {
                        if (!ch.read) unread++;
                    }
                    if (unread > 0) {
                        info += " (" + std::to_string(unread) + " unread)";
                    }
                    m_chapterCountLabel->setText(info);
                }

                // Update the read button text based on reading progress
                updateReadButtonText();

                // Notify user if auto-download was triggered
                if (!chaptersToDownload.empty()) {
                    brls::Application::notify("Auto-downloading " +
                        std::to_string(chaptersToDownload.size()) + " new chapters");
                }
            });
        } else {
            brls::Logger::error("MangaDetailView: Failed to fetch chapters");
        }
    });
}

void MangaDetailView::updateChapterDownloadStates() {
    // Guard: check if view is still alive
    if (!m_alive || !*m_alive) return;
    if (m_chapterDlElements.empty()) return;
    if (!m_chaptersBox) return;

    DownloadsManager& dm = DownloadsManager::getInstance();

    for (auto& elem : m_chapterDlElements) {
        // Null check: ensure button is still valid
        if (!elem.dlBtn) continue;

        DownloadedChapter* localCh = dm.getChapterDownload(m_manga.id, elem.chapterIndex);

        int newState = -1;  // not local
        int newPages = -1;
        if (localCh) {
            newState = static_cast<int>(localCh->state);
            if (localCh->state == LocalDownloadState::DOWNLOADING) {
                newPages = localCh->downloadedPages;
            }
        }

        // Skip if state hasn't changed
        if (newState == elem.cachedState && newPages == elem.cachedPercent) continue;

        elem.cachedState = newState;
        elem.cachedPercent = newPages;

        // Update appearance in-place using tracked icon and label
        if (localCh && localCh->state == LocalDownloadState::DOWNLOADING) {
            // Hide icon, show progress label
            if (elem.dlIcon) elem.dlIcon->setVisibility(brls::Visibility::GONE);
            if (elem.dlLabel) {
                elem.dlLabel->setVisibility(brls::Visibility::VISIBLE);
                elem.dlLabel->setText(std::to_string(localCh->downloadedPages) + "/" +
                                      std::to_string(localCh->pageCount));
            }
            elem.dlBtn->setBackgroundColor(nvgRGBA(52, 152, 219, 220));  // Blue
        } else {
            // Show icon, hide progress label
            if (elem.dlLabel) elem.dlLabel->setVisibility(brls::Visibility::GONE);
            if (elem.dlIcon) {
                elem.dlIcon->setVisibility(brls::Visibility::VISIBLE);

                if (localCh && localCh->state == LocalDownloadState::COMPLETED) {
                    elem.dlIcon->setImageFromFile("app0:resources/icons/checkbox_checked.png");
                    elem.dlBtn->setBackgroundColor(nvgRGBA(46, 204, 113, 200));  // Green
                } else if (localCh && localCh->state == LocalDownloadState::QUEUED) {
                    elem.dlIcon->setImageFromFile("app0:resources/icons/refresh.png");
                    elem.dlBtn->setBackgroundColor(nvgRGBA(241, 196, 15, 200));  // Yellow
                } else if (localCh && localCh->state == LocalDownloadState::FAILED) {
                    elem.dlIcon->setImageFromFile("app0:resources/icons/cross.png");
                    elem.dlBtn->setBackgroundColor(nvgRGBA(231, 76, 60, 200));  // Red
                } else {
                    elem.dlIcon->setImageFromFile("app0:resources/icons/download.png");
                    elem.dlBtn->setBackgroundColor(nvgRGBA(60, 60, 60, 200));
                }
            }
        }
    }
}

void MangaDetailView::populateChaptersList() {
    if (!m_chaptersBox) return;

    m_chaptersBox->clearViews();
    m_chapterDlElements.clear();

    // Sort chapters
    std::vector<Chapter> sortedChapters = m_chapters;
    if (m_sortDescending) {
        std::sort(sortedChapters.begin(), sortedChapters.end(),
                  [](const Chapter& a, const Chapter& b) {
                      return a.chapterNumber > b.chapterNumber;
                  });
    } else {
        std::sort(sortedChapters.begin(), sortedChapters.end(),
                  [](const Chapter& a, const Chapter& b) {
                      return a.chapterNumber < b.chapterNumber;
                  });
    }

    // Get downloads manager to check local download state
    DownloadsManager& dmForFilter = DownloadsManager::getInstance();

    // Create chapter cells (Komikku-style: rounded, clean design)
    for (const auto& chapter : sortedChapters) {
        // For downloaded filter, only check LOCAL download state (not server)
        if (m_filterDownloaded) {
            DownloadedChapter* localCh = dmForFilter.getChapterDownload(m_manga.id, chapter.index);
            bool isLocallyDownloaded = localCh && localCh->state == LocalDownloadState::COMPLETED;
            if (!isLocallyDownloaded) continue;
        }
        if (m_filterUnread && chapter.read) continue;
        if (m_filterBookmarked && !chapter.bookmarked) continue;
        if (!m_filterScanlator.empty() && chapter.scanlator != m_filterScanlator) continue;

        auto* chapterRow = new brls::Box();
        chapterRow->setAxis(brls::Axis::ROW);
        chapterRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        chapterRow->setAlignItems(brls::AlignItems::CENTER);
        chapterRow->setHeight(56);
        chapterRow->setPadding(10, 14, 10, 14);
        chapterRow->setMarginBottom(4);
        chapterRow->setCornerRadius(8);
        chapterRow->setBackgroundColor(nvgRGBA(40, 40, 40, 255));
        chapterRow->setFocusable(true);

        // Left side: chapter info
        auto* infoBox = new brls::Box();
        infoBox->setAxis(brls::Axis::COLUMN);
        infoBox->setGrow(1.0f);

        std::string title = chapter.name;
        if (title.empty()) {
            title = "Chapter " + std::to_string(static_cast<int>(chapter.chapterNumber));
        }

        auto* titleLabel = new brls::Label();
        titleLabel->setText(title);
        titleLabel->setFontSize(14);
        titleLabel->setTextColor(chapter.read ? nvgRGB(128, 128, 128) : nvgRGB(255, 255, 255));
        infoBox->addView(titleLabel);

        // Subtitle: scanlator + date
        std::string subtitle;
        if (!chapter.scanlator.empty()) {
            subtitle = chapter.scanlator;
        }
        if (!subtitle.empty()) {
            auto* subtitleLabel = new brls::Label();
            subtitleLabel->setText(subtitle);
            subtitleLabel->setFontSize(11);
            subtitleLabel->setTextColor(nvgRGB(128, 128, 128));
            infoBox->addView(subtitleLabel);
        }

        chapterRow->addView(infoBox);

        // Right side: status indicators and download button
        auto* statusBox = new brls::Box();
        statusBox->setAxis(brls::Axis::ROW);
        statusBox->setAlignItems(brls::AlignItems::CENTER);

        if (chapter.read) {
            auto* readLabel = new brls::Label();
            readLabel->setText("[Read]");
            readLabel->setFontSize(11);
            readLabel->setTextColor(nvgRGB(100, 100, 100));
            readLabel->setMarginRight(8);
            statusBox->addView(readLabel);
        }

        // Download button - shows state based on local download state
        Chapter capturedChapter = chapter;
        auto* dlBtn = new brls::Button();
        dlBtn->setWidth(40);
        dlBtn->setHeight(36);
        dlBtn->setCornerRadius(18);  // Circular button
        dlBtn->setFocusable(true);
        dlBtn->setJustifyContent(brls::JustifyContent::CENTER);
        dlBtn->setAlignItems(brls::AlignItems::CENTER);

        // Check local download state first
        DownloadsManager& dm = DownloadsManager::getInstance();
        DownloadedChapter* localCh = dm.getChapterDownload(m_manga.id, chapter.index);

        bool isLocallyDownloaded = localCh && localCh->state == LocalDownloadState::COMPLETED;
        bool isLocallyDownloading = localCh && localCh->state == LocalDownloadState::DOWNLOADING;
        bool isLocallyQueued = localCh && localCh->state == LocalDownloadState::QUEUED;
        bool isLocallyFailed = localCh && localCh->state == LocalDownloadState::FAILED;

        // Create icon and progress label for this button.
        // Both are tracked in ChapterDownloadUI for in-place updates.
        auto* dlIcon = new brls::Image();
        dlIcon->setWidth(20);
        dlIcon->setHeight(20);
        dlIcon->setScalingType(brls::ImageScalingType::FIT);
        dlBtn->addView(dlIcon);

        auto* dlLabel = new brls::Label();
        dlLabel->setFontSize(9);
        dlLabel->setTextColor(nvgRGB(255, 255, 255));
        dlLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        dlLabel->setVisibility(brls::Visibility::GONE);
        dlBtn->addView(dlLabel);

        // Set initial state
        if (isLocallyDownloading && localCh) {
            dlIcon->setVisibility(brls::Visibility::GONE);
            dlLabel->setVisibility(brls::Visibility::VISIBLE);
            dlLabel->setText(std::to_string(localCh->downloadedPages) + "/" +
                             std::to_string(localCh->pageCount));
            dlBtn->setBackgroundColor(nvgRGBA(52, 152, 219, 200));  // Blue
        } else if (isLocallyDownloaded) {
            dlIcon->setImageFromFile("app0:resources/icons/checkbox_checked.png");
            dlBtn->setBackgroundColor(nvgRGBA(46, 204, 113, 200));  // Green
            dlBtn->registerClickAction([this, capturedChapter](brls::View* view) {
                deleteChapterDownload(capturedChapter);
                return true;
            });
        } else if (isLocallyQueued) {
            dlIcon->setImageFromFile("app0:resources/icons/refresh.png");
            dlBtn->setBackgroundColor(nvgRGBA(241, 196, 15, 200));  // Yellow
        } else if (isLocallyFailed) {
            dlIcon->setImageFromFile("app0:resources/icons/cross.png");
            dlBtn->setBackgroundColor(nvgRGBA(231, 76, 60, 200));  // Red
            dlBtn->registerClickAction([this, capturedChapter](brls::View* view) {
                downloadChapter(capturedChapter);  // Retry
                return true;
            });
        } else {
            dlIcon->setImageFromFile("app0:resources/icons/download.png");
            dlBtn->setBackgroundColor(nvgRGBA(60, 60, 60, 200));
            dlBtn->registerClickAction([this, capturedChapter](brls::View* view) {
                downloadChapter(capturedChapter);
                return true;
            });
        }
        dlBtn->addGestureRecognizer(new brls::TapGestureRecognizer(dlBtn));

        // Track button, icon, and label for incremental download state updates
        {
            ChapterDownloadUI cdui;
            cdui.chapterIndex = chapter.index;
            cdui.dlBtn = dlBtn;
            cdui.dlIcon = dlIcon;
            cdui.dlLabel = dlLabel;
            cdui.cachedState = localCh ? static_cast<int>(localCh->state) : -1;
            cdui.cachedPercent = (isLocallyDownloading && localCh)
                ? localCh->downloadedPages : -1;
            m_chapterDlElements.push_back(cdui);
        }

        statusBox->addView(dlBtn);

        // X button icon indicator (shows X button action is available) - only visible when focused
        auto* xButtonIcon = new brls::Image();
        xButtonIcon->setWidth(24);
        xButtonIcon->setHeight(24);
        xButtonIcon->setScalingType(brls::ImageScalingType::FIT);
        xButtonIcon->setImageFromFile("app0:resources/images/square_button.png");
        xButtonIcon->setMarginLeft(8);
        xButtonIcon->setVisibility(brls::Visibility::INVISIBLE);  // Hidden by default
        statusBox->addView(xButtonIcon);

        chapterRow->addView(statusBox);

        // Show X button icon when this row gets focus, hide previous
        chapterRow->getFocusEvent()->subscribe([this, xButtonIcon](brls::View* view) {
            // Hide previously focused icon
            if (m_currentFocusedIcon && m_currentFocusedIcon != xButtonIcon) {
                m_currentFocusedIcon->setVisibility(brls::Visibility::INVISIBLE);
            }
            // Show this icon
            xButtonIcon->setVisibility(brls::Visibility::VISIBLE);
            m_currentFocusedIcon = xButtonIcon;
        });

        // Click action - open chapter
        chapterRow->registerClickAction([this, capturedChapter](brls::View* view) {
            onChapterSelected(capturedChapter);
            return true;
        });

        // X button to download/delete/cancel chapter when row is focused
        bool localDownloaded = isLocallyDownloaded;
        bool localQueued = isLocallyQueued;
        bool localDownloading = isLocallyDownloading;
        bool chapterRead = chapter.read;
        int chapterIdx = chapter.index;
        chapterRow->registerAction("Download", brls::ControllerButton::BUTTON_X, [this, capturedChapter, localDownloaded, localQueued, localDownloading, chapterIdx](brls::View* view) {
            if (localQueued || localDownloading) {
                // Cancel the queued/downloading chapter
                DownloadsManager& dm = DownloadsManager::getInstance();
                if (dm.cancelChapterDownload(m_manga.id, chapterIdx)) {
                    brls::Application::notify("Removed from queue");
                    updateChapterDownloadStates();
                }
            } else if (localDownloaded) {
                deleteChapterDownload(capturedChapter);
            } else {
                downloadChapter(capturedChapter);
            }
            return true;
        });

        // Select button to start reading (same as Start Reading button)
        chapterRow->registerAction("Read", brls::ControllerButton::BUTTON_BACK, [this, capturedChapter](brls::View* view) {
            onRead(capturedChapter.id);
            return true;
        });

        // Touch support - tap to open chapter
        chapterRow->addGestureRecognizer(new brls::TapGestureRecognizer(chapterRow));

        // Swipe gesture support (Komikku-style)
        // Left swipe: Mark as read/unread
        // Right swipe: Download/Delete chapter
        int mangaId = m_manga.id;
        int chapterIndex = capturedChapter.index;
        int chapterId = capturedChapter.id;
        std::string mangaTitle = m_manga.title;
        std::string chapterName = capturedChapter.name;
        bool serverDownloaded = capturedChapter.downloaded;

        chapterRow->addGestureRecognizer(new brls::PanGestureRecognizer(
            [this, chapterRow, mangaId, chapterIndex, chapterId, chapterRead, localDownloaded, localQueued, localDownloading, serverDownloaded, mangaTitle, chapterName](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
                static brls::Point touchStart;
                static bool isValidSwipe = false;
                // Original background color for chapter rows
                const NVGcolor originalBgColor = nvgRGBA(40, 40, 40, 255);
                const float SWIPE_THRESHOLD = 60.0f;  // Minimum distance to trigger action
                const float TAP_THRESHOLD = 15.0f;    // Max movement for tap (ignore)

                if (status.state == brls::GestureState::START) {
                    touchStart = status.position;
                    isValidSwipe = false;
                } else if (status.state == brls::GestureState::STAY) {
                    float dx = status.position.x - touchStart.x;
                    float dy = status.position.y - touchStart.y;

                    // Only consider horizontal swipes (ignore vertical scrolling)
                    if (std::abs(dx) > std::abs(dy) * 1.5f && std::abs(dx) > TAP_THRESHOLD) {
                        isValidSwipe = true;

                        // Visual feedback - change background color based on swipe direction
                        if (dx < -SWIPE_THRESHOLD * 0.5f) {
                            // Swiping left - mark read/unread (blue tint)
                            chapterRow->setBackgroundColor(nvgRGBA(52, 152, 219, 100));
                        } else if (dx > SWIPE_THRESHOLD * 0.5f) {
                            // Swiping right - download/delete/cancel (green/red tint)
                            if (localDownloaded || serverDownloaded || localQueued || localDownloading) {
                                chapterRow->setBackgroundColor(nvgRGBA(231, 76, 60, 100));  // Red for delete/cancel
                            } else {
                                chapterRow->setBackgroundColor(nvgRGBA(46, 204, 113, 100));  // Green for download
                            }
                        } else {
                            chapterRow->setBackgroundColor(originalBgColor);
                        }
                    }
                } else if (status.state == brls::GestureState::END) {
                    // Reset background color
                    chapterRow->setBackgroundColor(originalBgColor);

                    float dx = status.position.x - touchStart.x;
                    float dy = status.position.y - touchStart.y;
                    float distance = std::sqrt(dx * dx + dy * dy);

                    // Only process horizontal swipes that exceed threshold
                    if (isValidSwipe && std::abs(dx) > SWIPE_THRESHOLD && std::abs(dx) > std::abs(dy) * 1.5f) {
                        if (dx < 0) {
                            // Left swipe - toggle read status
                            if (chapterRead) {
                                // Mark as unread
                                asyncRun([mangaId, chapterIndex]() {
                                    SuwayomiClient::getInstance().markChapterUnread(mangaId, chapterIndex);
                                    brls::sync([]() {
                                        brls::Application::notify("Marked as unread");
                                    });
                                });
                            } else {
                                // Mark as read
                                asyncRun([mangaId, chapterIndex]() {
                                    SuwayomiClient::getInstance().markChapterRead(mangaId, chapterIndex);
                                    brls::sync([]() {
                                        brls::Application::notify("Marked as read");
                                    });
                                });
                            }
                        } else {
                            // Right swipe - download, delete, or cancel
                            // Get download mode setting
                            DownloadMode downloadMode = Application::getInstance().getSettings().downloadMode;

                            if (localQueued || localDownloading) {
                                // Cancel queued/downloading chapter (local only - server queue managed separately)
                                asyncRun([this, mangaId, chapterIndex]() {
                                    DownloadsManager& dm = DownloadsManager::getInstance();
                                    if (dm.cancelChapterDownload(mangaId, chapterIndex)) {
                                        brls::sync([this]() {
                                            brls::Application::notify("Removed from queue");
                                            updateChapterDownloadStates();
                                        });
                                    }
                                });
                            } else if (localDownloaded || serverDownloaded) {
                                // Delete download - respect downloadMode setting
                                asyncRun([this, mangaId, chapterId, chapterIndex, downloadMode, serverDownloaded, localDownloaded]() {
                                    int serverDeleted = 0;
                                    int localDeleted = 0;

                                    // Delete from server if applicable
                                    if (serverDownloaded &&
                                        (downloadMode == DownloadMode::SERVER_ONLY || downloadMode == DownloadMode::BOTH)) {
                                        SuwayomiClient& client = SuwayomiClient::getInstance();
                                        std::vector<int> chapterIds = {chapterId};
                                        std::vector<int> chapterIndexes = {chapterIndex};
                                        if (client.deleteChapterDownloads(chapterIds, mangaId, chapterIndexes)) {
                                            serverDeleted = 1;
                                        }
                                    }

                                    // Delete from local if applicable
                                    if (localDownloaded &&
                                        (downloadMode == DownloadMode::LOCAL_ONLY || downloadMode == DownloadMode::BOTH)) {
                                        DownloadsManager& dm = DownloadsManager::getInstance();
                                        if (dm.deleteChapterDownload(mangaId, chapterIndex)) {
                                            localDeleted = 1;
                                        }
                                    }

                                    brls::sync([this, serverDeleted, localDeleted]() {
                                        if (serverDeleted > 0 || localDeleted > 0) {
                                            brls::Application::notify("Download deleted");
                                        }
                                        updateChapterDownloadStates();
                                    });
                                });
                            } else {
                                // Start download - respect downloadMode setting
                                asyncRun([this, mangaId, chapterId, chapterIndex, mangaTitle, chapterName, downloadMode]() {
                                    SuwayomiClient& client = SuwayomiClient::getInstance();
                                    DownloadsManager& dm = DownloadsManager::getInstance();
                                    bool serverQueued = false;
                                    bool localQueued = false;

                                    // Queue to server if applicable
                                    if (downloadMode == DownloadMode::SERVER_ONLY || downloadMode == DownloadMode::BOTH) {
                                        std::vector<int> chapterIds = {chapterId};
                                        serverQueued = client.queueChapterDownloads(chapterIds);
                                    }

                                    // Queue to local if applicable
                                    if (downloadMode == DownloadMode::LOCAL_ONLY || downloadMode == DownloadMode::BOTH) {
                                        dm.init();
                                        localQueued = dm.queueChapterDownload(mangaId, chapterId, chapterIndex, mangaTitle, chapterName);
                                        if (localQueued) {
                                            dm.startDownloads();
                                        }
                                    }

                                    brls::sync([this, serverQueued, localQueued]() {
                                        if (serverQueued || localQueued) {
                                            brls::Application::notify("Download queued");
                                        } else {
                                            brls::Application::notify("Failed to queue");
                                        }
                                        updateChapterDownloadStates();
                                    });
                                });
                            }
                        }
                    }
                    isValidSwipe = false;
                }
            }, brls::PanAxis::HORIZONTAL));

        m_chaptersBox->addView(chapterRow);
    }

    // Set up D-pad navigation routes between left panel buttons, chapter header buttons, and chapter list
    auto& children = m_chaptersBox->getChildren();

    // UP from read button -> sort button (chapter header area)
    if (m_readButton && m_sortBtn) {
        m_readButton->setCustomNavigationRoute(brls::FocusDirection::UP, m_sortBtn);
    }

    // DOWN from read button -> library button if it exists and is visible, else tracking button
    if (m_readButton) {
        if (m_libraryButton && m_libraryButton->getVisibility() == brls::Visibility::VISIBLE) {
            m_readButton->setCustomNavigationRoute(brls::FocusDirection::DOWN, m_libraryButton);
        } else if (m_trackingButton) {
            m_readButton->setCustomNavigationRoute(brls::FocusDirection::DOWN, m_trackingButton);
        }
    }

    // DOWN from library button -> tracking button
    if (m_libraryButton && m_trackingButton) {
        m_libraryButton->setCustomNavigationRoute(brls::FocusDirection::DOWN, m_trackingButton);
    }

    // DOWN from tracking button -> first chapter row
    if (m_trackingButton && !children.empty()) {
        m_trackingButton->setCustomNavigationRoute(brls::FocusDirection::DOWN, children.front());
    }

    // UP from first chapter row -> sort button (chapter header area)
    if (!children.empty() && m_sortBtn) {
        children.front()->setCustomNavigationRoute(brls::FocusDirection::UP, m_sortBtn);
    }

    // DOWN from chapter header buttons -> first chapter row
    if (!children.empty()) {
        if (m_sortBtn) {
            m_sortBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, children.front());
        }
        if (m_filterBtn) {
            m_filterBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, children.front());
        }
        if (m_menuBtn) {
            m_menuBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, children.front());
        }
    }

    // UP from chapter header buttons -> read button (to cycle back to left panel)
    if (m_readButton) {
        if (m_sortBtn) {
            m_sortBtn->setCustomNavigationRoute(brls::FocusDirection::UP, m_readButton);
        }
        if (m_filterBtn) {
            m_filterBtn->setCustomNavigationRoute(brls::FocusDirection::UP, m_readButton);
        }
        if (m_menuBtn) {
            m_menuBtn->setCustomNavigationRoute(brls::FocusDirection::UP, m_readButton);
        }
    }

    brls::Logger::debug("MangaDetailView: Populated {} chapters", sortedChapters.size());
}

void MangaDetailView::showMangaMenu() {
    // Collect data before creating dropdown callbacks to avoid capturing 'this' unsafely
    int mangaId = m_manga.id;
    std::string mangaTitle = m_manga.title;

    // Copy chapters data for the callbacks
    std::vector<Chapter> chapters = m_chapters;

    std::vector<std::string> options = {
        "Download all",
        "Download unread",
        "Remove all chapters",
        "Cancel downloading chapters",
        "Reset cover"
    };

    brls::Dropdown* dropdown = new brls::Dropdown(
        "Options", options,
        [this, mangaId, chapters](int selected) {
            if (selected < 0) return;  // Cancelled

            switch (selected) {
                case 0:  // Download all
                    brls::sync([this]() {
                        downloadAllChapters();
                    });
                    break;
                case 1:  // Download unread
                    brls::sync([this]() {
                        downloadUnreadChapters();
                    });
                    break;
                case 2: {  // Remove all chapters
                    brls::sync([mangaId, chapters]() {
                        // Get download mode setting
                        DownloadMode downloadMode = Application::getInstance().getSettings().downloadMode;
                        brls::Logger::debug("Remove all chapters: downloadMode = {} (0=Server, 1=Local, 2=Both)",
                                           static_cast<int>(downloadMode));

                        // Collect server-downloaded chapter IDs and indexes (from chapter data)
                        std::vector<int> serverChapterIds;
                        std::vector<int> serverChapterIndexes;
                        if (downloadMode == DownloadMode::SERVER_ONLY || downloadMode == DownloadMode::BOTH) {
                            for (const auto& ch : chapters) {
                                if (ch.downloaded) {
                                    serverChapterIds.push_back(ch.id);
                                    serverChapterIndexes.push_back(ch.index);
                                }
                            }
                        }

                        // Collect locally-downloaded chapter indexes from DownloadsManager
                        DownloadsManager& dm = DownloadsManager::getInstance();
                        std::vector<int> localChapterIndexes;
                        if (downloadMode == DownloadMode::LOCAL_ONLY || downloadMode == DownloadMode::BOTH) {
                            for (const auto& ch : chapters) {
                                if (dm.isChapterDownloaded(mangaId, ch.index)) {
                                    localChapterIndexes.push_back(ch.index);
                                }
                            }
                        }

                        if (serverChapterIndexes.empty() && localChapterIndexes.empty()) {
                            brls::Application::notify("No downloads to delete");
                            return;
                        }

                        int totalToDelete = 0;
                        if (downloadMode == DownloadMode::SERVER_ONLY) {
                            totalToDelete = serverChapterIndexes.size();
                        } else if (downloadMode == DownloadMode::LOCAL_ONLY) {
                            totalToDelete = localChapterIndexes.size();
                        } else {
                            // BOTH - count unique chapters (some may be in both)
                            std::set<int> uniqueIndexes(serverChapterIndexes.begin(), serverChapterIndexes.end());
                            uniqueIndexes.insert(localChapterIndexes.begin(), localChapterIndexes.end());
                            totalToDelete = uniqueIndexes.size();
                        }

                        brls::Application::notify("Deleting " + std::to_string(totalToDelete) + " downloads...");

                        // Run delete in async without capturing 'this'
                        asyncRun([downloadMode, mangaId, serverChapterIds, serverChapterIndexes, localChapterIndexes]() {
                            int serverDeletedCount = 0;
                            int localDeletedCount = 0;

                            // Delete from server if applicable
                            if (!serverChapterIds.empty() &&
                                (downloadMode == DownloadMode::SERVER_ONLY || downloadMode == DownloadMode::BOTH)) {
                                SuwayomiClient& client = SuwayomiClient::getInstance();
                                if (client.deleteChapterDownloads(serverChapterIds, mangaId, serverChapterIndexes)) {
                                    serverDeletedCount = serverChapterIds.size();
                                }
                            }

                            // Delete from local if applicable
                            if (!localChapterIndexes.empty() &&
                                (downloadMode == DownloadMode::LOCAL_ONLY || downloadMode == DownloadMode::BOTH)) {
                                DownloadsManager& dm = DownloadsManager::getInstance();
                                for (int chapterIndex : localChapterIndexes) {
                                    if (dm.deleteChapterDownload(mangaId, chapterIndex)) {
                                        localDeletedCount++;
                                    }
                                }
                            }

                            brls::sync([downloadMode, serverDeletedCount, localDeletedCount]() {
                                if (downloadMode == DownloadMode::SERVER_ONLY) {
                                    brls::Application::notify("Deleted " + std::to_string(serverDeletedCount) + " server downloads");
                                } else if (downloadMode == DownloadMode::LOCAL_ONLY) {
                                    brls::Application::notify("Deleted " + std::to_string(localDeletedCount) + " local downloads");
                                } else {
                                    brls::Application::notify("Deleted " + std::to_string(serverDeletedCount) + " server + " +
                                                             std::to_string(localDeletedCount) + " local downloads");
                                }
                            });
                        });
                    });
                    break;
                }
                case 3:  // Cancel downloading chapters
                    brls::sync([this]() {
                        cancelAllDownloading();
                    });
                    break;
                case 4:  // Reset cover
                    brls::sync([this]() {
                        resetCover();
                    });
                    break;
            }
        }, 0);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void MangaDetailView::onChapterSelected(const Chapter& chapter) {
    brls::Logger::debug("MangaDetailView: Selected chapter id={} ({})", chapter.id, chapter.name);

    // Open reader at this chapter - use chapter.id not chapter.index
    Application::getInstance().pushReaderActivity(m_manga.id, chapter.id, m_manga.title);
}

void MangaDetailView::onRead(int chapterId) {
    int startPage = 0;

    if (chapterId == -1) {
        // Continue from last read or start from first chapter
        // Find the chapter with the most recent lastReadAt timestamp
        const Chapter* lastReadChapter = nullptr;
        int64_t mostRecentReadAt = 0;

        for (const auto& ch : m_chapters) {
            if (ch.lastReadAt > mostRecentReadAt) {
                mostRecentReadAt = ch.lastReadAt;
                lastReadChapter = &ch;
            }
        }

        if (lastReadChapter != nullptr) {
            // Found a chapter with reading progress
            chapterId = lastReadChapter->id;
            startPage = lastReadChapter->lastPageRead;

            // If the chapter is fully read, find the next unread chapter
            if (lastReadChapter->read && lastReadChapter->pageCount > 0) {
                // Find the next chapter in reading order (higher chapter number)
                const Chapter* nextUnread = nullptr;
                float lastChapterNum = lastReadChapter->chapterNumber;
                float smallestHigherChapterNum = std::numeric_limits<float>::max();

                for (const auto& ch : m_chapters) {
                    if (!ch.read && ch.chapterNumber > lastChapterNum) {
                        if (ch.chapterNumber < smallestHigherChapterNum) {
                            smallestHigherChapterNum = ch.chapterNumber;
                            nextUnread = &ch;
                        }
                    }
                }

                if (nextUnread != nullptr) {
                    chapterId = nextUnread->id;
                    startPage = 0;  // Start from beginning of new chapter
                    brls::Logger::info("MangaDetailView: Last chapter read, continuing to next unread Ch. {}",
                                      nextUnread->chapterNumber);
                }
                // If no next unread, stay on the last read chapter
            }

            brls::Logger::info("MangaDetailView: Continuing from chapter id={}, page={}", chapterId, startPage);
        } else if (m_manga.lastChapterRead > 0 && !m_chapters.empty()) {
            // Fallback to stored lastChapterRead ID
            chapterId = m_manga.lastChapterRead;

            // Try to find the page for this chapter
            for (const auto& ch : m_chapters) {
                if (ch.id == chapterId) {
                    startPage = ch.lastPageRead;
                    break;
                }
            }
            brls::Logger::info("MangaDetailView: Using stored lastChapterRead id={}, page={}", chapterId, startPage);
        } else if (!m_chapters.empty()) {
            // Start from first chapter (lowest chapter number)
            auto firstChapter = std::min_element(m_chapters.begin(), m_chapters.end(),
                [](const Chapter& a, const Chapter& b) {
                    return a.chapterNumber < b.chapterNumber;
                });
            chapterId = firstChapter->id;
            startPage = 0;
            brls::Logger::info("MangaDetailView: Starting from first chapter id={}", chapterId);
        } else {
            brls::Application::notify("No chapters available");
            return;
        }
    } else {
        // Specific chapter requested, find its lastPageRead
        for (const auto& ch : m_chapters) {
            if (ch.id == chapterId) {
                startPage = ch.lastPageRead;
                break;
            }
        }
    }

    brls::Logger::info("MangaDetailView: Reading chapter id={}, startPage={}", chapterId, startPage);
    Application::getInstance().pushReaderActivityAtPage(m_manga.id, chapterId, startPage, m_manga.title);
}

void MangaDetailView::onAddToLibrary() {
    brls::Logger::info("MangaDetailView: Adding to library with category selection");

    // First add to library, then show category picker
    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        if (!client.addMangaToLibrary(m_manga.id)) {
            brls::sync([]() {
                brls::Application::notify("Failed to add to library");
            });
            return;
        }

        // Fetch categories for selection
        std::vector<Category> categories;
        client.fetchCategories(categories);

        brls::sync([this, categories]() {
            m_manga.inLibrary = true;

            // Track addition for immediate library UI update
            Application::getInstance().trackLibraryAddition(m_manga.id);

            // Hide the button after adding to library and transfer focus to read button
            if (m_libraryButton) {
                m_libraryButton->setVisibility(brls::Visibility::GONE);
                // Update read button DOWN route to skip hidden library button
                if (m_readButton && m_trackingButton) {
                    m_readButton->setCustomNavigationRoute(brls::FocusDirection::DOWN, m_trackingButton);
                }
                // Transfer focus back to read button
                if (m_readButton) {
                    brls::Application::giveFocus(m_readButton);
                }
            }

            // If categories available, show selection dropdown
            if (categories.size() > 1) {
                m_categories = categories;
                std::vector<std::string> options;
                for (const auto& cat : categories) {
                    options.push_back(cat.name);
                }

                brls::Dropdown* dropdown = new brls::Dropdown(
                    "Select Category", options,
                    [this](int selected) {
                        if (selected < 0 || selected >= static_cast<int>(m_categories.size())) return;

                        int categoryId = m_categories[selected].id;
                        brls::sync([this, categoryId]() {
                            asyncRun([this, categoryId]() {
                                SuwayomiClient& client = SuwayomiClient::getInstance();
                                if (client.setMangaCategories(m_manga.id, {categoryId})) {
                                    brls::sync([this]() {
                                        brls::Application::notify("Added to library");
                                    });
                                } else {
                                    brls::sync([]() {
                                        brls::Application::notify("Added to library (category failed)");
                                    });
                                }
                            });
                        });
                    }, 0);
                brls::Application::pushActivity(new brls::Activity(dropdown));
            } else {
                brls::Application::notify("Added to library");
            }
        });
    });
}

void MangaDetailView::onRemoveFromLibrary() {
    brls::Logger::info("MangaDetailView: Removing from library");

    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        if (client.removeMangaFromLibrary(m_manga.id)) {
            brls::sync([this]() {
                m_manga.inLibrary = false;
                if (m_libraryButton) {
                    m_libraryButton->setText("Add to Library");
                }
                brls::Application::notify("Removed from library");
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to remove from library");
            });
        }
    });
}

void MangaDetailView::showDownloadOptions() {
    brls::Dialog* dialog = new brls::Dialog("Download");
    dialog->setCancelable(false);  // Prevent exit dialog from appearing

    dialog->addButton("All", [this, dialog]() {
        dialog->close();
        downloadAllChapters();
    });

    dialog->addButton("Unread", [this, dialog]() {
        dialog->close();
        downloadUnreadChapters();
    });

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->open();
}

void MangaDetailView::downloadAllChapters() {
    brls::Logger::info("MangaDetailView: Downloading all chapters");

    // Get download mode setting
    DownloadMode downloadMode = Application::getInstance().getSettings().downloadMode;
    brls::Logger::info("MangaDetailView: Download mode = {} (0=Server, 1=Local, 2=Both)",
                       static_cast<int>(downloadMode));

    int mangaId = m_manga.id;
    std::string mangaTitle = m_manga.title;

    // Collect chapter data BEFORE async task to avoid accessing view after destruction
    // Also check local download status here since we have access to DownloadsManager
    DownloadsManager& localMgr = DownloadsManager::getInstance();
    localMgr.init();

    std::vector<int> serverChapterIds;
    std::vector<std::pair<int, int>> localChapterPairs;  // (chapterId, chapterIndex)

    for (const auto& ch : m_chapters) {
        bool needsServerDownload = !ch.downloaded;
        bool needsLocalDownload = !localMgr.isChapterDownloaded(mangaId, ch.index);

        if ((downloadMode == DownloadMode::SERVER_ONLY || downloadMode == DownloadMode::BOTH) && needsServerDownload) {
            serverChapterIds.push_back(ch.id);
        }

        if ((downloadMode == DownloadMode::LOCAL_ONLY || downloadMode == DownloadMode::BOTH) && needsLocalDownload) {
            localChapterPairs.push_back({ch.id, ch.index});
        }
    }

    brls::Logger::info("MangaDetailView: Found {} chapters for server, {} for local",
                      serverChapterIds.size(), localChapterPairs.size());

    if (serverChapterIds.empty() && localChapterPairs.empty()) {
        brls::Application::notify("All chapters already downloaded");
        return;
    }

    // Show confirmation for large downloads to prevent accidental mass-downloads
    const size_t LARGE_DOWNLOAD_THRESHOLD = 50;
    size_t totalChapters = serverChapterIds.size() + localChapterPairs.size();
    if (totalChapters > LARGE_DOWNLOAD_THRESHOLD) {
        brls::Dialog* confirmDialog = new brls::Dialog("Confirm Download");

        std::string message = "Are you sure you want to download " + std::to_string(totalChapters) + " chapters?\n\n";
        message += "This may take a while and use significant storage space.";

        brls::Label* label = new brls::Label();
        label->setText(message);
        label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        confirmDialog->addView(label);

        confirmDialog->addButton("Download", [downloadMode, mangaId, mangaTitle, serverChapterIds, localChapterPairs, confirmDialog]() {
            confirmDialog->close();

            // Proceed with download (same code as before but in async)
            asyncRun([downloadMode, mangaId, mangaTitle, serverChapterIds, localChapterPairs]() {
                SuwayomiClient& client = SuwayomiClient::getInstance();
                DownloadsManager& localMgr = DownloadsManager::getInstance();

                bool serverSuccess = true;
                bool localSuccess = true;

                // Queue to server if needed
                if (!serverChapterIds.empty()) {
                    brls::Logger::info("MangaDetailView: Queueing {} to SERVER", serverChapterIds.size());
                    if (client.queueChapterDownloads(serverChapterIds)) {
                        client.startDownloads();
                        brls::Logger::info("MangaDetailView: Server queue SUCCESS");
                    } else {
                        serverSuccess = false;
                        brls::Logger::error("MangaDetailView: Server queue FAILED");
                    }
                }

                // Queue to local if needed
                if (!localChapterPairs.empty()) {
                    brls::Logger::info("MangaDetailView: Queueing {} to LOCAL", localChapterPairs.size());
                    if (localMgr.queueChaptersDownload(mangaId, localChapterPairs, mangaTitle)) {
                        localMgr.startDownloads();
                        brls::Logger::info("MangaDetailView: Local queue SUCCESS");
                    } else {
                        localSuccess = false;
                        brls::Logger::error("MangaDetailView: Local queue FAILED");
                    }
                }

                // Show result notification
                brls::sync([downloadMode, serverSuccess, localSuccess, serverChapterIds, localChapterPairs]() {
                    if (downloadMode == DownloadMode::SERVER_ONLY) {
                        if (serverSuccess) {
                            brls::Application::notify("Queued " + std::to_string(serverChapterIds.size()) + " chapters to server");
                        } else {
                            brls::Application::notify("Failed to queue to server");
                        }
                    } else if (downloadMode == DownloadMode::LOCAL_ONLY) {
                        if (localSuccess) {
                            brls::Application::notify("Queued " + std::to_string(localChapterPairs.size()) + " chapters locally");
                        } else {
                            brls::Application::notify("Failed to queue locally");
                        }
                    } else {
                        std::string msg = "Queued ";
                        if (!serverChapterIds.empty()) msg += std::to_string(serverChapterIds.size()) + " to server";
                        if (!serverChapterIds.empty() && !localChapterPairs.empty()) msg += ", ";
                        if (!localChapterPairs.empty()) msg += std::to_string(localChapterPairs.size()) + " locally";
                        brls::Application::notify(msg);
                    }
                });
            });
        });

        confirmDialog->addButton("Cancel", [confirmDialog]() {
            confirmDialog->close();
        });

        confirmDialog->open();
        return;
    }

    // Don't capture 'this' - use only copied data to avoid crashes if view is destroyed
    asyncRun([downloadMode, mangaId, mangaTitle, serverChapterIds, localChapterPairs]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        DownloadsManager& localMgr = DownloadsManager::getInstance();

        bool serverSuccess = true;
        bool localSuccess = true;

        // Queue to server if needed
        if (!serverChapterIds.empty()) {
            brls::Logger::info("MangaDetailView: Queueing {} to SERVER", serverChapterIds.size());
            if (client.queueChapterDownloads(serverChapterIds)) {
                client.startDownloads();
                brls::Logger::info("MangaDetailView: Server queue SUCCESS");
            } else {
                serverSuccess = false;
                brls::Logger::error("MangaDetailView: Server queue FAILED");
            }
        }

        // Queue to local if needed
        if (!localChapterPairs.empty()) {
            brls::Logger::info("MangaDetailView: Queueing {} to LOCAL", localChapterPairs.size());
            if (localMgr.queueChaptersDownload(mangaId, localChapterPairs, mangaTitle)) {
                localMgr.startDownloads();
                brls::Logger::info("MangaDetailView: Local queue SUCCESS");
            } else {
                localSuccess = false;
                brls::Logger::error("MangaDetailView: Local queue FAILED");
            }
        }

        // Show result notification
        brls::sync([downloadMode, serverSuccess, localSuccess, serverChapterIds, localChapterPairs]() {
            if (downloadMode == DownloadMode::SERVER_ONLY) {
                if (serverSuccess) {
                    brls::Application::notify("Queued " + std::to_string(serverChapterIds.size()) + " chapters to server");
                } else {
                    brls::Application::notify("Failed to queue to server");
                }
            } else if (downloadMode == DownloadMode::LOCAL_ONLY) {
                if (localSuccess) {
                    brls::Application::notify("Queued " + std::to_string(localChapterPairs.size()) + " chapters locally");
                } else {
                    brls::Application::notify("Failed to queue locally");
                }
            } else {
                std::string msg = "Queued ";
                if (!serverChapterIds.empty()) msg += std::to_string(serverChapterIds.size()) + " to server";
                if (!serverChapterIds.empty() && !localChapterPairs.empty()) msg += ", ";
                if (!localChapterPairs.empty()) msg += std::to_string(localChapterPairs.size()) + " locally";
                brls::Application::notify(msg);
            }
        });
    });
}

void MangaDetailView::downloadUnreadChapters() {
    brls::Logger::info("MangaDetailView: Downloading unread chapters");

    // Get download mode setting
    DownloadMode downloadMode = Application::getInstance().getSettings().downloadMode;
    int mangaId = m_manga.id;
    std::string mangaTitle = m_manga.title;

    // Collect chapter data BEFORE async task to avoid accessing view after destruction
    DownloadsManager& localMgr = DownloadsManager::getInstance();
    localMgr.init();

    std::vector<int> serverChapterIds;
    std::vector<std::pair<int, int>> localChapterPairs;

    for (const auto& ch : m_chapters) {
        if (ch.read) continue;  // Skip read chapters

        bool needsServerDownload = !ch.downloaded;
        bool needsLocalDownload = !localMgr.isChapterDownloaded(mangaId, ch.index);

        if ((downloadMode == DownloadMode::SERVER_ONLY || downloadMode == DownloadMode::BOTH) && needsServerDownload) {
            serverChapterIds.push_back(ch.id);
        }

        if ((downloadMode == DownloadMode::LOCAL_ONLY || downloadMode == DownloadMode::BOTH) && needsLocalDownload) {
            localChapterPairs.push_back({ch.id, ch.index});
        }
    }

    brls::Logger::info("MangaDetailView: Found {} unread chapters for server, {} for local",
                      serverChapterIds.size(), localChapterPairs.size());

    if (serverChapterIds.empty() && localChapterPairs.empty()) {
        brls::Application::notify("All unread chapters already downloaded");
        return;
    }

    // Don't capture 'this' - use only copied data to avoid crashes if view is destroyed
    asyncRun([downloadMode, mangaId, mangaTitle, serverChapterIds, localChapterPairs]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        DownloadsManager& localMgr = DownloadsManager::getInstance();

        bool serverSuccess = true;
        bool localSuccess = true;

        if (!serverChapterIds.empty()) {
            if (client.queueChapterDownloads(serverChapterIds)) {
                client.startDownloads();
            } else {
                serverSuccess = false;
            }
        }

        if (!localChapterPairs.empty()) {
            if (localMgr.queueChaptersDownload(mangaId, localChapterPairs, mangaTitle)) {
                localMgr.startDownloads();
            } else {
                localSuccess = false;
            }
        }

        // Show result notification
        brls::sync([downloadMode, serverSuccess, localSuccess, serverChapterIds, localChapterPairs]() {
            if (downloadMode == DownloadMode::SERVER_ONLY) {
                if (serverSuccess && !serverChapterIds.empty()) {
                    brls::Application::notify("Queued " + std::to_string(serverChapterIds.size()) + " unread to server");
                } else if (serverChapterIds.empty()) {
                    brls::Application::notify("All unread already on server");
                } else {
                    brls::Application::notify("Failed to queue to server");
                }
            } else if (downloadMode == DownloadMode::LOCAL_ONLY) {
                if (localSuccess && !localChapterPairs.empty()) {
                    brls::Application::notify("Queued " + std::to_string(localChapterPairs.size()) + " unread locally");
                } else if (localChapterPairs.empty()) {
                    brls::Application::notify("All unread already local");
                } else {
                    brls::Application::notify("Failed to queue locally");
                }
            } else {
                std::string msg = "Queued ";
                if (!serverChapterIds.empty()) msg += std::to_string(serverChapterIds.size()) + " to server";
                if (!serverChapterIds.empty() && !localChapterPairs.empty()) msg += ", ";
                if (!localChapterPairs.empty()) msg += std::to_string(localChapterPairs.size()) + " locally";
                brls::Application::notify(msg);
            }
        });
    });
}

void MangaDetailView::deleteAllDownloads() {
    brls::Logger::info("MangaDetailView: Deleting all downloads");

    // Collect chapter data before async to avoid capturing 'this'
    int mangaId = m_manga.id;
    std::vector<int> chapterIds;
    std::vector<int> chapterIndexes;
    for (const auto& ch : m_chapters) {
        if (ch.downloaded) {
            chapterIds.push_back(ch.id);
            chapterIndexes.push_back(ch.index);
        }
    }

    if (chapterIds.empty()) {
        brls::Application::notify("No downloads to delete");
        return;
    }

    // Don't capture 'this' - use only copied data
    asyncRun([mangaId, chapterIds, chapterIndexes]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        if (client.deleteChapterDownloads(chapterIds, mangaId, chapterIndexes)) {
            brls::sync([]() {
                brls::Application::notify("Downloads deleted");
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to delete downloads");
            });
        }
    });
}

void MangaDetailView::markAllRead() {
    brls::Logger::info("MangaDetailView: Marking all chapters as read");

    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        std::vector<int> chapterIndexes;
        for (const auto& ch : m_chapters) {
            if (!ch.read) {
                chapterIndexes.push_back(ch.index);
            }
        }

        if (client.markChaptersRead(m_manga.id, chapterIndexes)) {
            brls::sync([this]() {
                brls::Application::notify("Marked all as read");
                loadChapters();  // Refresh
            });
        }
    });
}

void MangaDetailView::markAllUnread() {
    brls::Logger::info("MangaDetailView: Marking all chapters as unread");

    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        std::vector<int> chapterIndexes;
        for (const auto& ch : m_chapters) {
            if (ch.read) {
                chapterIndexes.push_back(ch.index);
            }
        }

        if (client.markChaptersUnread(m_manga.id, chapterIndexes)) {
            brls::sync([this]() {
                brls::Application::notify("Marked all as unread");
                loadChapters();  // Refresh
            });
        }
    });
}

void MangaDetailView::onDownloadChapters() {
    showDownloadOptions();
}

void MangaDetailView::onDeleteDownloads() {
    brls::Dialog* dialog = new brls::Dialog("Delete all downloaded chapters?");
    dialog->setCancelable(false);  // Prevent exit dialog from appearing

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Delete", [this, dialog]() {
        dialog->close();
        deleteAllDownloads();
    });

    dialog->open();
}

void MangaDetailView::showChapterMenu(const Chapter& chapter) {
    // Collect data before creating callbacks to avoid unsafe 'this' capture
    int mangaId = m_manga.id;
    int chapterId = chapter.id;
    int chapterIndex = chapter.index;
    bool isRead = chapter.read;
    bool isDownloaded = chapter.downloaded;

    std::vector<std::string> options = {
        "Read",
        isRead ? "Mark Unread" : "Mark Read",
        isDownloaded ? "Delete Download" : "Download"
    };

    brls::Dropdown* dropdown = new brls::Dropdown(
        chapter.name, options,
        [this, chapter, mangaId, chapterIndex, isRead, isDownloaded](int selected) {
            if (selected < 0) return;  // Cancelled

            switch (selected) {
                case 0:  // Read
                    brls::sync([this, chapter]() {
                        onChapterSelected(chapter);
                    });
                    break;
                case 1:  // Mark Read/Unread
                    if (isRead) {
                        asyncRun([mangaId, chapterIndex]() {
                            SuwayomiClient::getInstance().markChapterUnread(mangaId, chapterIndex);
                            brls::sync([]() {
                                brls::Application::notify("Marked as unread");
                            });
                        });
                    } else {
                        asyncRun([mangaId, chapterIndex]() {
                            SuwayomiClient::getInstance().markChapterRead(mangaId, chapterIndex);
                            brls::sync([]() {
                                brls::Application::notify("Marked as read");
                            });
                        });
                    }
                    break;
                case 2:  // Download/Delete
                    if (isDownloaded) {
                        brls::sync([this, chapter]() {
                            deleteChapterDownload(chapter);
                        });
                    } else {
                        brls::sync([this, chapter]() {
                            downloadChapter(chapter);
                        });
                    }
                    break;
            }
        }, 0);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void MangaDetailView::markChapterRead(const Chapter& chapter) {
    // Collect data before async to avoid capturing 'this'
    int mangaId = m_manga.id;
    int chapterIndex = chapter.index;

    // Don't capture 'this' - just mark and notify
    asyncRun([mangaId, chapterIndex]() {
        SuwayomiClient::getInstance().markChapterRead(mangaId, chapterIndex);
        brls::sync([]() {
            brls::Application::notify("Marked as read");
        });
    });
}

void MangaDetailView::downloadChapter(const Chapter& chapter) {
    // Collect chapter data before async to avoid accessing view members
    int mangaId = m_manga.id;
    int chapterId = chapter.id;
    int chapterIndex = chapter.index;
    std::string mangaTitle = m_manga.title;
    std::string chapterName = chapter.name;

    // Get download mode setting
    DownloadMode downloadMode = Application::getInstance().getSettings().downloadMode;

    asyncRun([this, mangaId, chapterId, chapterIndex, mangaTitle, chapterName, downloadMode]() {
        bool serverQueued = false;
        bool localQueued = false;

        // Queue to server if applicable
        if (downloadMode == DownloadMode::SERVER_ONLY || downloadMode == DownloadMode::BOTH) {
            SuwayomiClient& client = SuwayomiClient::getInstance();
            std::vector<int> chapterIds = {chapterId};
            serverQueued = client.queueChapterDownloads(chapterIds);
        }

        // Queue to local if applicable
        if (downloadMode == DownloadMode::LOCAL_ONLY || downloadMode == DownloadMode::BOTH) {
            DownloadsManager& dm = DownloadsManager::getInstance();
            dm.init();
            localQueued = dm.queueChapterDownload(mangaId, chapterId, chapterIndex, mangaTitle, chapterName);
            if (localQueued) {
                dm.startDownloads();
            }
        }

        brls::sync([this, downloadMode, serverQueued, localQueued]() {
            if (serverQueued || localQueued) {
                std::string msg = "Queued";
                if (serverQueued) msg += " server";
                if (serverQueued && localQueued) msg += " +";
                if (localQueued) msg += " local";
                brls::Application::notify(msg);
            } else {
                brls::Application::notify("Failed to queue download");
            }
            updateChapterDownloadStates();
        });
    });
}

void MangaDetailView::deleteChapterDownload(const Chapter& chapter) {
    // Collect data before async to avoid capturing 'this'
    int mangaId = m_manga.id;
    int chapterId = chapter.id;
    int chapterIndex = chapter.index;
    bool serverDownloaded = chapter.downloaded;

    // Get download mode setting
    DownloadMode downloadMode = Application::getInstance().getSettings().downloadMode;

    // Check if locally downloaded
    DownloadsManager& dm = DownloadsManager::getInstance();
    bool localDownloaded = dm.isChapterDownloaded(mangaId, chapterIndex);

    asyncRun([this, mangaId, chapterId, chapterIndex, downloadMode, serverDownloaded, localDownloaded]() {
        int serverDeleted = 0;
        int localDeleted = 0;

        // Delete from server if applicable
        if (serverDownloaded &&
            (downloadMode == DownloadMode::SERVER_ONLY || downloadMode == DownloadMode::BOTH)) {
            SuwayomiClient& client = SuwayomiClient::getInstance();
            std::vector<int> chapterIds = {chapterId};
            std::vector<int> chapterIndexes = {chapterIndex};
            if (client.deleteChapterDownloads(chapterIds, mangaId, chapterIndexes)) {
                serverDeleted = 1;
            }
        }

        // Delete from local if applicable
        if (localDownloaded &&
            (downloadMode == DownloadMode::LOCAL_ONLY || downloadMode == DownloadMode::BOTH)) {
            DownloadsManager& dm = DownloadsManager::getInstance();
            if (dm.deleteChapterDownload(mangaId, chapterIndex)) {
                localDeleted = 1;
            }
        }

        brls::sync([this, downloadMode, serverDeleted, localDeleted]() {
            if (serverDeleted > 0 || localDeleted > 0) {
                std::string msg = "Deleted";
                if (serverDeleted > 0) msg += " server";
                if (serverDeleted > 0 && localDeleted > 0) msg += " +";
                if (localDeleted > 0) msg += " local";
                msg += " download";
                brls::Application::notify(msg);
            } else {
                brls::Application::notify("Failed to delete download");
            }
            updateChapterDownloadStates();
        });
    });
}

void MangaDetailView::showCategoryDialog() {
    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Category> categories;
        client.fetchCategories(categories);

        brls::sync([this, categories]() {
            if (categories.empty()) {
                brls::Application::notify("No categories found");
                return;
            }

            m_categories = categories;
            std::vector<std::string> options;
            for (const auto& cat : categories) {
                options.push_back(cat.name);
            }

            brls::Dropdown* dropdown = new brls::Dropdown(
                "Move to Category", options,
                [this](int selected) {
                    if (selected < 0 || selected >= static_cast<int>(m_categories.size())) return;

                    int categoryId = m_categories[selected].id;
                    brls::sync([this, categoryId]() {
                        asyncRun([this, categoryId]() {
                            SuwayomiClient& client = SuwayomiClient::getInstance();
                            if (client.setMangaCategories(m_manga.id, {categoryId})) {
                                brls::sync([]() {
                                    brls::Application::notify("Category updated");
                                });
                            } else {
                                brls::sync([]() {
                                    brls::Application::notify("Failed to update category");
                                });
                            }
                        });
                    });
                }, 0);
            brls::Application::pushActivity(new brls::Activity(dropdown));
        });
    });
}

void MangaDetailView::showTrackingDialog() {
    brls::Logger::info("MangaDetailView: Opening tracking dialog for manga {}", m_manga.id);

    // Load tracking data asynchronously
    int mangaId = m_manga.id;
    asyncRun([this, mangaId]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        std::vector<Tracker> trackers;
        std::vector<TrackRecord> records;

        bool trackersOk = client.fetchTrackers(trackers);
        bool recordsOk = client.fetchMangaTracking(mangaId, records);

        brls::sync([this, trackers, records, trackersOk, recordsOk]() {
            if (!trackersOk) {
                brls::Application::notify("Failed to load trackers");
                return;
            }

            m_trackers = trackers;
            m_trackRecords = records;

            // Filter to show only logged-in trackers
            std::vector<Tracker> loggedInTrackers;
            for (const auto& t : m_trackers) {
                if (t.isLoggedIn) {
                    loggedInTrackers.push_back(t);
                }
            }

            if (loggedInTrackers.empty()) {
                brls::Application::notify("No trackers logged in. Configure in server settings.");
                return;
            }

            // If only one tracker is logged in, skip directly to search input or edit dialog
            if (loggedInTrackers.size() == 1) {
                const Tracker& tracker = loggedInTrackers[0];
                TrackRecord* existingRecord = nullptr;
                for (auto& r : m_trackRecords) {
                    if (r.trackerId == tracker.id) {
                        existingRecord = &r;
                        break;
                    }
                }

                if (existingRecord && existingRecord->id > 0) {
                    showTrackEditDialog(*existingRecord, tracker);
                } else {
                    showTrackerSearchInputDialog(tracker);
                }
                return;
            }

            // Check if only one tracker has an existing record - skip to edit dialog
            std::vector<std::pair<Tracker, TrackRecord>> trackersWithRecords;
            for (const auto& tracker : loggedInTrackers) {
                for (auto& r : m_trackRecords) {
                    if (r.trackerId == tracker.id && r.id > 0) {
                        trackersWithRecords.push_back({tracker, r});
                        break;
                    }
                }
            }

            if (trackersWithRecords.size() == 1) {
                showTrackEditDialog(trackersWithRecords[0].second, trackersWithRecords[0].first);
                return;
            }

            // Build tracker options for dropdown
            std::vector<std::string> trackerOptions;
            std::vector<Tracker> capturedTrackers;
            std::vector<TrackRecord> capturedRecords;
            std::vector<bool> hasRecords;

            for (const auto& tracker : loggedInTrackers) {
                TrackRecord* existingRecord = nullptr;
                for (auto& r : m_trackRecords) {
                    if (r.trackerId == tracker.id) {
                        existingRecord = &r;
                        break;
                    }
                }

                std::string label = tracker.name;
                if (existingRecord) {
                    if (!existingRecord->title.empty()) {
                        label += " - " + existingRecord->title.substr(0, 20);
                        if (existingRecord->title.length() > 20) label += "...";
                    }
                }

                trackerOptions.push_back(label);
                capturedTrackers.push_back(tracker);
                capturedRecords.push_back(existingRecord ? *existingRecord : TrackRecord());
                hasRecords.push_back(existingRecord != nullptr);
            }

            brls::Dropdown* dropdown = new brls::Dropdown(
                "Tracking", trackerOptions,
                [this, capturedTrackers, capturedRecords, hasRecords](int selected) {
                    if (selected < 0 || selected >= static_cast<int>(capturedTrackers.size())) return;

                    Tracker selectedTracker = capturedTrackers[selected];
                    TrackRecord selectedRecord = capturedRecords[selected];
                    bool hasRecord = hasRecords[selected];

                    brls::sync([this, selectedTracker, selectedRecord, hasRecord]() {
                        if (hasRecord && selectedRecord.id > 0) {
                            showTrackEditDialog(selectedRecord, selectedTracker);
                        } else {
                            showTrackerSearchInputDialog(selectedTracker);
                        }
                    });
                }, 0);
            brls::Application::pushActivity(new brls::Activity(dropdown));
        });
    });
}

void MangaDetailView::showTrackerSearchInputDialog(const Tracker& tracker) {
    brls::Logger::info("MangaDetailView: Opening search input dialog for tracker {}", tracker.name);

    Tracker capturedTracker = tracker;
    std::string defaultQuery = m_manga.title;

    brls::Application::getImeManager()->openForText([this, capturedTracker](std::string text) {
        if (text.empty()) return;
        showTrackerSearchDialog(capturedTracker, text);
    }, "Search " + tracker.name, "Enter manga title to search", 256, defaultQuery);
}

void MangaDetailView::showTrackerSearchDialog(const Tracker& tracker, const std::string& searchQuery) {
    brls::Logger::info("MangaDetailView: Opening search dialog for tracker {}", tracker.name);

    int trackerId = tracker.id;
    int mangaId = m_manga.id;
    std::string trackerName = tracker.name;

    brls::Application::notify("Searching " + trackerName + "...");

    asyncRun([this, trackerId, mangaId, searchQuery, trackerName]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<TrackSearchResult> results;

        if (!client.searchTracker(trackerId, searchQuery, results)) {
            brls::sync([trackerName]() {
                brls::Application::notify("Search failed for " + trackerName);
            });
            return;
        }

        brls::sync([this, results, trackerId, mangaId, trackerName]() {
            if (results.empty()) {
                brls::Application::notify("No results found on " + trackerName);
                return;
            }

            // Push visual tracking search view with cover images and titles
            auto* searchView = new TrackingSearchView(trackerName, trackerId, mangaId, results);

            // Set callback to update tracking button when result is selected
            searchView->setOnResultSelected([this, trackerId, mangaId, trackerName](const TrackSearchResult& result) {
                brls::Application::notify("Adding to " + trackerName + "...");

                int64_t remoteId = result.remoteId;
                asyncRun([this, trackerId, mangaId, remoteId, trackerName]() {
                    SuwayomiClient& client = SuwayomiClient::getInstance();

                    if (client.bindTracker(mangaId, trackerId, remoteId)) {
                        brls::sync([this, trackerName]() {
                            brls::Application::notify("Added to " + trackerName);
                            updateTrackingButtonText();
                            brls::Application::popActivity();
                        });
                    } else {
                        brls::sync([trackerName]() {
                            brls::Application::notify("Failed to add to " + trackerName);
                        });
                    }
                });
            });

            brls::Application::pushActivity(new brls::Activity(searchView));
        });
    });
}

void MangaDetailView::showTrackEditDialog(const TrackRecord& record, const Tracker& tracker) {
    brls::Logger::info("MangaDetailView: Opening edit dialog for track record {}", record.id);
    brls::Logger::info("MangaDetailView: Tracker has {} statuses, {} scores",
                       tracker.statuses.size(), tracker.scores.size());

    int recordId = record.id;
    int currentStatus = record.status;
    double currentChapter = record.lastChapterRead;
    std::string currentScore = record.displayScore;
    std::string trackerName = tracker.name;

    // Get current status text for display
    std::string currentStatusText;
    if (currentStatus >= 0 && currentStatus < static_cast<int>(tracker.statuses.size())) {
        currentStatusText = tracker.statuses[currentStatus];
    } else {
        currentStatusText = "Unknown";
    }

    std::vector<std::string> statuses = tracker.statuses;
    std::vector<std::string> scores = tracker.scores;
    int64_t currentStartDate = record.startDate;
    int64_t currentFinishDate = record.finishDate;

    std::vector<std::string> options = {
        "Status: " + currentStatusText,
        "Chapter: " + std::to_string(static_cast<int>(currentChapter)),
        "Score: " + (currentScore.empty() ? "Not set" : currentScore),
        "Start Date",
        "Finish Date",
        "Remove Tracking"
    };

    brls::Dropdown* dropdown = new brls::Dropdown(
        tracker.name + ": " + record.title, options,
        [this, recordId, statuses, scores, currentChapter, currentScore, trackerName,
         currentStartDate, currentFinishDate](int selected) {
            if (selected < 0) return;  // Cancelled

            switch (selected) {
                case 0: {  // Status
                    brls::sync([this, recordId, statuses, trackerName]() {
                        brls::Dropdown* statusDropdown = new brls::Dropdown(
                            "Select Status", statuses,
                            [this, recordId, trackerName](int sel) {
                                if (sel < 0) return;
                                asyncRun([this, recordId, sel, trackerName]() {
                                    SuwayomiClient& client = SuwayomiClient::getInstance();
                                    if (client.updateTrackRecord(recordId, sel)) {
                                        brls::sync([trackerName]() {
                                            brls::Application::notify("Status updated on " + trackerName);
                                        });
                                    } else {
                                        brls::sync([]() {
                                            brls::Application::notify("Failed to update status");
                                        });
                                    }
                                });
                            }, 0);
                        brls::Application::pushActivity(new brls::Activity(statusDropdown));
                    });
                    break;
                }
                case 1: {  // Chapter
                    brls::sync([this, recordId, currentChapter, trackerName]() {
                        std::string defaultValue = std::to_string(static_cast<int>(currentChapter));
                        brls::Application::getImeManager()->openForText([recordId, trackerName](std::string text) {
                            if (text.empty()) return;
                            try {
                                double newChapter = std::stod(text);
                                if (newChapter < 0) {
                                    brls::Application::notify("Invalid chapter number");
                                    return;
                                }
                                brls::Application::notify("Updating chapter to " + std::to_string(static_cast<int>(newChapter)) + "...");
                                asyncRun([recordId, newChapter, trackerName]() {
                                    SuwayomiClient& client = SuwayomiClient::getInstance();
                                    if (client.updateTrackRecord(recordId, -1, newChapter)) {
                                        brls::sync([newChapter, trackerName]() {
                                            brls::Application::notify("Chapter updated to " + std::to_string(static_cast<int>(newChapter)));
                                        });
                                    } else {
                                        brls::sync([]() {
                                            brls::Application::notify("Failed to update chapter");
                                        });
                                    }
                                });
                            } catch (...) {
                                brls::Application::notify("Invalid chapter number");
                            }
                        }, "Update Chapter", "Enter chapter number", 10, defaultValue);
                    });
                    break;
                }
                case 2: {  // Score
                    brls::sync([this, recordId, scores, currentScore, trackerName]() {
                        std::vector<std::string> scoreOptions = scores;
                        scoreOptions.push_back("Custom...");
                        brls::Dropdown* scoreDropdown = new brls::Dropdown(
                            "Select Score", scoreOptions,
                            [this, recordId, scores, currentScore, trackerName](int sel) {
                                if (sel < 0) return;
                                if (sel == static_cast<int>(scores.size())) {
                                    brls::sync([recordId, currentScore, trackerName]() {
                                        brls::Application::getImeManager()->openForText([recordId, trackerName](std::string text) {
                                            if (text.empty()) return;
                                            brls::Application::notify("Updating score to " + text + "...");
                                            asyncRun([recordId, text, trackerName]() {
                                                SuwayomiClient& client = SuwayomiClient::getInstance();
                                                if (client.updateTrackRecord(recordId, -1, -1, text)) {
                                                    brls::sync([trackerName]() {
                                                        brls::Application::notify("Score updated on " + trackerName);
                                                    });
                                                } else {
                                                    brls::sync([]() {
                                                        brls::Application::notify("Failed to update score");
                                                    });
                                                }
                                            });
                                        }, "Enter Score", "Score (e.g., 8 or 7.5)", 10, currentScore);
                                    });
                                } else {
                                    std::string scoreValue = scores[sel];
                                    asyncRun([recordId, scoreValue, trackerName]() {
                                        SuwayomiClient& client = SuwayomiClient::getInstance();
                                        if (client.updateTrackRecord(recordId, -1, -1, scoreValue)) {
                                            brls::sync([trackerName]() {
                                                brls::Application::notify("Score updated on " + trackerName);
                                            });
                                        } else {
                                            brls::sync([]() {
                                                brls::Application::notify("Failed to update score");
                                            });
                                        }
                                    });
                                }
                            }, 0);
                        brls::Application::pushActivity(new brls::Activity(scoreDropdown));
                    });
                    break;
                }
                case 3: {  // Start Date
                    brls::sync([this, recordId, currentStartDate, trackerName]() {
                        std::vector<std::string> dateOptions = {"Set to Today", "Clear"};
                        brls::Dropdown* dateDropdown = new brls::Dropdown(
                            "Set Start Date", dateOptions,
                            [this, recordId, trackerName](int sel) {
                                if (sel < 0) return;
                                if (sel == 0) {  // Set to Today
                                    int64_t today = static_cast<int64_t>(std::time(nullptr)) * 1000;
                                    asyncRun([recordId, today, trackerName]() {
                                        SuwayomiClient& client = SuwayomiClient::getInstance();
                                        if (client.updateTrackRecord(recordId, -1, -1, "", today, -1)) {
                                            brls::sync([trackerName]() {
                                                brls::Application::notify("Start date set to today");
                                            });
                                        } else {
                                            brls::sync([]() {
                                                brls::Application::notify("Failed to update start date");
                                            });
                                        }
                                    });
                                } else {  // Clear
                                    asyncRun([recordId, trackerName]() {
                                        SuwayomiClient& client = SuwayomiClient::getInstance();
                                        if (client.updateTrackRecord(recordId, -1, -1, "", 0, -1)) {
                                            brls::sync([trackerName]() {
                                                brls::Application::notify("Start date cleared");
                                            });
                                        } else {
                                            brls::sync([]() {
                                                brls::Application::notify("Failed to clear start date");
                                            });
                                        }
                                    });
                                }
                            }, 0);
                        brls::Application::pushActivity(new brls::Activity(dateDropdown));
                    });
                    break;
                }
                case 4: {  // Finish Date
                    brls::sync([this, recordId, currentFinishDate, trackerName]() {
                        std::vector<std::string> dateOptions = {"Set to Today", "Clear"};
                        brls::Dropdown* dateDropdown = new brls::Dropdown(
                            "Set Finish Date", dateOptions,
                            [this, recordId, trackerName](int sel) {
                                if (sel < 0) return;
                                if (sel == 0) {  // Set to Today
                                    int64_t today = static_cast<int64_t>(std::time(nullptr)) * 1000;
                                    asyncRun([recordId, today, trackerName]() {
                                        SuwayomiClient& client = SuwayomiClient::getInstance();
                                        if (client.updateTrackRecord(recordId, -1, -1, "", -1, today)) {
                                            brls::sync([trackerName]() {
                                                brls::Application::notify("Finish date set to today");
                                            });
                                        } else {
                                            brls::sync([]() {
                                                brls::Application::notify("Failed to update finish date");
                                            });
                                        }
                                    });
                                } else {  // Clear
                                    asyncRun([recordId, trackerName]() {
                                        SuwayomiClient& client = SuwayomiClient::getInstance();
                                        if (client.updateTrackRecord(recordId, -1, -1, "", -1, 0)) {
                                            brls::sync([trackerName]() {
                                                brls::Application::notify("Finish date cleared");
                                            });
                                        } else {
                                            brls::sync([]() {
                                                brls::Application::notify("Failed to clear finish date");
                                            });
                                        }
                                    });
                                }
                            }, 0);
                        brls::Application::pushActivity(new brls::Activity(dateDropdown));
                    });
                    break;
                }
                case 5: {  // Remove Tracking
                    brls::sync([this, recordId, trackerName]() {
                        // Show removal options
                        std::vector<std::string> removeOptions = {
                            "Remove from app only",
                            "Remove from " + trackerName + " too"
                        };

                        brls::Dropdown* removeDropdown = new brls::Dropdown(
                            "Remove Tracking",
                            removeOptions,
                            [this, recordId, trackerName](int selected) {
                                if (selected < 0) return;

                                bool deleteRemote = (selected == 1);
                                std::string message = deleteRemote
                                    ? "This will remove tracking from both the app and " + trackerName + "."
                                    : "This will only remove tracking from the app. Your entry on " + trackerName + " will remain.";

                                // Confirmation dialog
                                brls::Dialog* confirmDialog = new brls::Dialog(
                                    "Remove from " + trackerName + "?\n\n" + message);
                                confirmDialog->setCancelable(false);

                                confirmDialog->addButton("Remove", [this, confirmDialog, recordId, trackerName, deleteRemote]() {
                                    confirmDialog->close();

                                    asyncRun([this, recordId, trackerName, deleteRemote]() {
                                        SuwayomiClient& client = SuwayomiClient::getInstance();

                                        if (client.unbindTracker(recordId, deleteRemote)) {
                                            // Remove from local tracking list immediately
                                            auto it = std::find_if(m_trackRecords.begin(), m_trackRecords.end(),
                                                [recordId](const TrackRecord& r) { return r.id == recordId; });
                                            if (it != m_trackRecords.end()) {
                                                brls::sync([this, it]() {
                                                    m_trackRecords.erase(it);
                                                });
                                            }

                                            brls::sync([this, trackerName, deleteRemote]() {
                                                std::string msg = deleteRemote
                                                    ? "Removed from " + trackerName + " and app"
                                                    : "Removed from app (kept on " + trackerName + ")";
                                                brls::Application::notify(msg);
                                                updateTrackingButtonText();
                                            });
                                        } else {
                                            brls::sync([]() {
                                                brls::Application::notify("Failed to remove tracking");
                                            });
                                        }
                                    });
                                });

                                confirmDialog->addButton("Cancel", [confirmDialog]() {
                                    confirmDialog->close();
                                });

                                confirmDialog->open();
                            }, 0);

                        brls::Application::pushActivity(new brls::Activity(removeDropdown));
                    });
                    break;
                }
            }
        }, 0);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void MangaDetailView::showTrackerLoginDialog(const Tracker& tracker) {
    // OAuth-based trackers (MAL, AniList) require browser login
    // which isn't practical on PS Vita. Users should configure
    // tracking in the Suwayomi server's web interface.
    brls::Application::notify("Configure " + tracker.name + " login in server settings");
}

void MangaDetailView::loadTrackingData() {
    int mangaId = m_manga.id;
    asyncRun([this, mangaId]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<TrackRecord> records;

        if (client.fetchMangaTracking(mangaId, records)) {
            brls::sync([this, records]() {
                m_trackRecords = records;
                updateTrackingButtonText();
            });
        }
    });
}

void MangaDetailView::updateTrackingButtonText() {
    if (!m_trackingButton) return;

    // Update button text based on active track records
    if (m_trackRecords.empty()) {
        m_trackingButton->setText("Tracking");
    } else if (m_trackRecords.size() == 1) {
        m_trackingButton->setText("Tracking (1)");
    } else {
        m_trackingButton->setText("Tracking (" + std::to_string(m_trackRecords.size()) + ")");
    }
}

void MangaDetailView::updateTracking() {
    loadTrackingData();
}

void MangaDetailView::updateSortIcon() {
    if (!m_sortIcon) return;

    std::string iconPath = m_sortDescending
        ? "app0:resources/icons/sort-9-1.png"
        : "app0:resources/icons/sort-1-9.png";

    m_sortIcon->setImageFromFile(iconPath);
}

void MangaDetailView::updateReadButtonText() {
    if (!m_readButton || m_chapters.empty()) return;

    // Find the chapter with the most recent lastReadAt timestamp
    const Chapter* lastReadChapter = nullptr;
    int64_t mostRecentReadAt = 0;

    for (const auto& ch : m_chapters) {
        if (ch.lastReadAt > mostRecentReadAt) {
            mostRecentReadAt = ch.lastReadAt;
            lastReadChapter = &ch;
        }
    }

    std::string readText;

    if (lastReadChapter != nullptr) {
        // If the chapter is fully read, check for next unread chapter
        if (lastReadChapter->read) {
            // Find the next unread chapter
            const Chapter* nextUnread = nullptr;
            float lastChapterNum = lastReadChapter->chapterNumber;
            float smallestHigherChapterNum = std::numeric_limits<float>::max();

            for (const auto& ch : m_chapters) {
                if (!ch.read && ch.chapterNumber > lastChapterNum) {
                    if (ch.chapterNumber < smallestHigherChapterNum) {
                        smallestHigherChapterNum = ch.chapterNumber;
                        nextUnread = &ch;
                    }
                }
            }

            if (nextUnread != nullptr) {
                // Format chapter number nicely
                float chNum = nextUnread->chapterNumber;
                if (chNum == static_cast<int>(chNum)) {
                    readText = "Start Ch. " + std::to_string(static_cast<int>(chNum));
                } else {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "Start Ch. %.1f", chNum);
                    readText = buf;
                }
            } else {
                // All chapters read, show re-read
                readText = "Re-read";
            }
        } else {
            // Continue from last read chapter
            float chNum = lastReadChapter->chapterNumber;
            int pageNum = lastReadChapter->lastPageRead + 1;  // Display as 1-indexed

            if (chNum == static_cast<int>(chNum)) {
                readText = "Continue Ch. " + std::to_string(static_cast<int>(chNum));
            } else {
                char buf[32];
                snprintf(buf, sizeof(buf), "Continue Ch. %.1f", chNum);
                readText = buf;
            }

            // Add page info if there's progress
            if (lastReadChapter->lastPageRead > 0 && lastReadChapter->pageCount > 0) {
                readText += " p." + std::to_string(pageNum);
            }
        }
    } else {
        // No reading progress, show "Start Reading"
        readText = "Start Reading";
    }

    m_readButton->setText(readText);
    brls::Logger::debug("MangaDetailView: Updated read button text to '{}'", readText);
}

void MangaDetailView::toggleDescription() {
    if (!m_descriptionLabel || m_fullDescription.empty()) return;

    m_descriptionExpanded = !m_descriptionExpanded;

    if (m_descriptionExpanded) {
        // Show full description
        m_descriptionLabel->setText(m_fullDescription + " [L]");
    } else {
        // Show truncated description (first ~80 chars / 2 lines)
        std::string truncatedDesc = m_fullDescription;
        if (truncatedDesc.length() > 80) {
            truncatedDesc = truncatedDesc.substr(0, 77) + "... [L]";
        }
        m_descriptionLabel->setText(truncatedDesc);
    }
}

void MangaDetailView::cancelAllDownloading() {
    brls::Logger::info("MangaDetailView: Cancelling all downloading chapters");

    // Collect chapter data before async to avoid capturing 'this'
    int mangaId = m_manga.id;
    std::vector<int> chapterIndexes;
    for (const auto& ch : m_chapters) {
        chapterIndexes.push_back(ch.index);
    }

    // Don't capture 'this' - use only copied data
    asyncRun([mangaId, chapterIndexes]() {
        DownloadsManager& dm = DownloadsManager::getInstance();

        int cancelledCount = 0;
        for (int chapterIndex : chapterIndexes) {
            DownloadedChapter* localCh = dm.getChapterDownload(mangaId, chapterIndex);
            if (localCh && (localCh->state == LocalDownloadState::QUEUED ||
                           localCh->state == LocalDownloadState::DOWNLOADING)) {
                if (dm.cancelChapterDownload(mangaId, chapterIndex)) {
                    cancelledCount++;
                }
            }
        }

        // Pause the download manager to stop any active downloads
        dm.pauseDownloads();

        brls::sync([cancelledCount]() {
            if (cancelledCount > 0) {
                brls::Application::notify("Cancelled " + std::to_string(cancelledCount) + " downloads");
            } else {
                brls::Application::notify("No active downloads to cancel");
            }
        });
    });
}

void MangaDetailView::resetCover() {
    brls::Logger::info("MangaDetailView: Resetting cover for manga {}", m_manga.id);

    asyncRun([this]() {
        DownloadsManager& dm = DownloadsManager::getInstance();
        dm.init();

        // Get the manga download item to find and delete local cover
        DownloadItem* mangaDl = dm.getMangaDownload(m_manga.id);
        if (mangaDl && !mangaDl->localCoverPath.empty()) {
            // Delete the local cover file
            brls::Logger::info("MangaDetailView: Deleting local cover at {}", mangaDl->localCoverPath);
            // The cover will be re-downloaded next time it's loaded
            mangaDl->localCoverPath = "";
            dm.saveState();
        }

        // Re-download cover from server
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::string coverUrl = client.getMangaThumbnailUrl(m_manga.id);

        brls::sync([this, coverUrl]() {
            brls::Application::notify("Refreshing cover...");

            // Reload cover image from server
            if (m_coverImage && !coverUrl.empty()) {
                ImageLoader::loadAsync(coverUrl, [](brls::Image* image) {
                    brls::Logger::debug("MangaDetailView: Cover refreshed");
                }, m_coverImage);
            }

            brls::Application::notify("Cover reset");
        });
    });
}

// Chapter Selection Mode implementation
void MangaDetailView::toggleSelectionMode() {
    m_selectionMode = !m_selectionMode;
    m_rangeSelectStart = -1;

    if (!m_selectionMode) {
        clearSelection();
    }

    updateSelectionUI();
    populateChaptersList();

    if (m_selectionMode) {
        brls::Application::notify("Selection mode enabled (L1 for range select)");
    } else {
        brls::Application::notify("Selection mode disabled");
    }
}

void MangaDetailView::toggleChapterSelection(int chapterIndex) {
    if (m_selectedChapters.count(chapterIndex)) {
        m_selectedChapters.erase(chapterIndex);
    } else {
        m_selectedChapters.insert(chapterIndex);
    }
    updateSelectionUI();
}

void MangaDetailView::selectChapterRange(int startIndex, int endIndex) {
    if (startIndex > endIndex) std::swap(startIndex, endIndex);

    for (int i = startIndex; i <= endIndex; i++) {
        m_selectedChapters.insert(i);
    }
    updateSelectionUI();
    brls::Application::notify("Selected chapters " + std::to_string(startIndex + 1) +
                              " to " + std::to_string(endIndex + 1));
}

void MangaDetailView::clearSelection() {
    m_selectedChapters.clear();
    m_rangeSelectStart = -1;
    updateSelectionUI();
}

void MangaDetailView::markSelectedRead() {
    if (m_selectedChapters.empty()) return;

    std::vector<int> chapterIndexes;
    for (int idx : m_selectedChapters) {
        if (idx >= 0 && idx < static_cast<int>(m_chapters.size())) {
            if (!m_chapters[idx].read) {
                chapterIndexes.push_back(m_chapters[idx].index);
            }
        }
    }

    if (chapterIndexes.empty()) {
        brls::Application::notify("Selected chapters already read");
        return;
    }

    int mangaId = m_manga.id;
    asyncRun([this, mangaId, chapterIndexes]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        if (client.markChaptersRead(mangaId, chapterIndexes)) {
            brls::sync([this, count = chapterIndexes.size()]() {
                brls::Application::notify("Marked " + std::to_string(count) + " chapters as read");
                clearSelection();
                loadChapters();
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to mark chapters as read");
            });
        }
    });
}

void MangaDetailView::markSelectedUnread() {
    if (m_selectedChapters.empty()) return;

    std::vector<int> chapterIndexes;
    for (int idx : m_selectedChapters) {
        if (idx >= 0 && idx < static_cast<int>(m_chapters.size())) {
            if (m_chapters[idx].read) {
                chapterIndexes.push_back(m_chapters[idx].index);
            }
        }
    }

    if (chapterIndexes.empty()) {
        brls::Application::notify("Selected chapters already unread");
        return;
    }

    int mangaId = m_manga.id;
    asyncRun([this, mangaId, chapterIndexes]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        if (client.markChaptersUnread(mangaId, chapterIndexes)) {
            brls::sync([this, count = chapterIndexes.size()]() {
                brls::Application::notify("Marked " + std::to_string(count) + " chapters as unread");
                clearSelection();
                loadChapters();
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to mark chapters as unread");
            });
        }
    });
}

void MangaDetailView::downloadSelected() {
    if (m_selectedChapters.empty()) return;

    int mangaId = m_manga.id;
    std::string mangaTitle = m_manga.title;
    std::vector<int> chapterIds;
    std::vector<std::pair<int, int>> localChapterPairs;

    for (int idx : m_selectedChapters) {
        if (idx >= 0 && idx < static_cast<int>(m_chapters.size())) {
            const auto& ch = m_chapters[idx];
            if (!ch.downloaded) {
                chapterIds.push_back(ch.id);
                localChapterPairs.push_back({ch.id, ch.index});
            }
        }
    }

    if (chapterIds.empty()) {
        brls::Application::notify("Selected chapters already downloaded");
        return;
    }

    DownloadMode downloadMode = Application::getInstance().getSettings().downloadMode;

    asyncRun([mangaId, mangaTitle, chapterIds, localChapterPairs, downloadMode]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        DownloadsManager& localMgr = DownloadsManager::getInstance();

        bool serverSuccess = true;
        bool localSuccess = true;

        if (downloadMode == DownloadMode::SERVER_ONLY || downloadMode == DownloadMode::BOTH) {
            if (client.queueChapterDownloads(chapterIds)) {
                client.startDownloads();
            } else {
                serverSuccess = false;
            }
        }

        if (downloadMode == DownloadMode::LOCAL_ONLY || downloadMode == DownloadMode::BOTH) {
            if (localMgr.queueChaptersDownload(mangaId, localChapterPairs, mangaTitle)) {
                localMgr.startDownloads();
            } else {
                localSuccess = false;
            }
        }

        brls::sync([chapterIds, serverSuccess, localSuccess]() {
            if (serverSuccess || localSuccess) {
                brls::Application::notify("Queued " + std::to_string(chapterIds.size()) + " chapters for download");
            } else {
                brls::Application::notify("Failed to queue downloads");
            }
        });
    });

    clearSelection();
}

void MangaDetailView::deleteSelectedDownloads() {
    if (m_selectedChapters.empty()) return;

    int mangaId = m_manga.id;
    std::vector<int> chapterIds;
    std::vector<int> chapterIndexes;

    for (int idx : m_selectedChapters) {
        if (idx >= 0 && idx < static_cast<int>(m_chapters.size())) {
            const auto& ch = m_chapters[idx];
            if (ch.downloaded) {
                chapterIds.push_back(ch.id);
                chapterIndexes.push_back(ch.index);
            }
        }
    }

    if (chapterIds.empty()) {
        brls::Application::notify("No downloaded chapters selected");
        return;
    }

    asyncRun([mangaId, chapterIds, chapterIndexes]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        if (client.deleteChapterDownloads(chapterIds, mangaId, chapterIndexes)) {
            brls::sync([count = chapterIds.size()]() {
                brls::Application::notify("Deleted " + std::to_string(count) + " downloads");
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to delete downloads");
            });
        }
    });

    clearSelection();
}

void MangaDetailView::showSelectionActionMenu() {
    if (m_selectedChapters.empty()) {
        brls::Application::notify("No chapters selected");
        return;
    }

    std::vector<std::string> options = {
        "Mark as Read (" + std::to_string(m_selectedChapters.size()) + ")",
        "Mark as Unread (" + std::to_string(m_selectedChapters.size()) + ")",
        "Download (" + std::to_string(m_selectedChapters.size()) + ")",
        "Delete Downloads",
        "Clear Selection",
        "Exit Selection Mode"
    };

    brls::Dropdown* dropdown = new brls::Dropdown(
        "Batch Actions", options,
        [this](int selected) {
            if (selected < 0) return;

            switch (selected) {
                case 0: markSelectedRead(); break;
                case 1: markSelectedUnread(); break;
                case 2: downloadSelected(); break;
                case 3: deleteSelectedDownloads(); break;
                case 4: clearSelection(); break;
                case 5: toggleSelectionMode(); break;
            }
        }, 0);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void MangaDetailView::updateSelectionUI() {
    // Update selection count label if available
    if (m_selectionCountLabel) {
        if (m_selectionMode && !m_selectedChapters.empty()) {
            m_selectionCountLabel->setText(std::to_string(m_selectedChapters.size()) + " selected");
            m_selectionCountLabel->setVisibility(brls::Visibility::VISIBLE);
        } else {
            m_selectionCountLabel->setVisibility(brls::Visibility::GONE);
        }
    }

    // Update select button text
    if (m_selectBtn) {
        if (m_selectionMode) {
            m_selectBtn->setText("Exit Select");
        } else {
            m_selectBtn->setText("Select");
        }
    }

    // Update chapter list to show checkboxes
    // This is handled in populateChaptersList
}

MangaDetailView::~MangaDetailView() {
    brls::Logger::debug("MangaDetailView: Destroying view for manga {}", m_manga.id);

    // Mark as no longer alive to stop any pending callbacks
    if (m_alive) {
        *m_alive = false;
    }
    m_progressCallbackActive.store(false);

    // Clear any callbacks that might reference this view to prevent crashes
    DownloadsManager& dm = DownloadsManager::getInstance();
    dm.setProgressCallback(nullptr);
    dm.setChapterCompletionCallback(nullptr);
}

} // namespace vitasuwayomi
