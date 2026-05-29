#include "view/manga_item_cell.hpp"
#include "utils/image_loader.hpp"
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

MangaItemCell::MangaItemCell() {
    m_alive = std::make_shared<bool>(true);
    this->setFocusable(true);
    this->setCornerRadius(4.0f);
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
    m_titleCached = false;
    m_badgeText = (manga.unreadCount > 0) ? std::to_string(manga.unreadCount) : std::string();
    m_badgeTextW = 0;
    m_badgeTextH = 0;
}

void MangaItemCell::updateMangaData(const Manga& manga) {
    bool coverChanged = (m_manga.thumbnailUrl != manga.thumbnailUrl);
    bool unreadChanged = (m_manga.unreadCount != manga.unreadCount);
    bool titleChanged = (m_manga.title != manga.title);
    m_manga = manga;
    if (titleChanged) {
        m_titleCached = false;
    }
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
    if (unreadChanged) {
        m_badgeText = (manga.unreadCount > 0) ? std::to_string(manga.unreadCount) : std::string();
        m_badgeTextW = 0;
        m_badgeTextH = 0;
    }
}

void MangaItemCell::cacheBadgeBounds(NVGcontext* vg, float fontSize) {
    if (m_badgeTextW > 0 || m_badgeText.empty()) return;
    nvgFontFace(vg, "regular");
    nvgFontSize(vg, fontSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    float bb[4];
    nvgTextBounds(vg, 0, 0, m_badgeText.c_str(), nullptr, bb);
    m_badgeTextW = bb[2] - bb[0];
    m_badgeTextH = bb[3] - bb[1];
}

void MangaItemCell::draw(NVGcontext* vg, float x, float y, float width, float height,
                         brls::Style style, brls::FrameContext* ctx) {
    m_drawX = x;
    m_drawY = y;
    m_drawW = width;
    m_drawH = height;
    brls::Box::draw(vg, x, y, width, height, style, ctx);

    if (m_selfDrawCover && width > 0 && height > 0) {
        if (m_nvgCover != 0) {
            float imgW = static_cast<float>(m_coverW);
            float imgH = static_cast<float>(m_coverH);
            if (imgW > 0 && imgH > 0) {
                float scaleW = width / imgW, scaleH = height / imgH;
                float scale = (scaleW > scaleH) ? scaleW : scaleH;
                float sw = imgW * scale;
                float sh = imgH * scale;
                float ox = x + (width - sw) * 0.5f;
                float oy = y + (height - sh) * 0.5f;

                NVGpaint paint = nvgImagePattern(vg, ox, oy, sw, sh, 0, m_nvgCover, 1.0f);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, x, y, width, height, 4.0f);
                nvgFillPaint(vg, paint);
                nvgFill(vg);
            }
        } else {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, 4.0f);
            nvgFillColor(vg, nvgRGB(40, 40, 48));
            nvgFill(vg);
        }
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

void MangaItemCell::resetThumbnailLoadState() {
    m_thumbnailLoaded = false;
}

void MangaItemCell::cacheTitleText(NVGcontext* vg, float fontSize, float maxWidth, int maxLines) {
    if (m_titleCached) return;
    m_titleCached = true;

    const std::string& title = m_manga.title;
    if (title.empty()) {
        m_cachedTitle.clear();
        return;
    }

    nvgFontFace(vg, "regular");
    nvgFontSize(vg, fontSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    // Measure how many characters fit on each line using NVG's row-break API
    NVGtextRow rows[4];
    int maxRows = (maxLines > 4) ? 4 : maxLines;
    int nrows = nvgTextBreakLines(vg, title.c_str(), nullptr, maxWidth, rows, maxRows);

    m_cachedTitle.clear();
    for (int r = 0; r < nrows; r++) {
        if (r > 0) m_cachedTitle += '\n';
        if (r == maxRows - 1 && rows[r].end != (title.c_str() + title.size())) {
            // Last visible line but more text remains — add ellipsis
            m_cachedTitle.append(rows[r].start, rows[r].end);
            // Trim trailing space before ellipsis
            while (!m_cachedTitle.empty() && m_cachedTitle.back() == ' ')
                m_cachedTitle.pop_back();
            m_cachedTitle += "...";
        } else {
            m_cachedTitle.append(rows[r].start, rows[r].end);
        }
    }
}

void MangaItemCell::setPressed(bool pressed) {
    m_pressed = pressed;
}

} // namespace vitasuwayomi
