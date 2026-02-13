/**
 * VitaSuwayomi - Manga Item Cell implementation
 * Komikku-style card with cover image, title overlay, and badges
 */

#include "view/media_item_cell.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/image_loader.hpp"
#include <ctime>
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
    m_listInfoBox->setPadding(8, 16, 8, 16);
    m_listInfoBox->setGrow(1.0f);
    m_listInfoBox->setVisibility(brls::Visibility::GONE);

    // List mode title label (persistent, not dynamically created)
    m_listTitleLabel = new brls::Label();
    m_listTitleLabel->setFontSize(14);
    m_listTitleLabel->setTextColor(nvgRGB(255, 255, 255));
    m_listInfoBox->addView(m_listTitleLabel);

    this->addView(m_listInfoBox);

    // Unread badge - top left corner
    m_unreadBadge = new brls::Label();
    m_unreadBadge->setFontSize(10);
    m_unreadBadge->setTextColor(nvgRGB(255, 255, 255));
    m_unreadBadge->setBackgroundColor(nvgRGBA(0, 150, 136, 255)); // Teal badge
    m_unreadBadge->setMargins(6, 0, 0, 6);
    m_unreadBadge->setPositionType(brls::PositionType::ABSOLUTE);
    m_unreadBadge->setPositionTop(0);
    m_unreadBadge->setPositionLeft(0);
    m_unreadBadge->setVisibility(brls::Visibility::GONE);
    this->addView(m_unreadBadge);

    // NEW badge - top left, below unread badge (shows if recently updated)
    m_newBadge = new brls::Label();
    m_newBadge->setFontSize(8);
    m_newBadge->setText("NEW");
    m_newBadge->setTextColor(nvgRGB(255, 255, 255));
    m_newBadge->setBackgroundColor(nvgRGBA(231, 76, 60, 255)); // Red badge
    m_newBadge->setPositionType(brls::PositionType::ABSOLUTE);
    m_newBadge->setPositionTop(26);  // Below unread badge
    m_newBadge->setPositionLeft(0);
    m_newBadge->setVisibility(brls::Visibility::GONE);
    this->addView(m_newBadge);

    // Star badge - top right corner (shows if manga is in library, browser/search only)
    m_starBadge = new brls::Image();
    m_starBadge->setWidth(16);
    m_starBadge->setHeight(16);
    m_starBadge->setScalingType(brls::ImageScalingType::FIT);
    m_starBadge->setImageFromFile("app0:resources/icons/star.png");
    m_starBadge->setPositionType(brls::PositionType::ABSOLUTE);
    m_starBadge->setPositionTop(6);
    m_starBadge->setPositionRight(6);
    m_starBadge->setVisibility(brls::Visibility::GONE);
    this->addView(m_starBadge);

    // Description label (hidden, for focus state)
    m_descriptionLabel = new brls::Label();
    m_descriptionLabel->setFontSize(9);
    m_descriptionLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_descriptionLabel->setVisibility(brls::Visibility::GONE);

    // Start button hint icon - shown on focus (top-right) to indicate menu action
    m_startHintIcon = new brls::Image();
    m_startHintIcon->setWidth(64);
    m_startHintIcon->setHeight(16);
    m_startHintIcon->setScalingType(brls::ImageScalingType::FIT);
    m_startHintIcon->setImageFromFile("app0:resources/images/start_button.png");
    m_startHintIcon->setPositionType(brls::PositionType::ABSOLUTE);
    m_startHintIcon->setPositionTop(6);
    m_startHintIcon->setPositionRight(6);
    m_startHintIcon->setVisibility(brls::Visibility::GONE);
    this->addView(m_startHintIcon);
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

    // Update list mode title as well
    if (m_listTitleLabel) {
        std::string listTitle = m_manga.title;
        // In auto mode (3), show full title without truncation
        // In other modes, truncate based on row size
        if (m_listRowSize != 3) {
            int maxChars = 60;  // Default (medium)
            switch (m_listRowSize) {
                case 0:  // Small
                    maxChars = 45;
                    break;
                case 1:  // Medium
                    maxChars = 60;
                    break;
                case 2:  // Large
                    maxChars = 80;
                    break;
            }
            if (static_cast<int>(listTitle.length()) > maxChars) {
                listTitle = listTitle.substr(0, maxChars - 2) + "..";
            }
        }
        // In auto mode, show full title (no truncation)
        m_listTitleLabel->setText(listTitle);
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

    // Show unread badge (teal, top-left) - respects showUnreadBadge setting
    if (m_unreadBadge) {
        const auto& settings = Application::getInstance().getSettings();
        if (settings.showUnreadBadge && m_manga.unreadCount > 0) {
            m_unreadBadge->setText(std::to_string(m_manga.unreadCount));
            m_unreadBadge->setVisibility(brls::Visibility::VISIBLE);
        } else {
            m_unreadBadge->setVisibility(brls::Visibility::GONE);
        }
    }

    // Show NEW badge if manga was added to library recently (within 7 days)
    if (m_newBadge) {
        bool showNew = false;

        // If manga has unread chapters and was recently added to library
        if (m_manga.unreadCount > 0 && m_manga.inLibraryAt > 0) {
            // Check if added within the last 7 days
            int64_t now = std::time(nullptr) * 1000;  // Current time in ms
            int64_t sevenDaysMs = 7 * 24 * 60 * 60 * 1000LL;  // 7 days in ms
            showNew = (now - m_manga.inLibraryAt) < sevenDaysMs;
        }

        m_newBadge->setVisibility(showNew ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

    // Show star badge if manga is in library (browser/search tabs only)
    if (m_starBadge) {
        bool showStar = m_showLibraryBadge && m_manga.inLibrary;
        m_starBadge->setVisibility(showStar ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
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

void MangaItemCell::updateMangaData(const Manga& manga) {
    // Update manga data in place without reloading thumbnail
    // Used for incremental updates when only counts/metadata change
    m_manga = manga;
    // Don't reset m_thumbnailLoaded - keep existing thumbnail
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
    // Show start button hint on focus (but not in browser/search tabs where library badge is shown)
    if (m_startHintIcon && !m_showLibraryBadge) {
        m_startHintIcon->setVisibility(brls::Visibility::VISIBLE);
    }
}

void MangaItemCell::onFocusLost() {
    brls::Box::onFocusLost();
    updateFocusInfo(false);
    // Hide start button hint when focus is lost
    if (m_startHintIcon) {
        m_startHintIcon->setVisibility(brls::Visibility::GONE);
    }
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

void MangaItemCell::setShowLibraryBadge(bool show) {
    if (m_showLibraryBadge == show) return;
    m_showLibraryBadge = show;
    // Update star badge visibility
    if (m_starBadge) {
        bool showStar = m_showLibraryBadge && m_manga.inLibrary;
        m_starBadge->setVisibility(showStar ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    // Hide start hint icon when in browser/search mode
    if (m_startHintIcon && m_showLibraryBadge) {
        m_startHintIcon->setVisibility(brls::Visibility::GONE);
    }
}

void MangaItemCell::setListRowSize(int rowSize) {
    if (m_listRowSize == rowSize) return;
    m_listRowSize = rowSize;
    // Update display if already in list mode
    if (m_listMode) {
        updateDisplay();
    }
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

        // Show list info box (title label is already added and updated by updateDisplay)
        if (m_listInfoBox) {
            m_listInfoBox->setVisibility(brls::Visibility::VISIBLE);
        }

        // Adjust title font size based on row size
        if (m_listTitleLabel) {
            switch (m_listRowSize) {
                case 0:  // Small
                    m_listTitleLabel->setFontSize(12);
                    break;
                case 1:  // Medium (default)
                    m_listTitleLabel->setFontSize(14);
                    break;
                case 2:  // Large
                    m_listTitleLabel->setFontSize(16);
                    break;
                case 3:  // Auto - use medium font, text will wrap if needed
                    m_listTitleLabel->setFontSize(14);
                    break;
                default:
                    m_listTitleLabel->setFontSize(14);
                    break;
            }
        }

        // Position badges for list mode (left side)
        if (m_unreadBadge) {
            m_unreadBadge->setPositionTop(4);
            m_unreadBadge->setPositionLeft(8);
        }
        if (m_newBadge) {
            m_newBadge->setPositionTop(4);
            m_newBadge->setPositionLeft(40);  // Position next to unread badge
        }
        // Position start hint for list mode (top-right)
        if (m_startHintIcon) {
            m_startHintIcon->setPositionTop(4);
            m_startHintIcon->setPositionRight(4);
        }
        // Position star badge for list mode (top-right, below start hint)
        if (m_starBadge) {
            m_starBadge->setPositionTop(4);
            m_starBadge->setPositionRight(72);  // Left of start hint
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

        // Restore badge positions for grid mode (left side)
        if (m_unreadBadge) {
            m_unreadBadge->setPositionTop(0);
            m_unreadBadge->setPositionLeft(0);
        }
        if (m_newBadge) {
            m_newBadge->setPositionTop(26);
            m_newBadge->setPositionLeft(0);
        }
        // Restore start hint position for compact mode (top-right)
        if (m_startHintIcon) {
            m_startHintIcon->setPositionTop(6);
            m_startHintIcon->setPositionRight(6);
        }
        // Star badge position for compact mode (top-right, below start hint area)
        if (m_starBadge) {
            m_starBadge->setPositionTop(26);
            m_starBadge->setPositionRight(6);
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

        // Restore badge positions for grid mode (left side)
        if (m_unreadBadge) {
            m_unreadBadge->setPositionTop(0);
            m_unreadBadge->setPositionLeft(0);
        }
        if (m_newBadge) {
            m_newBadge->setPositionTop(26);
            m_newBadge->setPositionLeft(0);
        }
        // Restore start hint position for normal grid mode (top-right)
        if (m_startHintIcon) {
            m_startHintIcon->setPositionTop(6);
            m_startHintIcon->setPositionRight(6);
        }
        // Star badge position for normal grid mode (top-right, below start hint area)
        if (m_starBadge) {
            m_starBadge->setPositionTop(26);
            m_starBadge->setPositionRight(6);
        }
    }
}

} // namespace vitasuwayomi
