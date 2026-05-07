#include "view/manga_item_cell.hpp"
#include "utils/image_loader.hpp"
#include "app/suwayomi_client.hpp"
#include "platform/platform.hpp"

namespace vitasuwayomi {

static void loadLocalCoverToImage(brls::Image* image, const std::string& localPath) {
    if (localPath.empty() || !image) return;
    auto data = platform::readFile(localPath);
    if (!data.empty() && data.size() < 10 * 1024 * 1024) {
        image->setImageFromMem(data.data(), data.size());
    }
}

MangaItemCell::MangaItemCell() {
    m_alive = std::make_shared<bool>(true);
    this->setFocusable(true);
    this->setCornerRadius(4.0f);
    this->setBackgroundColor(nvgRGB(40, 40, 48));
    this->setClipsToBounds(true);

    m_thumbnailImage = new brls::Image();
    m_thumbnailImage->setScalingType(brls::ImageScalingType::FILL);
    m_thumbnailImage->setCornerRadius(4.0f);
    m_thumbnailImage->setGrow(1.0f);
    m_thumbnailImage->setPositionType(brls::PositionType::ABSOLUTE);
    m_thumbnailImage->setPositionTop(0);
    m_thumbnailImage->setPositionLeft(0);
    m_thumbnailImage->setPositionRight(0);
    m_thumbnailImage->setPositionBottom(0);
    this->addView(m_thumbnailImage);
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
    if (m_thumbnailLoaded) return;
    if (m_manga.id <= 0 && m_manga.thumbnailUrl.empty()) return;
    m_thumbnailLoaded = true;

    if (!m_manga.thumbnailUrl.empty() && m_manga.thumbnailUrl.find("ux0:") == 0) {
        loadLocalCoverToImage(m_thumbnailImage, m_manga.thumbnailUrl);
        return;
    }

    if (m_manga.id <= 0) return;

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

    ImageLoader::loadAsync(url, nullptr, m_thumbnailImage, m_alive);
}

void MangaItemCell::unloadThumbnail() {
    if (!m_thumbnailLoaded || !m_thumbnailImage) return;

    static const unsigned char s_clearPixel[] = {
        0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 0, 1, 0, 32, 0,
        0, 0, 0, 0
    };
    m_thumbnailImage->setImageFromMem(s_clearPixel, sizeof(s_clearPixel));
    m_thumbnailLoaded = false;
}

void MangaItemCell::resetThumbnailLoadState() {
    m_thumbnailLoaded = false;
}

void MangaItemCell::setPressed(bool pressed) {
    m_pressed = pressed;
}

} // namespace vitasuwayomi
