/**
 * VitaSuwayomi - Manga Item Cell (simple box placeholder)
 *
 * Minimal focusable box per manga for FPS tuning. No cover image,
 * no title, no text — just a solid box so the grid layout works.
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

class MangaItemCell : public brls::Box {
public:
    MangaItemCell() {
        // Make the cell focusable so it works with grid navigation.
        this->setFocusable(true);
        // Solid background so the box is visible.
        this->setBackgroundColor(nvgRGB(60, 60, 70));
        // A small margin border visible via cornerRadius.
        this->setCornerRadius(4.0f);
    }
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

    void setPressed(bool pressed) {
        m_pressed = pressed;
        this->setBackgroundColor(pressed ? nvgRGB(90, 90, 110) : nvgRGB(60, 60, 70));
    }
    bool isPressed() const { return m_pressed; }

    void setSelected(bool selected) { m_selected = selected; }
    bool isSelected() const { return m_selected; }

    static brls::View* create() { return new MangaItemCell(); }

protected:
    void onFocusGained() override {
        brls::Box::onFocusGained();
        this->setBackgroundColor(nvgRGB(100, 120, 160));
    }

    void onFocusLost() override {
        brls::Box::onFocusLost();
        this->setBackgroundColor(m_pressed ? nvgRGB(90, 90, 110) : nvgRGB(60, 60, 70));
    }

private:
    Manga m_manga;
    bool m_thumbnailLoaded = false;
    bool m_pressed = false;
    bool m_selected = false;
};

using MediaItemCell = MangaItemCell;

} // namespace vitasuwayomi
