/**
 * VitaSuwayomi - Manga Detail View implementation
 * Shows detailed information about a manga including chapters list
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

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);

    // Register back button
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    }, false, false, brls::Sound::SOUND_BACK);

    // Create scrollable content
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_mainContent = new brls::Box();
    m_mainContent->setAxis(brls::Axis::COLUMN);
    m_mainContent->setPadding(30);

    // Top row - cover and info
    auto* topRow = new brls::Box();
    topRow->setAxis(brls::Axis::ROW);
    topRow->setJustifyContent(brls::JustifyContent::FLEX_START);
    topRow->setAlignItems(brls::AlignItems::FLEX_START);
    topRow->setMarginBottom(20);

    // Left side - cover image
    auto* leftBox = new brls::Box();
    leftBox->setAxis(brls::Axis::COLUMN);
    leftBox->setWidth(200);
    leftBox->setMarginRight(20);

    m_coverImage = new brls::Image();
    m_coverImage->setWidth(180);
    m_coverImage->setHeight(260);
    m_coverImage->setScalingType(brls::ImageScalingType::FIT);
    leftBox->addView(m_coverImage);

    // Action buttons
    auto* buttonRow = new brls::Box();
    buttonRow->setAxis(brls::Axis::ROW);
    buttonRow->setMarginTop(15);
    buttonRow->setJustifyContent(brls::JustifyContent::FLEX_START);

    m_readButton = new brls::Button();
    m_readButton->setText("Read");
    m_readButton->setWidth(80);
    m_readButton->setHeight(40);
    m_readButton->setMarginRight(10);
    m_readButton->registerClickAction([this](brls::View* view) {
        onRead();
        return true;
    });
    buttonRow->addView(m_readButton);

    m_libraryButton = new brls::Button();
    m_libraryButton->setText(m_manga.inLibrary ? "Remove" : "Add");
    m_libraryButton->setWidth(80);
    m_libraryButton->setHeight(40);
    m_libraryButton->setMarginRight(10);
    m_libraryButton->registerClickAction([this](brls::View* view) {
        if (m_manga.inLibrary) {
            onRemoveFromLibrary();
        } else {
            onAddToLibrary();
        }
        return true;
    });
    buttonRow->addView(m_libraryButton);

    leftBox->addView(buttonRow);

    // Download button row
    auto* dlButtonRow = new brls::Box();
    dlButtonRow->setAxis(brls::Axis::ROW);
    dlButtonRow->setMarginTop(10);
    dlButtonRow->setJustifyContent(brls::JustifyContent::FLEX_START);

    m_downloadButton = new brls::Button();
    m_downloadButton->setText("Download");
    m_downloadButton->setWidth(100);
    m_downloadButton->setHeight(40);
    m_downloadButton->registerClickAction([this](brls::View* view) {
        showDownloadOptions();
        return true;
    });
    dlButtonRow->addView(m_downloadButton);

    leftBox->addView(dlButtonRow);
    topRow->addView(leftBox);

    // Right side - details
    auto* rightBox = new brls::Box();
    rightBox->setAxis(brls::Axis::COLUMN);
    rightBox->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(m_manga.title);
    m_titleLabel->setFontSize(26);
    m_titleLabel->setMarginBottom(10);
    rightBox->addView(m_titleLabel);

    // Author
    if (!m_manga.author.empty()) {
        m_authorLabel = new brls::Label();
        m_authorLabel->setText("Author: " + m_manga.author);
        m_authorLabel->setFontSize(18);
        m_authorLabel->setMarginBottom(5);
        rightBox->addView(m_authorLabel);
    }

    // Artist
    if (!m_manga.artist.empty() && m_manga.artist != m_manga.author) {
        m_artistLabel = new brls::Label();
        m_artistLabel->setText("Artist: " + m_manga.artist);
        m_artistLabel->setFontSize(18);
        m_artistLabel->setMarginBottom(5);
        rightBox->addView(m_artistLabel);
    }

    // Status
    m_statusLabel = new brls::Label();
    m_statusLabel->setText("Status: " + m_manga.getStatusString());
    m_statusLabel->setFontSize(16);
    m_statusLabel->setMarginBottom(5);
    rightBox->addView(m_statusLabel);

    // Source
    if (!m_manga.sourceName.empty()) {
        m_sourceLabel = new brls::Label();
        m_sourceLabel->setText("Source: " + m_manga.sourceName);
        m_sourceLabel->setFontSize(16);
        m_sourceLabel->setMarginBottom(10);
        rightBox->addView(m_sourceLabel);
    }

    // Chapter count and unread
    m_chapterCountLabel = new brls::Label();
    std::string chapterInfo = std::to_string(m_manga.chapterCount) + " chapters";
    if (m_manga.unreadCount > 0) {
        chapterInfo += " (" + std::to_string(m_manga.unreadCount) + " unread)";
    }
    m_chapterCountLabel->setText(chapterInfo);
    m_chapterCountLabel->setFontSize(16);
    m_chapterCountLabel->setMarginBottom(10);
    rightBox->addView(m_chapterCountLabel);

    // Genre tags
    if (!m_manga.genre.empty()) {
        m_genreBox = new brls::Box();
        m_genreBox->setAxis(brls::Axis::ROW);
        m_genreBox->setMarginBottom(10);

        for (size_t i = 0; i < m_manga.genre.size() && i < 5; i++) {
            auto* genreLabel = new brls::Label();
            genreLabel->setText(m_manga.genre[i]);
            genreLabel->setFontSize(14);
            genreLabel->setMarginRight(10);
            m_genreBox->addView(genreLabel);
        }
        rightBox->addView(m_genreBox);
    }

    // Description
    if (!m_manga.description.empty()) {
        m_descriptionLabel = new brls::Label();
        // Truncate long descriptions
        std::string desc = m_manga.description;
        if (desc.length() > 500) {
            desc = desc.substr(0, 497) + "...";
        }
        m_descriptionLabel->setText(desc);
        m_descriptionLabel->setFontSize(14);
        m_descriptionLabel->setMarginTop(10);
        rightBox->addView(m_descriptionLabel);
    }

    topRow->addView(rightBox);
    m_mainContent->addView(topRow);

    // Chapters section header
    auto* chaptersHeaderRow = new brls::Box();
    chaptersHeaderRow->setAxis(brls::Axis::ROW);
    chaptersHeaderRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    chaptersHeaderRow->setAlignItems(brls::AlignItems::CENTER);
    chaptersHeaderRow->setMarginBottom(10);

    m_chaptersLabel = new brls::Label();
    m_chaptersLabel->setText("Chapters");
    m_chaptersLabel->setFontSize(20);
    chaptersHeaderRow->addView(m_chaptersLabel);

    // Sort/filter buttons
    auto* chapterActionsBox = new brls::Box();
    chapterActionsBox->setAxis(brls::Axis::ROW);

    m_sortBtn = new brls::Button();
    m_sortBtn->setText("Sort");
    m_sortBtn->setWidth(60);
    m_sortBtn->setHeight(30);
    m_sortBtn->setMarginRight(10);
    m_sortBtn->registerClickAction([this](brls::View* view) {
        m_sortDescending = !m_sortDescending;
        populateChaptersList();
        return true;
    });
    chapterActionsBox->addView(m_sortBtn);

    chaptersHeaderRow->addView(chapterActionsBox);
    m_mainContent->addView(chaptersHeaderRow);

    // Chapters list
    m_chaptersScroll = new brls::ScrollingFrame();
    m_chaptersScroll->setHeight(300);
    m_chaptersScroll->setMarginBottom(20);

    m_chaptersBox = new brls::Box();
    m_chaptersBox->setAxis(brls::Axis::COLUMN);

    m_chaptersScroll->setContentView(m_chaptersBox);
    m_mainContent->addView(m_chaptersScroll);

    m_scrollView->setContentView(m_mainContent);
    this->addView(m_scrollView);

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

    // Apply filters
    for (const auto& chapter : sortedChapters) {
        if (m_filterDownloaded && !chapter.downloaded) continue;
        if (m_filterUnread && chapter.read) continue;

        auto* chapterCell = new brls::DetailCell();

        std::string title = chapter.name;
        if (title.empty()) {
            title = "Chapter " + std::to_string(static_cast<int>(chapter.chapterNumber));
        }

        // Add read indicator
        if (chapter.read) {
            title = "[Read] " + title;
        }

        // Add download indicator
        if (chapter.downloaded) {
            title = title + " [DL]";
        }

        chapterCell->setText(title);

        // Show scanlator as detail if available
        if (!chapter.scanlator.empty()) {
            chapterCell->setDetailText(chapter.scanlator);
        }

        Chapter capturedChapter = chapter;
        chapterCell->registerClickAction([this, capturedChapter](brls::View* view) {
            onChapterSelected(capturedChapter);
            return true;
        });

        m_chaptersBox->addView(chapterCell);
    }

    brls::Logger::debug("MangaDetailView: Populated {} chapters", sortedChapters.size());
}

void MangaDetailView::onChapterSelected(const Chapter& chapter) {
    brls::Logger::debug("MangaDetailView: Selected chapter {} ({})", chapter.index, chapter.name);

    // Open reader at this chapter
    Application::getInstance().pushReaderActivity(m_manga.id, chapter.index, m_manga.title);
}

void MangaDetailView::onRead(int chapterIndex) {
    if (chapterIndex == -1) {
        // Continue from last read
        if (m_manga.lastChapterRead > 0) {
            chapterIndex = m_manga.lastChapterRead;
        } else {
            // Start from first chapter
            chapterIndex = 1;
        }
    }

    brls::Logger::info("MangaDetailView: Reading chapter {}", chapterIndex);
    Application::getInstance().pushReaderActivity(m_manga.id, chapterIndex, m_manga.title);
}

void MangaDetailView::onAddToLibrary() {
    brls::Logger::info("MangaDetailView: Adding to library");

    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        if (client.addMangaToLibrary(m_manga.id)) {
            brls::sync([this]() {
                m_manga.inLibrary = true;
                if (m_libraryButton) {
                    m_libraryButton->setText("Remove");
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
                    m_libraryButton->setText("Add");
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
    brls::Dialog* dialog = new brls::Dialog("Download Options");

    dialog->addButton("Download All", [this, dialog]() {
        dialog->close();
        downloadAllChapters();
    });

    dialog->addButton("Download Unread", [this, dialog]() {
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

    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        std::vector<int> chapterIndexes;
        for (const auto& ch : m_chapters) {
            if (!ch.downloaded) {
                chapterIndexes.push_back(ch.index);
            }
        }

        if (client.queueChapterDownloads(m_manga.id, chapterIndexes)) {
            client.startDownloads();
            brls::sync([chapterIndexes]() {
                brls::Application::notify("Queued " + std::to_string(chapterIndexes.size()) + " chapters");
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to queue downloads");
            });
        }
    });
}

void MangaDetailView::downloadUnreadChapters() {
    brls::Logger::info("MangaDetailView: Downloading unread chapters");
    brls::Application::notify("Queueing unread chapters for download...");

    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        std::vector<int> chapterIndexes;
        for (const auto& ch : m_chapters) {
            if (!ch.downloaded && !ch.read) {
                chapterIndexes.push_back(ch.index);
            }
        }

        if (client.queueChapterDownloads(m_manga.id, chapterIndexes)) {
            client.startDownloads();
            brls::sync([chapterIndexes]() {
                brls::Application::notify("Queued " + std::to_string(chapterIndexes.size()) + " chapters");
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to queue downloads");
            });
        }
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
    brls::Application::notify("Downloading chapter...");

    asyncRun([this, chapter]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        if (client.queueChapterDownload(m_manga.id, chapter.index)) {
            client.startDownloads();
            brls::sync([this]() {
                brls::Application::notify("Download started");
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to start download");
            });
        }
    });
}

void MangaDetailView::deleteChapterDownload(const Chapter& chapter) {
    asyncRun([this, chapter]() {
        if (SuwayomiClient::getInstance().deleteChapterDownload(m_manga.id, chapter.index)) {
            brls::sync([this]() {
                brls::Application::notify("Download deleted");
                loadChapters();
            });
        }
    });
}

void MangaDetailView::showCategoryDialog() {
    // TODO: Implement category selection
    brls::Application::notify("Category management not yet implemented");
}

void MangaDetailView::showTrackingDialog() {
    // TODO: Implement tracking
    brls::Application::notify("Tracking not yet implemented");
}

void MangaDetailView::updateTracking() {
    // TODO: Implement tracking update
}

} // namespace vitasuwayomi
