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
#include <map>
#include <set>
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

// Authentication mode (matches Suwayomi-Server authMode)
enum class AuthMode {
    NONE = 0,        // No authentication
    BASIC_AUTH = 1,  // HTTP Basic Access Authentication
    SIMPLE_LOGIN = 2, // Cookie-based session (custom login page)
    UI_LOGIN = 3     // JWT-based authentication (v2.1.1894+)
};

// Reading history item
struct ReadingHistoryItem {
    int chapterId = 0;
    int mangaId = 0;
    std::string mangaTitle;
    std::string mangaThumbnail;
    std::string chapterName;
    float chapterNumber = 0.0f;
    int lastPageRead = 0;
    int pageCount = 0;
    int64_t lastReadAt = 0;  // Unix timestamp in milliseconds
    std::string sourceName;
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
    bool hasConfigurableSources = false;  // True if any source has settings
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
    int64_t lastReadAt = 0;       // When chapter was last read (Unix timestamp)
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
    int64_t inLibraryAt = 0;      // Timestamp when added to library (Unix ms)
    bool initialized = false;
    bool freshData = false;
    int64_t realUrl = 0;

    // Reading progress
    int unreadCount = 0;
    int downloadedCount = 0;
    int chapterCount = 0;
    int lastChapterRead = 0;
    float lastReadProgress = 0.0f;  // Progress in last chapter (0.0-1.0)
    int64_t lastReadAt = 0;         // Timestamp when manga was last read (Unix ms)
    int64_t latestChapterUploadDate = 0;  // Latest chapter upload date for "Date Updated" sort

    // Tracking info
    std::vector<int> categoryIds;

    // Per-manga metadata (key-value pairs from server)
    std::map<std::string, std::string> meta;

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

    // Helper to detect if manga is a webtoon/long strip format
    // Based on genre tags and source name
    bool isWebtoon() const {
        // Check genre for webtoon indicators
        for (const auto& g : genre) {
            std::string lower = g;
            for (auto& c : lower) c = std::tolower(c);

            if (lower.find("long strip") != std::string::npos ||
                lower.find("webtoon") != std::string::npos ||
                lower.find("web comic") != std::string::npos ||
                lower.find("manhwa") != std::string::npos ||
                lower.find("manhua") != std::string::npos ||
                lower.find("full color") != std::string::npos) {
                return true;
            }
        }

        // Check source name for common webtoon sources
        std::string lowerSource = sourceName;
        for (auto& c : lowerSource) c = std::tolower(c);

        if (lowerSource.find("webtoon") != std::string::npos ||
            lowerSource.find("tapas") != std::string::npos ||
            lowerSource.find("tappytoon") != std::string::npos ||
            lowerSource.find("lezhin") != std::string::npos ||
            lowerSource.find("toomics") != std::string::npos ||
            lowerSource.find("manhwa") != std::string::npos ||
            lowerSource.find("manhua") != std::string::npos ||
            lowerSource.find("bilibili") != std::string::npos ||
            lowerSource.find("asura") != std::string::npos ||
            lowerSource.find("reaper") != std::string::npos ||
            lowerSource.find("flame") != std::string::npos) {
            return true;
        }

        return false;
    }
};

// Page info for chapter reader
struct Page {
    int index = 0;
    std::string url;
    std::string imageUrl;

    // For webtoon page splitting (tall images split into segments)
    int segment = 0;        // Which segment of the original image (0 = first/only)
    int totalSegments = 1;  // Total segments for this page (1 = not split)
    int originalIndex = -1; // Original page index before splitting (-1 = not split)
};

// Recent chapter update
struct RecentUpdate {
    Manga manga;
    Chapter chapter;
};

// Global search result
struct GlobalSearchResult {
    Source source;
    std::vector<Manga> manga;
    bool hasNextPage = false;
};

// Download queue item
struct DownloadQueueItem {
    int chapterId = 0;
    int mangaId = 0;
    std::string mangaTitle;
    std::string chapterName;
    float chapterNumber = 0.0f;
    int pageCount = 0;
    int downloadedPages = 0;
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

// Track search result (from tracker search)
struct TrackSearchResult {
    int64_t remoteId = 0;
    std::string title;
    std::string coverUrl;
    std::string summary;
    std::string publishingStatus;
    std::string publishingType;
    std::string startDate;  // String format from API
    int totalChapters = 0;
};

// Track item for external tracking services
struct TrackRecord {
    int id = 0;
    int mangaId = 0;
    int trackerId = 0;
    std::string trackerName;
    int64_t remoteId = 0;
    std::string remoteUrl;
    std::string title;
    double lastChapterRead = 0.0;
    int totalChapters = 0;
    double score = 0.0;
    int status = 0;
    std::string displayScore;
    int64_t startDate = 0;
    int64_t finishDate = 0;
};

// Tracker service
struct Tracker {
    int id = 0;
    std::string name;
    std::string iconUrl;
    bool isLoggedIn = false;
    bool isTokenExpired = false;
    std::vector<std::string> statuses;    // Status options (e.g., "Reading", "Completed")
    std::vector<std::string> scores;       // Score format options
    bool supportsTrackDeletion = false;
};

// Source preference types (matches Suwayomi preference types)
enum class SourcePreferenceType {
    SWITCH,
    CHECKBOX,
    EDIT_TEXT,
    LIST,
    MULTI_SELECT_LIST
};

// Source preference (for source settings/configuration)
struct SourcePreference {
    SourcePreferenceType type = SourcePreferenceType::SWITCH;
    std::string key;
    std::string title;
    std::string summary;
    bool visible = true;
    bool enabled = true;

    // For Switch/CheckBox preferences
    bool currentValue = false;
    bool defaultValue = false;

    // For EditText preferences
    std::string currentText;
    std::string defaultText;
    std::string dialogTitle;
    std::string dialogMessage;

    // For List preferences (single select)
    std::vector<std::string> entries;      // Display names
    std::vector<std::string> entryValues;  // Actual values
    std::string selectedValue;
    std::string defaultListValue;

    // For MultiSelectList preferences
    std::vector<std::string> selectedValues;
    std::vector<std::string> defaultMultiValues;
};

// Source preference change (for updating preferences)
struct SourcePreferenceChange {
    int position = 0;  // Position in preferences list

    // Only one of these should be set based on preference type
    bool switchState = false;
    bool checkBoxState = false;
    std::string editTextState;
    std::string listState;
    std::vector<std::string> multiSelectState;

    // Which field is set
    bool hasSwitchState = false;
    bool hasCheckBoxState = false;
    bool hasEditTextState = false;
    bool hasListState = false;
    bool hasMultiSelectState = false;
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

    // Check if server requires authentication (returns true if 401 received)
    bool checkServerRequiresAuth(const std::string& url);

    // Check if server supports JWT login (simple_login/ui_login modes)
    // Returns true if login mutation exists, false if server only supports basic auth
    bool checkServerSupportsJWTLogin(const std::string& url);

    // Get suggested auth mode based on server response
    // Returns the likely auth mode the server is configured to use
    // Also returns an error message if detection fails
    AuthMode detectServerAuthMode(const std::string& url, std::string& errorMessage);

    // Extension Management
    bool fetchExtensionList(std::vector<Extension>& extensions);
    bool fetchInstalledExtensions(std::vector<Extension>& extensions);  // Server-side filtered: installed only
    bool fetchUninstalledExtensions(std::vector<Extension>& extensions, const std::set<std::string>& languages);  // Server-side filtered by languages
    bool installExtension(const std::string& pkgName);
    bool updateExtension(const std::string& pkgName);
    bool uninstallExtension(const std::string& pkgName);
    std::string getExtensionIconUrl(const std::string& apkName);

    // Source Management
    bool fetchSourceList(std::vector<Source>& sources);
    bool fetchSource(int64_t sourceId, Source& source);
    bool fetchSourceFilters(int64_t sourceId, std::vector<SourceFilter>& filters);
    bool setSourceFilters(int64_t sourceId, const std::vector<SourceFilter>& filters);

    // Source Preferences (for configurable sources)
    bool fetchSourcePreferences(int64_t sourceId, std::vector<SourcePreference>& preferences);
    bool updateSourcePreference(int64_t sourceId, const SourcePreferenceChange& change);

    // Source Metadata
    bool setSourceMeta(int64_t sourceId, const std::string& key, const std::string& value);
    bool deleteSourceMeta(int64_t sourceId, const std::string& key);

    // Get sources for an extension (by package name)
    bool fetchSourcesForExtension(const std::string& pkgName, std::vector<Source>& sources);

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
    bool markAllChaptersRead(int mangaId);
    bool markAllChaptersUnread(int mangaId);
    bool updateChapterProgress(int mangaId, int chapterIndex, int lastPageRead);

    // Page Operations
    bool fetchChapterPages(int mangaId, int chapterId, std::vector<Page>& pages);
    std::string getPageImageUrl(int chapterId, int pageIndex);

    // Category Management
    bool fetchCategories(std::vector<Category>& categories);
    bool createCategory(const std::string& name);
    bool deleteCategory(int categoryId);
    bool updateCategory(int categoryId, const std::string& name, bool isDefault);
    bool reorderCategories(const std::vector<int>& categoryIds);
    bool moveCategoryOrder(int categoryId, int newPosition);  // Move category to new position (0-indexed)
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
    // GraphQL-first with REST fallback for all download operations
    bool queueChapterDownload(int chapterId, int mangaId, int chapterIndex);
    bool deleteChapterDownload(int chapterId, int mangaId, int chapterIndex);
    bool queueChapterDownloads(const std::vector<int>& chapterIds);
    bool deleteChapterDownloads(const std::vector<int>& chapterIds, int mangaId = 0, const std::vector<int>& chapterIndexes = {});
    bool fetchDownloadQueue(std::vector<DownloadQueueItem>& queue);
    bool startDownloads();
    bool stopDownloads();
    bool clearDownloadQueue();
    bool reorderDownload(int chapterId, int mangaId, int chapterIndex, int newPosition);

    // Backup/Restore
    bool exportBackup(const std::string& savePath);
    bool importBackup(const std::string& filePath);
    bool validateBackup(const std::string& filePath);

    // Tracking
    bool fetchTrackers(std::vector<Tracker>& trackers);
    bool fetchTracker(int trackerId, Tracker& tracker);
    bool loginTrackerCredentials(int trackerId, const std::string& username, const std::string& password);
    bool loginTrackerOAuth(int trackerId, const std::string& callbackUrl, std::string& oauthUrl);
    bool logoutTracker(int trackerId);
    bool searchTracker(int trackerId, const std::string& query, std::vector<TrackSearchResult>& results);
    bool bindTracker(int mangaId, int trackerId, int64_t remoteId);
    bool unbindTracker(int recordId, bool deleteRemoteTrack = false);
    bool updateTrackRecord(int recordId, int status = -1, double lastChapterRead = -1,
                          const std::string& scoreString = "", int64_t startDate = -1, int64_t finishDate = -1);
    bool fetchMangaTracking(int mangaId, std::vector<TrackRecord>& records);

    // Legacy compatibility
    bool loginTracker(int trackerId, const std::string& username, const std::string& password);
    bool bindTracker(int mangaId, int trackerId, int remoteId);
    bool updateTracking(int mangaId, int trackerId, const TrackRecord& record);

    // Reading History (Continue Reading)
    bool fetchReadingHistory(int offset, int limit, std::vector<ReadingHistoryItem>& history);
    bool fetchReadingHistory(std::vector<ReadingHistoryItem>& history);

    // Global Search (search across all sources)
    bool globalSearch(const std::string& query, std::vector<GlobalSearchResult>& results);
    bool globalSearch(const std::string& query, const std::vector<int64_t>& sourceIds,
                      std::vector<GlobalSearchResult>& results);

    // Set manga categories (replaces all categories)
    bool setMangaCategories(int mangaId, const std::vector<int>& categoryIds);

    // Manga Metadata (per-manga settings like reader preferences)
    bool fetchMangaMeta(int mangaId, std::map<std::string, std::string>& meta);
    bool setMangaMeta(int mangaId, const std::string& key, const std::string& value);
    bool deleteMangaMeta(int mangaId, const std::string& key);

    // Configuration
    void setServerUrl(const std::string& url) { m_serverUrl = url; }
    const std::string& getServerUrl() const { return m_serverUrl; }
    void setAuthCredentials(const std::string& username, const std::string& password);
    void clearAuth();

    // Authentication mode
    void setAuthMode(AuthMode mode) { m_authMode = mode; }
    AuthMode getAuthMode() const { return m_authMode; }

    // Login methods for different auth modes
    // Returns true if login successful, false otherwise
    bool login(const std::string& username, const std::string& password);
    bool refreshToken();  // Refresh JWT access token using refresh token
    bool isAuthenticated() const;
    void logout();

    // Get stored tokens (for persistence)
    const std::string& getAccessToken() const { return m_accessToken; }
    const std::string& getRefreshToken() const { return m_refreshToken; }
    const std::string& getSessionCookie() const { return m_sessionCookie; }

    // Set tokens (for restoring from persistence)
    void setTokens(const std::string& accessToken, const std::string& refreshToken);
    void setSessionCookie(const std::string& cookie);

    // Check if client is connected
    bool isConnected() const { return !m_serverUrl.empty() && m_isConnected; }

    // Build a proxied URL for external images (tracker covers, etc.)
    // Returns the URL as-is if it's already a server URL or if proxy isn't available
    std::string buildProxiedImageUrl(const std::string& externalUrl) const;

    // Get update summary
    bool fetchUpdateSummary(int& pendingUpdates, int& runningJobs, bool& isUpdating);

    // Create HTTP client with authentication (public for use by other managers)
    HttpClient createHttpClient();

private:
    SuwayomiClient() = default;
    ~SuwayomiClient() = default;

    std::string buildApiUrl(const std::string& endpoint);
    std::string buildGraphQLUrl();

    // GraphQL query executor - returns response body or empty string on failure
    std::string executeGraphQL(const std::string& query, const std::string& variables = "");

    // Internal GraphQL executor with retry control (for token refresh)
    std::string executeGraphQLInternal(const std::string& query, const std::string& variables, bool allowRetry);

    // GraphQL-based implementations (primary API)
    bool fetchSourceListGraphQL(std::vector<Source>& sources);
    bool fetchPopularMangaGraphQL(int64_t sourceId, int page, std::vector<Manga>& manga, bool& hasNextPage);
    bool fetchLatestMangaGraphQL(int64_t sourceId, int page, std::vector<Manga>& manga, bool& hasNextPage);
    bool searchMangaGraphQL(int64_t sourceId, const std::string& query, int page, std::vector<Manga>& manga, bool& hasNextPage);
    bool fetchLibraryMangaGraphQL(std::vector<Manga>& manga);
    bool fetchCategoriesGraphQL(std::vector<Category>& categories);
    bool fetchChaptersGraphQL(int mangaId, std::vector<Chapter>& chapters);
    bool fetchMangaGraphQL(int mangaId, Manga& manga);
    bool fetchServerInfoGraphQL(ServerInfo& info);
    bool addMangaToLibraryGraphQL(int mangaId);
    bool removeMangaFromLibraryGraphQL(int mangaId);
    bool markChapterReadGraphQL(int chapterId, bool read);
    bool updateChapterProgressGraphQL(int chapterId, int lastPageRead);
    bool fetchChapterPagesGraphQL(int chapterId, std::vector<Page>& pages);
    bool fetchReadingHistoryGraphQL(int offset, int limit, std::vector<ReadingHistoryItem>& history);
    bool globalSearchGraphQL(const std::string& query, std::vector<GlobalSearchResult>& results);
    bool setMangaCategoriesGraphQL(int mangaId, const std::vector<int>& categoryIds);
    bool fetchCategoryMangaGraphQL(int categoryId, std::vector<Manga>& manga);
    bool fetchCategoryMangaGraphQLFallback(int categoryId, std::vector<Manga>& manga);

    // Category Management GraphQL methods
    bool createCategoryGraphQL(const std::string& name);
    bool deleteCategoryGraphQL(int categoryId);
    bool updateCategoryGraphQL(int categoryId, const std::string& name, bool isDefault);
    bool updateCategoryOrderGraphQL(int categoryId, int newPosition);
    bool triggerCategoryUpdateGraphQL(int categoryId);
    bool triggerLibraryUpdateGraphQL();

    // Manga Meta GraphQL methods
    bool fetchMangaMetaGraphQL(int mangaId, std::map<std::string, std::string>& meta);
    bool setMangaMetaGraphQL(int mangaId, const std::string& key, const std::string& value);
    bool deleteMangaMetaGraphQL(int mangaId, const std::string& key);

    // Extension GraphQL methods
    bool fetchExtensionListGraphQL(std::vector<Extension>& extensions);
    bool fetchInstalledExtensionsGraphQL(std::vector<Extension>& extensions);  // Server-side filtered
    bool fetchUninstalledExtensionsGraphQL(std::vector<Extension>& extensions, const std::set<std::string>& languages);  // Server-side filtered
    bool installExtensionGraphQL(const std::string& pkgName);
    bool updateExtensionGraphQL(const std::string& pkgName);
    bool uninstallExtensionGraphQL(const std::string& pkgName);

    // Download GraphQL methods
    bool enqueueChapterDownloadGraphQL(int chapterId);
    bool dequeueChapterDownloadGraphQL(int chapterId);
    bool enqueueChapterDownloadsGraphQL(const std::vector<int>& chapterIds);
    bool dequeueChapterDownloadsGraphQL(const std::vector<int>& chapterIds);
    bool reorderChapterDownloadGraphQL(int chapterId, int newPosition);
    bool startDownloadsGraphQL();
    bool stopDownloadsGraphQL();
    bool clearDownloadQueueGraphQL();

    // Single source GraphQL
    bool fetchSourceGraphQL(int64_t sourceId, Source& source);

    // Tracking GraphQL methods
    bool fetchTrackersGraphQL(std::vector<Tracker>& trackers);
    bool fetchTrackerGraphQL(int trackerId, Tracker& tracker);
    bool fetchMangaTrackingGraphQL(int mangaId, std::vector<TrackRecord>& records);
    bool searchTrackerGraphQL(int trackerId, const std::string& query, std::vector<TrackSearchResult>& results);
    bool bindTrackerGraphQL(int mangaId, int trackerId, int64_t remoteId);
    bool unbindTrackerGraphQL(int recordId, bool deleteRemoteTrack);
    bool updateTrackRecordGraphQL(int recordId, int status, double lastChapterRead,
                                  const std::string& scoreString, int64_t startDate, int64_t finishDate);
    bool loginTrackerCredentialsGraphQL(int trackerId, const std::string& username, const std::string& password);
    bool logoutTrackerGraphQL(int trackerId);

    // Source Preferences GraphQL
    bool fetchSourcePreferencesGraphQL(int64_t sourceId, std::vector<SourcePreference>& preferences);
    bool updateSourcePreferenceGraphQL(int64_t sourceId, const SourcePreferenceChange& change);
    bool setSourceMetaGraphQL(int64_t sourceId, const std::string& key, const std::string& value);
    bool deleteSourceMetaGraphQL(int64_t sourceId, const std::string& key);
    bool fetchSourcesForExtensionGraphQL(const std::string& pkgName, std::vector<Source>& sources);

    // Parse source preference from GraphQL
    SourcePreference parseSourcePreferenceFromGraphQL(const std::string& json);

    // Parse extension from GraphQL
    Extension parseExtensionFromGraphQL(const std::string& json);

    // Parse GraphQL response data
    Manga parseMangaFromGraphQL(const std::string& json);
    Chapter parseChapterFromGraphQL(const std::string& json);
    Source parseSourceFromGraphQL(const std::string& json);
    Category parseCategoryFromGraphQL(const std::string& json);

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
    Tracker parseTrackerFromGraphQL(const std::string& json);
    TrackRecord parseTrackRecord(const std::string& json);
    TrackRecord parseTrackRecordFromGraphQL(const std::string& json);
    TrackSearchResult parseTrackSearchResultFromGraphQL(const std::string& json);

    // Split array helper
    std::vector<std::string> splitJsonArray(const std::string& arrayJson);

    std::string m_serverUrl;
    std::string m_authUsername;
    std::string m_authPassword;
    bool m_isConnected = false;
    ServerInfo m_serverInfo;

    // Authentication state
    AuthMode m_authMode = AuthMode::NONE;
    std::string m_accessToken;       // JWT access token (ui_login)
    std::string m_refreshToken;      // JWT refresh token (ui_login)
    std::string m_sessionCookie;     // Session cookie (simple_login)

    // Login GraphQL methods
    bool loginGraphQL(const std::string& username, const std::string& password);
    bool refreshTokenGraphQL();
};

} // namespace vitasuwayomi
