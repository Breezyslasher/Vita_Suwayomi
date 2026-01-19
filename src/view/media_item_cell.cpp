/**
 * VitaSuwayomi - Manga Item Cell implementation
 * Komikku-style card with cover image, title overlay, and badges
 */

#include "view/media_item_cell.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "utils/image_loader.hpp"
#include <fstream>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

namespace vitasuwayomi {

// Helper to load local cover image on Vita
static void loadLocalCoverToImage(brls::Image* image, const std::string& localPath) {
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

MangaItemCell::MangaItemCell() {
    // Komikku-style: card with cover filling the space, title at bottom
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setFocusable(true);
    this->setCornerRadius(8);
    this->setBackgroundColor(nvgRGBA(30, 30, 30, 255));
    this->setClipsToBounds(true);

    // Cover image - fills the card
    m_thumbnailImage = new brls::Image();
    m_thumbnailImage->setSize(brls::Size(140, 180));
    m_thumbnailImage->setScalingType(brls::ImageScalingType::FILL);
    m_thumbnailImage->setCornerRadius(8);
    this->addView(m_thumbnailImage);

    // Bottom overlay for title (gradient effect simulated with solid color)
    auto* titleOverlay = new brls::Box();
    titleOverlay->setAxis(brls::Axis::COLUMN);
    titleOverlay->setJustifyContent(brls::JustifyContent::FLEX_END);
    titleOverlay->setAlignItems(brls::AlignItems::STRETCH);
    titleOverlay->setBackgroundColor(nvgRGBA(0, 0, 0, 180));
    titleOverlay->setPadding(6, 6, 4, 6);
    titleOverlay->setPositionType(brls::PositionType::ABSOLUTE);
    titleOverlay->setPositionBottom(0);
    titleOverlay->setWidth(140);

    // Title label - at bottom of card
    m_titleLabel = new brls::Label();
    m_titleLabel->setFontSize(11);
    m_titleLabel->setTextColor(nvgRGB(255, 255, 255));
    m_titleLabel->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    titleOverlay->addView(m_titleLabel);

    // Subtitle (author) - smaller, below title
    m_subtitleLabel = new brls::Label();
    m_subtitleLabel->setFontSize(9);
    m_subtitleLabel->setTextColor(nvgRGB(180, 180, 180));
    m_subtitleLabel->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    titleOverlay->addView(m_subtitleLabel);

    this->addView(titleOverlay);

    // Unread badge - top right corner
    m_unreadBadge = new brls::Label();
    m_unreadBadge->setFontSize(10);
    m_unreadBadge->setTextColor(nvgRGB(255, 255, 255));
    m_unreadBadge->setBackgroundColor(nvgRGBA(0, 150, 136, 255)); // Teal badge
    m_unreadBadge->setMargins(6, 6, 0, 0);
    m_unreadBadge->setPositionType(brls::PositionType::ABSOLUTE);
    m_unreadBadge->setPositionTop(0);
    m_unreadBadge->setPositionRight(0);
    m_unreadBadge->setVisibility(brls::Visibility::GONE);
    this->addView(m_unreadBadge);

    // Download badge - top left corner (shows checkmark if downloaded)
    m_downloadBadge = new brls::Label();
    m_downloadBadge->setFontSize(12);
    m_downloadBadge->setTextColor(nvgRGB(255, 255, 255));
    m_downloadBadge->setBackgroundColor(nvgRGBA(76, 175, 80, 255)); // Green badge
    m_downloadBadge->setMargins(6, 0, 0, 6);
    m_downloadBadge->setPositionType(brls::PositionType::ABSOLUTE);
    m_downloadBadge->setPositionTop(0);
    m_downloadBadge->setPositionLeft(0);
    m_downloadBadge->setVisibility(brls::Visibility::GONE);
    this->addView(m_downloadBadge);

    // Description label (hidden, for focus state)
    m_descriptionLabel = new brls::Label();
    m_descriptionLabel->setFontSize(9);
    m_descriptionLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_descriptionLabel->setVisibility(brls::Visibility::GONE);
}

void MangaItemCell::setManga(const Manga& manga) {
    m_manga = manga;

    // Set title - Komikku style, left-aligned
    if (m_titleLabel) {
        std::string title = manga.title;
        // Truncate long titles for card display
        if (title.length() > 16) {
            title = title.substr(0, 14) + "..";
        }
        m_originalTitle = title;
        m_titleLabel->setText(title);
    }

    // Set subtitle (author or chapter count)
    if (m_subtitleLabel) {
        if (!manga.author.empty()) {
            std::string author = manga.author;
            if (author.length() > 18) {
                author = author.substr(0, 16) + "..";
            }
            m_subtitleLabel->setText(author);
        } else if (manga.chapterCount > 0) {
            m_subtitleLabel->setText(std::to_string(manga.chapterCount) + " ch");
        } else {
            m_subtitleLabel->setText("");
        }
    }

    // Show unread badge (teal, top-right)
    if (m_unreadBadge) {
        if (manga.unreadCount > 0) {
            m_unreadBadge->setText(std::to_string(manga.unreadCount));
            m_unreadBadge->setVisibility(brls::Visibility::VISIBLE);
        } else {
            m_unreadBadge->setVisibility(brls::Visibility::GONE);
        }
    }

    // Show download badge if any chapters are downloaded locally
    if (m_downloadBadge) {
        static const std::string ICON_CHECK = "\xE2\x9C\x94";  // ✔ U+2714 heavy check mark
        DownloadsManager& dm = DownloadsManager::getInstance();
        DownloadItem* download = dm.getMangaDownload(manga.id);
        if (download != nullptr) {
            m_downloadBadge->setText(ICON_CHECK);
            m_downloadBadge->setVisibility(brls::Visibility::VISIBLE);
        } else {
            m_downloadBadge->setVisibility(brls::Visibility::GONE);
        }
    }

    // Load thumbnail
    loadThumbnail();
}

void MangaItemCell::loadThumbnail() {
    if (!m_thumbnailImage) return;

    brls::Logger::debug("MangaItemCell::loadThumbnail for '{}' id={} thumbnailUrl='{}'",
                       m_manga.title, m_manga.id, m_manga.thumbnailUrl);

    // Check if we have a Vita local cover path (for downloaded items)
    // Vita local paths start with "ux0:"
    if (!m_manga.thumbnailUrl.empty() && m_manga.thumbnailUrl.find("ux0:") == 0) {
        // Local Vita path - load directly from file
        brls::Logger::debug("MangaItemCell: Loading local cover from {}", m_manga.thumbnailUrl);
        loadLocalCoverToImage(m_thumbnailImage, m_manga.thumbnailUrl);
        return;
    }

    // Need valid manga ID to fetch thumbnail
    if (m_manga.id <= 0) {
        brls::Logger::warning("MangaItemCell: Invalid manga ID for '{}'", m_manga.title);
        return;
    }

    SuwayomiClient& client = SuwayomiClient::getInstance();
    std::string url;

    // Use thumbnailUrl from GraphQL if available (prepend server URL if relative)
    if (!m_manga.thumbnailUrl.empty()) {
        if (m_manga.thumbnailUrl[0] == '/') {
            // Relative URL - prepend server URL
            url = client.getServerUrl();
            // Remove trailing slash
            while (!url.empty() && url.back() == '/') url.pop_back();
            url += m_manga.thumbnailUrl;
        } else if (m_manga.thumbnailUrl.find("http") == 0) {
            // Absolute URL - use directly
            url = m_manga.thumbnailUrl;
        } else {
            // Unknown format - use REST endpoint
            url = client.getMangaThumbnailUrl(m_manga.id);
        }
    } else {
        // No thumbnailUrl - use REST endpoint
        url = client.getMangaThumbnailUrl(m_manga.id);
    }

    brls::Logger::debug("MangaItemCell: Loading cover from URL: {}", url);

    ImageLoader::loadAsync(url, [](brls::Image* img) {
        brls::Logger::debug("MangaItemCell: Cover loaded successfully");
    }, m_thumbnailImage);
}

brls::View* MangaItemCell::create() {
    return new MangaItemCell();
}

void MangaItemCell::onFocusGained() {
    brls::Box::onFocusGained();
    updateFocusInfo(true);
}

void MangaItemCell::onFocusLost() {
    brls::Box::onFocusLost();
    updateFocusInfo(false);
}

void MangaItemCell::updateFocusInfo(bool focused) {
    if (!m_titleLabel || !m_descriptionLabel) return;

    if (focused) {
        // Show full title
        m_titleLabel->setText(m_manga.title);

        // Show additional info
        std::string info;
        if (!m_manga.author.empty()) {
            info = m_manga.author;
        }
        if (m_manga.chapterCount > 0) {
            if (!info.empty()) info += " - ";
            info += std::to_string(m_manga.chapterCount) + " chapters";
        }
        std::string statusStr = m_manga.getStatusString();
        if (!statusStr.empty() && statusStr != "Unknown") {
            if (!info.empty()) info += " - ";
            info += statusStr;
        }
        if (!info.empty()) {
            m_descriptionLabel->setText(info);
            m_descriptionLabel->setVisibility(brls::Visibility::VISIBLE);
        }
    } else {
        // Restore truncated title
        m_titleLabel->setText(m_originalTitle);
        m_descriptionLabel->setVisibility(brls::Visibility::GONE);
    }
}

} // namespace vitasuwayomi
