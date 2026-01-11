/**
 * VitaSuwayomi - Suwayomi Server API Client
 * Handles all communication with Suwayomi manga server
 * API Reference: https://github.com/Suwayomi/Suwayomi-Server
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "utils/http_client.hpp"

namespace vitasuwayomi {

// Manga status enum (matches Suwayomi/Tachiyomi)
enum class MangaStatus {
    UNKNOWN = 0,
    ONGOING = 1,
    COMPLETED = 2,
    LICENSED = 3,
    PUBLISHING_FINISHED = 4,
    CANCELLED = 5,
    ON_HIATUS = 6
};

// Chapter download status
enum class DownloadState {
    NOT_DOWNLOADED,
    QUEUED,
    DOWNLOADING,
    DOWNLOADED,
    ERROR
};

// Extension/Source info
struct Source {
    int64_t id = 0;
    std::string name;
    std::string lang;
    std::string iconUrl;
    bool supportsLatest = false;
    bool isConfigurable = false;
    bool isNsfw = false;
};

// Extension info
struct Extension {
    std::string pkgName;
    std::string name;
    std::string lang;
    std::string versionName;
    int versionCode = 0;
    std::string iconUrl;
    bool installed = false;
    bool hasUpdate = false;
    bool obsolete = false;
    bool isNsfw = false;
};

// Category for organizing manga library
struct Category {
    int id = 0;
    std::string name;
    int order = 0;
    bool isDefault = false;
    int mangaCount = 0;
};

// Chapter info
struct Chapter {
    int id = 0;
    std::string url;
    std::string name;
    std::string scanlator;
    float chapterNumber = 0.0f;
    int64_t uploadDate = 0;       // Unix timestamp
    bool read = false;
    bool bookmarked = false;
    int lastPageRead = 0;
    int pageCount = 0;
    int index = 0;                // Chapter index in manga
    int64_t fetchedAt = 0;        // When chapter was fetched
    bool downloaded = false;
    DownloadState downloadState = DownloadState::NOT_DOWNLOADED;
    int mangaId = 0;
};

// Manga info
struct Manga {
    int id = 0;
    int64_t sourceId = 0;
    std::string url;
    std::string title;
    std::string thumbnailUrl;
    std::string artist;
    std::string author;
    std::string description;
    std::vector<std::string> genre;
    MangaStatus status = MangaStatus::UNKNOWN;
    bool inLibrary = false;
    bool inLibraryAt = false;     // Timestamp when added
    bool initialized = false;
    bool freshData = false;
    int64_t realUrl = 0;

    // Reading progress
    int unreadCount = 0;
    int downloadedCount = 0;
    int chapterCount = 0;
    int lastChapterRead = 0;
    float lastReadProgress = 0.0f;  // Progress in last chapter (0.0-1.0)

    // Tracking info
    std::vector<int> categoryIds;

    // Source info
    std::string sourceName;

    // Local state
    bool isDownloaded = false;

    // Helper to get status string
    std::string getStatusString() const {
        switch (status) {
            case MangaStatus::ONGOING: return "Ongoing";
            case MangaStatus::COMPLETED: return "Completed";
            case MangaStatus::LICENSED: return "Licensed";
            case MangaStatus::PUBLISHING_FINISHED: return "Publishing Finished";
            case MangaStatus::CANCELLED: return "Cancelled";
            case MangaStatus::ON_HIATUS: return "On Hiatus";
            default: return "Unknown";
        }
    }
};

// Page info for chapter reader
struct Page {
    int index = 0;
    std::string url;
    std::string imageUrl;
};

// Recent chapter update
struct RecentUpdate {
    Manga manga;
    Chapter chapter;
};

// Download queue item
struct DownloadQueueItem {
    int chapterId = 0;
    int mangaId = 0;
    std::string mangaTitle;
    std::string chapterName;
    float progress = 0.0f;
    DownloadState state = DownloadState::QUEUED;
    std::string error;
};

// Server settings/info (matches Suwayomi AboutDataClass)
struct ServerInfo {
    std::string name;
    std::string version;
    std::string revision;
    std::string buildType;
    int64_t buildTime = 0;
    std::string github;
    std::string discord;
};

// Source filter types
enum class FilterType {
    HEADER,
    SEPARATOR,
    TEXT,
    CHECKBOX,
    TRISTATE,
    SELECT,
    SORT,
    GROUP
};

// Source filter for search
struct SourceFilter {
    FilterType type = FilterType::TEXT;
    std::string name;
    std::string state;
    std::vector<std::string> options;      // For SELECT type
    std::vector<SourceFilter> filters;     // For GROUP type
};

// Track item for external tracking services
struct TrackRecord {
    int id = 0;
    int mangaId = 0;
    int trackerId = 0;
    std::string trackerName;
    std::string remoteId;
    std::string title;
    int lastChapterRead = 0;
    int totalChapters = 0;
    int score = 0;
    int status = 0;
    std::string displayScore;
};

// Tracker service
struct Tracker {
    int id = 0;
    std::string name;
    std::string iconUrl;
    bool isLoggedIn = false;
    std::vector<std::string> statusList;
    std::vector<std::string> scoreFormat;
};

/**
 * Suwayomi Server API Client singleton
 */
class SuwayomiClient {
public:
    static SuwayomiClient& getInstance();

    // Connection & Server Info
    bool connectToServer(const std::string& url);
    bool fetchServerInfo(ServerInfo& info);
    bool testConnection();

    // Extension Management
    bool fetchExtensionList(std::vector<Extension>& extensions);
    bool installExtension(const std::string& pkgName);
    bool updateExtension(const std::string& pkgName);
    bool uninstallExtension(const std::string& pkgName);
    std::string getExtensionIconUrl(const std::string& apkName);

    // Source Management
    bool fetchSourceList(std::vector<Source>& sources);
    bool fetchSource(int64_t sourceId, Source& source);
    bool fetchSourceFilters(int64_t sourceId, std::vector<SourceFilter>& filters);
    bool setSourceFilters(int64_t sourceId, const std::vector<SourceFilter>& filters);

    // Source Browsing
    bool fetchPopularManga(int64_t sourceId, int page, std::vector<Manga>& manga, bool& hasNextPage);
    bool fetchLatestManga(int64_t sourceId, int page, std::vector<Manga>& manga, bool& hasNextPage);
    bool searchManga(int64_t sourceId, const std::string& query, int page,
                     std::vector<Manga>& manga, bool& hasNextPage);
    bool quickSearchManga(int64_t sourceId, const std::string& query, std::vector<Manga>& manga);

    // Manga Operations
    bool fetchManga(int mangaId, Manga& manga);
    bool fetchMangaFull(int mangaId, Manga& manga);
    bool refreshManga(int mangaId, Manga& manga);
    bool addMangaToLibrary(int mangaId);
    bool removeMangaFromLibrary(int mangaId);
    std::string getMangaThumbnailUrl(int mangaId);

    // Chapter Operations
    bool fetchChapters(int mangaId, std::vector<Chapter>& chapters);
    bool fetchChapter(int mangaId, int chapterIndex, Chapter& chapter);
    bool updateChapter(int mangaId, int chapterIndex, bool read, bool bookmarked);
    bool markChapterRead(int mangaId, int chapterIndex);
    bool markChapterUnread(int mangaId, int chapterIndex);
    bool markChaptersRead(int mangaId, const std::vector<int>& chapterIndexes);
    bool markChaptersUnread(int mangaId, const std::vector<int>& chapterIndexes);
    bool updateChapterProgress(int mangaId, int chapterIndex, int lastPageRead);

    // Page Operations
    bool fetchChapterPages(int mangaId, int chapterIndex, std::vector<Page>& pages);
    std::string getPageImageUrl(int mangaId, int chapterIndex, int pageIndex);

    // Category Management
    bool fetchCategories(std::vector<Category>& categories);
    bool createCategory(const std::string& name);
    bool deleteCategory(int categoryId);
    bool updateCategory(int categoryId, const std::string& name, bool isDefault);
    bool reorderCategories(const std::vector<int>& categoryIds);
    bool addMangaToCategory(int mangaId, int categoryId);
    bool removeMangaFromCategory(int mangaId, int categoryId);
    bool fetchCategoryManga(int categoryId, std::vector<Manga>& manga);

    // Library Operations
    bool fetchLibraryManga(std::vector<Manga>& manga);
    bool fetchLibraryMangaByCategory(int categoryId, std::vector<Manga>& manga);
    bool triggerLibraryUpdate();
    bool triggerLibraryUpdate(int categoryId);
    bool fetchRecentUpdates(int page, std::vector<RecentUpdate>& updates);

    // Download Management
    bool queueChapterDownload(int mangaId, int chapterIndex);
    bool deleteChapterDownload(int mangaId, int chapterIndex);
    bool queueChapterDownloads(int mangaId, const std::vector<int>& chapterIndexes);
    bool deleteChapterDownloads(int mangaId, const std::vector<int>& chapterIndexes);
    bool fetchDownloadQueue(std::vector<DownloadQueueItem>& queue);
    bool startDownloads();
    bool stopDownloads();
    bool clearDownloadQueue();
    bool reorderDownload(int mangaId, int chapterIndex, int newPosition);

    // Backup/Restore
    bool exportBackup(const std::string& savePath);
    bool importBackup(const std::string& filePath);
    bool validateBackup(const std::string& filePath);

    // Tracking
    bool fetchTrackers(std::vector<Tracker>& trackers);
    bool loginTracker(int trackerId, const std::string& username, const std::string& password);
    bool logoutTracker(int trackerId);
    bool searchTracker(int trackerId, const std::string& query, std::vector<TrackRecord>& results);
    bool bindTracker(int mangaId, int trackerId, int remoteId);
    bool updateTracking(int mangaId, int trackerId, const TrackRecord& record);
    bool fetchMangaTracking(int mangaId, std::vector<TrackRecord>& records);

    // Configuration
    void setServerUrl(const std::string& url) { m_serverUrl = url; }
    const std::string& getServerUrl() const { return m_serverUrl; }
    void setAuthCredentials(const std::string& username, const std::string& password);
    void clearAuth();

    // Check if client is connected
    bool isConnected() const { return !m_serverUrl.empty() && m_isConnected; }

    // Get update summary
    bool fetchUpdateSummary(int& pendingUpdates, int& runningJobs, bool& isUpdating);

private:
    SuwayomiClient() = default;
    ~SuwayomiClient() = default;

    std::string buildApiUrl(const std::string& endpoint);

    // Create HTTP client with authentication
    HttpClient createHttpClient();

    // JSON parsing helpers
    std::string extractJsonValue(const std::string& json, const std::string& key);
    int extractJsonInt(const std::string& json, const std::string& key);
    float extractJsonFloat(const std::string& json, const std::string& key);
    bool extractJsonBool(const std::string& json, const std::string& key);
    int64_t extractJsonInt64(const std::string& json, const std::string& key);
    std::string extractJsonArray(const std::string& json, const std::string& key);
    std::string extractJsonObject(const std::string& json, const std::string& key);
    std::vector<std::string> extractJsonStringArray(const std::string& json, const std::string& key);

    // Parse complex objects from JSON
    Manga parseManga(const std::string& json);
    Chapter parseChapter(const std::string& json);
    Source parseSource(const std::string& json);
    Extension parseExtension(const std::string& json);
    Category parseCategory(const std::string& json);
    Page parsePage(const std::string& json);
    Tracker parseTracker(const std::string& json);
    TrackRecord parseTrackRecord(const std::string& json);

    // Split array helper
    std::vector<std::string> splitJsonArray(const std::string& arrayJson);

    std::string m_serverUrl;
    std::string m_authUsername;
    std::string m_authPassword;
    bool m_isConnected = false;
    ServerInfo m_serverInfo;
};

} // namespace vitasuwayomi
