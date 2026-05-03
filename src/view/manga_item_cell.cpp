/**
 * VitaSuwayomi - Manga Item Cell implementation
 * Empty focusable box - rebuild base for FPS testing.
 */

#include "view/manga_item_cell.hpp"

namespace vitasuwayomi {

MangaItemCell::MangaItemCell() {
    m_alive = std::make_shared<bool>(true);
    this->setFocusable(true);
    this->setCornerRadius(4.0f);
    this->setBackgroundColor(nvgRGB(40, 40, 48));
}

MangaItemCell::~MangaItemCell() {
    if (m_alive) {
        *m_alive = false;
    }
}

void MangaItemCell::setManga(const Manga& manga) {
    m_manga = manga;
}

void MangaItemCell::setPressed(bool pressed) {
    m_pressed = pressed;
}

} // namespace vitasuwayomi
