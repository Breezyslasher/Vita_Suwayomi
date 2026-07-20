/**
 * VitaSuwayomi - Storage Management View implementation
 * Usage-meter + ranked per-manga list (design direction A) with a per-manga
 * OptionsPopover menu (direction B).
 */

#include "view/storage_view.hpp"
#include "view/options_popover.hpp"
#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "utils/library_cache.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"

#include "platform/platform.hpp"
#include "platform/paths.hpp"

#include <algorithm>

namespace vitasuwayomi {

namespace {
// Fixed dark palette for the storage screen (matches the other redesigned views).
namespace svcol {
    inline NVGcolor page()       { return nvgRGB(18, 18, 20); }
    inline NVGcolor panel()      { return nvgRGB(28, 28, 31); }   // #1c1c1f
    inline NVGcolor row()        { return nvgRGB(35, 35, 38); }   // #232326
    inline NVGcolor borderCard() { return nvgRGBA(255, 255, 255, 15); }
    inline NVGcolor accent()     { return nvgRGB(100, 180, 255); } // #64B4FF
    inline NVGcolor success()    { return nvgRGB(78, 204, 163); }  // #4ECCA3
    inline NVGcolor danger()     { return nvgRGB(217, 107, 107); } // #d96b6b
    inline NVGcolor dangerBg()   { return nvgRGBA(217, 107, 107, 30); }
    inline NVGcolor muted()      { return nvgRGB(139, 139, 147); } // #8b8b93
    inline NVGcolor body()       { return nvgRGB(197, 198, 208); } // #C5C6D0
    inline NVGcolor heading()    { return nvgRGB(231, 231, 234); } // #E7E7EA
    inline NVGcolor empty()      { return nvgRGB(58, 58, 64); }    // #3a3a40
}
} // namespace

StorageView::StorageView() {
    m_alive = std::make_shared<bool>(true);

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);
    this->setBackgroundColor(svcol::page());

    // Fixed top area (breadcrumb + meter + actions + section label).
    m_topBox = new brls::Box();
    m_topBox->setAxis(brls::Axis::COLUMN);
    m_topBox->setAlignItems(brls::AlignItems::STRETCH);
    m_topBox->setPaddingTop(20);
    m_topBox->setPaddingBottom(6);
    m_topBox->setPaddingLeft(40);
    m_topBox->setPaddingRight(40);
    this->addView(m_topBox);

    // Scrollable per-manga list.
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);
    m_scrollView->setFocusable(false);
    m_contentBox = new brls::Box();
    m_contentBox->setAxis(brls::Axis::COLUMN);
    m_contentBox->setAlignItems(brls::AlignItems::STRETCH);
    m_contentBox->setPaddingLeft(40);
    m_contentBox->setPaddingRight(40);
    m_contentBox->setPaddingBottom(20);
    m_scrollView->setContentView(m_contentBox);
    this->addView(m_scrollView);

    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });

    loadStorageInfo();
}

StorageView::~StorageView() {
    if (m_alive) *m_alive = false;
}

void StorageView::refresh() {
    m_loaded = false;
    m_storageItems.clear();
    m_contentBox->clearViews();
    loadStorageInfo();
}

void StorageView::loadStorageInfo() {
    if (m_loaded) return;

    // Hold focus on a placeholder while loading: refresh() cleared the list and
    // destroyed the focused row, so currentFocus would otherwise dangle during
    // the async scan and crash on the next frame.
    m_contentBox->clearViews();
    auto* loading = new brls::Label();
    loading->setText("Loading…");
    loading->setFontSize(15);
    loading->setTextColor(svcol::muted());
    loading->setMarginTop(20);
    loading->setFocusable(true);
    m_contentBox->addView(loading);
    brls::Application::giveFocus(loading);

    std::weak_ptr<bool> aliveWeak = m_alive;

    asyncRun([this, aliveWeak]() {
        DownloadsManager& dm = DownloadsManager::getInstance();
        auto downloads = dm.getDownloads();

        std::vector<StorageItem> items;
        int64_t totalSize = 0;

        for (const auto& download : downloads) {
            StorageItem item;
            item.mangaId = download.mangaId;
            item.mangaTitle = download.title;
            item.coverUrl = download.localCoverPath;

            // Local (on-device) storage only: sum the files actually stored on
            // the device. Storage Management reflects device usage, so
            // server-side downloads (no local files) are excluded below.
            item.sizeBytes = 0;
            std::string mangaPath = dm.getDownloadsPath() + "/" + std::to_string(download.mangaId);
            for (const auto& name : platform::listDir(mangaPath)) {
                if (name.empty() || name[0] == '.') continue;
                std::string entryPath = mangaPath + "/" + name;
                int64_t sz = platform::fileSize(entryPath);
                if (sz > 0) {
                    item.sizeBytes += sz;
                } else {
                    for (const auto& fname : platform::listDir(entryPath)) {
                        if (fname.empty() || fname[0] == '.') continue;
                        int64_t fsz = platform::fileSize(entryPath + "/" + fname);
                        if (fsz > 0) item.sizeBytes += fsz;
                    }
                }
            }
            if (item.sizeBytes <= 0) continue;   // nothing stored locally -> skip

            // Count only the chapters that are present on the device.
            int localChapters = 0;
            for (const auto& ch : download.chapters) {
                if (dm.isChapterDownloaded(download.mangaId, ch.chapterIndex)) localChapters++;
            }
            item.chapterCount = localChapters > 0 ? localChapters
                                                  : static_cast<int>(download.chapters.size());

            totalSize += item.sizeBytes;
            items.push_back(item);
        }

        std::sort(items.begin(), items.end(),
            [](const StorageItem& a, const StorageItem& b) { return a.sizeBytes > b.sizeBytes; });

        int64_t cacheSize = LibraryCache::getInstance().getCacheSize();

        brls::sync([this, items, totalSize, cacheSize, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            m_storageItems = items;
            m_totalSize = totalSize;
            m_cacheSize = cacheSize;
            m_loaded = true;

            rebuildTop();

            // ---- Per-manga list ----
            m_contentBox->clearViews();
            if (m_storageItems.empty()) {
                auto* empty = new brls::Label();
                empty->setText("No downloaded content");
                empty->setFontSize(15);
                empty->setTextColor(svcol::muted());
                empty->setMarginTop(20);
                empty->setFocusable(true);
                m_contentBox->addView(empty);
                brls::Application::giveFocus(empty);   // don't leave the destroyed loader focused
                return;
            }

            int64_t maxSize = m_storageItems.front().sizeBytes;
            if (maxSize <= 0) maxSize = 1;

            brls::View* firstRow = nullptr;
            for (size_t i = 0; i < m_storageItems.size(); i++) {
                const auto& item = m_storageItems[i];

                auto* rowBox = new brls::Box();
                rowBox->setAxis(brls::Axis::ROW);
                rowBox->setAlignItems(brls::AlignItems::CENTER);
                rowBox->setHeight(72);
                rowBox->setPaddingTop(10); rowBox->setPaddingBottom(10);
                rowBox->setPaddingLeft(12); rowBox->setPaddingRight(14);
                rowBox->setMarginBottom(8);
                rowBox->setCornerRadius(12);
                rowBox->setHighlightCornerRadius(12);
                rowBox->setBackgroundColor(svcol::panel());
                rowBox->setBorderColor(svcol::borderCard());
                rowBox->setBorderThickness(1.0f);
                rowBox->setFocusable(true);

                // Cover thumb
                auto* cover = new brls::Image();
                cover->setWidth(34); cover->setHeight(48);
                cover->setCornerRadius(4);
                cover->setScalingType(brls::ImageScalingType::FIT);
                cover->setMarginRight(12);
                std::string coverUrl = SuwayomiClient::getInstance().getMangaThumbnailUrl(item.mangaId);
                if (!coverUrl.empty()) ImageLoader::loadAsync(coverUrl, nullptr, cover, m_alive);
                rowBox->addView(cover);

                // Title + proportional size bar
                auto* mid = new brls::Box();
                mid->setAxis(brls::Axis::COLUMN);
                mid->setGrow(1.0f);
                mid->setShrink(1.0f);
                auto* title = new brls::Label();
                title->setText(item.mangaTitle);
                title->setFontSize(15);
                title->setTextColor(svcol::heading());
                title->setSingleLine(true);
                mid->addView(title);

                auto* barTrack = new brls::Box();
                barTrack->setHeight(4);
                barTrack->setCornerRadius(2);
                barTrack->setBackgroundColor(svcol::row());
                barTrack->setMarginTop(8);
                barTrack->setMarginRight(14);
                barTrack->setAlignSelf(brls::AlignSelf::STRETCH);
                auto* barFill = new brls::Box();
                barFill->setHeight(4);
                barFill->setCornerRadius(2);
                barFill->setBackgroundColor(svcol::accent());
                float wp = static_cast<float>(item.sizeBytes) / static_cast<float>(maxSize) * 100.0f;
                if (wp < 2.0f) wp = 2.0f; if (wp > 100.0f) wp = 100.0f;
                barFill->setWidthPercentage(wp);
                barTrack->addView(barFill);
                mid->addView(barTrack);
                rowBox->addView(mid);

                // Chapters
                auto* chaps = new brls::Label();
                chaps->setText(std::to_string(item.chapterCount) + " ch");
                chaps->setFontSize(13);
                chaps->setTextColor(svcol::muted());
                chaps->setMarginRight(14);
                rowBox->addView(chaps);

                // Size
                auto* size = new brls::Label();
                size->setText(formatSize(item.sizeBytes));
                size->setFontSize(14);
                size->setTextColor(svcol::accent());
                size->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
                rowBox->addView(size);

                int index = static_cast<int>(i);
                StorageItem captured = item;
                rowBox->registerClickAction([this, captured, index](brls::View*) {
                    showMangaStorageMenu(captured, index);
                    return true;
                });
                rowBox->addGestureRecognizer(new brls::TapGestureRecognizer(rowBox));
                m_contentBox->addView(rowBox);
                if (!firstRow) firstRow = rowBox;
            }

            // Route UP from the first list row to the quick-action buttons (the
            // scroll frame otherwise swallows UP at the top row).
            if (firstRow && m_firstAction) {
                firstRow->setCustomNavigationRoute(brls::FocusDirection::UP, m_firstAction);
                m_firstAction->setCustomNavigationRoute(brls::FocusDirection::DOWN, firstRow);
            }

            // The loading placeholder we were focused on is gone now; move focus
            // to the first row so currentFocus stays valid.
            if (firstRow) brls::Application::giveFocus(firstRow);
        });
    });
}

void StorageView::rebuildTop() {
    namespace c = svcol;
    m_topBox->clearViews();

    // ---- Breadcrumb ----
    auto* crumb = new brls::Label();
    crumb->setText("Downloads  ›  Storage Management");
    crumb->setFontSize(26);
    crumb->setTextColor(c::heading());
    crumb->setMarginBottom(18);
    m_topBox->addView(crumb);

    // ---- Usage meter ----
    const int64_t used = m_totalSize + m_cacheSize;
    std::string totalStr = formatSize(m_totalSize);
    std::string num = totalStr, unit;
    auto sp = totalStr.find_last_of(' ');
    if (sp != std::string::npos) { num = totalStr.substr(0, sp); unit = totalStr.substr(sp + 1); }

    auto* totalRow = new brls::Box();
    totalRow->setAxis(brls::Axis::ROW);
    totalRow->setAlignItems(brls::AlignItems::FLEX_END);
    totalRow->setMarginBottom(12);
    auto* numLbl = new brls::Label();
    numLbl->setText(num); numLbl->setFontSize(38); numLbl->setTextColor(c::heading());
    totalRow->addView(numLbl);
    if (!unit.empty()) {
        auto* unitLbl = new brls::Label();
        unitLbl->setText(" " + unit); unitLbl->setFontSize(18); unitLbl->setTextColor(c::muted());
        unitLbl->setMarginBottom(5);
        totalRow->addView(unitLbl);
    }
    m_topBox->addView(totalRow);

    // Stacked bar
    auto* bar = new brls::Box();
    bar->setAxis(brls::Axis::ROW);
    bar->setHeight(16);
    bar->setCornerRadius(8);
    bar->setBackgroundColor(c::empty());
    bar->setAlignSelf(brls::AlignSelf::STRETCH);
    bar->setMarginBottom(12);
    if (used > 0) {
        float dlFrac = static_cast<float>(m_totalSize) / static_cast<float>(used) * 100.0f;
        float caFrac = static_cast<float>(m_cacheSize) / static_cast<float>(used) * 100.0f;
        auto* dl = new brls::Box();
        dl->setHeight(16); dl->setBackgroundColor(c::accent()); dl->setCornerRadius(8);
        dl->setWidthPercentage(dlFrac);
        bar->addView(dl);
        auto* ca = new brls::Box();
        ca->setHeight(16); ca->setBackgroundColor(c::success()); ca->setCornerRadius(8);
        ca->setWidthPercentage(caFrac);
        bar->addView(ca);
    }
    m_topBox->addView(bar);

    // Legend
    auto* legend = new brls::Box();
    legend->setAxis(brls::Axis::ROW);
    legend->setAlignItems(brls::AlignItems::CENTER);
    legend->setMarginBottom(18);
    auto legendItem = [&](NVGcolor swatch, const std::string& text, bool spacer) {
        auto* dot = new brls::Box();
        dot->setWidth(9); dot->setHeight(9); dot->setCornerRadius(3);
        dot->setBackgroundColor(swatch); dot->setMarginRight(8);
        legend->addView(dot);
        auto* l = new brls::Label();
        l->setText(text); l->setFontSize(13); l->setTextColor(c::body());
        if (spacer) l->setMarginRight(20);
        legend->addView(l);
    };
    legendItem(c::accent(), "Downloads " + formatSize(m_totalSize) + " · " +
                            std::to_string(m_storageItems.size()) + " manga", true);
    legendItem(c::success(), "Cache " + formatSize(m_cacheSize), false);
    m_topBox->addView(legend);

    // ---- Quick actions ----
    auto* actions = new brls::Box();
    actions->setAxis(brls::Axis::ROW);
    actions->setMarginBottom(18);
    auto action = [&](const std::string& icon, const std::string& label, bool danger,
                      bool last, std::function<void()> onClick) -> brls::Box* {
        auto* cell = new brls::Box();
        cell->setAxis(brls::Axis::ROW);
        cell->setAlignItems(brls::AlignItems::CENTER);
        cell->setJustifyContent(brls::JustifyContent::CENTER);
        cell->setGrow(1.0f);
        cell->setHeight(46);
        cell->setCornerRadius(11);
        cell->setHighlightCornerRadius(11);
        cell->setBackgroundColor(danger ? c::dangerBg() : c::row());
        cell->setBorderColor(danger ? c::danger() : c::borderCard());
        cell->setBorderThickness(1.0f);
        cell->setFocusable(true);
        if (!last) cell->setMarginRight(12);
        auto* img = new brls::Image();
        img->setWidth(18); img->setHeight(18);
        img->setScalingType(brls::ImageScalingType::FIT);
        img->setImageFromRes("icons/" + icon);
        img->setMarginRight(9);
        cell->addView(img);
        auto* l = new brls::Label();
        l->setText(label); l->setFontSize(14);
        l->setTextColor(danger ? c::danger() : c::body());
        cell->addView(l);
        cell->registerClickAction([onClick](brls::View*) { if (onClick) onClick(); return true; });
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));
        actions->addView(cell);
        return cell;
    };
    m_firstAction = action("check.png", "Clean read chapters", false, false, [this]() { deleteReadChapters(); });
    action("delete.png", "Clear cache " + formatSize(m_cacheSize), false, false, [this]() { clearCache(); });
    action("delete.png", "Clear all", true, true, [this]() { clearAllDownloads(); });
    m_topBox->addView(actions);

    // ---- Section label ----
    auto* section = new brls::Label();
    section->setText("PER-MANGA · LARGEST FIRST  (" + std::to_string(m_storageItems.size()) + ")");
    section->setFontSize(13);
    section->setTextColor(c::muted());
    section->setMarginBottom(10);
    m_topBox->addView(section);
}

void StorageView::showMangaStorageMenu(const StorageItem& item, int index) {
    // Estimate reclaimable read-chapter bytes and gather the read chapter list.
    DownloadsManager& dm = DownloadsManager::getInstance();
    int readChapters = 0;
    for (const auto& d : dm.getDownloads()) {
        if (d.mangaId != item.mangaId) continue;
        for (const auto& ch : d.chapters) {
            if (ch.state == LocalDownloadState::COMPLETED &&
                ch.lastPageRead > 0 && ch.lastPageRead >= ch.pageCount - 1) {
                readChapters++;
            }
        }
        break;
    }
    int64_t freed = (item.chapterCount > 0)
        ? item.sizeBytes * static_cast<int64_t>(readChapters) / item.chapterCount : 0;

    const std::string ctx = std::to_string(item.chapterCount) + " ch · " + formatSize(item.sizeBytes);
    StorageItem captured = item;

    std::vector<OptionRow> rows;

    OptionRow del;
    del.icon  = "checkbox_checked.png";
    del.label = "Delete read chapters";
    del.sub   = readChapters > 0 ? ("-" + formatSize(freed)) : "None";
    del.action = [this, captured]() {
        DownloadsManager& d = DownloadsManager::getInstance();
        int deleted = 0;
        for (const auto& mg : d.getDownloads()) {
            if (mg.mangaId != captured.mangaId) continue;
            for (const auto& ch : mg.chapters) {
                if (ch.state == LocalDownloadState::COMPLETED &&
                    ch.lastPageRead > 0 && ch.lastPageRead >= ch.pageCount - 1) {
                    if (d.deleteChapterDownload(captured.mangaId, ch.chapterIndex)) deleted++;
                }
            }
            break;
        }
        brls::Application::notify("Deleted " + std::to_string(deleted) + " read chapters");
        refresh();
    };
    rows.push_back(std::move(del));

    OptionRow go;
    go.icon  = "book-open-page-variant.png";
    go.label = "Go to manga";
    go.action = [captured]() { Application::getInstance().pushMangaDetailView(captured.mangaId); };
    rows.push_back(std::move(go));

    OptionRow delAll;
    delAll.icon   = "cross.png";
    delAll.label  = "Delete all downloads";
    delAll.sub    = formatSize(item.sizeBytes);
    delAll.danger = true;
    delAll.action = [this, captured]() {
        std::vector<OptionRow> confirm;
        confirm.push_back({ "cross.png", "Delete all", "", true, true,
            [this, captured]() { deleteMangaDownloads(captured); } });
        confirm.push_back({ "back.png", "Cancel", "", false, true, [](){} });
        OptionsPopover::show("CONFIRM", "Delete all downloads?", std::move(confirm));
    };
    rows.push_back(std::move(delAll));

    rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });

    OptionsPopover::show(ctx, item.mangaTitle, std::move(rows), nullptr, 5);
}

void StorageView::deleteMangaDownloads(const StorageItem& item) {
    brls::Application::notify("Deleting downloads for " + item.mangaTitle + "...");
    DownloadsManager::getInstance().deleteMangaDownload(item.mangaId);
    brls::Application::notify("Downloads deleted");
    refresh();
}

void StorageView::deleteReadChapters() {
    std::vector<OptionRow> rows;
    rows.push_back({ "checkbox_checked.png", "Delete read chapters", "", true, true, [this]() {
        brls::Application::notify("Cleaning up read chapters...");
        std::weak_ptr<bool> aliveWeak = m_alive;
        asyncRun([this, aliveWeak]() {
            DownloadsManager& dm = DownloadsManager::getInstance();
            int deleted = 0;
            for (const auto& manga : dm.getDownloads()) {
                for (const auto& chapter : manga.chapters) {
                    if (chapter.state == LocalDownloadState::COMPLETED &&
                        chapter.lastPageRead > 0 &&
                        chapter.lastPageRead >= chapter.pageCount - 1) {
                        if (dm.deleteChapterDownload(manga.mangaId, chapter.chapterIndex)) deleted++;
                    }
                }
            }
            brls::sync([this, deleted, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                brls::Application::notify("Deleted " + std::to_string(deleted) + " read chapters");
                refresh();
            });
        });
    }});
    rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });
    OptionsPopover::show("CONFIRM", "Delete all read chapters?", std::move(rows));
}

void StorageView::clearCache() {
    std::vector<OptionRow> rows;
    rows.push_back({ "refresh.png", "Clear cache", "", true, true, [this]() {
        LibraryCache::getInstance().clearAllCache();
        ImageLoader::clearCache();
        brls::Application::notify("Cache cleared");
        refresh();
    }});
    rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });
    OptionsPopover::show("CONFIRM", "Clear all cached data?", std::move(rows));
}

void StorageView::clearAllDownloads() {
    std::vector<OptionRow> rows;
    rows.push_back({ "cross.png", "Delete all", "", true, true, [this]() {
        brls::Application::notify("Deleting all downloads...");
        auto downloads = DownloadsManager::getInstance().getDownloads();
        for (const auto& item : downloads) {
            DownloadsManager::getInstance().deleteMangaDownload(item.mangaId);
        }
        brls::Application::notify("All downloads deleted");
        refresh();
    }});
    rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });
    OptionsPopover::show("CONFIRM", "Delete ALL downloaded content?", std::move(rows));
}

std::string StorageView::formatSize(int64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        return std::to_string(bytes / 1024) + " KB";
    } else if (bytes < 1024 * 1024 * 1024) {
        double mb = static_cast<double>(bytes) / (1024 * 1024);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f MB", mb);
        return std::string(buf);
    } else {
        double gb = static_cast<double>(bytes) / (1024 * 1024 * 1024);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f GB", gb);
        return std::string(buf);
    }
}

} // namespace vitasuwayomi
