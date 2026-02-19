/**
 * VitaSuwayomi - Library Cache Manager implementation
 */

#include "utils/library_cache.hpp"
#include <borealis.hpp>
#include <fstream>
#include <sstream>
#include <cstring>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#else
#include <sys/stat.h>
#include <dirent.h>
#endif

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

    m_initialized = true;
    brls::Logger::info("LibraryCache: Initialized at {}", getCacheDir());
    return true;
}

std::string LibraryCache::getCacheDir() {
#ifdef __vita__
    return "ux0:data/VitaSuwayomi/cache";
#else
    return "./cache";
#endif
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
#ifdef __vita__
    SceIoStat stat;
    if (sceIoGetstat(path.c_str(), &stat) < 0) {
        // Directory doesn't exist, create it
        if (sceIoMkdir(path.c_str(), 0777) < 0) {
            return false;
        }
    }
    return true;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return mkdir(path.c_str(), 0755) == 0;
    }
    return true;
#endif
}

std::string LibraryCache::serializeManga(const Manga& manga) {
    std::ostringstream ss;
    // Format: id|title|author|artist|description|thumbnailUrl|status|inLibrary|chapterCount|unreadCount|downloadCount
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
       << manga.downloadedCount;
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

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        brls::Logger::error("LibraryCache: Failed to open {} for writing", path);
        return false;
    }

    for (const auto& c : categories) {
        std::string line = serializeCategory(c) + "\n";
        sceIoWrite(fd, line.c_str(), line.size());
    }

    sceIoClose(fd);
#else
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    for (const auto& c : categories) {
        file << serializeCategory(c) << "\n";
    }

    file.close();
#endif

    brls::Logger::debug("LibraryCache: Saved {} categories", categories.size());
    return true;
}

bool LibraryCache::loadCategories(std::vector<Category>& categories) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getCategoriesFilePath();
    categories.clear();

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }

    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    if (size <= 0 || size > 1024 * 1024) {
        sceIoClose(fd);
        return false;
    }

    std::vector<char> buffer(size + 1);
    sceIoRead(fd, buffer.data(), size);
    buffer[size] = '\0';
    sceIoClose(fd);

    std::istringstream stream(buffer.data());
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        Category c;
        if (deserializeCategory(line, c)) {
            categories.push_back(c);
        }
    }
#else
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        Category c;
        if (deserializeCategory(line, c)) {
            categories.push_back(c);
        }
    }

    file.close();
#endif

    brls::Logger::debug("LibraryCache: Loaded {} categories from cache", categories.size());
    return !categories.empty();
}

bool LibraryCache::hasCategoriesCache() {
    std::string path = getCategoriesFilePath();

#ifdef __vita__
    SceIoStat stat;
    return sceIoGetstat(path.c_str(), &stat) >= 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

bool LibraryCache::saveCategoryManga(int categoryId, const std::vector<Manga>& manga) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getCategoryFilePath(categoryId);

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        brls::Logger::error("LibraryCache: Failed to open {} for writing", path);
        return false;
    }

    for (const auto& m : manga) {
        std::string line = serializeManga(m) + "\n";
        sceIoWrite(fd, line.c_str(), line.size());
    }

    sceIoClose(fd);
#else
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    for (const auto& m : manga) {
        file << serializeManga(m) << "\n";
    }

    file.close();
#endif

    brls::Logger::debug("LibraryCache: Saved {} manga for category {}", manga.size(), categoryId);
    return true;
}

bool LibraryCache::loadCategoryManga(int categoryId, std::vector<Manga>& manga) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getCategoryFilePath(categoryId);
    manga.clear();

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }

    // Get file size
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    if (size <= 0 || size > 10 * 1024 * 1024) {
        sceIoClose(fd);
        return false;
    }

    std::vector<char> buffer(size + 1);
    sceIoRead(fd, buffer.data(), size);
    buffer[size] = '\0';
    sceIoClose(fd);

    std::istringstream stream(buffer.data());
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        Manga m;
        if (deserializeManga(line, m)) {
            manga.push_back(m);
        }
    }
#else
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        Manga m;
        if (deserializeManga(line, m)) {
            manga.push_back(m);
        }
    }

    file.close();
#endif

    brls::Logger::debug("LibraryCache: Loaded {} manga for category {} from cache", manga.size(), categoryId);
    return !manga.empty();
}

bool LibraryCache::hasCategoryCache(int categoryId) {
    std::string path = getCategoryFilePath(categoryId);

#ifdef __vita__
    SceIoStat stat;
    return sceIoGetstat(path.c_str(), &stat) >= 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

void LibraryCache::invalidateCategoryCache(int categoryId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string path = getCategoryFilePath(categoryId);

#ifdef __vita__
    sceIoRemove(path.c_str());
#else
    remove(path.c_str());
#endif
}

bool LibraryCache::saveCoverImage(int mangaId, const std::vector<uint8_t>& imageData) {
    if (!m_coverCacheEnabled || imageData.empty()) return false;

    std::lock_guard<std::mutex> lock(m_coverMutex);

    std::string path = getCoverCachePath(mangaId);

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        return false;
    }
    sceIoWrite(fd, imageData.data(), imageData.size());
    sceIoClose(fd);
#else
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(imageData.data()), imageData.size());
    file.close();
#endif

    return true;
}

bool LibraryCache::loadCoverImage(int mangaId, std::vector<uint8_t>& imageData) {
    if (!m_coverCacheEnabled) return false;

    std::lock_guard<std::mutex> lock(m_coverMutex);

    std::string path = getCoverCachePath(mangaId);
    imageData.clear();

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }

    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    if (size <= 0 || size > 5 * 1024 * 1024) {
        sceIoClose(fd);
        return false;
    }

    imageData.resize(size);
    sceIoRead(fd, imageData.data(), size);
    sceIoClose(fd);
#else
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size <= 0 || size > 5 * 1024 * 1024) {
        return false;
    }

    imageData.resize(size);
    file.read(reinterpret_cast<char*>(imageData.data()), size);
    file.close();
#endif

    return !imageData.empty();
}

bool LibraryCache::hasCoverCache(int mangaId) {
    std::string path = getCoverCachePath(mangaId);

#ifdef __vita__
    SceIoStat stat;
    return sceIoGetstat(path.c_str(), &stat) >= 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

void LibraryCache::clearAllCache() {
    clearLibraryCache();
    clearCoverCache();
}

void LibraryCache::clearLibraryCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string dir = getCacheDir();

#ifdef __vita__
    SceUID dfd = sceIoDopen(dir.c_str());
    if (dfd >= 0) {
        SceIoDirent entry;
        while (sceIoDread(dfd, &entry) > 0) {
            if (strstr(entry.d_name, "category_") != nullptr) {
                std::string path = dir + "/" + entry.d_name;
                sceIoRemove(path.c_str());
            }
        }
        sceIoDclose(dfd);
    }
#else
    DIR* d = opendir(dir.c_str());
    if (d) {
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            if (strstr(entry->d_name, "category_") != nullptr) {
                std::string path = dir + "/" + entry->d_name;
                remove(path.c_str());
            }
        }
        closedir(d);
    }
#endif

    brls::Logger::info("LibraryCache: Library cache cleared");
}

void LibraryCache::clearCoverCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string dir = getCoverCacheDir();

#ifdef __vita__
    SceUID dfd = sceIoDopen(dir.c_str());
    if (dfd >= 0) {
        SceIoDirent entry;
        while (sceIoDread(dfd, &entry) > 0) {
            if (strstr(entry.d_name, ".tga") != nullptr) {
                std::string path = dir + "/" + entry.d_name;
                sceIoRemove(path.c_str());
            }
        }
        sceIoDclose(dfd);
    }
#else
    DIR* d = opendir(dir.c_str());
    if (d) {
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            if (strstr(entry->d_name, ".tga") != nullptr) {
                std::string path = dir + "/" + entry->d_name;
                remove(path.c_str());
            }
        }
        closedir(d);
    }
#endif

    brls::Logger::info("LibraryCache: Cover cache cleared");
}

size_t LibraryCache::getCacheSize() {
    // Approximate calculation
    size_t total = 0;
    std::string coverDir = getCoverCacheDir();

#ifdef __vita__
    SceUID dfd = sceIoDopen(coverDir.c_str());
    if (dfd >= 0) {
        SceIoDirent entry;
        while (sceIoDread(dfd, &entry) > 0) {
            total += entry.d_stat.st_size;
        }
        sceIoDclose(dfd);
    }
#else
    DIR* d = opendir(coverDir.c_str());
    if (d) {
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string path = coverDir + "/" + entry->d_name;
            struct stat st;
            if (stat(path.c_str(), &st) == 0) {
                total += st.st_size;
            }
        }
        closedir(d);
    }
#endif

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

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        brls::Logger::error("LibraryCache: Failed to open {} for writing", path);
        return false;
    }
    sceIoWrite(fd, data.c_str(), data.size());
    sceIoClose(fd);
#else
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << data;
    file.close();
#endif

    brls::Logger::debug("LibraryCache: Saved manga details for id={}", manga.id);
    return true;
}

bool LibraryCache::loadMangaDetails(int mangaId, Manga& manga) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getMangaDetailsFilePath(mangaId);

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }

    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    if (size <= 0 || size > 1024 * 1024) {
        sceIoClose(fd);
        return false;
    }

    std::vector<char> buffer(size + 1);
    sceIoRead(fd, buffer.data(), size);
    buffer[size] = '\0';
    sceIoClose(fd);

    std::string data(buffer.data());
#else
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    file.close();
    std::string data = ss.str();
#endif

    if (deserializeMangaDetails(data, manga)) {
        brls::Logger::debug("LibraryCache: Loaded manga details for id={}", mangaId);
        return true;
    }

    return false;
}

bool LibraryCache::hasMangaDetailsCache(int mangaId) {
    std::string path = getMangaDetailsFilePath(mangaId);

#ifdef __vita__
    SceIoStat stat;
    return sceIoGetstat(path.c_str(), &stat) >= 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
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

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        brls::Logger::error("LibraryCache: Failed to open {} for writing", path);
        return false;
    }

    for (const auto& item : history) {
        std::string line = serializeHistoryItem(item) + "\n";
        sceIoWrite(fd, line.c_str(), line.size());
    }

    sceIoClose(fd);
#else
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    for (const auto& item : history) {
        file << serializeHistoryItem(item) << "\n";
    }

    file.close();
#endif

    brls::Logger::debug("LibraryCache: Saved {} history items", history.size());
    return true;
}

bool LibraryCache::loadHistory(std::vector<ReadingHistoryItem>& history) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getHistoryFilePath();
    history.clear();

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }

    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    if (size <= 0 || size > 5 * 1024 * 1024) {
        sceIoClose(fd);
        return false;
    }

    std::vector<char> buffer(size + 1);
    sceIoRead(fd, buffer.data(), size);
    buffer[size] = '\0';
    sceIoClose(fd);

    std::istringstream stream(buffer.data());
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        ReadingHistoryItem item;
        if (deserializeHistoryItem(line, item)) {
            history.push_back(item);
        }
    }
#else
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        ReadingHistoryItem item;
        if (deserializeHistoryItem(line, item)) {
            history.push_back(item);
        }
    }

    file.close();
#endif

    brls::Logger::debug("LibraryCache: Loaded {} history items from cache", history.size());
    return !history.empty();
}

bool LibraryCache::hasHistoryCache() {
    std::string path = getHistoryFilePath();

#ifdef __vita__
    SceIoStat stat;
    return sceIoGetstat(path.c_str(), &stat) >= 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

void LibraryCache::invalidateHistoryCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string path = getHistoryFilePath();

#ifdef __vita__
    sceIoRemove(path.c_str());
#else
    remove(path.c_str());
#endif
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

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        brls::Logger::error("LibraryCache: Failed to open {} for writing", path);
        return false;
    }

    for (const auto& ch : chapters) {
        std::string line = serializeChapter(ch) + "\n";
        sceIoWrite(fd, line.c_str(), line.size());
    }

    sceIoClose(fd);
#else
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    for (const auto& ch : chapters) {
        file << serializeChapter(ch) << "\n";
    }

    file.close();
#endif

    brls::Logger::debug("LibraryCache: Saved {} chapters for manga {}", chapters.size(), mangaId);
    return true;
}

bool LibraryCache::loadChapters(int mangaId, std::vector<Chapter>& chapters) {
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string path = getChaptersFilePath(mangaId);
    chapters.clear();

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }

    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    if (size <= 0 || size > 10 * 1024 * 1024) {
        sceIoClose(fd);
        return false;
    }

    std::vector<char> buffer(size + 1);
    sceIoRead(fd, buffer.data(), size);
    buffer[size] = '\0';
    sceIoClose(fd);

    std::istringstream stream(buffer.data());
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        Chapter ch;
        if (deserializeChapter(line, ch)) {
            chapters.push_back(ch);
        }
    }
#else
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        Chapter ch;
        if (deserializeChapter(line, ch)) {
            chapters.push_back(ch);
        }
    }

    file.close();
#endif

    brls::Logger::debug("LibraryCache: Loaded {} chapters for manga {} from cache", chapters.size(), mangaId);
    return true;
}

bool LibraryCache::hasChaptersCache(int mangaId) {
    std::string path = getChaptersFilePath(mangaId);
#ifdef __vita__
    SceIoStat stat;
    return sceIoGetstat(path.c_str(), &stat) >= 0;
#else
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
#endif
}

void LibraryCache::invalidateChaptersCache(int mangaId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string path = getChaptersFilePath(mangaId);

#ifdef __vita__
    sceIoRemove(path.c_str());
#else
    remove(path.c_str());
#endif
}

} // namespace vitasuwayomi
