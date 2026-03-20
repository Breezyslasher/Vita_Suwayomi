/**
 * VitaSuwayomi - Manga Item Cell
 * A cell for displaying manga items in a grid
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include <memory>

namespace vitasuwayomi {

class MangaItemCell : public brls::Box {
public:
    MangaItemCell();
    ~MangaItemCell() override;

    void setManga(const Manga& manga);
    void setMangaDeferred(const Manga& manga);  // Set data without loading image
    void updateMangaData(const Manga& manga);   // Update data in place without reloading thumbnail
    void loadThumbnailIfNeeded();  // Load image if not already loaded
    void unloadThumbnail();        // Free GPU texture for off-screen cells (can reload later)
    void resetThumbnailLoadState();  // Mark thumbnail as not loaded (allows reload without clearing image)
    bool isThumbnailLoaded() const { return m_thumbnailLoaded; }
    const Manga& getManga() const { return m_manga; }

    // Display mode
    void setCompactMode(bool compact);  // Hide title overlay (covers only)
    void setListMode(bool listMode);    // Horizontal list layout
    void setListRowSize(int rowSize);   // List row size: 0=small, 1=medium, 2=large, 3=auto
    void setGridColumns(int columns);   // Set grid column count (adapts title font/truncation)
    void setShowLibraryBadge(bool show);  // Show star badge for library items (browser/search only)
    void refreshLibraryBadge();  // Re-evaluate star badge visibility from recent add/remove state

    void onFocusGained() override;
    void onFocusLost() override;
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    // Touch press feedback
    void setPressed(bool pressed);
    bool isPressed() const { return m_pressed; }

    // Selection mode
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

    static brls::View* create();

private:
    void loadThumbnail();
    void updateFocusInfo(bool focused);
    void updateDisplay();
    void updateSelectionVisual();
    void applyDisplayMode();

    // Lazy creation helpers - these views are only created when first needed
    brls::Label* ensureNewBadge();
    brls::Image* ensureStarBadge();
    brls::Image* ensureStartHintIcon();
    brls::Box* ensureListInfoBox();  // Also creates m_listTitleLabel
    brls::Label* ensureDescriptionLabel();

    bool m_selected = false;
    bool m_pressed = false;  // Touch press-down state for visual feedback
    bool m_compactMode = false;
    bool m_listMode = false;
    bool m_showLibraryBadge = false;  // Whether to show star badge for library items
    int m_listRowSize = 1;  // 0=small, 1=medium, 2=large, 3=auto
    int m_gridColumns = 6;  // Grid column count (affects title font/truncation)

    Manga m_manga;
    std::string m_originalTitle;
    bool m_thumbnailLoaded = false;
    bool m_starImageLoaded = false;       // Lazy: load star.png only when first shown
    bool m_startHintImageLoaded = false;  // Lazy: load start_button.png only when first shown

    brls::Image* m_thumbnailImage = nullptr;
    brls::Box* m_listInfoBox = nullptr;   // Info container (list mode)
    brls::Label* m_listTitleLabel = nullptr;  // Title label for list mode
    brls::Label* m_descriptionLabel = nullptr;
    brls::Rectangle* m_progressBar = nullptr;
    brls::Label* m_newBadge = nullptr;  // NEW indicator for recently updated
    brls::Image* m_startHintIcon = nullptr;  // Start button hint shown on focus
    brls::Image* m_starBadge = nullptr;  // Star icon for manga in library

    // --- Flat-rendered overlay state (no borealis sub-views, drawn directly via NanoVG) ---
    // Eliminates 4 frame() calls per cell per frame vs view hierarchy approach.
    std::string m_titleText;
    std::string m_subtitleText;
    std::string m_unreadText;
    int m_titleFontSize = 11;
    int m_subtitleFontSize = 9;
    int m_unreadFontSize = 10;
    float m_overlayMaxHeight = 50.0f;
    float m_overlayPadTop = 6.0f;
    float m_overlayPadSide = 6.0f;
    float m_overlayPadBottom = 4.0f;
    bool m_showOverlay = true;   // false in compact/list mode
    bool m_showUnread = false;
    NVGcolor m_subtitleColor;
    NVGcolor m_unreadBgColor;
    int m_unreadMarginTop = 6;
    int m_unreadMarginLeft = 6;

    // --- Cached text measurements (avoids per-frame nvgTextBox wrapping + measurement) ---
    bool m_overlayDirty = true;       // Recompute cached text on next draw
    float m_cachedCellWidth = 0;      // Cell width when cache was computed
    std::string m_cachedLine1;        // Pre-split title line 1
    std::string m_cachedLine2;        // Pre-split title line 2
    float m_cachedLineHeight = 0;     // Font line height for multi-line spacing
    float m_cachedTitleBlockH = 0;    // Total height of rendered title text
    float m_cachedSubtitleLineH = 0;  // Subtitle line height
    float m_cachedBadgeTextW = 0;     // Badge text width
    float m_cachedBadgeTextH = 0;     // Badge text height

    // Shared flag for async callback safety - set to false in destructor
    // so pending ImageLoader callbacks skip writing to our destroyed m_thumbnailImage
    std::shared_ptr<bool> m_alive;
};

// Alias for backward compatibility
using MediaItemCell = MangaItemCell;

} // namespace vitasuwayomi
