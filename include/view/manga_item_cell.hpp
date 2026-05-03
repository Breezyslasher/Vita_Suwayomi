/**
 * VitaSuwayomi - Manga Item Cell
 * Empty focusable box - rebuild base for FPS testing.
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include <memory>
#include <string>

namespace vitasuwayomi {

class MangaItemCell : public brls::Box {
public:
    MangaItemCell();
    ~MangaItemCell() override;

    void setManga(const Manga& manga);
    void setMangaDeferred(const Manga& manga) { setManga(manga); }
    void updateMangaData(const Manga& manga) { m_manga = manga; }

    void loadThumbnailIfNeeded() {}
    void unloadThumbnail() {}
    void resetThumbnailLoadState() {}

    bool isThumbnailLoaded() const { return false; }
    const Manga& getManga() const { return m_manga; }

    void setCompactMode(bool) {}
    void setListMode(bool) {}
    void setListRowSize(int) {}
    void setGridColumns(int) {}
    void setShowLibraryBadge(bool) {}
    void refreshLibraryBadge() {}

    void setPressed(bool pressed);
    bool isPressed() const { return m_pressed; }

    void setSelected(bool) {}
    bool isSelected() const { return false; }

    static brls::View* create() { return new MangaItemCell(); }

    static void setTitlesEnabled(bool) {}

private:
    Manga m_manga;
    bool m_pressed = false;
    std::shared_ptr<bool> m_alive;
};

using MediaItemCell = MangaItemCell;

} // namespace vitasuwayomi
