#include "view/manga_item_cell.hpp"
#include "utils/image_loader.hpp"
#include "app/suwayomi_client.hpp"

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
    if (m_nvgCover != 0) {
        NVGcontext* vg = brls::Application::getNVGContext();
        if (vg) nvgDeleteImage(vg, m_nvgCover);
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
        if (m_nvgCover != 0) {
            NVGcontext* vg = brls::Application::getNVGContext();
            if (vg) nvgDeleteImage(vg, m_nvgCover);
            m_nvgCover = 0;
            m_coverW = 0;
            m_coverH = 0;
        }
        m_thumbnailLoaded = false;
    }
}

void MangaItemCell::loadThumbnailIfNeeded() {
    if (m_thumbnailLoaded) return;
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

    std::weak_ptr<bool> weakAlive(m_alive);
    MangaItemCell* self = this;

    ImageLoader::loadCoverAsync(url,
        [self, weakAlive](int nvgImg, int w, int h) {
            auto alive = weakAlive.lock();
            if (!alive || !*alive) {
                NVGcontext* vg = brls::Application::getNVGContext();
                if (vg && nvgImg != 0) nvgDeleteImage(vg, nvgImg);
                return;
            }
            self->m_nvgCover = nvgImg;
            self->m_coverW = w;
            self->m_coverH = h;
        },
        m_alive);
}

void MangaItemCell::unloadThumbnail() {
    if (!m_thumbnailLoaded) return;
    if (m_nvgCover != 0) {
        NVGcontext* vg = brls::Application::getNVGContext();
        if (vg) nvgDeleteImage(vg, m_nvgCover);
        m_nvgCover = 0;
        m_coverW = 0;
        m_coverH = 0;
    }
    m_thumbnailLoaded = false;
}

void MangaItemCell::resetThumbnailLoadState() {
    m_thumbnailLoaded = false;
}

void MangaItemCell::setPressed(bool pressed) {
    m_pressed = pressed;
}

} // namespace vitasuwayomi
