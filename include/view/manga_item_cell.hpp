/**
 * VitaSuwayomi - Manga Item Cell (temporary placeholder)
 *
 * This no-op implementation keeps the app building while the real
 * media/manga item cell is being rewritten.
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

class MangaItemCell : public brls::Box {
public:
    MangaItemCell() = default;
    ~MangaItemCell() override = default;

    void setManga(const Manga& manga) { m_manga = manga; }
    void setMangaDeferred(const Manga& manga) { m_manga = manga; }
    void updateMangaData(const Manga& manga) { m_manga = manga; }

    void loadThumbnailIfNeeded() { m_thumbnailLoaded = true; }
    void unloadThumbnail() { m_thumbnailLoaded = false; }
    void resetThumbnailLoadState() { m_thumbnailLoaded = false; }

    bool isThumbnailLoaded() const { return m_thumbnailLoaded; }
    const Manga& getManga() const { return m_manga; }

    void setCompactMode(bool) {}
    void setListMode(bool) {}
    void setListRowSize(int) {}
    void setGridColumns(int) {}
    void setShowLibraryBadge(bool) {}
    void refreshLibraryBadge() {}

    void setPressed(bool pressed) { m_pressed = pressed; }
    bool isPressed() const { return m_pressed; }

    void setSelected(bool selected) { m_selected = selected; }
    bool isSelected() const { return m_selected; }

    static brls::View* create() { return new MangaItemCell(); }

private:
    Manga m_manga;
    bool m_thumbnailLoaded = false;
    bool m_pressed = false;
    bool m_selected = false;
};

using MediaItemCell = MangaItemCell;

} // namespace vitasuwayomi
