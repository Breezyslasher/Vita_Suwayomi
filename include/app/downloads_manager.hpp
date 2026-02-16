/**
 * VitaSuwayomi - Downloads Manager
 * Handles local manga chapter downloads for offline reading
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>

namespace vitasuwayomi {

// Download state
enum class LocalDownloadState {
    QUEUED,
    DOWNLOADING,
    PAUSED,
    COMPLETED,
    FAILED
};

// Downloaded page info
struct DownloadedPage {
    int index = 0;              // Page index
    std::string localPath;      // Local image path
    int64_t size = 0;           // File size
    bool downloaded = false;    // Download complete
};

// Downloaded chapter info
struct DownloadedChapter {
    int chapterId = 0;          // Chapter ID from server
    int chapterIndex = 0;       // Chapter index in manga
    std::string name;           // Chapter name
    float chapterNumber = 0.0f; // Chapter number (e.g., 1.5)
    std::string localPath;      // Local folder path for pages
    int pageCount = 0;          // Total pages
    int downloadedPages = 0;    // Downloaded pages count
    std::vector<DownloadedPage> pages;  // Page info
    LocalDownloadState state = LocalDownloadState::QUEUED;
    int lastPageRead = 0;       // Reading progress
    time_t lastReadTime = 0;    // Last read timestamp
};

// Download item information (manga)
struct DownloadItem {
    int mangaId = 0;            // Manga ID from server
    std::string title;          // Manga title
    std::string author;         // Author name
    std::string artist;         // Artist name
    std::string localPath;      // Local storage path
    std::string coverUrl;       // Cover image URL (remote)
    std::string localCoverPath; // Local cover image path
    std::string description;    // Manga description
    int64_t totalBytes = 0;     // Total downloaded size
    LocalDownloadState state = LocalDownloadState::QUEUED;

    // Downloaded chapters
    std::vector<DownloadedChapter> chapters;
    int totalChapters = 0;      // Total chapters downloaded
    int completedChapters = 0;  // Fully downloaded chapters

    // Reading progress
    int lastChapterRead = 0;    // Last chapter index read
    int lastPageRead = 0;       // Last page read in that chapter
    time_t lastReadTime = 0;    // Last read timestamp
};

// Progress callback: (downloadedPages, totalPages)
using DownloadProgressCallback = std::function<void(int, int)>;

// Chapter completion callback: (mangaId, chapterIndex, success)
using ChapterCompletionCallback = std::function<void(int, int, bool)>;

class DownloadsManager {
public:
    static DownloadsManager& getInstance();

    // Initialize downloads directory and load saved state
    bool init();

    // Queue manga chapter(s) for download
    bool queueChapterDownload(int mangaId, int chapterId, int chapterIndex,
                              const std::string& mangaTitle, const std::string& chapterName = "");
    bool queueChaptersDownload(int mangaId, const std::vector<std::pair<int,int>>& chapters,
                               const std::string& mangaTitle);

    // Start downloading queued items
    void startDownloads();

    // Pause all downloads
    void pauseDownloads();

    // Cancel a specific download
    bool cancelDownload(int mangaId);
    bool cancelChapterDownload(int mangaId, int chapterIndex);

    // Reorder queue items (for touch-based queue management)
    // direction: -1 for up (earlier in queue), +1 for down (later in queue)
    bool moveChapterInQueue(int mangaId, int chapterIndex, int direction);

    // Get flat list of all queued/downloading chapters for display
    struct QueuedChapterInfo {
        int mangaId;
        int chapterId;
        int chapterIndex;
        std::string mangaTitle;
        std::string chapterName;
        float chapterNumber;
        int pageCount;
        int downloadedPages;
        LocalDownloadState state;
    };
    std::vector<QueuedChapterInfo> getQueuedChapters() const;

    // Delete downloaded manga/chapters
    bool deleteMangaDownload(int mangaId);
    bool deleteChapterDownload(int mangaId, int chapterIndex);

    // Get all download items
    std::vector<DownloadItem> getDownloads() const;

    // Get a specific manga download
    DownloadItem* getMangaDownload(int mangaId);

    // Get a specific chapter download
    DownloadedChapter* getChapterDownload(int mangaId, int chapterIndex);

    // Check if manga/chapter is downloaded
    bool isMangaDownloaded(int mangaId) const;
    bool isChapterDownloaded(int mangaId, int chapterIndex) const;

    // Get local page path for offline reading
    std::string getPagePath(int mangaId, int chapterIndex, int pageIndex) const;

    // Get all page paths for a chapter
    std::vector<std::string> getChapterPages(int mangaId, int chapterIndex) const;

    // Update reading progress
    void updateReadingProgress(int mangaId, int chapterIndex, int lastPageRead);

    // Sync progress to/from server
    void syncProgressToServer();
    void syncProgressFromServer();

    // Save/load state to persistent storage
    void saveState();
    void loadState();

    // Resume incomplete downloads (queues PAUSED/FAILED/interrupted chapters)
    void resumeIncompleteDownloads();

    // Check if there are any incomplete downloads
    bool hasIncompleteDownloads() const;

    // Count incomplete downloads (returns number of chapters to resume)
    int countIncompleteDownloads() const;

    // Set progress callback for UI updates
    void setProgressCallback(DownloadProgressCallback callback);

    // Set chapter completion callback for UI refresh
    void setChapterCompletionCallback(ChapterCompletionCallback callback);

    // Get downloads directory path
    std::string getDownloadsPath() const;

    // Download and save cover image to local storage
    std::string downloadCoverImage(int mangaId, const std::string& coverUrl);

    // Get local cover path for a manga
    std::string getLocalCoverPath(int mangaId) const;

    // Get total download statistics
    int getTotalDownloadedChapters() const;
    int64_t getTotalDownloadSize() const;

private:
    DownloadsManager() = default;
    ~DownloadsManager() = default;
    DownloadsManager(const DownloadsManager&) = delete;
    DownloadsManager& operator=(const DownloadsManager&) = delete;

    // Download a single chapter (runs in background)
    void downloadChapter(int mangaId, DownloadedChapter& chapter);

    // Download a single page
    bool downloadPage(int mangaId, int chapterIndex, int pageIndex,
                      const std::string& imageUrl, std::string& localPath);

    // Internal save without locking (caller must hold m_mutex)
    void saveStateUnlocked();

    // Validate that downloaded files actually exist on disk
    void validateDownloadedFiles();

    // Create directories for manga/chapter
    std::string createMangaDir(int mangaId, const std::string& title);
    std::string createChapterDir(const std::string& mangaDir, int chapterIndex,
                                  const std::string& chapterName);

    std::vector<DownloadItem> m_downloads;
    mutable std::mutex m_mutex;
    std::atomic<bool> m_downloading{false};
    bool m_initialized = false;
    DownloadProgressCallback m_progressCallback;
    ChapterCompletionCallback m_chapterCompletionCallback;
    std::string m_downloadsPath;

    // Debouncing for saveStateUnlocked
    std::chrono::steady_clock::time_point m_lastSaveTime;
    bool m_saveStatePending = false;
};

} // namespace vitasuwayomi
