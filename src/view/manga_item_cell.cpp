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

    // Cover fills the cell minus a 36px strip at the bottom for the
    // 2-line title area (drawn flat via NanoVG in draw()).
    m_thumbnailImage = new brls::Image();
    m_thumbnailImage->setScalingType(brls::ImageScalingType::FILL);
    m_thumbnailImage->setCornerRadius(4.0f);
    m_thumbnailImage->setPositionType(brls::PositionType::ABSOLUTE);
    m_thumbnailImage->setPositionTop(0);
    m_thumbnailImage->setPositionLeft(0);
    m_thumbnailImage->setPositionRight(0);
    m_thumbnailImage->setPositionBottom(36);
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
    m_line2.clear();
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

    // --- Flat-rendered title below the cover (2 lines) ---
    // Cached line splits so the draw path is just 2 nvgText calls per
    // cell with no per-frame measurement.
    constexpr float kPadSide = 5.0f;
    constexpr float kFontSize = 10.0f;
    constexpr float kLineH = 13.0f;
    constexpr float kTitleAreaH = 36.0f;  // matches thumbnail positionBottom
    constexpr float kTitleTopPad = 3.0f;

    float textW = width - kPadSide * 2.0f;

    nvgFontFace(vg, "regular");
    nvgFontSize(vg, kFontSize);

    // Re-wrap only when cell width changed or title was reassigned.
    if (m_wrappedForWidth < 0.0f || std::fabs(m_wrappedForWidth - width) > 0.5f) {
        m_wrappedForWidth = width;
        m_line1.clear();
        m_line2.clear();

        float bounds[4];
        const char* s = m_title.c_str();
        int len = static_cast<int>(m_title.size());

        nvgTextBounds(vg, 0, 0, s, s + len, bounds);
        float fullW = bounds[2] - bounds[0];

        if (fullW <= textW) {
            // Fits on one line
            m_line1 = m_title;
        } else {
            // Find a word break inside the first-line fit range
            int fit1 = fitPrefix(vg, s, len, textW);
            int breakAt = fit1;
            for (int i = fit1; i > 0; i--) {
                if (s[i] == ' ') { breakAt = i; break; }
            }
            if (breakAt <= 0) breakAt = fit1;
            m_line1.assign(s, breakAt);

            int line2Start = breakAt;
            while (line2Start < len && s[line2Start] == ' ') line2Start++;

            int rest = len - line2Start;
            const char* s2 = s + line2Start;
            nvgTextBounds(vg, 0, 0, s2, s2 + rest, bounds);
            float remW = bounds[2] - bounds[0];

            if (remW <= textW) {
                m_line2.assign(s2, rest);
            } else {
                // Truncate line 2 with ellipsis
                nvgTextBounds(vg, 0, 0, "\xE2\x80\xA6", nullptr, bounds);
                float ellipsisW = bounds[2] - bounds[0];
                int fit2 = fitPrefix(vg, s2, rest, textW - ellipsisW);
                while (fit2 > 0 && s2[fit2 - 1] == ' ') fit2--;
                m_line2.assign(s2, fit2);
                m_line2.append("\xE2\x80\xA6");
            }
        }
    }

    float titleY = y + height - kTitleAreaH + kTitleTopPad;

    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(235, 235, 235, 255));
    nvgText(vg, x + kPadSide, titleY, m_line1.c_str(), nullptr);
    if (!m_line2.empty()) {
        nvgText(vg, x + kPadSide, titleY + kLineH, m_line2.c_str(), nullptr);
    }
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
