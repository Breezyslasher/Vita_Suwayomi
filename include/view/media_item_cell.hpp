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
    void loadThumbnailIfNeeded();  // Load image if not already loaded
    const Manga& getManga() const { return m_manga; }

    // Display mode
    void setCompactMode(bool compact);  // Hide title overlay (covers only)
    void setListMode(bool listMode);    // Horizontal list layout

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

    Manga m_manga;
    std::string m_originalTitle;
    bool m_thumbnailLoaded = false;

    brls::Image* m_thumbnailImage = nullptr;
    brls::Box* m_titleOverlay = nullptr;  // Title overlay container (grid mode)
    brls::Box* m_listInfoBox = nullptr;   // Info container (list mode)
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_subtitleLabel = nullptr;
    brls::Label* m_descriptionLabel = nullptr;
    brls::Rectangle* m_progressBar = nullptr;
    brls::Label* m_unreadBadge = nullptr;
    brls::Label* m_downloadBadge = nullptr;
    brls::Label* m_newBadge = nullptr;  // NEW indicator for recently updated
    brls::Image* m_startHintIcon = nullptr;  // Start button hint shown on focus
};

// Alias for backward compatibility
using MediaItemCell = MangaItemCell;

} // namespace vitasuwayomi
