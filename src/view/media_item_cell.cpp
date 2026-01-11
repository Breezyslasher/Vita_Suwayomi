/**
 * VitaSuwayomi - Manga Item Cell implementation
 */

#include "view/media_item_cell.hpp"
#include "app/suwayomi_client.hpp"
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
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(5);
    this->setFocusable(true);
    this->setCornerRadius(8);
    this->setBackgroundColor(nvgRGBA(50, 50, 50, 255));

    // Thumbnail image - manga covers are typically taller than wide
    m_thumbnailImage = new brls::Image();
    m_thumbnailImage->setWidth(100);
    m_thumbnailImage->setHeight(140);
    m_thumbnailImage->setScalingType(brls::ImageScalingType::FIT);
    m_thumbnailImage->setCornerRadius(4);
    this->addView(m_thumbnailImage);

    // Title label
    m_titleLabel = new brls::Label();
    m_titleLabel->setFontSize(12);
    m_titleLabel->setMarginTop(5);
    m_titleLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    this->addView(m_titleLabel);

    // Subtitle label (author or chapter count)
    m_subtitleLabel = new brls::Label();
    m_subtitleLabel->setFontSize(10);
    m_subtitleLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_subtitleLabel->setTextColor(nvgRGB(160, 160, 160));
    this->addView(m_subtitleLabel);

    // Description label (shows on focus)
    m_descriptionLabel = new brls::Label();
    m_descriptionLabel->setFontSize(9);
    m_descriptionLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_descriptionLabel->setVisibility(brls::Visibility::GONE);
    this->addView(m_descriptionLabel);

    // Unread badge
    m_unreadBadge = new brls::Label();
    m_unreadBadge->setFontSize(10);
    m_unreadBadge->setTextColor(nvgRGB(255, 255, 255));
    m_unreadBadge->setVisibility(brls::Visibility::GONE);
    this->addView(m_unreadBadge);
}

void MangaItemCell::setManga(const Manga& manga) {
    m_manga = manga;

    // Set title
    if (m_titleLabel) {
        std::string title = manga.title;
        // Truncate long titles
        if (title.length() > 18) {
            title = title.substr(0, 16) + "...";
        }
        m_originalTitle = title;  // Store truncated title for focus restore
        m_titleLabel->setText(title);
    }

    // Set subtitle (author or chapter count)
    if (m_subtitleLabel) {
        if (!manga.author.empty()) {
            std::string author = manga.author;
            if (author.length() > 20) {
                author = author.substr(0, 18) + "...";
            }
            m_subtitleLabel->setText(author);
        } else if (manga.chapterCount > 0) {
            m_subtitleLabel->setText(std::to_string(manga.chapterCount) + " chapters");
        } else {
            m_subtitleLabel->setText("");
        }
    }

    // Show unread badge if there are unread chapters
    if (m_unreadBadge && manga.unreadCount > 0) {
        m_unreadBadge->setText(std::to_string(manga.unreadCount));
        m_unreadBadge->setVisibility(brls::Visibility::VISIBLE);
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

    // Load from server URL
    if (m_manga.thumbnailUrl.empty()) {
        brls::Logger::warning("MangaItemCell: No thumbnail URL for '{}'", m_manga.title);
        return;
    }

    // Build full URL with server base
    SuwayomiClient& client = SuwayomiClient::getInstance();
    std::string url = client.getMangaThumbnailUrl(m_manga.id);

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
