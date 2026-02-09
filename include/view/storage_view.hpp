/**
 * VitaSuwayomi - Storage Management View
 * Shows storage usage and allows cleanup of downloaded content
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

struct StorageItem {
    int mangaId = 0;
    std::string mangaTitle;
    std::string coverUrl;
    int chapterCount = 0;
    int64_t sizeBytes = 0;
};

class StorageView : public brls::Box {
public:
    StorageView();
    ~StorageView() override;

    void refresh();

private:
    void loadStorageInfo();
    void showMangaStorageMenu(const StorageItem& item, int index);
    void deleteMangaDownloads(const StorageItem& item);
    void deleteReadChapters();
    void clearAllDownloads();
    void clearCache();
    std::string formatSize(int64_t bytes);

    // UI Components
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_totalSizeLabel = nullptr;
    brls::Label* m_cacheSizeLabel = nullptr;
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;

    // Data
    std::vector<StorageItem> m_storageItems;
    int64_t m_totalSize = 0;
    int64_t m_cacheSize = 0;
    bool m_loaded = false;

    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
