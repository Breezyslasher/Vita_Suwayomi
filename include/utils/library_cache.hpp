/**
 * VitaSuwayomi - Library Cache Manager
 * Caches manga info and cover images to disk for faster loading
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

class LibraryCache {
public:
    static LibraryCache& getInstance();

    // Initialize cache (creates directories if needed)
    bool init();

    // Categories caching (the category list itself)
    bool saveCategories(const std::vector<Category>& categories);
    bool loadCategories(std::vector<Category>& categories);
    bool hasCategoriesCache();

    // Category manga caching
    bool saveCategoryManga(int categoryId, const std::vector<Manga>& manga);
    bool loadCategoryManga(int categoryId, std::vector<Manga>& manga);
    bool hasCategoryCache(int categoryId);
    void invalidateCategoryCache(int categoryId);

    // Individual manga details caching (for detail view)
    bool saveMangaDetails(const Manga& manga);
    bool loadMangaDetails(int mangaId, Manga& manga);
    bool hasMangaDetailsCache(int mangaId);

    // Cover image caching
    bool saveCoverImage(int mangaId, const std::vector<uint8_t>& imageData);
    bool loadCoverImage(int mangaId, std::vector<uint8_t>& imageData);
    bool hasCoverCache(int mangaId);
    std::string getCoverCachePath(int mangaId);

    // Reading history caching
    bool saveHistory(const std::vector<ReadingHistoryItem>& history);
    bool loadHistory(std::vector<ReadingHistoryItem>& history);
    bool hasHistoryCache();
    void invalidateHistoryCache();

    // Cache management
    void clearAllCache();
    void clearCoverCache();
    void clearLibraryCache();
    size_t getCacheSize();  // Returns approximate size in bytes

    // Enable/disable caching
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    void setCoverCacheEnabled(bool enabled) { m_coverCacheEnabled = enabled; }
    bool isCoverCacheEnabled() const { return m_coverCacheEnabled; }

private:
    LibraryCache() = default;
    ~LibraryCache() = default;
    LibraryCache(const LibraryCache&) = delete;
    LibraryCache& operator=(const LibraryCache&) = delete;

    std::string getCacheDir();
    std::string getCoverCacheDir();
    std::string getMangaDetailsCacheDir();
    std::string getCategoryFilePath(int categoryId);
    std::string getCategoriesFilePath();
    std::string getMangaDetailsFilePath(int mangaId);
    bool ensureDirectoryExists(const std::string& path);

    // Serialize/deserialize manga (basic - for category lists)
    std::string serializeManga(const Manga& manga);
    bool deserializeManga(const std::string& line, Manga& manga);

    // Serialize/deserialize manga details (full - for detail view)
    std::string serializeMangaDetails(const Manga& manga);
    bool deserializeMangaDetails(const std::string& data, Manga& manga);

    // Serialize/deserialize category
    std::string serializeCategory(const Category& category);
    bool deserializeCategory(const std::string& line, Category& category);

    // Serialize/deserialize history item
    std::string serializeHistoryItem(const ReadingHistoryItem& item);
    bool deserializeHistoryItem(const std::string& line, ReadingHistoryItem& item);
    std::string getHistoryFilePath();

    bool m_enabled = true;
    bool m_coverCacheEnabled = true;
    bool m_initialized = false;
    std::mutex m_mutex;       // Protects metadata operations (categories, manga lists, details)
    std::mutex m_coverMutex;  // Separate mutex for cover image I/O (allows parallel reads by worker threads)
};

} // namespace vitasuwayomi
