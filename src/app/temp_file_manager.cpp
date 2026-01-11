/**
 * VitaABS - Temp File Manager implementation
 */

#include "app/temp_file_manager.hpp"
#include "app/application.hpp"
#include <borealis.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#else
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#endif

namespace vitaabs {

static const char* TEMP_DIR = "ux0:data/VitaABS/temp";
static const char* STATE_FILE = "ux0:data/VitaABS/temp_state.txt";

TempFileManager& TempFileManager::getInstance() {
    static TempFileManager instance;
    return instance;
}

bool TempFileManager::init() {
    if (m_initialized) return true;

    m_tempDir = TEMP_DIR;

#ifdef __vita__
    // Create directories if needed
    sceIoMkdir("ux0:data/VitaABS", 0777);
    sceIoMkdir(TEMP_DIR, 0777);
#else
    mkdir("ux0:data/VitaABS", 0777);
    mkdir(TEMP_DIR, 0777);
#endif

    loadState();
    m_initialized = true;

    brls::Logger::info("TempFileManager: Initialized with {} cached files", m_tempFiles.size());
    return true;
}

std::string TempFileManager::generateTempFilename(const std::string& itemId, const std::string& episodeId,
                                                   const std::string& extension) {
    // Create a unique but readable filename
    std::string filename = "temp_";

    // Use first 8 chars of item ID
    if (itemId.length() > 8) {
        filename += itemId.substr(0, 8);
    } else {
        filename += itemId;
    }

    // Add episode ID if present
    if (!episodeId.empty()) {
        filename += "_";
        if (episodeId.length() > 8) {
            filename += episodeId.substr(0, 8);
        } else {
            filename += episodeId;
        }
    }

    // Add extension
    filename += extension;

    return m_tempDir + "/" + filename;
}

std::string TempFileManager::getTempFilePath(const std::string& itemId, const std::string& episodeId,
                                              const std::string& extension) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if we already have this file cached
    TempFileInfo* existing = findTempFile(itemId, episodeId);
    if (existing && !existing->filePath.empty()) {
        // Verify file still exists
#ifdef __vita__
        SceIoStat stat;
        if (sceIoGetstat(existing->filePath.c_str(), &stat) >= 0) {
            return existing->filePath;
        }
#else
        struct stat st;
        if (stat(existing->filePath.c_str(), &st) == 0) {
            return existing->filePath;
        }
#endif
        // File doesn't exist anymore, remove from list
        brls::Logger::debug("TempFileManager: Cached file no longer exists, removing from list");
    }

    // Generate new filename
    return generateTempFilename(itemId, episodeId, extension);
}

bool TempFileManager::hasCachedFile(const std::string& itemId, const std::string& episodeId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    TempFileInfo* existing = findTempFile(itemId, episodeId);
    if (!existing || existing->filePath.empty()) {
        return false;
    }

    // Verify file still exists
#ifdef __vita__
    SceIoStat stat;
    return sceIoGetstat(existing->filePath.c_str(), &stat) >= 0;
#else
    struct stat st;
    return stat(existing->filePath.c_str(), &st) == 0;
#endif
}

std::string TempFileManager::getCachedFilePath(const std::string& itemId, const std::string& episodeId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    TempFileInfo* existing = findTempFile(itemId, episodeId);
    if (!existing || existing->filePath.empty()) {
        return "";
    }

    // Verify file still exists
#ifdef __vita__
    SceIoStat stat;
    if (sceIoGetstat(existing->filePath.c_str(), &stat) < 0) {
        return "";
    }
#else
    struct stat st;
    if (stat(existing->filePath.c_str(), &st) != 0) {
        return "";
    }
#endif

    return existing->filePath;
}

void TempFileManager::registerTempFile(const std::string& itemId, const std::string& episodeId,
                                        const std::string& filePath, const std::string& title, int64_t fileSize) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if already registered
    TempFileInfo* existing = findTempFile(itemId, episodeId);
    if (existing) {
        existing->filePath = filePath;
        existing->title = title;
        existing->fileSize = fileSize;
        existing->lastAccessed = time(nullptr);
        brls::Logger::debug("TempFileManager: Updated existing temp file: {}", filePath);
    } else {
        TempFileInfo info;
        info.itemId = itemId;
        info.episodeId = episodeId;
        info.filePath = filePath;
        info.title = title;
        info.fileSize = fileSize;
        info.lastAccessed = time(nullptr);
        m_tempFiles.push_back(info);
        brls::Logger::info("TempFileManager: Registered new temp file: {} ({} bytes)", filePath, fileSize);
    }

    saveState();
}

void TempFileManager::touchTempFile(const std::string& itemId, const std::string& episodeId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    TempFileInfo* existing = findTempFile(itemId, episodeId);
    if (existing) {
        existing->lastAccessed = time(nullptr);
        saveState();
    }
}

int TempFileManager::cleanupTempFiles() {
    std::lock_guard<std::mutex> lock(m_mutex);

    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    int deletedCount = 0;

    // Sort by last accessed time (oldest first)
    std::sort(m_tempFiles.begin(), m_tempFiles.end(),
              [](const TempFileInfo& a, const TempFileInfo& b) {
                  return a.lastAccessed < b.lastAccessed;
              });

    // Delete oldest files if we exceed max count
    while ((int)m_tempFiles.size() > settings.maxTempFiles && !m_tempFiles.empty()) {
        TempFileInfo& oldest = m_tempFiles.front();
        brls::Logger::info("TempFileManager: Deleting old temp file: {} (max {} files)",
                          oldest.filePath, settings.maxTempFiles);

#ifdef __vita__
        sceIoRemove(oldest.filePath.c_str());
#else
        unlink(oldest.filePath.c_str());
#endif

        m_tempFiles.erase(m_tempFiles.begin());
        deletedCount++;
    }

    // Delete oldest files if we exceed max size
    if (settings.maxTempSizeMB > 0) {
        int64_t maxBytes = settings.maxTempSizeMB * 1024 * 1024;
        int64_t totalSize = 0;

        for (const auto& file : m_tempFiles) {
            totalSize += file.fileSize;
        }

        while (totalSize > maxBytes && !m_tempFiles.empty()) {
            TempFileInfo& oldest = m_tempFiles.front();
            brls::Logger::info("TempFileManager: Deleting temp file for size limit: {} ({} MB > {} MB)",
                              oldest.filePath, totalSize / (1024 * 1024), settings.maxTempSizeMB);

            totalSize -= oldest.fileSize;

#ifdef __vita__
            sceIoRemove(oldest.filePath.c_str());
#else
            unlink(oldest.filePath.c_str());
#endif

            m_tempFiles.erase(m_tempFiles.begin());
            deletedCount++;
        }
    }

    if (deletedCount > 0) {
        saveState();
    }

    return deletedCount;
}

bool TempFileManager::deleteTempFile(const std::string& itemId, const std::string& episodeId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_tempFiles.begin(); it != m_tempFiles.end(); ++it) {
        if (it->itemId == itemId && it->episodeId == episodeId) {
#ifdef __vita__
            sceIoRemove(it->filePath.c_str());
#else
            unlink(it->filePath.c_str());
#endif
            brls::Logger::info("TempFileManager: Deleted temp file: {}", it->filePath);
            m_tempFiles.erase(it);
            saveState();
            return true;
        }
    }
    return false;
}

void TempFileManager::clearAllTempFiles() {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& file : m_tempFiles) {
#ifdef __vita__
        sceIoRemove(file.filePath.c_str());
#else
        unlink(file.filePath.c_str());
#endif
    }

    brls::Logger::info("TempFileManager: Cleared {} temp files", m_tempFiles.size());
    m_tempFiles.clear();
    saveState();
}

int64_t TempFileManager::getTotalTempSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    int64_t total = 0;
    for (const auto& file : m_tempFiles) {
        total += file.fileSize;
    }
    return total;
}

int TempFileManager::getTempFileCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return (int)m_tempFiles.size();
}

std::vector<TempFileInfo> TempFileManager::getTempFiles() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tempFiles;
}

TempFileInfo* TempFileManager::findTempFile(const std::string& itemId, const std::string& episodeId) {
    for (auto& file : m_tempFiles) {
        if (file.itemId == itemId && file.episodeId == episodeId) {
            return &file;
        }
    }
    return nullptr;
}

void TempFileManager::saveState() {
    std::ofstream file(STATE_FILE);
    if (!file.is_open()) {
        brls::Logger::error("TempFileManager: Failed to save state");
        return;
    }

    for (const auto& info : m_tempFiles) {
        file << info.itemId << "|"
             << info.episodeId << "|"
             << info.filePath << "|"
             << info.title << "|"
             << info.fileSize << "|"
             << info.lastAccessed << "\n";
    }

    file.close();
}

void TempFileManager::loadState() {
    std::ifstream file(STATE_FILE);
    if (!file.is_open()) {
        return;
    }

    m_tempFiles.clear();
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::istringstream ss(line);
        TempFileInfo info;
        std::string field;

        if (std::getline(ss, info.itemId, '|') &&
            std::getline(ss, info.episodeId, '|') &&
            std::getline(ss, info.filePath, '|') &&
            std::getline(ss, info.title, '|') &&
            std::getline(ss, field, '|')) {
            info.fileSize = std::stoll(field);
            if (std::getline(ss, field, '|')) {
                info.lastAccessed = std::stoll(field);
            }

            // Verify file still exists before adding
#ifdef __vita__
            SceIoStat stat;
            if (sceIoGetstat(info.filePath.c_str(), &stat) >= 0) {
                m_tempFiles.push_back(info);
            }
#else
            struct stat st;
            if (stat(info.filePath.c_str(), &st) == 0) {
                m_tempFiles.push_back(info);
            }
#endif
        }
    }

    file.close();
}

} // namespace vitaabs
