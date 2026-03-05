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
    m_alive = std::make_shared<bool>(true);

    // Komikku-style: card with cover filling the space, title at bottom
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setFocusable(true);
    this->setCornerRadius(8);
    this->setBackgroundColor(Application::getInstance().getCardBackground());
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
    m_titleOverlay->setClipsToBounds(true);
    // Limit overlay height so titles don't cover the entire cover.
    // 2 lines of title (fontSize 11, ~14px line height) + 1 line subtitle (~12px) + padding (10px)
    m_titleOverlay->setMaxHeight(50);

    // Title label - at bottom of card, wraps to 2 lines for longer titles
    m_titleLabel = new brls::Label();
    m_titleLabel->setFontSize(11);
    m_titleLabel->setTextColor(nvgRGB(255, 255, 255));
    m_titleLabel->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    m_titleOverlay->addView(m_titleLabel);

    // Subtitle (author) - smaller, below title, single line
    m_subtitleLabel = new brls::Label();
    m_subtitleLabel->setFontSize(9);
    m_subtitleLabel->setTextColor(Application::getInstance().getSubtitleColor());
    m_subtitleLabel->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    m_subtitleLabel->setSingleLine(true);
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
    m_unreadBadge->setBackgroundColor(Application::getInstance().isVaporwaveTheme() ? nvgRGBA(0, 255, 200, 255) : nvgRGBA(0, 150, 136, 255)); // Teal badge
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
    // Image loaded lazily on first visibility to avoid 98+ useless GPU texture creations
    m_starBadge = new brls::Image();
    m_starBadge->setWidth(16);
    m_starBadge->setHeight(16);
    m_starBadge->setScalingType(brls::ImageScalingType::FIT);
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
    // Image loaded lazily on first focus to avoid 98+ useless GPU texture creations
    m_startHintIcon = new brls::Image();
    m_startHintIcon->setWidth(64);
    m_startHintIcon->setHeight(16);
    m_startHintIcon->setScalingType(brls::ImageScalingType::FIT);
    m_startHintIcon->setPositionType(brls::PositionType::ABSOLUTE);
    m_startHintIcon->setPositionTop(6);
    m_startHintIcon->setPositionRight(6);
    m_startHintIcon->setVisibility(brls::Visibility::GONE);
    this->addView(m_startHintIcon);
}

MangaItemCell::~MangaItemCell() {
    // Signal to any in-flight ImageLoader callbacks that our m_thumbnailImage
    // pointer is no longer valid. Without this, a background worker thread
    // that finishes downloading after this cell is destroyed would write
    // to freed memory via processPendingTextures() → target->setImageFromMem().
    if (m_alive) {
        *m_alive = false;
    }
}

void MangaItemCell::updateDisplay() {
    // Set title - Komikku style, left-aligned, up to 2 lines
    // Truncation and font size adapt to grid column count
    if (m_titleLabel) {
        std::string title = m_manga.title;

        // Max characters depends on grid size (wider cells fit more text)
        int maxChars = 40;  // Default for 6 columns
        if (m_gridColumns <= 4) {
            maxChars = 55;   // Wider cells: ~27 chars per line x 2 lines
        } else if (m_gridColumns <= 6) {
            maxChars = 40;   // Default: ~20 chars per line x 2 lines
        } else {
            maxChars = 28;   // Narrow cells: ~14 chars per line x 2 lines
        }

        if (static_cast<int>(title.length()) > maxChars) {
            title = title.substr(0, maxChars - 2) + "..";
        }
        m_originalTitle = title;
        m_titleLabel->setText(title);
    }

    // Update list mode title - show full title (row auto-adapts to fit)
    if (m_listTitleLabel) {
        m_listTitleLabel->setText(m_manga.title);
    }

    // Set subtitle (author or chapter count)
    if (m_subtitleLabel) {
        if (!m_manga.author.empty()) {
            std::string author = m_manga.author;
            // Max subtitle chars depends on grid size
            int maxSubChars = 18;
            if (m_gridColumns <= 4) {
                maxSubChars = 28;
            } else if (m_gridColumns <= 6) {
                maxSubChars = 18;
            } else {
                maxSubChars = 14;
            }
            if (static_cast<int>(author.length()) > maxSubChars) {
                author = author.substr(0, maxSubChars - 2) + "..";
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
    // Also check recent additions cache for immediate update
    if (m_starBadge) {
        bool inLib = m_manga.inLibrary || Application::getInstance().isRecentlyAdded(m_manga.id);
        bool showStar = m_showLibraryBadge && inLib;
        if (showStar && !m_starImageLoaded) {
            m_starBadge->setImageFromFile("app0:resources/icons/star.png");
            m_starImageLoaded = true;
        }
        m_starBadge->setVisibility(showStar ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
}

void MangaItemCell::setManga(const Manga& manga) {
    // Invalidate any in-flight thumbnail loads for the previous manga.
    // Without this, a stale worker-thread load could finish after the new load
    // and overwrite the correct cover (race condition during sort changes).
    if (m_alive) *m_alive = false;
    m_alive = std::make_shared<bool>(true);

    m_manga = manga;
    m_thumbnailLoaded = false;
    updateDisplay();
    loadThumbnail();
}

void MangaItemCell::setMangaDeferred(const Manga& manga) {
    // Invalidate any in-flight thumbnail loads for the previous manga.
    if (m_alive) *m_alive = false;
    m_alive = std::make_shared<bool>(true);

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

void MangaItemCell::unloadThumbnail() {
    if (!m_thumbnailLoaded || !m_thumbnailImage) return;

    // Clear the GPU texture by setting a tiny 1x1 transparent TGA image
    // This frees VRAM on the Vita's limited 128MB while keeping the cell structure intact
    // The thumbnail can be reloaded later via loadThumbnailIfNeeded() when scrolled back
    static const unsigned char s_clearPixel[] = {
        0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // TGA header
        1, 0, 1, 0, 32, 0,                      // 1x1, 32bpp
        0, 0, 0, 0                               // 1 transparent pixel (BGRA)
    };
    m_thumbnailImage->setImageFromMem(s_clearPixel, sizeof(s_clearPixel));
    m_thumbnailLoaded = false;
}

void MangaItemCell::resetThumbnailLoadState() {
    // Just reset the load flag without clearing the existing image.
    // This allows loadThumbnailIfNeeded() to reload the thumbnail from cache/network
    // while keeping the old image visible until the new one arrives (no visual flash).
    m_thumbnailLoaded = false;
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

    ImageLoader::loadAsync(url, nullptr, m_thumbnailImage, m_alive);
}

brls::View* MangaItemCell::create() {
    return new MangaItemCell();
}

void MangaItemCell::onFocusGained() {
    brls::Box::onFocusGained();
    updateFocusInfo(true);
    // Show start button hint on focus (but not in browser/search tabs where library badge is shown)
    if (m_startHintIcon && !m_showLibraryBadge) {
        if (!m_startHintImageLoaded) {
            m_startHintIcon->setImageFromFile("app0:resources/images/start_button.png");
            m_startHintImageLoaded = true;
        }
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

void MangaItemCell::setPressed(bool pressed) {
    m_pressed = pressed;
}

void MangaItemCell::draw(NVGcontext* vg, float x, float y, float width, float height,
                          brls::Style style, brls::FrameContext* ctx) {
    // Draw normally first
    brls::Box::draw(vg, x, y, width, height, style, ctx);

    // Overlay a semi-transparent dark rect when pressed for touch feedback
    if (m_pressed) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 8);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 80));
        nvgFill(vg);
    }
}

void MangaItemCell::setSelected(bool selected) {
    m_selected = selected;
    updateSelectionVisual();
}

void MangaItemCell::updateSelectionVisual() {
    if (m_selected) {
        this->setBorderColor(Application::getInstance().isVaporwaveTheme() ? nvgRGBA(0, 255, 200, 255) : nvgRGBA(0, 150, 136, 255));  // Teal border
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

void MangaItemCell::setGridColumns(int columns) {
    if (m_gridColumns == columns) return;
    m_gridColumns = columns;

    // Adapt title font size and re-truncate based on grid column count
    if (m_titleLabel) {
        int fontSize = 11;  // Default for 6 columns
        if (columns <= 4) {
            fontSize = 14;
        } else if (columns <= 6) {
            fontSize = 11;
        } else {
            fontSize = 9;
        }
        m_titleLabel->setFontSize(fontSize);
    }
    if (m_subtitleLabel) {
        int subFontSize = 9;
        if (columns <= 4) {
            subFontSize = 11;
        } else if (columns <= 6) {
            subFontSize = 9;
        } else {
            subFontSize = 8;
        }
        m_subtitleLabel->setFontSize(subFontSize);
    }
    // Adjust overlay max height for font size so title stays at most 2 lines
    if (m_titleOverlay) {
        if (columns <= 4) {
            m_titleOverlay->setMaxHeight(66);  // 2 lines @ 14px font + subtitle @ 11px + padding
        } else if (columns <= 6) {
            m_titleOverlay->setMaxHeight(50);  // 2 lines @ 11px font + subtitle @ 9px + padding
        } else {
            m_titleOverlay->setMaxHeight(42);  // 2 lines @ 9px font + subtitle @ 8px + padding
        }
    }

    // Scale unread badge font size and padding with grid size
    if (m_unreadBadge) {
        int badgeFontSize = 10;
        int badgeMargin = 6;
        if (columns <= 4) {
            badgeFontSize = 13;
            badgeMargin = 8;
        } else if (columns <= 6) {
            badgeFontSize = 10;
            badgeMargin = 6;
        } else {
            badgeFontSize = 8;
            badgeMargin = 4;
        }
        m_unreadBadge->setFontSize(badgeFontSize);
        m_unreadBadge->setMargins(badgeMargin, 0, 0, badgeMargin);
    }

    // Scale NEW badge font size and position with grid size
    if (m_newBadge) {
        int newFontSize = 8;
        int newTopPos = 26;
        if (columns <= 4) {
            newFontSize = 10;
            newTopPos = 34;
        } else if (columns <= 6) {
            newFontSize = 8;
            newTopPos = 26;
        } else {
            newFontSize = 7;
            newTopPos = 20;
        }
        m_newBadge->setFontSize(newFontSize);
        if (!m_listMode) {
            m_newBadge->setPositionTop(newTopPos);
        }
    }

    // Scale star badge size with grid size
    if (m_starBadge) {
        int starSize = 16;
        if (columns <= 4) {
            starSize = 20;
        } else if (columns <= 6) {
            starSize = 16;
        } else {
            starSize = 12;
        }
        m_starBadge->setWidth(starSize);
        m_starBadge->setHeight(starSize);
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
    // Update star badge visibility (also check recent additions cache)
    if (m_starBadge) {
        bool inLib = m_showLibraryBadge && (m_manga.inLibrary || Application::getInstance().isRecentlyAdded(m_manga.id));
        if (inLib && !m_starImageLoaded) {
            m_starBadge->setImageFromFile("app0:resources/icons/star.png");
            m_starImageLoaded = true;
        }
        m_starBadge->setVisibility(inLib ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    // Hide start hint icon when in browser/search mode
    if (m_startHintIcon && m_showLibraryBadge) {
        m_startHintIcon->setVisibility(brls::Visibility::GONE);
    }
}

void MangaItemCell::setListRowSize(int rowSize) {
    // No-op: list mode always auto-adapts row height to title length
    (void)rowSize;
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

        // Font size for list mode
        if (m_listTitleLabel) {
            m_listTitleLabel->setFontSize(14);
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
        // NEW badge top position scales with grid columns (set in setGridColumns)
        if (m_unreadBadge) {
            m_unreadBadge->setPositionTop(0);
            m_unreadBadge->setPositionLeft(0);
        }
        if (m_newBadge) {
            int newTop = (m_gridColumns <= 4) ? 34 : (m_gridColumns >= 8) ? 20 : 26;
            m_newBadge->setPositionTop(newTop);
            m_newBadge->setPositionLeft(0);
        }
        // Restore start hint position for compact mode (top-right)
        if (m_startHintIcon) {
            m_startHintIcon->setPositionTop(6);
            m_startHintIcon->setPositionRight(6);
        }
        // Star badge position for compact mode (top-right, below start hint area)
        if (m_starBadge) {
            int starTop = (m_gridColumns <= 4) ? 34 : (m_gridColumns >= 8) ? 20 : 26;
            m_starBadge->setPositionTop(starTop);
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
        // NEW badge top position scales with grid columns (set in setGridColumns)
        if (m_unreadBadge) {
            m_unreadBadge->setPositionTop(0);
            m_unreadBadge->setPositionLeft(0);
        }
        if (m_newBadge) {
            int newTop = (m_gridColumns <= 4) ? 34 : (m_gridColumns >= 8) ? 20 : 26;
            m_newBadge->setPositionTop(newTop);
            m_newBadge->setPositionLeft(0);
        }
        // Restore start hint position for normal grid mode (top-right)
        if (m_startHintIcon) {
            m_startHintIcon->setPositionTop(6);
            m_startHintIcon->setPositionRight(6);
        }
        // Star badge position for normal grid mode (top-right, below start hint area)
        if (m_starBadge) {
            int starTop = (m_gridColumns <= 4) ? 34 : (m_gridColumns >= 8) ? 20 : 26;
            m_starBadge->setPositionTop(starTop);
            m_starBadge->setPositionRight(6);
        }
    }
}

} // namespace vitasuwayomi
