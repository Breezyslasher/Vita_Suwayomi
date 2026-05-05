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
    void updateMangaData(const Manga& manga);

    void loadThumbnailIfNeeded();
    void unloadThumbnail();
    void resetThumbnailLoadState();

    bool isThumbnailLoaded() const { return m_thumbnailLoaded; }
    const Manga& getManga() const { return m_manga; }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    int getCoverImage() const { return m_nvgCover; }
    int getCoverWidth() const { return m_coverW; }
    int getCoverHeight() const { return m_coverH; }

    float getDrawX() const { return m_drawX; }
    float getDrawY() const { return m_drawY; }
    float getDrawW() const { return m_drawW; }
    float getDrawH() const { return m_drawH; }

    void setCompactMode(bool compact) { m_compact = compact; }
    void setListMode(bool list) { m_listMode = list; }
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
    int m_nvgCover = 0;
    int m_coverW = 0;
    int m_coverH = 0;
    float m_drawX = 0;
    float m_drawY = 0;
    float m_drawW = 0;
    float m_drawH = 0;
    bool m_pressed = false;
    bool m_compact = false;
    bool m_listMode = false;
    bool m_thumbnailLoaded = false;
    std::shared_ptr<bool> m_alive;
};

using MediaItemCell = MangaItemCell;

} // namespace vitasuwayomi
