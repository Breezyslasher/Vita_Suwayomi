/**
 * VitaSuwayomi - Downloads Tab Implementation
 * Shows download queue and downloaded manga for offline reading
 */

#include "view/downloads_tab.hpp"
#include "app/downloads_manager.hpp"
#include "app/suwayomi_client.hpp"
#include "app/application.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"
#include <fstream>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

namespace vitasuwayomi {

// Helper to load local cover image on Vita
static void loadLocalCoverImage(brls::Image* image, const std::string& localPath) {
    if (localPath.empty() || !image) return;

#ifdef __vita__
    SceUID fd = sceIoOpen(localPath.c_str(), SCE_O_RDONLY, 0);
    if (fd >= 0) {
        SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoLseek(fd, 0, SCE_SEEK_SET);

        if (size > 0 && size < 10 * 1024 * 1024) {  // Max 10MB
            std::vector<uint8_t> data(size);
            if (sceIoRead(fd, data.data(), size) == size) {
                image->setImageFromMem(data.data(), data.size());
            }
        }
        sceIoClose(fd);
    }
#else
    std::ifstream file(localPath, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (size > 0 && size < 10 * 1024 * 1024) {
            std::vector<uint8_t> data(size);
            if (file.read(reinterpret_cast<char*>(data.data()), size)) {
                image->setImageFromMem(data.data(), data.size());
            }
        }
        file.close();
    }
#endif
}

DownloadsTab::DownloadsTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Header
    auto header = new brls::Label();
    header->setText("Downloads");
    header->setFontSize(24);
    header->setMargins(0, 0, 15, 0);
    this->addView(header);

    // === Download Queue Section (server downloads) ===
    m_queueSection = new brls::Box();
    m_queueSection->setAxis(brls::Axis::COLUMN);
    m_queueSection->setMargins(0, 0, 15, 0);
    m_queueSection->setVisibility(brls::Visibility::GONE);  // Hidden by default
    this->addView(m_queueSection);

    m_queueHeader = new brls::Label();
    m_queueHeader->setText("Download Queue");
    m_queueHeader->setFontSize(18);
    m_queueHeader->setMargins(0, 0, 10, 0);
    m_queueSection->addView(m_queueHeader);

    // Scrollable queue container
    m_queueScroll = new brls::ScrollingFrame();
    m_queueScroll->setMaxHeight(200);  // Limit height to make it scrollable
    m_queueSection->addView(m_queueScroll);

    m_queueContainer = new brls::Box();
    m_queueContainer->setAxis(brls::Axis::COLUMN);
    m_queueScroll->setContentView(m_queueContainer);

    // Queue empty label (not used when section is hidden, but kept for error states)
    m_queueEmptyLabel = new brls::Label();
    m_queueEmptyLabel->setText("No downloads in queue");
    m_queueEmptyLabel->setFontSize(14);
    m_queueEmptyLabel->setTextColor(nvgRGBA(150, 150, 150, 255));
    m_queueEmptyLabel->setMargins(0, 0, 10, 0);
    m_queueEmptyLabel->setVisibility(brls::Visibility::GONE);

    // === Local Downloads Section ===
    m_localSection = new brls::Box();
    m_localSection->setAxis(brls::Axis::COLUMN);
    m_localSection->setGrow(1.0f);
    m_localSection->setVisibility(brls::Visibility::GONE);  // Hidden by default
    this->addView(m_localSection);

    m_localHeader = new brls::Label();
    m_localHeader->setText("Downloaded Manga");
    m_localHeader->setFontSize(18);
    m_localHeader->setMargins(0, 0, 10, 0);
    m_localSection->addView(m_localHeader);

    // Scrollable local container
    m_localScroll = new brls::ScrollingFrame();
    m_localScroll->setGrow(1.0f);
    m_localSection->addView(m_localScroll);

    m_localContainer = new brls::Box();
    m_localContainer->setAxis(brls::Axis::COLUMN);
    m_localScroll->setContentView(m_localContainer);

    // Local empty label (not used when section is hidden)
    m_localEmptyLabel = new brls::Label();
    m_localEmptyLabel->setText("No downloaded manga.\nUse the download option on manga details to save for offline reading.");
    m_localEmptyLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_localEmptyLabel->setFontSize(14);
    m_localEmptyLabel->setTextColor(nvgRGBA(150, 150, 150, 255));
    m_localEmptyLabel->setVisibility(brls::Visibility::GONE);
}

void DownloadsTab::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);
    refresh();
}

void DownloadsTab::refresh() {
    refreshQueue();
    refreshLocalDownloads();
}

void DownloadsTab::refreshQueue() {
    // Clear existing queue items
    while (m_queueContainer->getChildren().size() > 0) {
        m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
    }

    // Fetch download queue from server
    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<DownloadQueueItem> queue;

        if (!client.fetchDownloadQueue(queue)) {
            brls::sync([this]() {
                // Hide section on fetch failure (no active downloads to show)
                m_queueSection->setVisibility(brls::Visibility::GONE);
            });
            return;
        }

        brls::sync([this, queue]() {
            // Clear existing items again (in case of race)
            while (m_queueContainer->getChildren().size() > 0) {
                m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
            }

            // Hide section if queue is empty
            if (queue.empty()) {
                m_queueSection->setVisibility(brls::Visibility::GONE);
                return;
            }

            // Show section when there are items
            m_queueSection->setVisibility(brls::Visibility::VISIBLE);

            for (const auto& item : queue) {
                auto row = new brls::Box();
                row->setAxis(brls::Axis::ROW);
                row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
                row->setAlignItems(brls::AlignItems::CENTER);
                row->setPadding(8);
                row->setMargins(0, 0, 8, 0);
                row->setCornerRadius(6);

                // Background color based on state
                if (item.state == DownloadState::DOWNLOADING) {
                    row->setBackgroundColor(nvgRGBA(30, 60, 30, 200));  // Green tint for active
                } else if (item.state == DownloadState::ERROR) {
                    row->setBackgroundColor(nvgRGBA(60, 30, 30, 200));  // Red tint for error
                } else {
                    row->setBackgroundColor(nvgRGBA(40, 40, 40, 200));
                }

                // Info box
                auto infoBox = new brls::Box();
                infoBox->setAxis(brls::Axis::COLUMN);
                infoBox->setGrow(1.0f);

                // Manga title
                auto titleLabel = new brls::Label();
                titleLabel->setText(item.mangaTitle);
                titleLabel->setFontSize(16);
                infoBox->addView(titleLabel);

                // Chapter name
                auto chapterLabel = new brls::Label();
                std::string chapterText = item.chapterName;
                if (item.chapterNumber > 0) {
                    chapterText = "Ch. " + std::to_string(static_cast<int>(item.chapterNumber));
                    if (!item.chapterName.empty()) {
                        chapterText += " - " + item.chapterName;
                    }
                }
                chapterLabel->setText(chapterText);
                chapterLabel->setFontSize(14);
                chapterLabel->setTextColor(nvgRGBA(180, 180, 180, 255));
                infoBox->addView(chapterLabel);

                row->addView(infoBox);

                // Progress label (x/x pages)
                auto progressLabel = new brls::Label();
                progressLabel->setFontSize(16);
                progressLabel->setMargins(0, 0, 0, 10);

                std::string progressText;
                if (item.state == DownloadState::DOWNLOADING) {
                    progressText = std::to_string(item.downloadedPages) + "/" +
                                   std::to_string(item.pageCount) + " pages";
                } else if (item.state == DownloadState::QUEUED) {
                    progressText = "Queued";
                } else if (item.state == DownloadState::DOWNLOADED) {
                    progressText = "Done";
                } else if (item.state == DownloadState::ERROR) {
                    progressText = "Error";
                } else {
                    progressText = std::to_string(item.downloadedPages) + "/" +
                                   std::to_string(item.pageCount);
                }
                progressLabel->setText(progressText);

                // Color based on state
                if (item.state == DownloadState::DOWNLOADING) {
                    progressLabel->setTextColor(nvgRGBA(100, 200, 100, 255));  // Green
                } else if (item.state == DownloadState::ERROR) {
                    progressLabel->setTextColor(nvgRGBA(200, 100, 100, 255));  // Red
                }

                row->addView(progressLabel);

                // Add row
                m_queueContainer->addView(row);
            }
        });
    });
}

void DownloadsTab::refreshLocalDownloads() {
    // Clear existing local items
    while (m_localContainer->getChildren().size() > 0) {
        m_localContainer->removeView(m_localContainer->getChildren()[0]);
    }

    // Ensure manager is initialized and state is loaded
    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.init();

    auto downloads = mgr.getDownloads();
    brls::Logger::info("DownloadsTab: Found {} local downloads", downloads.size());

    // Hide section if no local downloads
    if (downloads.empty()) {
        m_localSection->setVisibility(brls::Visibility::GONE);
        return;
    }

    // Show section when there are items
    m_localSection->setVisibility(brls::Visibility::VISIBLE);

    for (const auto& item : downloads) {
        auto row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setPadding(10);
        row->setMargins(0, 0, 10, 0);
        row->setBackgroundColor(nvgRGBA(40, 40, 40, 200));
        row->setCornerRadius(8);

        // Cover image (try local first, then remote URL)
        auto coverImage = new brls::Image();
        coverImage->setWidth(60);
        coverImage->setHeight(80);
        coverImage->setCornerRadius(4);
        coverImage->setMargins(0, 15, 0, 0);
        row->addView(coverImage);

        if (!item.localCoverPath.empty()) {
            loadLocalCoverImage(coverImage, item.localCoverPath);
        } else if (!item.coverUrl.empty()) {
            ImageLoader::loadAsync(item.coverUrl, [](brls::Image* img) {
                // Image loaded callback
            }, coverImage);
        }

        // Title and info
        auto infoBox = new brls::Box();
        infoBox->setAxis(brls::Axis::COLUMN);
        infoBox->setGrow(1.0f);

        auto titleLabel = new brls::Label();
        titleLabel->setText(item.title);
        titleLabel->setFontSize(18);
        infoBox->addView(titleLabel);

        // Author name (if available)
        if (!item.author.empty()) {
            auto authorLabel = new brls::Label();
            authorLabel->setText(item.author);
            authorLabel->setFontSize(14);
            authorLabel->setTextColor(nvgRGBA(180, 180, 180, 255));
            infoBox->addView(authorLabel);
        }

        // Status/progress
        auto statusLabel = new brls::Label();
        statusLabel->setFontSize(14);

        std::string statusText;
        switch (item.state) {
            case LocalDownloadState::QUEUED:
                statusText = "Queued";
                break;
            case LocalDownloadState::DOWNLOADING:
                if (item.totalChapters > 0) {
                    statusText = "Downloading... " + std::to_string(item.completedChapters) +
                                 "/" + std::to_string(item.totalChapters) + " chapters";
                } else {
                    statusText = "Downloading...";
                }
                break;
            case LocalDownloadState::PAUSED:
                statusText = "Paused";
                break;
            case LocalDownloadState::COMPLETED:
                statusText = std::to_string(item.completedChapters) + " chapters downloaded";
                if (item.lastChapterRead > 0) {
                    statusText += " (reading ch. " + std::to_string(item.lastChapterRead) + ")";
                }
                break;
            case LocalDownloadState::FAILED:
                statusText = "Download failed";
                break;
        }
        statusLabel->setText(statusText);
        infoBox->addView(statusLabel);

        row->addView(infoBox);

        // Actions based on state
        if (item.state == LocalDownloadState::COMPLETED || item.completedChapters > 0) {
            auto readBtn = new brls::Button();
            readBtn->setText("Read");
            readBtn->setMargins(0, 0, 0, 10);

            int mangaId = item.mangaId;
            int lastChapter = item.lastChapterRead > 0 ? item.lastChapterRead : 1;
            std::string mangaTitle = item.title;
            readBtn->registerClickAction([mangaId, lastChapter, mangaTitle](brls::View*) {
                // Open reader at last read chapter
                Application::getInstance().pushReaderActivity(mangaId, lastChapter, mangaTitle);
                return true;
            });
            row->addView(readBtn);

            auto deleteBtn = new brls::Button();
            deleteBtn->setText("Delete");
            int deleteId = item.mangaId;
            deleteBtn->registerClickAction([deleteId](brls::View*) {
                DownloadsManager::getInstance().deleteMangaDownload(deleteId);
                brls::Application::notify("Download deleted");
                return true;
            });
            row->addView(deleteBtn);
        } else if (item.state == LocalDownloadState::DOWNLOADING ||
                   item.state == LocalDownloadState::QUEUED) {
            auto cancelBtn = new brls::Button();
            cancelBtn->setText("Cancel");
            int cancelId = item.mangaId;
            cancelBtn->registerClickAction([cancelId](brls::View*) {
                DownloadsManager::getInstance().cancelDownload(cancelId);
                brls::Application::notify("Download cancelled");
                return true;
            });
            row->addView(cancelBtn);
        }

        // Add row
        m_localContainer->addView(row);
    }
}

void DownloadsTab::showDownloadOptions(const std::string& ratingKey, const std::string& title) {
    // Not implemented - download options are shown from manga detail view
}

} // namespace vitasuwayomi
