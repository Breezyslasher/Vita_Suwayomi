/**
 * VitaSuwayomi - Manga Item Cell
 * A cell for displaying manga items in a grid
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

class MangaItemCell : public brls::Box {
public:
    MangaItemCell();

    void setManga(const Manga& manga);
    void setMangaDeferred(const Manga& manga);  // Set data without loading image
    void updateMangaData(const Manga& manga);   // Update data in place without reloading thumbnail
    void loadThumbnailIfNeeded();  // Load image if not already loaded
    const Manga& getManga() const { return m_manga; }

    // Display mode
    void setCompactMode(bool compact);  // Hide title overlay (covers only)
    void setListMode(bool listMode);    // Horizontal list layout
    void setListRowSize(int rowSize);   // List row size: 0=small, 1=medium, 2=large, 3=auto
    void setShowLibraryBadge(bool show);  // Show star badge for library items (browser/search only)

    void onFocusGained() override;
    void onFocusLost() override;

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

    bool m_selected = false;
    bool m_compactMode = false;
    bool m_listMode = false;
    bool m_showLibraryBadge = false;  // Whether to show star badge for library items
    int m_listRowSize = 1;  // 0=small, 1=medium, 2=large, 3=auto

    Manga m_manga;
    std::string m_originalTitle;
    bool m_thumbnailLoaded = false;
    bool m_starImageLoaded = false;       // Lazy: load star.png only when first shown
    bool m_startHintImageLoaded = false;  // Lazy: load start_button.png only when first shown

    brls::Image* m_thumbnailImage = nullptr;
    brls::Box* m_titleOverlay = nullptr;  // Title overlay container (grid mode)
    brls::Box* m_listInfoBox = nullptr;   // Info container (list mode)
    brls::Label* m_listTitleLabel = nullptr;  // Title label for list mode
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_subtitleLabel = nullptr;
    brls::Label* m_descriptionLabel = nullptr;
    brls::Rectangle* m_progressBar = nullptr;
    brls::Label* m_unreadBadge = nullptr;
    brls::Label* m_newBadge = nullptr;  // NEW indicator for recently updated
    brls::Image* m_startHintIcon = nullptr;  // Start button hint shown on focus
    brls::Image* m_starBadge = nullptr;  // Star icon for manga in library
};

// Alias for backward compatibility
using MediaItemCell = MangaItemCell;

} // namespace vitasuwayomi
