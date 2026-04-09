/**
 * VitaSuwayomi - Manga Item Cell implementation
 * Minimal cell: rounded card with a cover image and title label.
 */

#include "view/manga_item_cell.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/image_loader.hpp"
#include <fstream>
#include <vector>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

namespace vitasuwayomi {

static bool s_titlesEnabled = true;

void MangaItemCell::setTitlesEnabled(bool enabled) {
    s_titlesEnabled = enabled;
}

static void loadLocalCoverToImage(brls::Image* image, const std::string& localPath) {
    if (localPath.empty() || !image) return;

#ifdef __vita__
    SceUID fd = sceIoOpen(localPath.c_str(), SCE_O_RDONLY, 0);
    if (fd >= 0) {
        SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoLseek(fd, 0, SCE_SEEK_SET);
        if (size > 0 && size < 10 * 1024 * 1024) {
            std::vector<uint8_t> data(size);
            if (sceIoRead(fd, data.data(), size) == size) {
                image->setImageFromMem(data.data(), data.size());
            }
        }
        sceIoClose(fd);
    }
#else
    std::ifstream file(localPath, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        if (size > 0 && size < 10 * 1024 * 1024) {
            std::vector<uint8_t> data(size);
            if (file.read(reinterpret_cast<char*>(data.data()), size)) {
                image->setImageFromMem(data.data(), data.size());
            }
        }
        file.close();
    }
#endif
}

MangaItemCell::MangaItemCell() {
    m_alive = std::make_shared<bool>(true);

    this->setFocusable(true);
    this->setCornerRadius(4.0f);
    this->setBackgroundColor(nvgRGB(40, 40, 48));
    this->setClipsToBounds(true);

    // Cover fills the cell minus a 20px strip at the bottom for title.
    m_thumbnailImage = new brls::Image();
    m_thumbnailImage->setScalingType(brls::ImageScalingType::FILL);
    m_thumbnailImage->setCornerRadius(4.0f);
    m_thumbnailImage->setPositionType(brls::PositionType::ABSOLUTE);
    m_thumbnailImage->setPositionTop(0);
    m_thumbnailImage->setPositionLeft(0);
    m_thumbnailImage->setPositionRight(0);
    m_thumbnailImage->setPositionBottom(20);
    this->addView(m_thumbnailImage);

    m_titleBox = new brls::Box();
    m_titleBox->setPositionType(brls::PositionType::ABSOLUTE);
    m_titleBox->setPositionLeft(0);
    m_titleBox->setPositionRight(0);
    m_titleBox->setPositionBottom(0);
    m_titleBox->setHeight(20);
    m_titleBox->setPaddingLeft(5);
    m_titleBox->setPaddingRight(5);
    m_titleBox->setPaddingTop(4);
    m_titleBox->setBackgroundColor(nvgRGBA(0, 0, 0, 160));
    this->addView(m_titleBox);

    m_titleLabel = new brls::Label();
    m_titleLabel->setFontSize(10);
    m_titleLabel->setTextColor(nvgRGBA(235, 235, 235, 255));
    m_titleBox->addView(m_titleLabel);
    m_titleBox->setVisibility((m_showTitle && s_titlesEnabled) ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

MangaItemCell::~MangaItemCell() {
    if (m_alive) {
        *m_alive = false;
    }
}

void MangaItemCell::setManga(const Manga& manga) {
    m_manga = manga;
    m_title = manga.title;
    if (m_titleLabel) {
        m_titleLabel->setText(m_title);
    }
    m_thumbnailLoaded = false;
}

void MangaItemCell::updateMangaData(const Manga& manga) {
    m_manga = manga;
    m_title = manga.title;
    if (m_titleLabel) {
        m_titleLabel->setText(m_title);
    }
}

void MangaItemCell::loadThumbnailIfNeeded() {
    if (!m_thumbnailLoaded && m_manga.id > 0) {
        loadThumbnail();
    }
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

void MangaItemCell::loadThumbnail() {
    if (!m_thumbnailImage || m_thumbnailLoaded) return;
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

void MangaItemCell::draw(NVGcontext* vg, float x, float y, float width, float height,
                         brls::Style style, brls::FrameContext* ctx) {
    if (m_titleBox) {
        m_titleBox->setVisibility((m_showTitle && s_titlesEnabled) ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

void MangaItemCell::setPressed(bool pressed) {
    m_pressed = pressed;
    this->setBackgroundColor(pressed ? nvgRGB(90, 90, 110) : nvgRGB(60, 60, 70));
}

void MangaItemCell::onFocusGained() {
    brls::Box::onFocusGained();
    this->setBackgroundColor(nvgRGB(100, 120, 160));
}

void MangaItemCell::onFocusLost() {
    brls::Box::onFocusLost();
    this->setBackgroundColor(m_pressed ? nvgRGB(90, 90, 110) : nvgRGB(60, 60, 70));
}

} // namespace vitasuwayomi
