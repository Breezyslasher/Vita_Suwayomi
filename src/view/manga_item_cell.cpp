/**
 * VitaSuwayomi - Manga Item Cell implementation
 * Minimal cell: rounded card with a cover image.
 */

#include "view/manga_item_cell.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/image_loader.hpp"
#include <cmath>
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

    // Cover fills the cell minus a 20px strip at the bottom for a single
    // title line (drawn flat via NanoVG in draw()). Going to 1 line cuts
    // per-frame text cost in half (~7ms saved on Vita at 24 visible cells).
    m_thumbnailImage = new brls::Image();
    m_thumbnailImage->setScalingType(brls::ImageScalingType::FILL);
    m_thumbnailImage->setCornerRadius(4.0f);
    m_thumbnailImage->setPositionType(brls::PositionType::ABSOLUTE);
    m_thumbnailImage->setPositionTop(0);
    m_thumbnailImage->setPositionLeft(0);
    m_thumbnailImage->setPositionRight(0);
    m_thumbnailImage->setPositionBottom(20);
    this->addView(m_thumbnailImage);
}

MangaItemCell::~MangaItemCell() {
    if (m_alive) {
        *m_alive = false;
    }
}

void MangaItemCell::setManga(const Manga& manga) {
    m_manga = manga;
    m_title = manga.title;
    m_line1.clear();
    m_wrappedForWidth = -1.0f;
    m_thumbnailLoaded = false;
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

// Binary-search the largest byte prefix of `str` (len `len`) that fits
// into `maxW` when measured through nvgTextBounds. Returns the byte
// count.
static int fitPrefix(NVGcontext* vg, const char* str, int len, float maxW) {
    float bounds[4];
    int lo = 0, hi = len, best = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        nvgTextBounds(vg, 0, 0, str, str + mid, bounds);
        float w = bounds[2] - bounds[0];
        if (w <= maxW) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

void MangaItemCell::draw(NVGcontext* vg, float x, float y, float width, float height,
                         brls::Style style, brls::FrameContext* ctx) {
    // Draw the cover image (child view)
    brls::Box::draw(vg, x, y, width, height, style, ctx);

    if (!m_showTitle || m_title.empty() || !s_titlesEnabled) return;

    // --- Flat-rendered single-line title below the cover ---
    // One line keeps text cost per cell to a single nvgText call; on
    // Vita with 24 visible cells this is ~7ms/frame vs ~14ms for 2 lines.
    constexpr float kPadSide = 5.0f;
    constexpr float kFontSize = 10.0f;
    constexpr float kTitleAreaH = 20.0f;  // matches thumbnail positionBottom
    constexpr float kTitleTopPad = 4.0f;

    float textW = width - kPadSide * 2.0f;

    nvgFontFace(vg, "regular");
    nvgFontSize(vg, kFontSize);

    // Re-wrap only when cell width changed or title was reassigned.
    if (m_wrappedForWidth < 0.0f || std::fabs(m_wrappedForWidth - width) > 0.5f) {
        m_wrappedForWidth = width;
        m_line1.clear();

        float bounds[4];
        const char* s = m_title.c_str();
        int len = static_cast<int>(m_title.size());

        nvgTextBounds(vg, 0, 0, s, s + len, bounds);
        float fullW = bounds[2] - bounds[0];

        if (fullW <= textW) {
            m_line1 = m_title;
        } else {
            // Truncate with trailing ellipsis
            nvgTextBounds(vg, 0, 0, "\xE2\x80\xA6", nullptr, bounds);
            float ellipsisW = bounds[2] - bounds[0];
            int fit = fitPrefix(vg, s, len, textW - ellipsisW);
            while (fit > 0 && s[fit - 1] == ' ') fit--;
            m_line1.assign(s, fit);
            m_line1.append("\xE2\x80\xA6");
        }
    }

    float titleY = y + height - kTitleAreaH + kTitleTopPad;

    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(235, 235, 235, 255));
    nvgText(vg, x + kPadSide, titleY, m_line1.c_str(), nullptr);
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
