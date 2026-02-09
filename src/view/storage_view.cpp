/**
 * VitaSuwayomi - Storage Management View implementation
 * Shows storage usage and allows cleanup of downloaded content
 */

#include "view/storage_view.hpp"
#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "utils/library_cache.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#endif

namespace vitasuwayomi {

StorageView::StorageView() {
    m_alive = std::make_shared<bool>(true);

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Storage Management");
    m_titleLabel->setFontSize(28);
    m_titleLabel->setMarginBottom(15);
    this->addView(m_titleLabel);

    // Storage summary box
    auto* summaryBox = new brls::Box();
    summaryBox->setAxis(brls::Axis::COLUMN);
    summaryBox->setBackgroundColor(nvgRGBA(40, 40, 40, 255));
    summaryBox->setCornerRadius(8);
    summaryBox->setPadding(15);
    summaryBox->setMarginBottom(20);

    m_totalSizeLabel = new brls::Label();
    m_totalSizeLabel->setText("Total Downloads: Calculating...");
    m_totalSizeLabel->setFontSize(18);
    m_totalSizeLabel->setTextColor(nvgRGB(0, 150, 136));
    m_totalSizeLabel->setMarginBottom(8);
    summaryBox->addView(m_totalSizeLabel);

    m_cacheSizeLabel = new brls::Label();
    m_cacheSizeLabel->setText("Cache: Calculating...");
    m_cacheSizeLabel->setFontSize(16);
    m_cacheSizeLabel->setTextColor(nvgRGB(180, 180, 180));
    summaryBox->addView(m_cacheSizeLabel);

    this->addView(summaryBox);

    // Action buttons row
    auto* actionsRow = new brls::Box();
    actionsRow->setAxis(brls::Axis::ROW);
    actionsRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    actionsRow->setMarginBottom(15);

    // Clean read chapters button
    auto* cleanReadBtn = new brls::Button();
    cleanReadBtn->setText("Clean Read Chapters");
    cleanReadBtn->setWidth(200);
    cleanReadBtn->setHeight(40);
    cleanReadBtn->registerClickAction([this](brls::View* view) {
        deleteReadChapters();
        return true;
    });
    actionsRow->addView(cleanReadBtn);

    // Clear cache button
    auto* clearCacheBtn = new brls::Button();
    clearCacheBtn->setText("Clear Cache");
    clearCacheBtn->setWidth(150);
    clearCacheBtn->setHeight(40);
    clearCacheBtn->registerClickAction([this](brls::View* view) {
        clearCache();
        return true;
    });
    actionsRow->addView(clearCacheBtn);

    // Clear all button
    auto* clearAllBtn = new brls::Button();
    clearAllBtn->setText("Clear All");
    clearAllBtn->setWidth(120);
    clearAllBtn->setHeight(40);
    clearAllBtn->setBackgroundColor(nvgRGBA(180, 60, 60, 255));
    clearAllBtn->registerClickAction([this](brls::View* view) {
        clearAllDownloads();
        return true;
    });
    actionsRow->addView(clearAllBtn);

    this->addView(actionsRow);

    // Per-manga breakdown header
    auto* breakdownLabel = new brls::Label();
    breakdownLabel->setText("Per-Manga Storage");
    breakdownLabel->setFontSize(18);
    breakdownLabel->setMarginBottom(10);
    this->addView(breakdownLabel);

    // Scrollable content
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_contentBox = new brls::Box();
    m_contentBox->setAxis(brls::Axis::COLUMN);

    m_scrollView->setContentView(m_contentBox);
    this->addView(m_scrollView);

    // Register refresh action
    this->registerAction("Refresh", brls::ControllerButton::BUTTON_BACK, [this](brls::View*) {
        refresh();
        return true;
    });

    // Load storage info
    loadStorageInfo();
}

StorageView::~StorageView() {
    if (m_alive) {
        *m_alive = false;
    }
}

void StorageView::refresh() {
    m_loaded = false;
    m_storageItems.clear();
    m_contentBox->clearViews();
    loadStorageInfo();
}

void StorageView::loadStorageInfo() {
    if (m_loaded) return;

    m_titleLabel->setText("Storage Management (Loading...)");

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
            item.chapterCount = static_cast<int>(download.chapters.size());

            // Calculate size
            item.sizeBytes = 0;
#ifdef __vita__
            std::string mangaPath = dm.getDownloadsPath() + "/" + std::to_string(download.mangaId);
            SceUID dir = sceIoDopen(mangaPath.c_str());
            if (dir >= 0) {
                SceIoDirent entry;
                while (sceIoDread(dir, &entry) > 0) {
                    if (SCE_S_ISDIR(entry.d_stat.st_mode) && entry.d_name[0] != '.') {
                        // Count files in chapter directory
                        std::string chapterPath = mangaPath + "/" + entry.d_name;
                        SceUID chapterDir = sceIoDopen(chapterPath.c_str());
                        if (chapterDir >= 0) {
                            SceIoDirent fileEntry;
                            while (sceIoDread(chapterDir, &fileEntry) > 0) {
                                if (SCE_S_ISREG(fileEntry.d_stat.st_mode)) {
                                    item.sizeBytes += fileEntry.d_stat.st_size;
                                }
                            }
                            sceIoDclose(chapterDir);
                        }
                    } else if (SCE_S_ISREG(entry.d_stat.st_mode)) {
                        item.sizeBytes += entry.d_stat.st_size;
                    }
                }
                sceIoDclose(dir);
            }
#endif
            totalSize += item.sizeBytes;
            items.push_back(item);
        }

        // Sort by size (largest first)
        std::sort(items.begin(), items.end(),
            [](const StorageItem& a, const StorageItem& b) {
                return a.sizeBytes > b.sizeBytes;
            });

        // Get cache size
        int64_t cacheSize = LibraryCache::getInstance().getCacheSize();

        brls::sync([this, items, totalSize, cacheSize, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;

            m_storageItems = items;
            m_totalSize = totalSize;
            m_cacheSize = cacheSize;
            m_loaded = true;

            // Update UI
            m_titleLabel->setText("Storage Management");
            m_totalSizeLabel->setText("Total Downloads: " + formatSize(m_totalSize) +
                                      " (" + std::to_string(m_storageItems.size()) + " manga)");
            m_cacheSizeLabel->setText("Cache: " + formatSize(m_cacheSize));

            // Display per-manga breakdown
            m_contentBox->clearViews();

            if (m_storageItems.empty()) {
                auto* emptyLabel = new brls::Label();
                emptyLabel->setText("No downloaded content");
                emptyLabel->setFontSize(16);
                emptyLabel->setTextColor(nvgRGB(128, 128, 128));
                m_contentBox->addView(emptyLabel);
            } else {
                for (size_t i = 0; i < m_storageItems.size(); i++) {
                    const auto& item = m_storageItems[i];

                    auto* itemRow = new brls::Box();
                    itemRow->setAxis(brls::Axis::ROW);
                    itemRow->setAlignItems(brls::AlignItems::CENTER);
                    itemRow->setPadding(10);
                    itemRow->setMarginBottom(6);
                    itemRow->setCornerRadius(8);
                    itemRow->setBackgroundColor(nvgRGBA(40, 40, 40, 255));
                    itemRow->setFocusable(true);
                    itemRow->setHeight(60);

                    // Manga title
                    auto* titleLabel = new brls::Label();
                    std::string title = item.mangaTitle;
                    if (title.length() > 30) {
                        title = title.substr(0, 28) + "..";
                    }
                    titleLabel->setText(title);
                    titleLabel->setFontSize(15);
                    titleLabel->setGrow(1.0f);
                    itemRow->addView(titleLabel);

                    // Chapter count
                    auto* chapterLabel = new brls::Label();
                    chapterLabel->setText(std::to_string(item.chapterCount) + " ch");
                    chapterLabel->setFontSize(13);
                    chapterLabel->setTextColor(nvgRGB(150, 150, 150));
                    chapterLabel->setWidth(60);
                    itemRow->addView(chapterLabel);

                    // Size
                    auto* sizeLabel = new brls::Label();
                    sizeLabel->setText(formatSize(item.sizeBytes));
                    sizeLabel->setFontSize(14);
                    sizeLabel->setTextColor(nvgRGB(0, 150, 136));
                    sizeLabel->setWidth(80);
                    sizeLabel->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
                    itemRow->addView(sizeLabel);

                    // Click to show menu
                    int index = static_cast<int>(i);
                    StorageItem capturedItem = item;
                    itemRow->registerClickAction([this, capturedItem, index](brls::View* view) {
                        showMangaStorageMenu(capturedItem, index);
                        return true;
                    });
                    itemRow->addGestureRecognizer(new brls::TapGestureRecognizer(itemRow));

                    m_contentBox->addView(itemRow);
                }
            }
        });
    });
}

void StorageView::showMangaStorageMenu(const StorageItem& item, int index) {
    std::vector<std::string> options = {
        "Delete Downloads (" + formatSize(item.sizeBytes) + ")",
        "Cancel"
    };

    auto* dropdown = new brls::Dropdown(
        item.mangaTitle,
        options,
        [this, item](int selected) {
            if (selected == 0) {
                deleteMangaDownloads(item);
            }
        }
    );
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void StorageView::deleteMangaDownloads(const StorageItem& item) {
    brls::Application::notify("Deleting downloads for " + item.mangaTitle + "...");

    DownloadsManager::getInstance().deleteMangaDownload(item.mangaId);

    brls::Application::notify("Downloads deleted");
    refresh();
}

void StorageView::deleteReadChapters() {
    brls::Dialog* dialog = new brls::Dialog("Delete all read chapters?\n\nThis will free up space by removing chapters you've already read.");
    dialog->setCancelable(false);  // Prevent exit dialog from appearing

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Delete", [dialog, this]() {
        dialog->close();

        brls::Application::notify("Cleaning up read chapters...");

        std::weak_ptr<bool> aliveWeak = m_alive;

        asyncRun([this, aliveWeak]() {
            DownloadsManager& dm = DownloadsManager::getInstance();
            int deleted = 0;

            // Get all downloads and find read chapters to delete
            auto downloads = dm.getDownloads();
            for (const auto& manga : downloads) {
                for (const auto& chapter : manga.chapters) {
                    // Consider a chapter "read" if the user has read to near the end
                    if (chapter.state == LocalDownloadState::COMPLETED &&
                        chapter.lastPageRead > 0 &&
                        chapter.lastPageRead >= chapter.pageCount - 1) {
                        if (dm.deleteChapterDownload(manga.mangaId, chapter.chapterIndex)) {
                            deleted++;
                        }
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
    });

    dialog->open();
}

void StorageView::clearCache() {
    brls::Dialog* dialog = new brls::Dialog("Clear all cached data?\n\nThis includes cached manga info and cover images.");
    dialog->setCancelable(false);  // Prevent exit dialog from appearing

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Clear", [dialog, this]() {
        dialog->close();

        LibraryCache::getInstance().clearAllCache();
        ImageLoader::clearCache();

        brls::Application::notify("Cache cleared");
        refresh();
    });

    dialog->open();
}

void StorageView::clearAllDownloads() {
    brls::Dialog* dialog = new brls::Dialog("Delete ALL downloaded content?\n\nThis cannot be undone!");
    dialog->setCancelable(false);  // Prevent exit dialog from appearing

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Delete All", [dialog, this]() {
        dialog->close();

        brls::Application::notify("Deleting all downloads...");

        auto downloads = DownloadsManager::getInstance().getDownloads();
        for (const auto& item : downloads) {
            DownloadsManager::getInstance().deleteMangaDownload(item.mangaId);
        }

        brls::Application::notify("All downloads deleted");
        refresh();
    });

    dialog->open();
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
