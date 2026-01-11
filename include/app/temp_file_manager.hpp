/**
 * VitaABS - Temp File Manager
 * Manages cached audio files for streaming playback
 */

#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <mutex>

namespace vitaabs {

// Temp file info
struct TempFileInfo {
    std::string itemId;         // Item ID for this file
    std::string episodeId;      // Episode ID (for podcasts)
    std::string filePath;       // Full path to temp file
    std::string title;          // Display title
    int64_t fileSize = 0;       // File size in bytes
    time_t lastAccessed = 0;    // Last access time (for LRU cleanup)
};

/**
 * Manages temp files for streaming audio playback
 * - Caches downloaded files by item ID
 * - Reuses cached files instead of re-downloading
 * - Cleans up old files when limits exceeded
 */
class TempFileManager {
public:
    static TempFileManager& getInstance();

    // Initialize temp directory
    bool init();

    // Get temp file path for an item (creates unique name based on item/episode ID)
    // Returns existing file path if cached, otherwise returns new path to download to
    std::string getTempFilePath(const std::string& itemId, const std::string& episodeId,
                                 const std::string& extension);

    // Check if a cached temp file exists for this item
    bool hasCachedFile(const std::string& itemId, const std::string& episodeId);

    // Get path to existing cached file (returns empty if not cached)
    std::string getCachedFilePath(const std::string& itemId, const std::string& episodeId);

    // Register a newly downloaded temp file
    void registerTempFile(const std::string& itemId, const std::string& episodeId,
                          const std::string& filePath, const std::string& title, int64_t fileSize);

    // Update last accessed time (call when playing a cached file)
    void touchTempFile(const std::string& itemId, const std::string& episodeId);

    // Clean up old temp files to stay within limits
    // Returns number of files deleted
    int cleanupTempFiles();

    // Delete a specific temp file
    bool deleteTempFile(const std::string& itemId, const std::string& episodeId);

    // Delete all temp files
    void clearAllTempFiles();

    // Get total size of all temp files
    int64_t getTotalTempSize() const;

    // Get number of cached temp files
    int getTempFileCount() const;

    // Get all temp file info
    std::vector<TempFileInfo> getTempFiles() const;

    // Get temp directory path
    const std::string& getTempDir() const { return m_tempDir; }

    // Save/load state
    void saveState();
    void loadState();

private:
    TempFileManager() = default;
    ~TempFileManager() = default;
    TempFileManager(const TempFileManager&) = delete;
    TempFileManager& operator=(const TempFileManager&) = delete;

    // Generate unique filename for item
    std::string generateTempFilename(const std::string& itemId, const std::string& episodeId,
                                      const std::string& extension);

    // Find temp file info by item/episode ID
    TempFileInfo* findTempFile(const std::string& itemId, const std::string& episodeId);

    std::vector<TempFileInfo> m_tempFiles;
    mutable std::mutex m_mutex;
    std::string m_tempDir;
    bool m_initialized = false;
};

} // namespace vitaabs
