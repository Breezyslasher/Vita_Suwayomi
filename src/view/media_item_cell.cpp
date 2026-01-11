/**
 * VitaSuwayomi - Media Item Cell implementation
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

MediaItemCell::MediaItemCell() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(5);
    this->setFocusable(true);
    this->setCornerRadius(8);
    this->setBackgroundColor(nvgRGBA(50, 50, 50, 255));

    // Thumbnail image - square for audiobook/podcast covers
    m_thumbnailImage = new brls::Image();
    m_thumbnailImage->setWidth(140);
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

    // Subtitle label (for episodes: Episode N)
    m_subtitleLabel = new brls::Label();
    m_subtitleLabel->setFontSize(10);
    m_subtitleLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_subtitleLabel->setVisibility(brls::Visibility::GONE);
    this->addView(m_subtitleLabel);

    // Description label (shows on focus)
    m_descriptionLabel = new brls::Label();
    m_descriptionLabel->setFontSize(9);
    m_descriptionLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_descriptionLabel->setVisibility(brls::Visibility::GONE);
    this->addView(m_descriptionLabel);

    // Progress bar (for continue listening)
    m_progressBar = new brls::Rectangle();
    m_progressBar->setHeight(3);
    m_progressBar->setWidth(0);
    m_progressBar->setColor(nvgRGBA(229, 160, 13, 255)); // Progress color
    m_progressBar->setVisibility(brls::Visibility::GONE);
    this->addView(m_progressBar);
}

void MediaItemCell::setItem(const MediaItem& item) {
    m_item = item;

    // Audiobookshelf uses square covers
    m_thumbnailImage->setWidth(140);
    m_thumbnailImage->setHeight(140);

    // Set title
    if (m_titleLabel) {
        std::string title = item.title;
        // Truncate long titles
        if (title.length() > 18) {
            title = title.substr(0, 16) + "...";
        }
        m_originalTitle = title;  // Store truncated title for focus restore
        m_titleLabel->setText(title);
    }

    // Set subtitle for podcast episodes
    if (m_subtitleLabel) {
        if (item.mediaType == MediaType::PODCAST_EPISODE) {
            // Show subtitle or episode title
            if (!item.subtitle.empty()) {
                m_subtitleLabel->setText(item.subtitle);
                m_subtitleLabel->setVisibility(brls::Visibility::VISIBLE);
            } else {
                m_subtitleLabel->setVisibility(brls::Visibility::GONE);
            }
        } else {
            m_subtitleLabel->setVisibility(brls::Visibility::GONE);
        }
    }

    // Show progress bar for items with listening progress
    if (m_progressBar && item.currentTime > 0 && item.duration > 0) {
        float progress = item.currentTime / item.duration;
        m_progressBar->setWidth(140 * progress);
        m_progressBar->setVisibility(brls::Visibility::VISIBLE);
    }

    // Load thumbnail
    loadThumbnail();
}

void MediaItemCell::loadThumbnail() {
    if (!m_thumbnailImage) return;

    brls::Logger::debug("MediaItemCell::loadThumbnail for '{}' id='{}' coverPath='{}'",
                       m_item.title, m_item.id, m_item.coverPath);

    // Check if we have a Vita local cover path (for downloaded items)
    // Vita local paths start with "ux0:"
    if (!m_item.coverPath.empty() && m_item.coverPath.find("ux0:") == 0) {
        // Local Vita path - load directly from file
        brls::Logger::debug("MediaItemCell: Loading local cover from {}", m_item.coverPath);
        loadLocalCoverToImage(m_thumbnailImage, m_item.coverPath);
        return;
    }

    // Load from server URL
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    // Use square dimensions for audiobook/podcast covers (request larger size for quality)
    int size = 280;

    // Use item ID for cover URL
    if (m_item.id.empty()) {
        brls::Logger::warning("MediaItemCell: No item ID for cover of '{}'", m_item.title);
        return;
    }

    std::string url = client.getCoverUrl(m_item.id, size, size);
    brls::Logger::debug("MediaItemCell: Loading cover from URL: {}", url);

    ImageLoader::loadAsync(url, [this](brls::Image* image) {
        brls::Logger::debug("MediaItemCell: Cover loaded for '{}'", m_item.title);
    }, m_thumbnailImage);
}

brls::View* MediaItemCell::create() {
    return new MediaItemCell();
}

void MediaItemCell::onFocusGained() {
    brls::Box::onFocusGained();
    updateFocusInfo(true);
}

void MediaItemCell::onFocusLost() {
    brls::Box::onFocusLost();
    updateFocusInfo(false);
}

void MediaItemCell::updateFocusInfo(bool focused) {
    if (!m_titleLabel || !m_descriptionLabel) return;

    // For podcast episodes, show extended info on focus
    if (m_item.mediaType == MediaType::PODCAST_EPISODE) {
        if (focused) {
            // Show full title
            m_titleLabel->setText(m_item.title);

            // Show duration and other info
            std::string info;
            if (m_item.duration > 0) {
                int minutes = (int)(m_item.duration / 60.0f);
                info = std::to_string(minutes) + " min";
            }
            if (!m_item.description.empty()) {
                // Show first 50 chars of description
                std::string desc = m_item.description;
                if (desc.length() > 50) {
                    desc = desc.substr(0, 47) + "...";
                }
                if (!info.empty()) info += " - ";
                info += desc;
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
    } else if (m_item.mediaType == MediaType::BOOK) {
        // Show author and duration for books on focus
        if (focused) {
            std::string info;
            if (!m_item.authorName.empty()) {
                info = m_item.authorName;
            }
            if (m_item.duration > 0) {
                int hours = (int)(m_item.duration / 3600.0f);
                int mins = (int)((m_item.duration - hours * 3600) / 60.0f);
                if (!info.empty()) info += " - ";
                info += std::to_string(hours) + "h " + std::to_string(mins) + "m";
            }
            if (!info.empty()) {
                m_descriptionLabel->setText(info);
                m_descriptionLabel->setVisibility(brls::Visibility::VISIBLE);
            }
            // Show full title
            m_titleLabel->setText(m_item.title);
        } else {
            m_titleLabel->setText(m_originalTitle);
            m_descriptionLabel->setVisibility(brls::Visibility::GONE);
        }
    } else if (m_item.mediaType == MediaType::PODCAST) {
        // Show podcast info on focus
        if (focused) {
            std::string info;
            if (!m_item.authorName.empty()) {
                info = m_item.authorName;
            }
            if (!info.empty()) {
                m_descriptionLabel->setText(info);
                m_descriptionLabel->setVisibility(brls::Visibility::VISIBLE);
            }
            m_titleLabel->setText(m_item.title);
        } else {
            m_titleLabel->setText(m_originalTitle);
            m_descriptionLabel->setVisibility(brls::Visibility::GONE);
        }
    }
}

} // namespace vitasuwayomi
