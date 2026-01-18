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

std::string LibraryCache::getCoverCachePath(int mangaId) {
    return getCoverCacheDir() + "/" + std::to_string(mangaId) + ".tga";
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
       << manga.downloadCount;
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
        manga.downloadCount = std::stoi(parts[10]);
        return true;
    } catch (...) {
        return false;
    }
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

    std::lock_guard<std::mutex> lock(m_mutex);

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

    std::lock_guard<std::mutex> lock(m_mutex);

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

} // namespace vitasuwayomi
