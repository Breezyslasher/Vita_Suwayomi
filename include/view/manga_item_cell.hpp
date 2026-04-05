/**
 * VitaSuwayomi - Manga Item Cell
 *
 * Minimal focusable cell with a cover image. No title/badge overlays —
 * just a rounded card that shows the manga cover.
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
    void setMangaDeferred(const Manga& manga) { setManga(manga); }
    void updateMangaData(const Manga& manga) { m_manga = manga; }

    void loadThumbnailIfNeeded();
    void unloadThumbnail();
    void resetThumbnailLoadState() { m_thumbnailLoaded = false; }

    bool isThumbnailLoaded() const { return m_thumbnailLoaded; }
    const Manga& getManga() const { return m_manga; }

    void setCompactMode(bool) {}
    void setListMode(bool) {}
    void setListRowSize(int) {}
    void setGridColumns(int) {}
    void setShowLibraryBadge(bool) {}
    void refreshLibraryBadge() {}

    void setPressed(bool pressed);
    bool isPressed() const { return m_pressed; }

    void setSelected(bool selected) { m_selected = selected; }
    bool isSelected() const { return m_selected; }

    static brls::View* create() { return new MangaItemCell(); }

protected:
    void onFocusGained() override;
    void onFocusLost() override;

private:
    void loadThumbnail();

    Manga m_manga;
    brls::Image* m_thumbnailImage = nullptr;
    bool m_thumbnailLoaded = false;
    bool m_pressed = false;
    bool m_selected = false;

    // Shared flag so in-flight ImageLoader callbacks skip writing to us
    // after this cell has been destroyed.
    std::shared_ptr<bool> m_alive;
};

using MediaItemCell = MangaItemCell;

} // namespace vitasuwayomi
