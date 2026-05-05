#include "view/manga_item_cell.hpp"
#include "utils/image_loader.hpp"
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

MangaItemCell::MangaItemCell() {
    m_alive = std::make_shared<bool>(true);
    this->setFocusable(true);
    this->setCornerRadius(4.0f);
    this->setBackgroundColor(nvgRGB(40, 40, 48));
    this->setClipsToBounds(true);

    m_coverImage = new brls::Image();
    m_coverImage->setScalingType(brls::ImageScalingType::FILL);
    m_coverImage->setCornerRadius(4.0f);
    m_coverImage->setWidthPercentage(100.0f);
    m_coverImage->setHeightPercentage(100.0f);
    this->addView(m_coverImage);
}

MangaItemCell::~MangaItemCell() {
    if (m_alive) {
        *m_alive = false;
    }
}

void MangaItemCell::setManga(const Manga& manga) {
    m_manga = manga;
    m_thumbnailLoaded = false;
}

void MangaItemCell::updateMangaData(const Manga& manga) {
    bool coverChanged = (m_manga.thumbnailUrl != manga.thumbnailUrl);
    m_manga = manga;
    if (coverChanged) {
        m_thumbnailLoaded = false;
    }
}

void MangaItemCell::loadThumbnailIfNeeded() {
    if (m_thumbnailLoaded || !m_coverImage) return;
    if (m_manga.id <= 0 && m_manga.thumbnailUrl.empty()) return;
    m_thumbnailLoaded = true;

    SuwayomiClient& client = SuwayomiClient::getInstance();
    std::string url;

    if (!m_manga.thumbnailUrl.empty()) {
        if (m_manga.thumbnailUrl[0] == '/') {
            url = client.getServerUrl();
            while (!url.empty() && url.back() == '/') url.pop_back();
            url += m_manga.thumbnailUrl;
        } else if (m_manga.thumbnailUrl.find("http") == 0) {
            url = m_manga.thumbnailUrl;
        } else {
            url = client.getMangaThumbnailUrl(m_manga.id);
        }
    } else {
        url = client.getMangaThumbnailUrl(m_manga.id);
    }

    ImageLoader::loadAsync(url, nullptr, m_coverImage, m_alive);
}

void MangaItemCell::unloadThumbnail() {
    if (!m_thumbnailLoaded || !m_coverImage) return;

    static const unsigned char s_clearPixel[] = {
        0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 0, 1, 0, 32, 0,
        0, 0, 0, 0
    };
    m_coverImage->setImageFromMem(s_clearPixel, sizeof(s_clearPixel));
    m_thumbnailLoaded = false;
}

void MangaItemCell::resetThumbnailLoadState() {
    m_thumbnailLoaded = false;
}

void MangaItemCell::setPressed(bool pressed) {
    m_pressed = pressed;
}

} // namespace vitasuwayomi
