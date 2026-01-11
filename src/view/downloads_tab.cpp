/**
 * VitaABS - Downloads Tab Implementation
 */

#include "view/downloads_tab.hpp"
#include "app/downloads_manager.hpp"
#include "activity/player_activity.hpp"
#include "utils/image_loader.hpp"
#include <fstream>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

namespace vitaabs {

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
    m_emptyLabel->setText("No downloads yet.\nUse the download button on media details to save for offline viewing.");
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
        coverImage->setHeight(60);
        coverImage->setCornerRadius(4);
        coverImage->setMargins(0, 15, 0, 0);
        row->addView(coverImage);

        brls::Logger::debug("DownloadsTab: Item '{}' - localCoverPath='{}', coverUrl empty={}",
                           item.title, item.localCoverPath, item.coverUrl.empty() ? "yes" : "no");

        if (!item.localCoverPath.empty()) {
            // Load local cover using Vita-compatible method
            brls::Logger::debug("DownloadsTab: Loading local cover for '{}'", item.title);
            loadLocalCoverImage(coverImage, item.localCoverPath);
        } else if (!item.coverUrl.empty()) {
            // Load from remote URL
            brls::Logger::debug("DownloadsTab: Loading remote cover for '{}'", item.title);
            ImageLoader::loadAsync(item.coverUrl, [](brls::Image* img) {
                // Image loaded callback
            }, coverImage);
        } else {
            brls::Logger::debug("DownloadsTab: No cover available for '{}'", item.title);
        }

        // Title and info
        auto infoBox = new brls::Box();
        infoBox->setAxis(brls::Axis::COLUMN);
        infoBox->setGrow(1.0f);

        auto titleLabel = new brls::Label();
        std::string displayTitle = item.title;
        if (!item.parentTitle.empty()) {
            displayTitle = item.parentTitle + " - " + item.title;
        }
        titleLabel->setText(displayTitle);
        titleLabel->setFontSize(18);
        infoBox->addView(titleLabel);

        // Author name (if available)
        if (!item.authorName.empty()) {
            auto authorLabel = new brls::Label();
            authorLabel->setText(item.authorName);
            authorLabel->setFontSize(14);
            authorLabel->setTextColor(nvgRGBA(180, 180, 180, 255));
            infoBox->addView(authorLabel);
        }

        // Status/progress
        auto statusLabel = new brls::Label();
        statusLabel->setFontSize(14);

        std::string statusText;
        switch (item.state) {
            case DownloadState::QUEUED:
                statusText = "Queued";
                break;
            case DownloadState::DOWNLOADING:
                if (item.totalBytes > 0) {
                    int percent = (int)((item.downloadedBytes * 100) / item.totalBytes);
                    statusText = "Downloading... " + std::to_string(percent) + "%";
                } else {
                    statusText = "Downloading...";
                }
                break;
            case DownloadState::PAUSED:
                statusText = "Paused";
                break;
            case DownloadState::COMPLETED:
                statusText = "Ready to play";
                if (item.currentTime > 0) {
                    int minutes = (int)(item.currentTime / 60.0f);  // currentTime is in seconds
                    statusText += " (" + std::to_string(minutes) + " min watched)";
                }
                break;
            case DownloadState::FAILED:
                statusText = "Download failed";
                break;
        }
        statusLabel->setText(statusText);
        infoBox->addView(statusLabel);

        row->addView(infoBox);

        // Actions based on state
        if (item.state == DownloadState::COMPLETED) {
            auto playBtn = new brls::Button();
            playBtn->setText("Play");
            playBtn->setMargins(0, 0, 0, 10);

            std::string ratingKey = item.itemId;
            std::string localPath = item.localPath;
            playBtn->registerClickAction([ratingKey, localPath](brls::View*) {
                // Play local file
                brls::Application::pushActivity(new PlayerActivity(ratingKey, true));
                return true;
            });
            row->addView(playBtn);

            auto deleteBtn = new brls::Button();
            deleteBtn->setText("Delete");
            std::string key = item.itemId;
            deleteBtn->registerClickAction([key](brls::View*) {
                DownloadsManager::getInstance().deleteDownload(key);
                brls::Application::notify("Download deleted");
                return true;
            });
            row->addView(deleteBtn);
        } else if (item.state == DownloadState::DOWNLOADING || item.state == DownloadState::QUEUED) {
            auto cancelBtn = new brls::Button();
            cancelBtn->setText("Cancel");
            std::string key = item.itemId;
            cancelBtn->registerClickAction([key](brls::View*) {
                DownloadsManager::getInstance().cancelDownload(key);
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
    // Not implemented - download options would be shown from media detail view
}

} // namespace vitaabs
