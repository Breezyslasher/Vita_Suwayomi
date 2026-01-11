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
    const Manga& getManga() const { return m_manga; }

    void onFocusGained() override;
    void onFocusLost() override;

    static brls::View* create();

private:
    void loadThumbnail();
    void updateFocusInfo(bool focused);

    Manga m_manga;
    std::string m_originalTitle;  // Store original truncated title

    brls::Image* m_thumbnailImage = nullptr;
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_subtitleLabel = nullptr;     // Shows author or chapter count
    brls::Label* m_descriptionLabel = nullptr;  // Shows on focus
    brls::Rectangle* m_progressBar = nullptr;   // Unread chapter indicator
    brls::Label* m_unreadBadge = nullptr;       // Unread count badge
};

// Alias for backward compatibility
using MediaItemCell = MangaItemCell;

} // namespace vitasuwayomi
