/**
 * VitaSuwayomi - Downloads Tab Implementation
 * Shows downloaded manga for offline reading
 */

#include "view/downloads_tab.hpp"
#include "app/downloads_manager.hpp"
#include "app/application.hpp"
#include "utils/image_loader.hpp"
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
    header->setMargins(0, 0, 20, 0);
    this->addView(header);

    // Sync button
    auto syncBtn = new brls::Button();
    syncBtn->setText("Sync Progress to Server");
    syncBtn->setMargins(0, 0, 20, 0);
    syncBtn->registerClickAction([](brls::View*) {
        DownloadsManager::getInstance().syncProgressToServer();
        brls::Application::notify("Progress synced to server");
        return true;
    });
    this->addView(syncBtn);

    // List container
    m_listContainer = new brls::Box();
    m_listContainer->setAxis(brls::Axis::COLUMN);
    m_listContainer->setGrow(1.0f);
    this->addView(m_listContainer);

    // Empty label
    m_emptyLabel = new brls::Label();
    m_emptyLabel->setText("No downloads yet.\nUse the download button on manga details to save for offline reading.");
    m_emptyLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_emptyLabel->setVerticalAlign(brls::VerticalAlign::CENTER);
    m_emptyLabel->setGrow(1.0f);
    m_emptyLabel->setVisibility(brls::Visibility::GONE);
    m_listContainer->addView(m_emptyLabel);
}

void DownloadsTab::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);
    refresh();
}

void DownloadsTab::refresh() {
    // Clear existing items (except empty label)
    while (m_listContainer->getChildren().size() > 1) {
        m_listContainer->removeView(m_listContainer->getChildren()[0]);
    }

    // Ensure manager is initialized and state is loaded
    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.init();

    auto downloads = mgr.getDownloads();
    brls::Logger::info("DownloadsTab: Found {} downloads", downloads.size());

    if (downloads.empty()) {
        m_emptyLabel->setVisibility(brls::Visibility::VISIBLE);
        return;
    }

    m_emptyLabel->setVisibility(brls::Visibility::GONE);

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

        // Add row at the beginning (before empty label)
        m_listContainer->addView(row, 0);
    }
}

void DownloadsTab::showDownloadOptions(const std::string& ratingKey, const std::string& title) {
    // Not implemented - download options are shown from manga detail view
}

} // namespace vitasuwayomi
