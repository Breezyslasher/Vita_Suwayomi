/**
 * VitaSuwayomi - Manga Item Cell
 *
 * Focusable cell with cover image and a title box at the bottom.
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
    void updateMangaData(const Manga& manga);

    void loadThumbnailIfNeeded();
    void unloadThumbnail();
    void resetThumbnailLoadState() { m_thumbnailLoaded = false; }

    bool isThumbnailLoaded() const { return m_thumbnailLoaded; }
    const Manga& getManga() const { return m_manga; }

    void setCompactMode(bool compact);
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

    // When false, hide title views during fast scrolling.
    static void setTitlesEnabled(bool enabled);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

protected:
    void onFocusGained() override;
    void onFocusLost() override;

private:
    void loadThumbnail();
    void syncTitleVisibility();

    Manga m_manga;
    std::string m_title;
    brls::Image* m_thumbnailImage = nullptr;
    brls::Box* m_titleBox = nullptr;
    brls::Label* m_titleLabel = nullptr;
    bool m_thumbnailLoaded = false;
    bool m_pressed = false;
    bool m_selected = false;
    bool m_showTitle = true;
    bool m_titleVisible = true;

    // Shared flag so in-flight ImageLoader callbacks skip writing to us
    // after this cell has been destroyed.
    std::shared_ptr<bool> m_alive;
};

using MediaItemCell = MangaItemCell;

} // namespace vitasuwayomi
