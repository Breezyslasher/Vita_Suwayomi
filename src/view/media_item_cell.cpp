/**
 * VitaSuwayomi - Manga Item Cell implementation
 * Komikku-style card with cover image, title overlay, and badges
 */

#include "view/media_item_cell.hpp"
#include "app/application.hpp"
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

    // Cover image - fills the card (no fixed size, uses parent dimensions)
    m_thumbnailImage = new brls::Image();
    m_thumbnailImage->setScalingType(brls::ImageScalingType::FILL);
    m_thumbnailImage->setCornerRadius(8);
    m_thumbnailImage->setGrow(1.0f);  // Fill available space
    m_thumbnailImage->setPositionType(brls::PositionType::ABSOLUTE);
    m_thumbnailImage->setPositionTop(0);
    m_thumbnailImage->setPositionLeft(0);
    m_thumbnailImage->setPositionRight(0);
    m_thumbnailImage->setPositionBottom(0);
    this->addView(m_thumbnailImage);

    // Bottom overlay for title (gradient effect simulated with solid color)
    // Use percentage width to match parent
    m_titleOverlay = new brls::Box();
    m_titleOverlay->setAxis(brls::Axis::COLUMN);
    m_titleOverlay->setJustifyContent(brls::JustifyContent::FLEX_END);
    m_titleOverlay->setAlignItems(brls::AlignItems::STRETCH);
    m_titleOverlay->setBackgroundColor(nvgRGBA(0, 0, 0, 180));
    m_titleOverlay->setPadding(6, 6, 4, 6);
    m_titleOverlay->setPositionType(brls::PositionType::ABSOLUTE);
    m_titleOverlay->setPositionBottom(0);
    m_titleOverlay->setPositionLeft(0);
    m_titleOverlay->setPositionRight(0);  // Stretch to fill width

    // Title label - at bottom of card
    m_titleLabel = new brls::Label();
    m_titleLabel->setFontSize(11);
    m_titleLabel->setTextColor(nvgRGB(255, 255, 255));
    m_titleLabel->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    m_titleOverlay->addView(m_titleLabel);

    // Subtitle (author) - smaller, below title
    m_subtitleLabel = new brls::Label();
    m_subtitleLabel->setFontSize(9);
    m_subtitleLabel->setTextColor(nvgRGB(180, 180, 180));
    m_subtitleLabel->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    m_titleOverlay->addView(m_subtitleLabel);

    this->addView(m_titleOverlay);

    // List mode info box (horizontal layout - initially hidden)
    m_listInfoBox = new brls::Box();
    m_listInfoBox->setAxis(brls::Axis::COLUMN);
    m_listInfoBox->setJustifyContent(brls::JustifyContent::CENTER);
    m_listInfoBox->setAlignItems(brls::AlignItems::FLEX_START);
    m_listInfoBox->setPadding(8, 12, 8, 12);
    m_listInfoBox->setGrow(1.0f);
    m_listInfoBox->setVisibility(brls::Visibility::GONE);
    this->addView(m_listInfoBox);

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

    // NEW badge - below download badge (shows if recently updated)
    m_newBadge = new brls::Label();
    m_newBadge->setFontSize(8);
    m_newBadge->setText("NEW");
    m_newBadge->setTextColor(nvgRGB(255, 255, 255));
    m_newBadge->setBackgroundColor(nvgRGBA(231, 76, 60, 255)); // Red badge
    m_newBadge->setPositionType(brls::PositionType::ABSOLUTE);
    m_newBadge->setPositionTop(26);  // Below download badge
    m_newBadge->setPositionLeft(0);
    m_newBadge->setVisibility(brls::Visibility::GONE);
    this->addView(m_newBadge);

    // Description label (hidden, for focus state)
    m_descriptionLabel = new brls::Label();
    m_descriptionLabel->setFontSize(9);
    m_descriptionLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_descriptionLabel->setVisibility(brls::Visibility::GONE);
}

void MangaItemCell::updateDisplay() {
    // Set title - Komikku style, left-aligned
    if (m_titleLabel) {
        std::string title = m_manga.title;
        if (title.length() > 16) {
            title = title.substr(0, 14) + "..";
        }
        m_originalTitle = title;
        m_titleLabel->setText(title);
    }

    // Set subtitle (author or chapter count)
    if (m_subtitleLabel) {
        if (!m_manga.author.empty()) {
            std::string author = m_manga.author;
            if (author.length() > 18) {
                author = author.substr(0, 16) + "..";
            }
            m_subtitleLabel->setText(author);
        } else if (m_manga.chapterCount > 0) {
            m_subtitleLabel->setText(std::to_string(m_manga.chapterCount) + " ch");
        } else {
            m_subtitleLabel->setText("");
        }
    }

    // Show unread badge (teal, top-right) - respects showUnreadBadge setting
    if (m_unreadBadge) {
        const auto& settings = Application::getInstance().getSettings();
        if (settings.showUnreadBadge && m_manga.unreadCount > 0) {
            m_unreadBadge->setText(std::to_string(m_manga.unreadCount));
            m_unreadBadge->setVisibility(brls::Visibility::VISIBLE);
        } else {
            m_unreadBadge->setVisibility(brls::Visibility::GONE);
        }
    }

    // Show download badge if any chapters are downloaded locally - respects showDownloadedBadge setting
    if (m_downloadBadge) {
        const auto& settings = Application::getInstance().getSettings();
        static const std::string ICON_CHECK = "\xEE\xA1\xAC";
        DownloadsManager& dm = DownloadsManager::getInstance();
        DownloadItem* download = dm.getMangaDownload(m_manga.id);
        if (settings.showDownloadedBadge && download != nullptr) {
            m_downloadBadge->setText(ICON_CHECK);
            m_downloadBadge->setVisibility(brls::Visibility::VISIBLE);
        } else {
            m_downloadBadge->setVisibility(brls::Visibility::GONE);
        }
    }

    // Show NEW badge if manga was updated recently (within 7 days)
    // Check if inLibraryAt timestamp is within the last 7 days
    if (m_newBadge) {
        bool showNew = false;

        // If manga has unread chapters and was recently added/updated
        if (m_manga.unreadCount > 0 && m_manga.inLibraryAt) {
            // inLibraryAt is a boolean in this struct, but for newly added manga
            // we can use other indicators. For now, we'll show NEW if unread > 0
            // and downloadedCount is 0 (meaning it's fresh)
            showNew = (m_manga.downloadedCount == 0);
        }

        m_newBadge->setVisibility(showNew ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
}

void MangaItemCell::setManga(const Manga& manga) {
    m_manga = manga;
    m_thumbnailLoaded = false;
    updateDisplay();
    loadThumbnail();
}

void MangaItemCell::setMangaDeferred(const Manga& manga) {
    // Set manga data but don't load thumbnail yet
    m_manga = manga;
    m_thumbnailLoaded = false;
    updateDisplay();
}

void MangaItemCell::loadThumbnailIfNeeded() {
    if (!m_thumbnailLoaded && m_manga.id > 0) {
        loadThumbnail();
    }
}

void MangaItemCell::loadThumbnail() {
    if (!m_thumbnailImage || m_thumbnailLoaded) return;

    m_thumbnailLoaded = true;  // Mark as loaded to prevent duplicate loads

    // Check for local Vita cover path
    if (!m_manga.thumbnailUrl.empty() && m_manga.thumbnailUrl.find("ux0:") == 0) {
        loadLocalCoverToImage(m_thumbnailImage, m_manga.thumbnailUrl);
        return;
    }

    if (m_manga.id <= 0) return;

    SuwayomiClient& client = SuwayomiClient::getInstance();
    std::string url;

    if (!m_manga.thumbnailUrl.empty()) {
        if (m_manga.thumbnailUrl[0] == '/') {
            url = client.getServerUrl();
            while (!url.empty() && url.back() == '/') url.pop_back();
            url += m_manga.thumbnailUrl;
        } else if (m_manga.thumbnailUrl.find("http") == 0) {
            url = m_manga.thumbnailUrl;
        } else {
            url = client.getMangaThumbnailUrl(m_manga.id);
        }
    } else {
        url = client.getMangaThumbnailUrl(m_manga.id);
    }

    ImageLoader::loadAsync(url, nullptr, m_thumbnailImage);
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

void MangaItemCell::setSelected(bool selected) {
    m_selected = selected;
    updateSelectionVisual();
}

void MangaItemCell::updateSelectionVisual() {
    if (m_selected) {
        this->setBorderColor(nvgRGBA(0, 150, 136, 255));  // Teal border
        this->setBorderThickness(3.0f);
    } else {
        this->setBorderColor(nvgRGBA(0, 0, 0, 0));
        this->setBorderThickness(0.0f);
    }
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

void MangaItemCell::setCompactMode(bool compact) {
    if (m_compactMode == compact) return;
    m_compactMode = compact;
    m_listMode = false;  // Mutually exclusive
    applyDisplayMode();
}

void MangaItemCell::setListMode(bool listMode) {
    if (m_listMode == listMode) return;
    m_listMode = listMode;
    m_compactMode = false;  // Mutually exclusive
    applyDisplayMode();
}

void MangaItemCell::applyDisplayMode() {
    if (m_listMode) {
        // List mode: simple horizontal layout with title only (no cover)
        this->setAxis(brls::Axis::ROW);
        this->setJustifyContent(brls::JustifyContent::FLEX_START);
        this->setAlignItems(brls::AlignItems::CENTER);

        // Hide thumbnail in list mode - titles only
        if (m_thumbnailImage) {
            m_thumbnailImage->setVisibility(brls::Visibility::GONE);
        }

        // Hide grid title overlay
        if (m_titleOverlay) {
            m_titleOverlay->setVisibility(brls::Visibility::GONE);
        }

        // Show and populate list info box with title only
        if (m_listInfoBox) {
            m_listInfoBox->setVisibility(brls::Visibility::VISIBLE);
            m_listInfoBox->clearViews();
            m_listInfoBox->setPadding(8, 16, 8, 16);
            m_listInfoBox->setJustifyContent(brls::JustifyContent::CENTER);
            m_listInfoBox->setAlignItems(brls::AlignItems::FLEX_START);

            // Title for list mode - show full title
            auto* listTitle = new brls::Label();
            listTitle->setFontSize(14);
            listTitle->setTextColor(nvgRGB(255, 255, 255));
            std::string title = m_manga.title;
            if (title.length() > 60) {
                title = title.substr(0, 58) + "..";
            }
            listTitle->setText(title);
            m_listInfoBox->addView(listTitle);
        }

        // Position badges for list mode (right side)
        if (m_unreadBadge) {
            m_unreadBadge->setPositionTop(4);
            m_unreadBadge->setPositionRight(8);
        }
        if (m_downloadBadge) {
            m_downloadBadge->setPositionTop(4);
            m_downloadBadge->setPositionRight(40);  // Position next to unread badge
        }
        if (m_newBadge) {
            m_newBadge->setPositionTop(4);
            m_newBadge->setPositionRight(72);  // Position next to download badge
        }

    } else if (m_compactMode) {
        // Compact mode: grid with covers only (no title overlay)
        this->setAxis(brls::Axis::COLUMN);
        this->setJustifyContent(brls::JustifyContent::FLEX_END);
        this->setAlignItems(brls::AlignItems::STRETCH);

        // Thumbnail - fill the cell (restore visibility if coming from list mode)
        if (m_thumbnailImage) {
            m_thumbnailImage->setVisibility(brls::Visibility::VISIBLE);
            m_thumbnailImage->setPositionType(brls::PositionType::ABSOLUTE);
            m_thumbnailImage->setPositionTop(0);
            m_thumbnailImage->setPositionLeft(0);
            m_thumbnailImage->setPositionRight(0);
            m_thumbnailImage->setPositionBottom(0);
            m_thumbnailImage->setGrow(1.0f);
            m_thumbnailImage->setCornerRadius(8);
        }

        // Hide title overlay in compact mode
        if (m_titleOverlay) {
            m_titleOverlay->setVisibility(brls::Visibility::GONE);
        }

        // Hide list info box
        if (m_listInfoBox) {
            m_listInfoBox->setVisibility(brls::Visibility::GONE);
        }

        // Restore badge positions for grid mode
        if (m_unreadBadge) {
            m_unreadBadge->setPositionTop(0);
            m_unreadBadge->setPositionRight(0);
        }
        if (m_downloadBadge) {
            m_downloadBadge->setPositionTop(0);
            m_downloadBadge->setPositionLeft(0);
        }
        if (m_newBadge) {
            m_newBadge->setPositionTop(26);
            m_newBadge->setPositionLeft(0);
        }

    } else {
        // Normal grid mode: cover + title overlay
        this->setAxis(brls::Axis::COLUMN);
        this->setJustifyContent(brls::JustifyContent::FLEX_END);
        this->setAlignItems(brls::AlignItems::STRETCH);

        // Thumbnail - fill the cell (restore visibility if coming from list mode)
        if (m_thumbnailImage) {
            m_thumbnailImage->setVisibility(brls::Visibility::VISIBLE);
            m_thumbnailImage->setPositionType(brls::PositionType::ABSOLUTE);
            m_thumbnailImage->setPositionTop(0);
            m_thumbnailImage->setPositionLeft(0);
            m_thumbnailImage->setPositionRight(0);
            m_thumbnailImage->setPositionBottom(0);
            m_thumbnailImage->setGrow(1.0f);
            m_thumbnailImage->setCornerRadius(8);
        }

        // Show title overlay
        if (m_titleOverlay) {
            m_titleOverlay->setVisibility(brls::Visibility::VISIBLE);
        }

        // Hide list info box
        if (m_listInfoBox) {
            m_listInfoBox->setVisibility(brls::Visibility::GONE);
        }

        // Restore badge positions for grid mode
        if (m_unreadBadge) {
            m_unreadBadge->setPositionTop(0);
            m_unreadBadge->setPositionRight(0);
        }
        if (m_downloadBadge) {
            m_downloadBadge->setPositionTop(0);
            m_downloadBadge->setPositionLeft(0);
        }
        if (m_newBadge) {
            m_newBadge->setPositionTop(26);
            m_newBadge->setPositionLeft(0);
        }
    }
}

} // namespace vitasuwayomi
