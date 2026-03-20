/**
 * VitaSuwayomi - Manga Item Cell implementation
 * Komikku-style card with cover image, title overlay, and badges
 *
 * PERF: Title overlay, title label, subtitle label, and unread badge are
 * rendered directly via NanoVG instead of borealis sub-views. This eliminates
 * ~4 frame() traversals per cell per frame on PS Vita's 444MHz CPU, cutting
 * per-cell draw cost roughly in half.
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

    // Initialize flat-rendered colors from theme
    m_subtitleColor = Application::getInstance().getSubtitleColor();
    m_unreadBgColor = Application::getInstance().getTealColor();

    // Komikku-style: card with cover filling the space
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setFocusable(true);
    this->setCornerRadius(8);
    this->setBackgroundColor(Application::getInstance().getCardBackground());
    this->setClipsToBounds(true);

    // Cover image - fills the card (only child view that needs borealis frame())
    m_thumbnailImage = new brls::Image();
    m_thumbnailImage->setScalingType(brls::ImageScalingType::FILL);
    m_thumbnailImage->setCornerRadius(8);
    m_thumbnailImage->setGrow(1.0f);
    m_thumbnailImage->setPositionType(brls::PositionType::ABSOLUTE);
    m_thumbnailImage->setPositionTop(0);
    m_thumbnailImage->setPositionLeft(0);
    m_thumbnailImage->setPositionRight(0);
    m_thumbnailImage->setPositionBottom(0);
    this->addView(m_thumbnailImage);

    // Title overlay, title label, subtitle label, and unread badge are NOT
    // added to the view hierarchy. They are rendered directly in draw() via
    // NanoVG to avoid per-frame borealis frame() traversal overhead.

    // Remaining sub-views (listInfoBox, listTitleLabel, newBadge, starBadge,
    // startHintIcon, descriptionLabel) are lazy-created via ensure*() methods
    // when first needed.
}

MangaItemCell::~MangaItemCell() {
    // Signal to any in-flight ImageLoader callbacks that our m_thumbnailImage
    // pointer is no longer valid.
    if (m_alive) {
        *m_alive = false;
    }

    // m_descriptionLabel is not added to the view hierarchy (no parent),
    // so brls won't auto-delete it.
    if (m_descriptionLabel) {
        delete m_descriptionLabel;
        m_descriptionLabel = nullptr;
    }
}

void MangaItemCell::updateDisplay() {
    // Set title - Komikku style, left-aligned, up to 2 lines
    {
        std::string title = m_manga.title;

        int maxChars = 40;
        if (m_gridColumns <= 4) {
            maxChars = 55;
        } else if (m_gridColumns <= 6) {
            maxChars = 40;
        } else {
            maxChars = 28;
        }

        if (static_cast<int>(title.length()) > maxChars) {
            title = title.substr(0, maxChars - 2) + "..";
        }
        m_originalTitle = title;
        m_titleText = title;
        m_overlayDirty = true;
    }

    // Update list mode title - only if already created (lazy)
    if (m_listTitleLabel) {
        m_listTitleLabel->setText(m_manga.title);
    }

    // Set subtitle (author or chapter count)
    {
        if (!m_manga.author.empty()) {
            std::string author = m_manga.author;
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
            m_subtitleText = author;
        } else if (m_manga.chapterCount > 0) {
            m_subtitleText = std::to_string(m_manga.chapterCount) + " ch";
        } else {
            m_subtitleText.clear();
        }
    }

    // Show unread badge (teal, top-left) - respects showUnreadBadge setting
    {
        const auto& settings = Application::getInstance().getSettings();
        if (settings.showUnreadBadge && m_manga.unreadCount > 0) {
            m_unreadText = std::to_string(m_manga.unreadCount);
            m_showUnread = true;
        } else {
            m_showUnread = false;
        }
    }

    // Show NEW badge if manga was added to library recently (within 7 days)
    {
        bool showNew = false;
        if (m_manga.unreadCount > 0 && m_manga.inLibraryAt > 0) {
            int64_t now = std::time(nullptr) * 1000;
            int64_t sevenDaysMs = 7 * 24 * 60 * 60 * 1000LL;
            showNew = (now - m_manga.inLibraryAt) < sevenDaysMs;
        }
        if (showNew) {
            ensureNewBadge()->setVisibility(brls::Visibility::VISIBLE);
        } else if (m_newBadge) {
            m_newBadge->setVisibility(brls::Visibility::GONE);
        }
    }

    // Show star badge if manga is in library (browser/search tabs only)
    {
        bool inLib = (m_manga.inLibrary || Application::getInstance().isRecentlyAdded(m_manga.id))
                     && !Application::getInstance().isRecentlyRemoved(m_manga.id);
        bool showStar = m_showLibraryBadge && inLib;
        if (showStar) {
            auto* star = ensureStarBadge();
            if (!m_starImageLoaded) {
                star->setImageFromFile("app0:resources/icons/star.png");
                m_starImageLoaded = true;
            }
            star->setVisibility(brls::Visibility::VISIBLE);
        } else if (m_starBadge) {
            m_starBadge->setVisibility(brls::Visibility::GONE);
        }
    }
}

void MangaItemCell::setManga(const Manga& manga) {
    if (m_alive) *m_alive = false;
    m_alive = std::make_shared<bool>(true);

    m_manga = manga;
    m_thumbnailLoaded = false;
    updateDisplay();
    loadThumbnail();
}

void MangaItemCell::setMangaDeferred(const Manga& manga) {
    if (m_alive) *m_alive = false;
    m_alive = std::make_shared<bool>(true);

    m_manga = manga;
    m_thumbnailLoaded = false;
    updateDisplay();
}

void MangaItemCell::updateMangaData(const Manga& manga) {
    m_manga = manga;
    updateDisplay();
}

void MangaItemCell::loadThumbnailIfNeeded() {
    if (!m_thumbnailLoaded && m_manga.id > 0) {
        loadThumbnail();
    }
}

void MangaItemCell::unloadThumbnail() {
    if (!m_thumbnailLoaded || !m_thumbnailImage) return;

    static const unsigned char s_clearPixel[] = {
        0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 0, 1, 0, 32, 0,
        0, 0, 0, 0
    };
    m_thumbnailImage->setImageFromMem(s_clearPixel, sizeof(s_clearPixel));
    m_thumbnailLoaded = false;
}

void MangaItemCell::resetThumbnailLoadState() {
    m_thumbnailLoaded = false;
}

void MangaItemCell::loadThumbnail() {
    if (!m_thumbnailImage || m_thumbnailLoaded) return;

    m_thumbnailLoaded = true;

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
    refreshLibraryBadge();
    if (!m_showLibraryBadge) {
        auto* hint = ensureStartHintIcon();
        if (!m_startHintImageLoaded) {
            hint->setImageFromFile("app0:resources/images/start_button.png");
            m_startHintImageLoaded = true;
        }
        hint->setVisibility(brls::Visibility::VISIBLE);
    }
}

void MangaItemCell::onFocusLost() {
    brls::Box::onFocusLost();
    updateFocusInfo(false);
    if (m_startHintIcon) {
        m_startHintIcon->setVisibility(brls::Visibility::GONE);
    }
}

void MangaItemCell::setPressed(bool pressed) {
    m_pressed = pressed;
}

void MangaItemCell::draw(NVGcontext* vg, float x, float y, float width, float height,
                          brls::Style style, brls::FrameContext* ctx) {
    // Draw child views via borealis (only Image + lazy badges in hierarchy now)
    brls::Box::draw(vg, x, y, width, height, style, ctx);

    // --- Flat-rendered title overlay ---
    // PERF: Pre-splits title into cached lines on text change, then draws with
    // nvgText (no wrapping) instead of nvgTextBox (wraps every frame).
    // Also caches badge text dimensions. Saves ~0.2ms/cell × 18 cells = ~3.6ms/frame.
    if (m_showOverlay && !m_titleText.empty()) {
        float textW = width - m_overlayPadSide * 2.0f;

        // Recompute cached text layout when text/font/width changes (not every frame)
        if (m_overlayDirty || m_cachedCellWidth != width) {
            m_cachedCellWidth = width;

            nvgFontFace(vg, "regular");
            nvgFontSize(vg, static_cast<float>(m_titleFontSize));
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

            // Measure line height from font metrics
            nvgTextMetrics(vg, nullptr, nullptr, &m_cachedLineHeight);

            // Pre-split title into lines by measuring word-by-word
            m_cachedLine1.clear();
            m_cachedLine2.clear();
            const char* str = m_titleText.c_str();
            const char* strEnd = str + m_titleText.size();

            // Check if full title fits in one line
            float fullBounds[4];
            nvgTextBounds(vg, 0, 0, str, strEnd, fullBounds);
            float fullW = fullBounds[2] - fullBounds[0];

            if (fullW <= textW) {
                // Single line - no wrapping needed
                m_cachedLine1 = m_titleText;
                m_cachedTitleBlockH = m_cachedLineHeight;
            } else {
                // Find word break point for line 1
                const char* lastSpace = nullptr;
                const char* lastGoodEnd = str;
                for (const char* p = str; p <= strEnd; p++) {
                    if (p == strEnd || *p == ' ') {
                        float wb[4];
                        nvgTextBounds(vg, 0, 0, str, p, wb);
                        if (wb[2] - wb[0] > textW && lastGoodEnd > str) {
                            break;
                        }
                        lastGoodEnd = p;
                        if (p < strEnd && *p == ' ') lastSpace = p;
                    }
                }
                // Prefer breaking at word boundary
                const char* breakAt = (lastSpace && lastSpace >= lastGoodEnd) ? lastSpace : lastGoodEnd;
                if (breakAt <= str) breakAt = lastGoodEnd;  // Fallback

                m_cachedLine1.assign(str, breakAt);
                const char* line2Start = breakAt;
                if (line2Start < strEnd && *line2Start == ' ') line2Start++;
                if (line2Start < strEnd) {
                    m_cachedLine2.assign(line2Start, strEnd);
                }
                int numLines = m_cachedLine2.empty() ? 1 : 2;
                m_cachedTitleBlockH = numLines * m_cachedLineHeight;
            }

            // Cache badge text dimensions
            if (m_showUnread && !m_unreadText.empty()) {
                nvgFontSize(vg, static_cast<float>(m_unreadFontSize));
                float bb[4];
                nvgTextBounds(vg, 0, 0, m_unreadText.c_str(), nullptr, bb);
                m_cachedBadgeTextW = bb[2] - bb[0];
                m_cachedBadgeTextH = bb[3] - bb[1];
            }

            m_overlayDirty = false;
        }

        float overlayH = m_overlayMaxHeight;
        float overlayY = y + height - overlayH;

        // Clip to overlay area
        nvgSave(vg);
        nvgIntersectScissor(vg, x, overlayY, width, overlayH);

        // Semi-transparent background
        nvgBeginPath(vg);
        nvgRect(vg, x, overlayY, width, overlayH);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 180));
        nvgFill(vg);

        // Draw pre-split title lines with nvgText (no per-frame wrapping)
        float textX = x + m_overlayPadSide;
        float textY = overlayY + m_overlayPadTop;

        nvgFontFace(vg, "regular");
        nvgFontSize(vg, static_cast<float>(m_titleFontSize));
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFillColor(vg, nvgRGB(255, 255, 255));

        if (!m_cachedLine1.empty()) {
            nvgText(vg, textX, textY, m_cachedLine1.c_str(), nullptr);
        }
        if (!m_cachedLine2.empty()) {
            nvgText(vg, textX, textY + m_cachedLineHeight, m_cachedLine2.c_str(), nullptr);
        }

        // Subtitle text (below title, using cached title height)
        if (!m_subtitleText.empty()) {
            float titleBottom = textY + m_cachedTitleBlockH;
            nvgFontSize(vg, static_cast<float>(m_subtitleFontSize));
            nvgFillColor(vg, m_subtitleColor);
            nvgText(vg, textX, titleBottom + 1.0f, m_subtitleText.c_str(), nullptr);
        }

        nvgRestore(vg);
    }

    // --- Flat-rendered unread badge (uses cached dimensions) ---
    if (m_showUnread && !m_unreadText.empty()) {
        // Cache badge dimensions if not already done (handles compact mode where overlay is hidden)
        if (m_overlayDirty || m_cachedBadgeTextW == 0) {
            nvgFontFace(vg, "regular");
            nvgFontSize(vg, static_cast<float>(m_unreadFontSize));
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            float bb[4];
            nvgTextBounds(vg, 0, 0, m_unreadText.c_str(), nullptr, bb);
            m_cachedBadgeTextW = bb[2] - bb[0];
            m_cachedBadgeTextH = bb[3] - bb[1];
        }

        float badgeX = x + static_cast<float>(m_unreadMarginLeft);
        float badgeY = y + static_cast<float>(m_unreadMarginTop);
        float padX = 4.0f;
        float padY = 2.0f;

        // Badge background (teal) - uses cached text dimensions
        nvgBeginPath(vg);
        nvgRoundedRect(vg, badgeX, badgeY, m_cachedBadgeTextW + padX * 2, m_cachedBadgeTextH + padY * 2, 2.0f);
        nvgFillColor(vg, m_unreadBgColor);
        nvgFill(vg);

        // Badge text (white)
        nvgFontFace(vg, "regular");
        nvgFontSize(vg, static_cast<float>(m_unreadFontSize));
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgText(vg, badgeX + padX, badgeY + padY, m_unreadText.c_str(), nullptr);
    }

    // Pressed overlay for touch feedback
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
        this->setBorderColor(Application::getInstance().getTealColor());
        this->setBorderThickness(3.0f);
    } else {
        this->setBorderColor(nvgRGBA(0, 0, 0, 0));
        this->setBorderThickness(0.0f);
    }
}

void MangaItemCell::updateFocusInfo(bool focused) {
    if (focused) {
        // Show full title on focus
        m_titleText = m_manga.title;
        m_overlayDirty = true;

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
            auto* desc = ensureDescriptionLabel();
            desc->setText(info);
            desc->setVisibility(brls::Visibility::VISIBLE);
        }
    } else {
        // Restore truncated title
        m_titleText = m_originalTitle;
        m_overlayDirty = true;
        if (m_descriptionLabel) {
            m_descriptionLabel->setVisibility(brls::Visibility::GONE);
        }
    }
}

void MangaItemCell::setGridColumns(int columns) {
    if (m_gridColumns == columns) return;
    m_gridColumns = columns;
    m_overlayDirty = true;  // Font sizes change with column count

    // Adapt title font size based on grid column count
    if (columns <= 4) {
        m_titleFontSize = 14;
    } else if (columns <= 6) {
        m_titleFontSize = 11;
    } else {
        m_titleFontSize = 9;
    }

    // Adapt subtitle font size
    if (columns <= 4) {
        m_subtitleFontSize = 11;
    } else if (columns <= 6) {
        m_subtitleFontSize = 9;
    } else {
        m_subtitleFontSize = 8;
    }

    // Adjust overlay max height for font size
    if (columns <= 4) {
        m_overlayMaxHeight = 66;
    } else if (columns <= 6) {
        m_overlayMaxHeight = 50;
    } else {
        m_overlayMaxHeight = 42;
    }

    // Scale unread badge font size and margin with grid size
    if (columns <= 4) {
        m_unreadFontSize = 13;
        m_unreadMarginTop = 8;
        m_unreadMarginLeft = 8;
    } else if (columns <= 6) {
        m_unreadFontSize = 10;
        m_unreadMarginTop = 6;
        m_unreadMarginLeft = 6;
    } else {
        m_unreadFontSize = 8;
        m_unreadMarginTop = 4;
        m_unreadMarginLeft = 4;
    }

    // Scale NEW badge font size and position with grid size (only if already created)
    if (m_newBadge) {
        int newFontSize = (columns <= 4) ? 10 : (columns >= 8) ? 7 : 8;
        int newTopPos = (columns <= 4) ? 34 : (columns >= 8) ? 20 : 26;
        m_newBadge->setFontSize(newFontSize);
        if (!m_listMode) {
            m_newBadge->setPositionTop(newTopPos);
        }
    }

    // Scale star badge size with grid size (only if already created)
    if (m_starBadge) {
        int starSize = (columns <= 4) ? 20 : (columns >= 8) ? 12 : 16;
        m_starBadge->setWidth(starSize);
        m_starBadge->setHeight(starSize);
    }
}

void MangaItemCell::setCompactMode(bool compact) {
    if (m_compactMode == compact) return;
    m_compactMode = compact;
    m_listMode = false;
    applyDisplayMode();
}

void MangaItemCell::setListMode(bool listMode) {
    if (m_listMode == listMode) return;
    m_listMode = listMode;
    m_compactMode = false;
    applyDisplayMode();
}

void MangaItemCell::setShowLibraryBadge(bool show) {
    if (m_showLibraryBadge == show) return;
    m_showLibraryBadge = show;
    bool inLib = m_showLibraryBadge && (m_manga.inLibrary || Application::getInstance().isRecentlyAdded(m_manga.id))
                 && !Application::getInstance().isRecentlyRemoved(m_manga.id);
    if (inLib) {
        auto* star = ensureStarBadge();
        if (!m_starImageLoaded) {
            star->setImageFromFile("app0:resources/icons/star.png");
            m_starImageLoaded = true;
        }
        star->setVisibility(brls::Visibility::VISIBLE);
    } else if (m_starBadge) {
        m_starBadge->setVisibility(brls::Visibility::GONE);
    }
    if (m_startHintIcon && m_showLibraryBadge) {
        m_startHintIcon->setVisibility(brls::Visibility::GONE);
    }
}

void MangaItemCell::refreshLibraryBadge() {
    if (!m_showLibraryBadge) return;
    bool inLib = (m_manga.inLibrary || Application::getInstance().isRecentlyAdded(m_manga.id))
                 && !Application::getInstance().isRecentlyRemoved(m_manga.id);
    if (inLib) {
        auto* star = ensureStarBadge();
        if (!m_starImageLoaded) {
            star->setImageFromFile("app0:resources/icons/star.png");
            m_starImageLoaded = true;
        }
        star->setVisibility(brls::Visibility::VISIBLE);
    } else if (m_starBadge) {
        m_starBadge->setVisibility(brls::Visibility::GONE);
    }
}

void MangaItemCell::setListRowSize(int rowSize) {
    (void)rowSize;
}

// --- Lazy creation helpers ---

brls::Label* MangaItemCell::ensureNewBadge() {
    if (!m_newBadge) {
        m_newBadge = new brls::Label();
        m_newBadge->setFontSize(8);
        m_newBadge->setText("NEW");
        m_newBadge->setTextColor(nvgRGB(255, 255, 255));
        m_newBadge->setBackgroundColor(nvgRGBA(231, 76, 60, 255));
        m_newBadge->setPositionType(brls::PositionType::ABSOLUTE);
        m_newBadge->setPositionTop(26);
        m_newBadge->setPositionLeft(0);
        m_newBadge->setVisibility(brls::Visibility::GONE);
        this->addView(m_newBadge);
    }
    return m_newBadge;
}

brls::Image* MangaItemCell::ensureStarBadge() {
    if (!m_starBadge) {
        m_starBadge = new brls::Image();
        m_starBadge->setWidth(16);
        m_starBadge->setHeight(16);
        m_starBadge->setScalingType(brls::ImageScalingType::FIT);
        m_starBadge->setPositionType(brls::PositionType::ABSOLUTE);
        m_starBadge->setPositionTop(6);
        m_starBadge->setPositionRight(6);
        m_starBadge->setVisibility(brls::Visibility::GONE);
        this->addView(m_starBadge);
    }
    return m_starBadge;
}

brls::Image* MangaItemCell::ensureStartHintIcon() {
    if (!m_startHintIcon) {
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
    return m_startHintIcon;
}

brls::Box* MangaItemCell::ensureListInfoBox() {
    if (!m_listInfoBox) {
        m_listInfoBox = new brls::Box();
        m_listInfoBox->setAxis(brls::Axis::COLUMN);
        m_listInfoBox->setJustifyContent(brls::JustifyContent::CENTER);
        m_listInfoBox->setAlignItems(brls::AlignItems::FLEX_START);
        m_listInfoBox->setPadding(8, 16, 8, 16);
        m_listInfoBox->setGrow(1.0f);
        m_listInfoBox->setVisibility(brls::Visibility::GONE);

        m_listTitleLabel = new brls::Label();
        m_listTitleLabel->setFontSize(14);
        m_listTitleLabel->setTextColor(nvgRGB(255, 255, 255));
        m_listInfoBox->addView(m_listTitleLabel);

        this->addView(m_listInfoBox);
    }
    return m_listInfoBox;
}

brls::Label* MangaItemCell::ensureDescriptionLabel() {
    if (!m_descriptionLabel) {
        m_descriptionLabel = new brls::Label();
        m_descriptionLabel->setFontSize(9);
        m_descriptionLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        m_descriptionLabel->setVisibility(brls::Visibility::GONE);
    }
    return m_descriptionLabel;
}

void MangaItemCell::applyDisplayMode() {
    if (m_listMode) {
        // List mode: horizontal layout with title only (no cover, no overlay)
        this->setAxis(brls::Axis::ROW);
        this->setJustifyContent(brls::JustifyContent::FLEX_START);
        this->setAlignItems(brls::AlignItems::CENTER);

        if (m_thumbnailImage) {
            m_thumbnailImage->setVisibility(brls::Visibility::GONE);
        }

        // Hide flat overlay in list mode
        m_showOverlay = false;

        ensureListInfoBox()->setVisibility(brls::Visibility::VISIBLE);
        if (m_listTitleLabel) {
            m_listTitleLabel->setFontSize(14);
            m_listTitleLabel->setText(m_manga.title);
        }

        // Adjust badge positions for list mode
        if (m_newBadge) {
            m_newBadge->setPositionTop(4);
            m_newBadge->setPositionLeft(40);
        }
        if (m_startHintIcon) {
            m_startHintIcon->setPositionTop(4);
            m_startHintIcon->setPositionRight(4);
        }
        if (m_starBadge) {
            m_starBadge->setPositionTop(4);
            m_starBadge->setPositionRight(72);
        }

        // Adjust flat-rendered unread badge position for list mode
        m_unreadMarginTop = 4;
        m_unreadMarginLeft = 8;

    } else if (m_compactMode) {
        // Compact mode: grid with covers only (no title overlay)
        this->setAxis(brls::Axis::COLUMN);
        this->setJustifyContent(brls::JustifyContent::FLEX_END);
        this->setAlignItems(brls::AlignItems::STRETCH);

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

        // Hide flat overlay in compact mode
        m_showOverlay = false;

        if (m_listInfoBox) {
            m_listInfoBox->setVisibility(brls::Visibility::GONE);
        }

        // Restore badge positions for grid mode
        if (m_newBadge) {
            int newTop = (m_gridColumns <= 4) ? 34 : (m_gridColumns >= 8) ? 20 : 26;
            m_newBadge->setPositionTop(newTop);
            m_newBadge->setPositionLeft(0);
        }
        if (m_startHintIcon) {
            m_startHintIcon->setPositionTop(6);
            m_startHintIcon->setPositionRight(6);
        }
        if (m_starBadge) {
            int starTop = (m_gridColumns <= 4) ? 34 : (m_gridColumns >= 8) ? 20 : 26;
            m_starBadge->setPositionTop(starTop);
            m_starBadge->setPositionRight(6);
        }

        // Restore flat badge position
        m_unreadMarginTop = (m_gridColumns <= 4) ? 8 : (m_gridColumns >= 8) ? 4 : 6;
        m_unreadMarginLeft = m_unreadMarginTop;

    } else {
        // Normal grid mode: cover + flat-rendered title overlay
        this->setAxis(brls::Axis::COLUMN);
        this->setJustifyContent(brls::JustifyContent::FLEX_END);
        this->setAlignItems(brls::AlignItems::STRETCH);

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

        // Show flat overlay in normal grid mode
        m_showOverlay = true;
        m_overlayDirty = true;

        if (m_listInfoBox) {
            m_listInfoBox->setVisibility(brls::Visibility::GONE);
        }

        // Restore badge positions for grid mode
        if (m_newBadge) {
            int newTop = (m_gridColumns <= 4) ? 34 : (m_gridColumns >= 8) ? 20 : 26;
            m_newBadge->setPositionTop(newTop);
            m_newBadge->setPositionLeft(0);
        }
        if (m_startHintIcon) {
            m_startHintIcon->setPositionTop(6);
            m_startHintIcon->setPositionRight(6);
        }
        if (m_starBadge) {
            int starTop = (m_gridColumns <= 4) ? 34 : (m_gridColumns >= 8) ? 20 : 26;
            m_starBadge->setPositionTop(starTop);
            m_starBadge->setPositionRight(6);
        }

        // Restore flat badge position
        m_unreadMarginTop = (m_gridColumns <= 4) ? 8 : (m_gridColumns >= 8) ? 4 : 6;
        m_unreadMarginLeft = m_unreadMarginTop;
    }
}

} // namespace vitasuwayomi
