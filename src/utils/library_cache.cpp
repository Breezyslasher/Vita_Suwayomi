/**
 * VitaSuwayomi - Library Cache Manager implementation
 */

#include "utils/library_cache.hpp"
#include <borealis.hpp>
#include <sstream>
#include <cstring>
#include <cstdlib>

#include "platform/platform.hpp"

namespace vitasuwayomi {

LibraryCache& LibraryCache::getInstance() {
    static LibraryCache instance;
    return instance;
}

bool LibraryCache::init() {
    if (m_initialized) return true;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Create cache directories
    if (!ensureDirectoryExists(getCacheDir())) {
        brls::Logger::error("LibraryCache: Failed to create cache directory");
        return false;
    }

    if (!ensureDirectoryExists(getCoverCacheDir())) {
        brls::Logger::error("LibraryCache: Failed to create cover cache directory");
        return false;
    }

    if (!ensureDirectoryExists(getMangaDetailsCacheDir())) {
        brls::Logger::error("LibraryCache: Failed to create manga details cache directory");
        return false;
    }

    if (!ensureDirectoryExists(getChaptersCacheDir())) {
        brls::Logger::error("LibraryCache: Failed to create chapters cache directory");
        return false;
    }

    if (!ensureDirectoryExists(getPageCacheDir())) {
        brls::Logger::error("LibraryCache: Failed to create page cache directory");
        return false;
    }

    m_initialized = true;
    brls::Logger::info("LibraryCache: Initialized at {}", getCacheDir());
    return true;
}

std::string LibraryCache::getCacheDir() {
    return platform::path("cache");
}

std::string LibraryCache::getCoverCacheDir() {
    return getCacheDir() + "/covers";
}

std::string LibraryCache::getCategoryFilePath(int categoryId) {
    return getCacheDir() + "/category_" + std::to_string(categoryId) + ".txt";
}

std::string LibraryCache::getCategoriesFilePath() {
    return getCacheDir() + "/categories.txt";
}

std::string LibraryCache::getAllLibraryFilePath() {
    return getCacheDir() + "/all_library.txt";
}

std::string LibraryCache::getCoverCachePath(int mangaId) {
    return getCoverCacheDir() + "/" + std::to_string(mangaId) + ".tga";
}

std::string LibraryCache::getMangaDetailsCacheDir() {
    return getCacheDir() + "/manga_details";
}

std::string LibraryCache::getMangaDetailsFilePath(int mangaId) {
    return getMangaDetailsCacheDir() + "/" + std::to_string(mangaId) + ".txt";
}

bool LibraryCache::ensureDirectoryExists(const std::string& path) {
    return platform::createDirRecursive(path);
}

std::string LibraryCache::serializeManga(const Manga& manga) {
    std::ostringstream ss;
    // Format: id|title|author|artist|description|thumbnailUrl|status|inLibrary|chapterCount|unreadCount|downloadCount|sourceName|inLibraryAt|lastReadAt|latestChapterUploadDate|categoryIds
    ss << manga.id << "|"
       << manga.title << "|"
       << manga.author << "|"
       << manga.artist << "|"
       << manga.description << "|"
       << manga.thumbnailUrl << "|"
       << static_cast<int>(manga.status) << "|"
       << (manga.inLibrary ? 1 : 0) << "|"
       << manga.chapterCount << "|"
       << manga.unreadCount << "|"
       << manga.downloadedCount << "|"
       << manga.sourceName << "|"
       << manga.inLibraryAt << "|"
       << manga.lastReadAt << "|"
       << manga.latestChapterUploadDate << "|";
    // Serialize categoryIds as comma-separated list (e.g. "1,3,5")
    for (size_t i = 0; i < manga.categoryIds.size(); i++) {
        if (i > 0) ss << ",";
        ss << manga.categoryIds[i];
    }
    return ss.str();
}

bool LibraryCache::deserializeManga(const std::string& line, Manga& manga) {
    std::istringstream ss(line);
    std::string token;
    std::vector<std::string> parts;

    while (std::getline(ss, token, '|')) {
        parts.push_back(token);
    }

    if (parts.size() < 11) return false;

    try {
        manga.id = std::stoi(parts[0]);
        manga.title = parts[1];
        manga.author = parts[2];
        manga.artist = parts[3];
        manga.description = parts[4];
        manga.thumbnailUrl = parts[5];
        manga.status = static_cast<MangaStatus>(std::stoi(parts[6]));
        manga.inLibrary = (parts[7] == "1");
        manga.chapterCount = std::stoi(parts[8]);
        manga.unreadCount = std::stoi(parts[9]);
        manga.downloadedCount = std::stoi(parts[10]);
        // sourceName added later; old caches have only 11 fields
        if (parts.size() > 11) manga.sourceName = parts[11];
        // Sort-related timestamps added later; old caches have only 12 fields
        if (parts.size() > 12) manga.inLibraryAt = std::stoll(parts[12]);
        if (parts.size() > 13) manga.lastReadAt = std::stoll(parts[13]);
        if (parts.size() > 14) manga.latestChapterUploadDate = std::stoll(parts[14]);
        // categoryIds added for cross-mode offline caching; old caches have only 15 fields
        if (parts.size() > 15 && !parts[15].empty()) {
            std::istringstream catSs(parts[15]);
            std::string catToken;
            while (std::getline(catSs, catToken, ',')) {
                if (!catToken.empty()) {
                    manga.categoryIds.push_back(std::stoi(catToken));
                }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string LibraryCache::serializeCategory(const Category& category) {
    std::ostringstream ss;
    // Format: id|name|order|mangaCount
    ss << category.id << "|"
       << category.name << "|"
       << category.order << "|"
       << category.mangaCount;
    return ss.str();
}

bool LibraryCache::deserializeCategory(const std::string& line, Category& category) {
    std::istringstream ss(line);
    std::string token;
    std::vector<std::string> parts;

    while (std::getline(ss, token, '|')) {
        parts.push_back(token);
    }

    if (parts.size() < 4) return false;

    try {
        category.id = std::stoi(parts[0]);
        category.name = parts[1];
        category.order = std::stoi(parts[2]);
        category.mangaCount = std::stoi(parts[3]);
        return true;
    } catch (...) {
        return false;
    }
}

bool LibraryCache::saveCategories(const std::vector<Category>& categories) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getCategoriesFilePath();

    std::string content;
    for (const auto& c : categories) {
        content += serializeCategory(c) + "\n";
    }

    if (!platform::writeFile(path, content)) {
        brls::Logger::error("LibraryCache: Failed to open {} for writing", path);
        return false;
    }

    brls::Logger::debug("LibraryCache: Saved {} categories", categories.size());
    return true;
}

bool LibraryCache::loadCategories(std::vector<Category>& categories) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getCategoriesFilePath();
    categories.clear();

    auto fileData = platform::readFile(path);
    if (fileData.empty()) return false;

    std::istringstream stream(std::string(reinterpret_cast<const char*>(fileData.data()), fileData.size()));
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        Category c;
        if (deserializeCategory(line, c)) {
            categories.push_back(c);
        }
    }

    brls::Logger::debug("LibraryCache: Loaded {} categories from cache", categories.size());
    return !categories.empty();
}

bool LibraryCache::hasCategoriesCache() {
    return platform::fileExists(getCategoriesFilePath());
}

bool LibraryCache::saveCategoryManga(int categoryId, const std::vector<Manga>& manga) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getCategoryFilePath(categoryId);

    std::string content;
    for (const auto& m : manga) {
        content += serializeManga(m) + "\n";
    }

    if (!platform::writeFile(path, content)) {
        brls::Logger::error("LibraryCache: Failed to open {} for writing", path);
        return false;
    }

    brls::Logger::debug("LibraryCache: Saved {} manga for category {}", manga.size(), categoryId);
    return true;
}

bool LibraryCache::loadCategoryManga(int categoryId, std::vector<Manga>& manga) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getCategoryFilePath(categoryId);
    manga.clear();

    auto fileData = platform::readFile(path);
    if (fileData.empty()) return false;

    std::istringstream stream(std::string(reinterpret_cast<const char*>(fileData.data()), fileData.size()));
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        Manga m;
        if (deserializeManga(line, m)) {
            manga.push_back(m);
        }
    }

    brls::Logger::debug("LibraryCache: Loaded {} manga for category {} from cache", manga.size(), categoryId);
    return !manga.empty();
}

bool LibraryCache::hasCategoryCache(int categoryId) {
    return platform::fileExists(getCategoryFilePath(categoryId));
}

void LibraryCache::invalidateCategoryCache(int categoryId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    platform::deleteFile(getCategoryFilePath(categoryId));
}

bool LibraryCache::saveAllLibraryManga(const std::vector<Manga>& manga) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getAllLibraryFilePath();

    std::string content;
    for (const auto& m : manga) {
        content += serializeManga(m) + "\n";
    }

    if (!platform::writeFile(path, content)) {
        brls::Logger::error("LibraryCache: Failed to open {} for writing", path);
        return false;
    }

    brls::Logger::debug("LibraryCache: Saved {} manga to all-library cache", manga.size());
    return true;
}

bool LibraryCache::loadAllLibraryManga(std::vector<Manga>& manga) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getAllLibraryFilePath();
    manga.clear();

    auto fileData = platform::readFile(path);
    if (fileData.empty()) return false;

    std::istringstream stream(std::string(reinterpret_cast<const char*>(fileData.data()), fileData.size()));
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        Manga m;
        if (deserializeManga(line, m)) {
            manga.push_back(m);
        }
    }

    brls::Logger::debug("LibraryCache: Loaded {} manga from all-library cache", manga.size());
    return !manga.empty();
}

bool LibraryCache::hasAllLibraryCache() {
    return platform::fileExists(getAllLibraryFilePath());
}

bool LibraryCache::saveCoverImage(int mangaId, const std::vector<uint8_t>& imageData) {
    if (!m_coverCacheEnabled || imageData.empty()) return false;

    std::lock_guard<std::mutex> lock(m_coverMutex);
    return platform::writeFile(getCoverCachePath(mangaId), imageData.data(), imageData.size());
}

bool LibraryCache::loadCoverImage(int mangaId, std::vector<uint8_t>& imageData) {
    if (!m_coverCacheEnabled) return false;

    std::lock_guard<std::mutex> lock(m_coverMutex);

    imageData = platform::readFile(getCoverCachePath(mangaId));
    if (imageData.size() > 5 * 1024 * 1024) {
        imageData.clear();
        return false;
    }
    return !imageData.empty();
}

bool LibraryCache::deleteCoverImage(int mangaId) {
    std::lock_guard<std::mutex> lock(m_coverMutex);
    return platform::deleteFile(getCoverCachePath(mangaId));
}

bool LibraryCache::hasCoverCache(int mangaId) {
    return platform::fileExists(getCoverCachePath(mangaId));
}

// --- Reader page image caching ---

std::string LibraryCache::getPageCacheDir() {
    return getCacheDir() + "/pages";
}

std::string LibraryCache::getPageCachePath(int mangaId, int chapterId, int pageIndex) {
    return getPageCacheDir() + "/" + std::to_string(mangaId) + "_" +
           std::to_string(chapterId) + "_" + std::to_string(pageIndex) + ".tga";
}

bool LibraryCache::savePageImage(int mangaId, int chapterId, int pageIndex, const std::vector<uint8_t>& imageData) {
    if (imageData.empty()) return false;

    std::lock_guard<std::mutex> lock(m_pageMutex);

    // Ensure page cache directory exists
    ensureDirectoryExists(getPageCacheDir());

    std::string path = getPageCachePath(mangaId, chapterId, pageIndex);
    return platform::writeFile(path, imageData.data(), imageData.size());
}

bool LibraryCache::loadPageImage(int mangaId, int chapterId, int pageIndex, std::vector<uint8_t>& imageData) {
    std::lock_guard<std::mutex> lock(m_pageMutex);

    imageData = platform::readFile(getPageCachePath(mangaId, chapterId, pageIndex));
    if (imageData.size() <= 18 || imageData.size() > 16 * 1024 * 1024) {
        imageData.clear();
        return false;
    }

    // Validate TGA header
    if (imageData.size() > 18 &&
        imageData[0] == 0 && imageData[1] == 0 &&
        imageData[2] == 2 && imageData[16] == 32) {
        return true;
    }

    // Invalid data, discard
    imageData.clear();
    return false;
}

bool LibraryCache::hasPageCache(int mangaId, int chapterId, int pageIndex) {
    return platform::fileExists(getPageCachePath(mangaId, chapterId, pageIndex));
}

void LibraryCache::clearPageCache() {
    std::lock_guard<std::mutex> lock(m_pageMutex);
    std::string dir = getPageCacheDir();

    for (const auto& name : platform::listDir(dir)) {
        if (name.find(".tga") != std::string::npos) {
            platform::deleteFile(dir + "/" + name);
        }
    }

    brls::Logger::info("LibraryCache: Page cache cleared");
}

void LibraryCache::clearPageCache(int mangaId) {
    std::lock_guard<std::mutex> lock(m_pageMutex);
    std::string dir = getPageCacheDir();
    std::string prefix = std::to_string(mangaId) + "_";

    for (const auto& name : platform::listDir(dir)) {
        if (name.compare(0, prefix.size(), prefix) == 0) {
            platform::deleteFile(dir + "/" + name);
        }
    }
}

void LibraryCache::clearAllCache() {
    clearLibraryCache();
    clearCoverCache();
    clearPageCache();
}

void LibraryCache::clearLibraryCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string dir = getCacheDir();

    for (const auto& name : platform::listDir(dir)) {
        if (name.find("category_") != std::string::npos || name == "all_library.txt") {
            platform::deleteFile(dir + "/" + name);
        }
    }

    brls::Logger::info("LibraryCache: Library cache cleared");
}

void LibraryCache::clearCoverCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string dir = getCoverCacheDir();

    for (const auto& name : platform::listDir(dir)) {
        if (name.find(".tga") != std::string::npos) {
            platform::deleteFile(dir + "/" + name);
        }
    }

    brls::Logger::info("LibraryCache: Cover cache cleared");
}

size_t LibraryCache::getCacheSize() {
    size_t total = 0;
    std::string coverDir = getCoverCacheDir();

    for (const auto& name : platform::listDir(coverDir)) {
        int64_t sz = platform::fileSize(coverDir + "/" + name);
        if (sz > 0) total += static_cast<size_t>(sz);
    }

    return total;
}

// Helper to escape special characters for serialization
static std::string escapeString(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        if (c == '|') result += "\\|";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\\') result += "\\\\";
        else result += c;
    }
    return result;
}

// Helper to unescape special characters for deserialization
static std::string unescapeString(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            char next = str[i + 1];
            if (next == '|') { result += '|'; i++; }
            else if (next == 'n') { result += '\n'; i++; }
            else if (next == 'r') { result += '\r'; i++; }
            else if (next == '\\') { result += '\\'; i++; }
            else result += str[i];
        } else {
            result += str[i];
        }
    }
    return result;
}

std::string LibraryCache::serializeMangaDetails(const Manga& manga) {
    std::ostringstream ss;
    // Format: key=value pairs, one per line for readability and robustness
    ss << "id=" << manga.id << "\n";
    ss << "sourceId=" << manga.sourceId << "\n";
    ss << "url=" << escapeString(manga.url) << "\n";
    ss << "title=" << escapeString(manga.title) << "\n";
    ss << "thumbnailUrl=" << escapeString(manga.thumbnailUrl) << "\n";
    ss << "artist=" << escapeString(manga.artist) << "\n";
    ss << "author=" << escapeString(manga.author) << "\n";
    ss << "description=" << escapeString(manga.description) << "\n";
    ss << "status=" << static_cast<int>(manga.status) << "\n";
    ss << "inLibrary=" << (manga.inLibrary ? 1 : 0) << "\n";
    ss << "unreadCount=" << manga.unreadCount << "\n";
    ss << "downloadedCount=" << manga.downloadedCount << "\n";
    ss << "chapterCount=" << manga.chapterCount << "\n";
    ss << "lastChapterRead=" << manga.lastChapterRead << "\n";
    ss << "sourceName=" << escapeString(manga.sourceName) << "\n";
    ss << "inLibraryAt=" << manga.inLibraryAt << "\n";
    ss << "lastReadAt=" << manga.lastReadAt << "\n";
    ss << "latestChapterUploadDate=" << manga.latestChapterUploadDate << "\n";

    // Serialize genres as comma-separated
    ss << "genre=";
    for (size_t i = 0; i < manga.genre.size(); i++) {
        if (i > 0) ss << ",";
        ss << escapeString(manga.genre[i]);
    }
    ss << "\n";

    return ss.str();
}

bool LibraryCache::deserializeMangaDetails(const std::string& data, Manga& manga) {
    std::istringstream ss(data);
    std::string line;

    while (std::getline(ss, line)) {
        if (line.empty()) continue;

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

        try {
            if (key == "id") manga.id = std::stoi(value);
            else if (key == "sourceId") manga.sourceId = std::stoll(value);
            else if (key == "url") manga.url = unescapeString(value);
            else if (key == "title") manga.title = unescapeString(value);
            else if (key == "thumbnailUrl") manga.thumbnailUrl = unescapeString(value);
            else if (key == "artist") manga.artist = unescapeString(value);
            else if (key == "author") manga.author = unescapeString(value);
            else if (key == "description") manga.description = unescapeString(value);
            else if (key == "status") manga.status = static_cast<MangaStatus>(std::stoi(value));
            else if (key == "inLibrary") manga.inLibrary = (value == "1");
            else if (key == "unreadCount") manga.unreadCount = std::stoi(value);
            else if (key == "downloadedCount") manga.downloadedCount = std::stoi(value);
            else if (key == "chapterCount") manga.chapterCount = std::stoi(value);
            else if (key == "lastChapterRead") manga.lastChapterRead = std::stoi(value);
            else if (key == "sourceName") manga.sourceName = unescapeString(value);
            else if (key == "inLibraryAt") manga.inLibraryAt = std::stoll(value);
            else if (key == "lastReadAt") manga.lastReadAt = std::stoll(value);
            else if (key == "latestChapterUploadDate") manga.latestChapterUploadDate = std::stoll(value);
            else if (key == "genre") {
                manga.genre.clear();
                if (!value.empty()) {
                    std::istringstream genreStream(value);
                    std::string genre;
                    while (std::getline(genreStream, genre, ',')) {
                        if (!genre.empty()) {
                            manga.genre.push_back(unescapeString(genre));
                        }
                    }
                }
            }
        } catch (...) {
            // Skip invalid values
        }
    }

    return manga.id > 0;
}

bool LibraryCache::saveMangaDetails(const Manga& manga) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Ensure directory exists
    ensureDirectoryExists(getMangaDetailsCacheDir());

    std::string path = getMangaDetailsFilePath(manga.id);
    std::string data = serializeMangaDetails(manga);

    if (!platform::writeFile(path, data)) {
        brls::Logger::error("LibraryCache: Failed to open {} for writing", path);
        return false;
    }

    brls::Logger::debug("LibraryCache: Saved manga details for id={}", manga.id);
    return true;
}

bool LibraryCache::loadMangaDetails(int mangaId, Manga& manga) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    auto fileData = platform::readFile(getMangaDetailsFilePath(mangaId));
    if (fileData.empty()) return false;

    std::string data(reinterpret_cast<const char*>(fileData.data()), fileData.size());

    if (deserializeMangaDetails(data, manga)) {
        brls::Logger::debug("LibraryCache: Loaded manga details for id={}", mangaId);
        return true;
    }

    return false;
}

bool LibraryCache::hasMangaDetailsCache(int mangaId) {
    return platform::fileExists(getMangaDetailsFilePath(mangaId));
}

// ---- Reading History Cache ----

std::string LibraryCache::getHistoryFilePath() {
    return getCacheDir() + "/history.txt";
}

std::string LibraryCache::serializeHistoryItem(const ReadingHistoryItem& item) {
    std::ostringstream ss;
    // Format: chapterId|mangaId|mangaTitle|mangaThumbnail|chapterName|chapterNumber|lastPageRead|pageCount|lastReadAt|sourceName
    ss << item.chapterId << "|"
       << item.mangaId << "|"
       << escapeString(item.mangaTitle) << "|"
       << escapeString(item.mangaThumbnail) << "|"
       << escapeString(item.chapterName) << "|"
       << item.chapterNumber << "|"
       << item.lastPageRead << "|"
       << item.pageCount << "|"
       << item.lastReadAt << "|"
       << escapeString(item.sourceName);
    return ss.str();
}

bool LibraryCache::deserializeHistoryItem(const std::string& line, ReadingHistoryItem& item) {
    // Split on unescaped pipe characters
    std::vector<std::string> parts;
    std::string current;
    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            current += line[i];
            current += line[i + 1];
            i++;
        } else if (line[i] == '|') {
            parts.push_back(current);
            current.clear();
        } else {
            current += line[i];
        }
    }
    parts.push_back(current);

    if (parts.size() < 10) return false;

    try {
        item.chapterId = std::stoi(parts[0]);
        item.mangaId = std::stoi(parts[1]);
        item.mangaTitle = unescapeString(parts[2]);
        item.mangaThumbnail = unescapeString(parts[3]);
        item.chapterName = unescapeString(parts[4]);
        item.chapterNumber = std::stof(parts[5]);
        item.lastPageRead = std::stoi(parts[6]);
        item.pageCount = std::stoi(parts[7]);
        item.lastReadAt = std::stoll(parts[8]);
        item.sourceName = unescapeString(parts[9]);
        return true;
    } catch (...) {
        return false;
    }
}

bool LibraryCache::saveHistory(const std::vector<ReadingHistoryItem>& history) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getHistoryFilePath();

    std::string content;
    for (const auto& item : history) {
        content += serializeHistoryItem(item) + "\n";
    }

    if (!platform::writeFile(path, content)) {
        brls::Logger::error("LibraryCache: Failed to open {} for writing", path);
        return false;
    }

    brls::Logger::debug("LibraryCache: Saved {} history items", history.size());
    return true;
}

bool LibraryCache::loadHistory(std::vector<ReadingHistoryItem>& history) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    history.clear();

    auto fileData = platform::readFile(getHistoryFilePath());
    if (fileData.empty()) return false;

    std::istringstream stream(std::string(reinterpret_cast<const char*>(fileData.data()), fileData.size()));
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        ReadingHistoryItem item;
        if (deserializeHistoryItem(line, item)) {
            history.push_back(item);
        }
    }

    brls::Logger::debug("LibraryCache: Loaded {} history items from cache", history.size());
    return !history.empty();
}

bool LibraryCache::hasHistoryCache() {
    return platform::fileExists(getHistoryFilePath());
}

void LibraryCache::invalidateHistoryCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    platform::deleteFile(getHistoryFilePath());
}

// ---- Chapter caching ----

std::string LibraryCache::getChaptersCacheDir() {
    return getCacheDir() + "/chapters";
}

std::string LibraryCache::getChaptersFilePath(int mangaId) {
    return getChaptersCacheDir() + "/" + std::to_string(mangaId) + ".txt";
}

std::string LibraryCache::serializeChapter(const Chapter& ch) {
    std::ostringstream ss;
    // Format: id|name|chapterNumber|scanlator|uploadDate|read|bookmarked|lastPageRead|pageCount|index|fetchedAt|lastReadAt|downloaded|mangaId
    ss << ch.id << "|"
       << ch.name << "|"
       << ch.chapterNumber << "|"
       << ch.scanlator << "|"
       << ch.uploadDate << "|"
       << (ch.read ? 1 : 0) << "|"
       << (ch.bookmarked ? 1 : 0) << "|"
       << ch.lastPageRead << "|"
       << ch.pageCount << "|"
       << ch.index << "|"
       << ch.fetchedAt << "|"
       << ch.lastReadAt << "|"
       << (ch.downloaded ? 1 : 0) << "|"
       << ch.mangaId;
    return ss.str();
}

bool LibraryCache::deserializeChapter(const std::string& line, Chapter& ch) {
    std::istringstream ss(line);
    std::string token;
    std::vector<std::string> parts;

    while (std::getline(ss, token, '|')) {
        parts.push_back(token);
    }

    if (parts.size() < 14) return false;

    try {
        ch.id = std::stoi(parts[0]);
        ch.name = parts[1];
        ch.chapterNumber = std::stof(parts[2]);
        ch.scanlator = parts[3];
        ch.uploadDate = std::stoll(parts[4]);
        ch.read = (parts[5] == "1");
        ch.bookmarked = (parts[6] == "1");
        ch.lastPageRead = std::stoi(parts[7]);
        ch.pageCount = std::stoi(parts[8]);
        ch.index = std::stoi(parts[9]);
        ch.fetchedAt = std::stoll(parts[10]);
        ch.lastReadAt = std::stoll(parts[11]);
        ch.downloaded = (parts[12] == "1");
        ch.mangaId = std::stoi(parts[13]);
        return true;
    } catch (...) {
        return false;
    }
}

bool LibraryCache::saveChapters(int mangaId, const std::vector<Chapter>& chapters) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    ensureDirectoryExists(getChaptersCacheDir());
    std::string path = getChaptersFilePath(mangaId);

    std::string content;
    for (const auto& ch : chapters) {
        content += serializeChapter(ch) + "\n";
    }

    if (!platform::writeFile(path, content)) {
        brls::Logger::error("LibraryCache: Failed to open {} for writing", path);
        return false;
    }

    brls::Logger::debug("LibraryCache: Saved {} chapters for manga {}", chapters.size(), mangaId);
    return true;
}

bool LibraryCache::loadChapters(int mangaId, std::vector<Chapter>& chapters) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    chapters.clear();

    auto fileData = platform::readFile(getChaptersFilePath(mangaId));
    if (fileData.empty()) return false;

    std::istringstream stream(std::string(reinterpret_cast<const char*>(fileData.data()), fileData.size()));
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        Chapter ch;
        if (deserializeChapter(line, ch)) {
            chapters.push_back(ch);
        }
    }

    brls::Logger::debug("LibraryCache: Loaded {} chapters for manga {} from cache", chapters.size(), mangaId);
    return true;
}

bool LibraryCache::hasChaptersCache(int mangaId) {
    return platform::fileExists(getChaptersFilePath(mangaId));
}

void LibraryCache::invalidateChaptersCache(int mangaId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    platform::deleteFile(getChaptersFilePath(mangaId));
}

} // namespace vitasuwayomi
