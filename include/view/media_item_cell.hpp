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

    bool m_selected = false;

    Manga m_manga;
    std::string m_originalTitle;
    bool m_thumbnailLoaded = false;

    brls::Image* m_thumbnailImage = nullptr;
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_subtitleLabel = nullptr;
    brls::Label* m_descriptionLabel = nullptr;
    brls::Rectangle* m_progressBar = nullptr;
    brls::Label* m_unreadBadge = nullptr;
    brls::Label* m_downloadBadge = nullptr;
    brls::Label* m_newBadge = nullptr;  // NEW indicator for recently updated
};

// Alias for backward compatibility
using MediaItemCell = MangaItemCell;

} // namespace vitasuwayomi
