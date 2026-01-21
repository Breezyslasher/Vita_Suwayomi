/**
 * VitaSuwayomi - Manga Detail View implementation
 * NOBORU-style layout with cover on left, info and chapters on right
 */

#include "view/media_detail_view.hpp"
#include "app/suwayomi_client.hpp"
#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"

namespace vitasuwayomi {

MangaDetailView::MangaDetailView(const Manga& manga)
    : m_manga(manga) {
    brls::Logger::info("MangaDetailView: Creating for '{}' id={}", manga.title, manga.id);

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

    // Register R trigger for sort toggle
    this->registerAction("Sort", brls::ControllerButton::BUTTON_RB, [this](brls::View* view) {
        m_sortDescending = !m_sortDescending;
        updateSortIcon();
        populateChaptersList();
        return true;
    });

    // Register Start button for options menu
    this->registerAction("Options", brls::ControllerButton::BUTTON_START, [this](brls::View* view) {
        showMangaMenu();
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

    // Continue Reading button (Komikku teal accent)
    m_readButton = new brls::Button();
    m_readButton->setWidth(220);
    m_readButton->setHeight(44);
    m_readButton->setMarginBottom(10);
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
    leftPanel->addView(m_readButton);

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

    // Menu button with icon
    auto* menuBtn = new brls::Button();
    menuBtn->setWidth(44);
    menuBtn->setHeight(40);
    menuBtn->setMarginLeft(10);
    menuBtn->setCornerRadius(8);
    menuBtn->setJustifyContent(brls::JustifyContent::CENTER);
    menuBtn->setAlignItems(brls::AlignItems::CENTER);
    menuBtn->setShrink(0.0f);

    auto* menuIcon = new brls::Image();
    menuIcon->setWidth(24);
    menuIcon->setHeight(24);
    menuIcon->setScalingType(brls::ImageScalingType::FIT);
    menuIcon->setImageFromFile("app0:resources/icons/menu.png");
    menuBtn->addView(menuIcon);

    menuBtn->registerClickAction([this](brls::View* view) {
        showMangaMenu();
        return true;
    });
    menuBtn->addGestureRecognizer(new brls::TapGestureRecognizer(menuBtn));
    titleRow->addView(menuBtn);

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

    // Description (truncated)
    if (!m_manga.description.empty()) {
        m_descriptionLabel = new brls::Label();
        std::string desc = m_manga.description;
        if (desc.length() > 300) {
            desc = desc.substr(0, 297) + "...";
        }
        m_descriptionLabel->setText(desc);
        m_descriptionLabel->setFontSize(12);
        m_descriptionLabel->setTextColor(nvgRGB(192, 192, 192));
        m_descriptionLabel->setMarginBottom(15);
        rightPanel->addView(m_descriptionLabel);
    }

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

    auto* chaptersActions = new brls::Box();
    chaptersActions->setAxis(brls::Axis::ROW);

    m_sortBtn = new brls::Button();
    m_sortBtn->setWidth(44);
    m_sortBtn->setHeight(40);
    m_sortBtn->setCornerRadius(8);

    // Add sort icon
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
    chaptersActions->addView(m_sortBtn);

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

void MangaDetailView::loadDetails() {
    brls::Logger::debug("MangaDetailView: Loading details for manga {}", m_manga.id);

    // Load cover
    loadCover();

    // Load chapters
    loadChapters();
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

            brls::sync([this, chapters]() {
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
            });
        } else {
            brls::Logger::error("MangaDetailView: Failed to fetch chapters");
        }
    });
}

void MangaDetailView::populateChaptersList() {
    if (!m_chaptersBox) return;

    m_chaptersBox->clearViews();

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

    // Create chapter cells (Komikku-style: rounded, clean design)
    for (const auto& chapter : sortedChapters) {
        if (m_filterDownloaded && !chapter.downloaded) continue;
        if (m_filterUnread && chapter.read) continue;

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

        // Show download progress if downloading
        if (isLocallyDownloading && localCh) {
            int percent = 0;
            if (localCh->pageCount > 0) {
                percent = (localCh->downloadedPages * 100) / localCh->pageCount;
            }
            dlBtn->setText(std::to_string(percent) + "%");
            dlBtn->setBackgroundColor(nvgRGBA(52, 152, 219, 200));  // Blue
        } else if (isLocallyDownloaded) {
            // Completed - show checkmark icon
            auto* icon = new brls::Image();
            icon->setWidth(20);
            icon->setHeight(20);
            icon->setScalingType(brls::ImageScalingType::FIT);
            icon->setImageFromFile("app0:resources/icons/checkbox_checked.png");
            dlBtn->addView(icon);
            dlBtn->setBackgroundColor(nvgRGBA(46, 204, 113, 200));  // Green
            dlBtn->registerClickAction([this, capturedChapter](brls::View* view) {
                deleteChapterDownload(capturedChapter);
                return true;
            });
        } else if (isLocallyQueued) {
            // Queued - show refresh/waiting icon
            auto* icon = new brls::Image();
            icon->setWidth(20);
            icon->setHeight(20);
            icon->setScalingType(brls::ImageScalingType::FIT);
            icon->setImageFromFile("app0:resources/icons/refresh.png");
            dlBtn->addView(icon);
            dlBtn->setBackgroundColor(nvgRGBA(241, 196, 15, 200));  // Yellow
        } else if (isLocallyFailed) {
            // Failed - show cross icon
            auto* icon = new brls::Image();
            icon->setWidth(20);
            icon->setHeight(20);
            icon->setScalingType(brls::ImageScalingType::FIT);
            icon->setImageFromFile("app0:resources/icons/cross.png");
            dlBtn->addView(icon);
            dlBtn->setBackgroundColor(nvgRGBA(231, 76, 60, 200));  // Red
            dlBtn->registerClickAction([this, capturedChapter](brls::View* view) {
                downloadChapter(capturedChapter);  // Retry
                return true;
            });
        } else {
            // Not downloaded - show download icon
            auto* icon = new brls::Image();
            icon->setWidth(20);
            icon->setHeight(20);
            icon->setScalingType(brls::ImageScalingType::FIT);
            icon->setImageFromFile("app0:resources/icons/download.png");
            dlBtn->addView(icon);
            dlBtn->setBackgroundColor(nvgRGBA(60, 60, 60, 200));
            dlBtn->registerClickAction([this, capturedChapter](brls::View* view) {
                downloadChapter(capturedChapter);
                return true;
            });
        }
        dlBtn->addGestureRecognizer(new brls::TapGestureRecognizer(dlBtn));
        statusBox->addView(dlBtn);

        chapterRow->addView(statusBox);

        // Click action - open chapter
        chapterRow->registerClickAction([this, capturedChapter](brls::View* view) {
            onChapterSelected(capturedChapter);
            return true;
        });

        // Square button to download/delete chapter when row is focused
        bool localDownloaded = isLocallyDownloaded;
        chapterRow->registerAction("Download", brls::ControllerButton::BUTTON_Y, [this, capturedChapter, localDownloaded](brls::View* view) {
            if (localDownloaded) {
                deleteChapterDownload(capturedChapter);
            } else {
                downloadChapter(capturedChapter);
            }
            return true;
        });

        // Touch support
        chapterRow->addGestureRecognizer(new brls::TapGestureRecognizer(chapterRow));

        m_chaptersBox->addView(chapterRow);
    }

    brls::Logger::debug("MangaDetailView: Populated {} chapters", sortedChapters.size());
}

void MangaDetailView::showMangaMenu() {
    brls::Dialog* dialog = new brls::Dialog("Options");

    dialog->addButton("Download all chapters", [this, dialog]() {
        dialog->close();
        downloadAllChapters();
    });

    dialog->addButton("Remove all chapters", [this, dialog]() {
        dialog->close();
        onDeleteDownloads();
    });

    dialog->addButton("Cancel downloading chapters", [this, dialog]() {
        dialog->close();
        cancelAllDownloading();
    });

    dialog->addButton("Reset cover", [this, dialog]() {
        dialog->close();
        resetCover();
    });

    dialog->open();
}

void MangaDetailView::onChapterSelected(const Chapter& chapter) {
    brls::Logger::debug("MangaDetailView: Selected chapter id={} ({})", chapter.id, chapter.name);

    // Open reader at this chapter - use chapter.id not chapter.index
    Application::getInstance().pushReaderActivity(m_manga.id, chapter.id, m_manga.title);
}

void MangaDetailView::onRead(int chapterId) {
    if (chapterId == -1) {
        // Continue from last read or start from first chapter
        if (m_manga.lastChapterRead > 0 && !m_chapters.empty()) {
            // lastChapterRead is a chapter ID
            chapterId = m_manga.lastChapterRead;
        } else if (!m_chapters.empty()) {
            // Start from first chapter (lowest chapter number)
            auto firstChapter = std::min_element(m_chapters.begin(), m_chapters.end(),
                [](const Chapter& a, const Chapter& b) {
                    return a.chapterNumber < b.chapterNumber;
                });
            chapterId = firstChapter->id;
        } else {
            brls::Application::notify("No chapters available");
            return;
        }
    }

    brls::Logger::info("MangaDetailView: Reading chapter id={}", chapterId);
    Application::getInstance().pushReaderActivity(m_manga.id, chapterId, m_manga.title);
}

void MangaDetailView::onAddToLibrary() {
    brls::Logger::info("MangaDetailView: Adding to library");

    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        if (client.addMangaToLibrary(m_manga.id)) {
            brls::sync([this]() {
                m_manga.inLibrary = true;
                // Hide the button after adding to library
                if (m_libraryButton) {
                    m_libraryButton->setVisibility(brls::Visibility::GONE);
                }
                brls::Application::notify("Added to library");
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to add to library");
            });
        }
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
    brls::Application::notify("Queueing all chapters for download...");

    // Get download mode setting
    DownloadMode downloadMode = Application::getInstance().getSettings().downloadMode;
    brls::Logger::info("MangaDetailView: Download mode = {} (0=Server, 1=Local, 2=Both)",
                       static_cast<int>(downloadMode));

    int mangaId = m_manga.id;
    std::string mangaTitle = m_manga.title;

    asyncRun([this, downloadMode, mangaId, mangaTitle]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        DownloadsManager& localMgr = DownloadsManager::getInstance();
        localMgr.init();

        brls::Logger::info("MangaDetailView: In async, download mode = {}", static_cast<int>(downloadMode));

        // Collect chapters based on download mode
        std::vector<int> serverChapterIds;      // Chapters to download to server
        std::vector<int> localChapterIds;       // Chapters to download locally
        std::vector<std::pair<int, int>> localChapterPairs;  // (chapterId, chapterIndex)

        for (const auto& ch : m_chapters) {
            bool needsServerDownload = !ch.downloaded;  // Not on server
            bool needsLocalDownload = !localMgr.isChapterDownloaded(mangaId, ch.index);  // Not local

            // For server downloads
            if ((downloadMode == DownloadMode::SERVER_ONLY || downloadMode == DownloadMode::BOTH) && needsServerDownload) {
                serverChapterIds.push_back(ch.id);
            }

            // For local downloads - download if not already local (regardless of server status)
            if ((downloadMode == DownloadMode::LOCAL_ONLY || downloadMode == DownloadMode::BOTH) && needsLocalDownload) {
                localChapterIds.push_back(ch.id);
                localChapterPairs.push_back({ch.id, ch.index});
            }
        }

        brls::Logger::info("MangaDetailView: Found {} chapters for server, {} for local",
                          serverChapterIds.size(), localChapterIds.size());

        bool serverSuccess = true;
        bool localSuccess = true;
        int totalQueued = 0;

        // Queue to server if needed
        if (!serverChapterIds.empty()) {
            brls::Logger::info("MangaDetailView: Queueing {} to SERVER", serverChapterIds.size());
            if (client.queueChapterDownloads(serverChapterIds)) {
                client.startDownloads();
                brls::Logger::info("MangaDetailView: Server queue SUCCESS");
                totalQueued += serverChapterIds.size();
            } else {
                serverSuccess = false;
                brls::Logger::error("MangaDetailView: Server queue FAILED");
            }
        }

        // Queue to local if needed
        if (!localChapterPairs.empty()) {
            brls::Logger::info("MangaDetailView: Queueing {} to LOCAL", localChapterPairs.size());
            if (localMgr.queueChaptersDownload(mangaId, localChapterPairs, mangaTitle)) {
                // Set up completion callback to refresh UI when each chapter finishes
                localMgr.setChapterCompletionCallback([this, mangaId](int completedMangaId, int chapterIndex, bool success) {
                    if (completedMangaId == mangaId) {
                        brls::sync([this]() {
                            populateChaptersList();  // Refresh to show completed status
                        });
                    }
                });

                localMgr.startDownloads();
                brls::Logger::info("MangaDetailView: Local queue SUCCESS");
                if (downloadMode == DownloadMode::LOCAL_ONLY) {
                    totalQueued = localChapterPairs.size();
                }
            } else {
                localSuccess = false;
                brls::Logger::error("MangaDetailView: Local queue FAILED");
            }
        }

        // Handle case where nothing to download
        if (serverChapterIds.empty() && localChapterPairs.empty()) {
            brls::sync([]() {
                brls::Application::notify("All chapters already downloaded");
            });
            return;
        }

        brls::sync([this, downloadMode, serverSuccess, localSuccess, serverChapterIds, localChapterPairs]() {
            if (downloadMode == DownloadMode::SERVER_ONLY) {
                if (serverSuccess && !serverChapterIds.empty()) {
                    brls::Application::notify("Queued " + std::to_string(serverChapterIds.size()) + " chapters to server");
                } else if (serverChapterIds.empty()) {
                    brls::Application::notify("All chapters already on server");
                } else {
                    brls::Application::notify("Failed to queue to server");
                }
            } else if (downloadMode == DownloadMode::LOCAL_ONLY) {
                if (localSuccess && !localChapterPairs.empty()) {
                    brls::Application::notify("Queued " + std::to_string(localChapterPairs.size()) + " chapters locally");
                } else if (localChapterPairs.empty()) {
                    brls::Application::notify("All chapters already downloaded locally");
                } else {
                    brls::Application::notify("Failed to queue locally");
                }
            } else {  // BOTH
                std::string msg = "Queued ";
                if (!serverChapterIds.empty()) msg += std::to_string(serverChapterIds.size()) + " to server";
                if (!serverChapterIds.empty() && !localChapterPairs.empty()) msg += ", ";
                if (!localChapterPairs.empty()) msg += std::to_string(localChapterPairs.size()) + " locally";
                brls::Application::notify(msg);
            }
            // Refresh chapter list to show queued status
            populateChaptersList();
        });
    });
}

void MangaDetailView::downloadUnreadChapters() {
    brls::Logger::info("MangaDetailView: Downloading unread chapters");
    brls::Application::notify("Queueing unread chapters for download...");

    // Get download mode setting
    DownloadMode downloadMode = Application::getInstance().getSettings().downloadMode;
    int mangaId = m_manga.id;
    std::string mangaTitle = m_manga.title;

    asyncRun([this, downloadMode, mangaId, mangaTitle]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        DownloadsManager& localMgr = DownloadsManager::getInstance();
        localMgr.init();

        // Collect unread chapters based on download mode
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
                // Set up completion callback to refresh UI when each chapter finishes
                localMgr.setChapterCompletionCallback([this, mangaId](int completedMangaId, int chapterIndex, bool success) {
                    if (completedMangaId == mangaId) {
                        brls::sync([this]() {
                            populateChaptersList();  // Refresh to show completed status
                        });
                    }
                });

                localMgr.startDownloads();
            } else {
                localSuccess = false;
            }
        }

        if (serverChapterIds.empty() && localChapterPairs.empty()) {
            brls::sync([]() {
                brls::Application::notify("All unread chapters already downloaded");
            });
            return;
        }

        brls::sync([this, downloadMode, serverSuccess, localSuccess, serverChapterIds, localChapterPairs]() {
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
            // Refresh chapter list to show queued status
            populateChaptersList();
        });
    });
}

void MangaDetailView::deleteAllDownloads() {
    brls::Logger::info("MangaDetailView: Deleting all downloads");

    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        std::vector<int> chapterIndexes;
        for (const auto& ch : m_chapters) {
            if (ch.downloaded) {
                chapterIndexes.push_back(ch.index);
            }
        }

        if (client.deleteChapterDownloads(m_manga.id, chapterIndexes)) {
            brls::sync([this]() {
                brls::Application::notify("Downloads deleted");
                loadChapters();  // Refresh chapter list
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
    brls::Dialog* dialog = new brls::Dialog(chapter.name);

    dialog->addButton("Read", [this, chapter, dialog]() {
        dialog->close();
        onChapterSelected(chapter);
    });

    if (chapter.read) {
        dialog->addButton("Mark Unread", [this, chapter, dialog]() {
            dialog->close();
            asyncRun([this, chapter]() {
                SuwayomiClient::getInstance().markChapterUnread(m_manga.id, chapter.index);
                brls::sync([this]() { loadChapters(); });
            });
        });
    } else {
        dialog->addButton("Mark Read", [this, chapter, dialog]() {
            dialog->close();
            asyncRun([this, chapter]() {
                SuwayomiClient::getInstance().markChapterRead(m_manga.id, chapter.index);
                brls::sync([this]() { loadChapters(); });
            });
        });
    }

    if (chapter.downloaded) {
        dialog->addButton("Delete Download", [this, chapter, dialog]() {
            dialog->close();
            deleteChapterDownload(chapter);
        });
    } else {
        dialog->addButton("Download", [this, chapter, dialog]() {
            dialog->close();
            downloadChapter(chapter);
        });
    }

    dialog->addButton("Close", [dialog]() {
        dialog->close();
    });

    dialog->open();
}

void MangaDetailView::markChapterRead(const Chapter& chapter) {
    asyncRun([this, chapter]() {
        SuwayomiClient::getInstance().markChapterRead(m_manga.id, chapter.index);
        brls::sync([this]() { loadChapters(); });
    });
}

void MangaDetailView::downloadChapter(const Chapter& chapter) {
    brls::Application::notify("Downloading to Vita...");

    int mangaId = m_manga.id;
    asyncRun([this, chapter, mangaId]() {
        DownloadsManager& dm = DownloadsManager::getInstance();
        dm.init();

        // Queue chapter for local download (using chapter.id for API calls)
        if (dm.queueChapterDownload(m_manga.id, chapter.id, chapter.index, m_manga.title, chapter.name)) {
            // Set progress callback for UI updates
            dm.setProgressCallback([](int downloaded, int total) {
                brls::sync([downloaded, total]() {
                    std::string msg = std::to_string(downloaded) + "/" + std::to_string(total) + " pages";
                    brls::Application::notify(msg);
                });
            });

            // Set up completion callback to refresh UI when chapter finishes
            dm.setChapterCompletionCallback([this, mangaId](int completedMangaId, int chapterIndex, bool success) {
                if (completedMangaId == mangaId) {
                    brls::sync([this]() {
                        populateChaptersList();  // Refresh to show completed status
                    });
                }
            });

            // Start downloading
            dm.startDownloads();

            brls::sync([this]() {
                brls::Application::notify("Download started");
                populateChaptersList();  // Refresh to show queued status
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to queue download");
            });
        }
    });
}

void MangaDetailView::deleteChapterDownload(const Chapter& chapter) {
    asyncRun([this, chapter]() {
        DownloadsManager& dm = DownloadsManager::getInstance();

        if (dm.deleteChapterDownload(m_manga.id, chapter.index)) {
            brls::sync([this]() {
                brls::Application::notify("Local download deleted");
                loadChapters();
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to delete download");
            });
        }
    });
}

void MangaDetailView::showCategoryDialog() {
    brls::Application::notify("Category management not yet implemented");
}

void MangaDetailView::showTrackingDialog() {
    brls::Application::notify("Tracking not yet implemented");
}

void MangaDetailView::updateTracking() {
    // TODO: Implement tracking update
}

void MangaDetailView::updateSortIcon() {
    if (!m_sortIcon) return;

    std::string iconPath = m_sortDescending
        ? "app0:resources/icons/sort-9-1.png"
        : "app0:resources/icons/sort-1-9.png";

    m_sortIcon->setImageFromFile(iconPath);
}

void MangaDetailView::cancelAllDownloading() {
    brls::Logger::info("MangaDetailView: Cancelling all downloading chapters");

    asyncRun([this]() {
        DownloadsManager& dm = DownloadsManager::getInstance();

        int cancelledCount = 0;
        for (const auto& ch : m_chapters) {
            DownloadedChapter* localCh = dm.getChapterDownload(m_manga.id, ch.index);
            if (localCh && (localCh->state == LocalDownloadState::QUEUED ||
                           localCh->state == LocalDownloadState::DOWNLOADING)) {
                if (dm.cancelChapterDownload(m_manga.id, ch.index)) {
                    cancelledCount++;
                }
            }
        }

        // Pause the download manager to stop any active downloads
        dm.pauseDownloads();

        brls::sync([this, cancelledCount]() {
            if (cancelledCount > 0) {
                brls::Application::notify("Cancelled " + std::to_string(cancelledCount) + " downloads");
            } else {
                brls::Application::notify("No active downloads to cancel");
            }
            loadChapters();  // Refresh chapter list
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

} // namespace vitasuwayomi
