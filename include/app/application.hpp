/**
 * VitaSuwayomi - Suwayomi Client for PlayStation Vita
 * Borealis-based Application
 */

#pragma once

#include <string>
#include <functional>
#include <mutex>
#include <set>
#include <map>
#include <vector>
#include <cstdint>
#include <nanovg.h>

// Application version
#define VITA_SUWAYOMI_VERSION "1.0.0"
#define VITA_SUWAYOMI_VERSION_NUM 100

// Client identification
#define SUWAYOMI_CLIENT_ID "vita-suwayomi-client-001"
#define SUWAYOMI_CLIENT_NAME "VitaSuwayomi"
#define SUWAYOMI_CLIENT_VERSION VITA_SUWAYOMI_VERSION
#define SUWAYOMI_PLATFORM "PlayStation Vita"
#define SUWAYOMI_DEVICE "PS Vita"

namespace vitasuwayomi {

// Theme options
enum class AppTheme {
    SYSTEM = 0,  // Follow system setting
    LIGHT = 1,
    DARK = 2,
    NEON_VAPORWAVE = 3,
    TACHIYOMI = 4,
    CATPPUCCIN = 5,
    NORD = 6,
    TAKO = 7,
    MIDNIGHT_DUSK = 8,
    STRAWBERRY = 9,
    GREEN_APPLE = 10,
    LAVENDER = 11,
    MATRIX = 12,
    DOOM = 13,
    MOCHA = 14,
    SAPPHIRE = 15,
    CLOUDFLARE = 16,
    TEAL_TURQUOISE = 17,
    TIDAL_WAVE = 18,
    YOTSUBA = 19,
    YIN_YANG = 20,
    MONOCHROME = 21,
    COTTON_CANDY = 22,
};

// Reading mode options
enum class ReadingMode {
    LEFT_TO_RIGHT = 0,      // Western style (LTR)
    RIGHT_TO_LEFT = 1,      // Manga style (RTL)
    VERTICAL = 2,           // Vertical scrolling
    WEBTOON = 3             // Continuous vertical (webtoon)
};

// Page scale mode options
enum class PageScaleMode {
    FIT_SCREEN = 0,         // Fit entire page on screen
    FIT_WIDTH = 1,          // Fit width, scroll vertically
    FIT_HEIGHT = 2,         // Fit height, scroll horizontally
    ORIGINAL = 3            // Original size (1:1)
};

// Reader background color
enum class ReaderBackground {
    BLACK = 0,
    WHITE = 1,
    GRAY = 2
};

// Library display mode
enum class LibraryDisplayMode {
    GRID_NORMAL = 0,   // Standard grid with covers and titles
    GRID_COMPACT = 1,  // Compact grid (covers only)
    LIST = 2           // List view with details
};

// Library grid size
enum class LibraryGridSize {
    SMALL = 0,    // 4 columns (larger covers)
    MEDIUM = 1,   // 6 columns (default)
    LARGE = 2     // 8 columns (more manga visible)
};

// List view row size
enum class ListRowSize {
    SMALL = 0,    // Compact rows (60px)
    MEDIUM = 1,   // Standard rows (80px, default)
    LARGE = 2,    // Large rows (100px)
    AUTO = 3      // Auto-size to fit title
};

// Library grouping mode
enum class LibraryGroupMode {
    BY_CATEGORY = 0,   // Group by category (default, with tabs)
    BY_SOURCE = 1,     // Group by manga source
    NO_GROUPING = 2    // Show all manga in one grid
};

// Download mode options
enum class DownloadMode {
    SERVER_ONLY = 0,    // Download to server queue only
    LOCAL_ONLY = 1,     // Download to local device only
    BOTH = 2            // Download to both server and local
};

// Download quality options (controls local download image quality)
enum class DownloadQuality {
    ORIGINAL = 0,       // Keep original quality from server
    HIGH = 1,           // Resize to max 1280px width, JPEG quality 90
    MEDIUM = 2,         // Resize to max 960px width, JPEG quality 80
    LOW = 3             // Resize to max 720px width, JPEG quality 70
};

// Per-manga reader settings (overrides defaults when set)
struct MangaReaderSettings {
    ReadingMode readingMode = ReadingMode::RIGHT_TO_LEFT;
    PageScaleMode pageScaleMode = PageScaleMode::FIT_SCREEN;
    int imageRotation = 0;  // 0, 90, 180, or 270 degrees
    bool cropBorders = false;
    int webtoonSidePadding = 0;
    bool isWebtoonFormat = false;  // Treat as webtoon (vertical scroll)
    ReaderBackground readerBackground = ReaderBackground::BLACK;  // Per-manga background color
};

// Application settings structure
struct AppSettings {
    // UI Settings
    AppTheme theme = AppTheme::DARK;
    bool showClock = true;
    bool debugLogging = false;

    // Reader Settings
    ReadingMode readingMode = ReadingMode::RIGHT_TO_LEFT;
    PageScaleMode pageScaleMode = PageScaleMode::FIT_SCREEN;
    ReaderBackground readerBackground = ReaderBackground::BLACK;
    int imageRotation = 0;  // 0, 90, 180, or 270 degrees
    bool keepScreenOn = true;
    bool showPageNumber = true;
    bool tapToNavigate = true;
    bool goToEndOnPrevChapter = true;  // When going back to prev chapter, land on last page instead of first

    // Webtoon Settings (also applies to vertical mode)
    bool cropBorders = false;           // Auto-crop white/black borders from pages
    bool webtoonDetection = true;       // Auto-detect webtoon format (aspect ratio based)
    int webtoonSidePadding = 0;         // Side padding percentage (0-20%)

    // Library Settings
    bool updateOnStart = false;
    bool updateOnlyWifi = true;
    int defaultCategoryId = 0;
    std::set<int> hiddenCategoryIds;  // Categories hidden from library view
    bool cacheLibraryData = true;     // Cache manga info for faster loading
    bool cacheCoverImages = true;     // Cache cover images to disk
    bool downloadsOnlyMode = false;   // Show only locally downloaded manga/chapters (offline mode)
    int librarySortMode = 0;          // Library sort mode (0-10, see LibrarySortMode enum)
    bool chapterSortDescending = true; // Chapter sort order (true=newest first)

    // Library Grid Customization
    LibraryDisplayMode libraryDisplayMode = LibraryDisplayMode::GRID_NORMAL;
    LibraryGridSize libraryGridSize = LibraryGridSize::MEDIUM;
    ListRowSize listRowSize = ListRowSize::MEDIUM;  // List view row size
    LibraryGroupMode libraryGroupMode = LibraryGroupMode::BY_CATEGORY;  // Library grouping mode

    // Library Sort Settings
    int defaultLibrarySortMode = 0;  // Default sort mode for new categories (0=A-Z by default)
    std::map<int, int> categorySortModes;  // Per-category sort modes (categoryId -> sortMode, -1 means use default)

    // Search History
    std::vector<std::string> searchHistory;  // Recent search queries
    int maxSearchHistory = 20;               // Max number of searches to remember

    // Reading Statistics
    int totalChaptersRead = 0;        // Total chapters read
    int totalMangaCompleted = 0;      // Total manga completed
    int currentStreak = 0;            // Current reading streak (days)
    int longestStreak = 0;            // Longest reading streak
    int64_t lastReadDate = 0;         // Last reading date (for streak calculation)
    int64_t totalReadingTime = 0;     // Total reading time in seconds (estimated)

    // Download Settings
    DownloadMode downloadMode = DownloadMode::SERVER_ONLY;  // Where to download chapters
    DownloadQuality downloadQuality = DownloadQuality::ORIGINAL;  // Image quality for local downloads
    bool autoDownloadChapters = false;
    bool deleteAfterRead = false;
    bool autoResumeDownloads = true;  // Auto-resume queued downloads on app restart
    bool pageCacheEnabled = true;     // Cache decoded TGA pages to disk for instant loading

    // Source/Browse Settings
    std::set<std::string> enabledSourceLanguages;  // Empty = all languages, otherwise filter by these (e.g. "en", "multi")
    bool showNsfwSources = false;

    // Network Settings
    std::string localServerUrl;        // Local network URL (e.g., http://192.168.1.100:4567)
    std::string remoteServerUrl;       // Remote/external URL (e.g., https://myserver.com:4567)
    bool useRemoteUrl = false;         // true = use remote URL, false = use local URL
    bool autoSwitchOnFailure = false;  // Auto-switch to alternate URL if connection fails
    int connectionTimeout = 30;        // seconds

    // Authentication Settings
    // authMode: 0=none, 1=basic_auth, 2=simple_login, 3=ui_login
    int authMode = 0;
    std::string accessToken;           // JWT access token (for ui_login/simple_login)
    std::string refreshToken;          // JWT refresh token (for ui_login/simple_login)
    std::string sessionCookie;         // Session cookie (for simple_login)

    // Display Settings
    bool showUnreadBadge = true;

    // Server image settings (applied once on first connection)
    bool serverImageSettingsApplied = false;

    // Per-manga reader settings (keyed by manga ID)
    // If a manga has custom settings, they override the defaults above
    std::map<int, MangaReaderSettings> mangaReaderSettings;
};

/**
 * Application singleton - manages app lifecycle and global state
 */
class Application {
public:
    static Application& getInstance();

    // Initialize and run the application
    bool init();
    void run();
    void shutdown();

    // Navigation
    void pushLoginActivity();
    void pushMainActivity();
    void pushReaderActivity(int mangaId, int chapterIndex, const std::string& mangaTitle);
    void pushReaderActivityAtPage(int mangaId, int chapterIndex, int startPage,
                                   const std::string& mangaTitle);
    void pushMangaDetailView(int mangaId);

    // Connection state
    bool isConnected() const { return !m_serverUrl.empty() && m_isConnected; }
    const std::string& getServerUrl() const { return m_serverUrl; }
    void setServerUrl(const std::string& url) { m_serverUrl = url; }
    void setConnected(bool connected) { m_isConnected = connected; }

    // Local/Remote URL switching
    std::string getActiveServerUrl() const;  // Returns local or remote URL based on setting
    std::string getAlternateServerUrl() const;  // Returns the URL not currently in use
    void switchToLocalUrl();
    void switchToRemoteUrl();
    bool hasLocalUrl() const { return !m_settings.localServerUrl.empty(); }
    bool hasRemoteUrl() const { return !m_settings.remoteServerUrl.empty(); }
    bool hasBothUrls() const { return hasLocalUrl() && hasRemoteUrl(); }
    bool tryAlternateUrl();  // Try alternate URL on failure (returns true if switched successfully)

    // Auth credentials
    const std::string& getAuthUsername() const { return m_authUsername; }
    const std::string& getAuthPassword() const { return m_authPassword; }
    void setAuthCredentials(const std::string& username, const std::string& password) {
        m_authUsername = username;
        m_authPassword = password;
    }

    // Settings persistence
    bool loadSettings();
    bool saveSettings();

    // Current category (for context)
    int getCurrentCategoryId() const { return m_currentCategoryId; }
    void setCurrentCategoryId(int id) { m_currentCategoryId = id; }

    // Track library additions for immediate UI update
    void trackLibraryAddition(int mangaId) { m_recentLibraryAdditions.insert(mangaId); }
    bool isRecentlyAdded(int mangaId) const { return m_recentLibraryAdditions.count(mangaId) > 0; }
    void clearRecentAdditions() { m_recentLibraryAdditions.clear(); }

    // Application settings access
    AppSettings& getSettings() { return m_settings; }
    const AppSettings& getSettings() const { return m_settings; }

    // Apply theme
    void applyTheme();

    // Theme color accessors - returns colors based on current theme
    // Vaporwave theme overrides these with neon/Miami colors
    NVGcolor getAccentColor() const;        // Primary accent (buttons, highlights)
    NVGcolor getSecondaryColor() const;     // Secondary accent
    NVGcolor getHeaderTextColor() const;    // Section headers
    NVGcolor getSubtitleColor() const;      // Subtitle/detail text
    NVGcolor getHighlightColor() const;     // Focus highlight
    NVGcolor getSidebarColor() const;       // Sidebar background tint
    NVGcolor getCardBackground() const;     // Card/cell background
    NVGcolor getDialogBackground() const;   // Dialog/overlay background
    NVGcolor getTealColor() const;          // Teal accent (badges, status)
    NVGcolor getStatusColor() const;        // Status text color (blue accent)
    NVGcolor getDescriptionColor() const;   // Description/body text
    NVGcolor getDimTextColor() const;       // Dimmed text (120-128 gray)
    NVGcolor getRowBackground() const;      // List row background
    NVGcolor getActiveRowBackground() const; // Active/selected row bg
    NVGcolor getInactiveRowBackground() const; // Inactive row bg
    NVGcolor getTrackingButtonColor() const; // Tracking button background
    NVGcolor getCtaButtonColor() const;    // Call-to-action button (read, retry)
    NVGcolor getSectionHeaderBg() const;   // Section header background
    NVGcolor getReaderBackground() const;  // Reader/transition background
    NVGcolor getErrorOverlayBg() const;    // Error overlay background
    NVGcolor getFocusedRowBg() const;      // Focused/hovered row
    NVGcolor getDeepBackground() const;    // Very deep/dark background
    NVGcolor getSeparatorColor() const;    // Separator lines
    NVGcolor getSuccessTextColor() const;  // Success/visible status text
    NVGcolor getTextColor() const;         // Primary text color
    NVGcolor getButtonColor() const;       // Default button background
    bool isVaporwaveTheme() const { return m_settings.theme == AppTheme::NEON_VAPORWAVE; }

    // Apply log level based on settings
    void applyLogLevel();

    // Reading statistics management
    void updateReadingStatistics(bool chapterCompleted = false, bool mangaCompleted = false);
    void syncStatisticsFromServer();

    // Last reader session result - updated by reader on close, consumed by detail view
    struct ReaderResult {
        int mangaId = 0;
        int chapterId = 0;       // Chapter ID that was being read
        int lastPageRead = 0;    // Last page position
        bool markedRead = false;  // Whether chapter was marked as read
        int64_t timestamp = 0;   // When this was updated (milliseconds)
        std::vector<int> chaptersRead;  // All chapter IDs marked read during session
    };
    void setLastReaderResult(const ReaderResult& result) {
        m_lastReaderResult = result;
        // Notify registered listener (MangaDetailView) to update UI
        if (m_readerResultCallback) {
            m_readerResultCallback(result);
        }
    }
    const ReaderResult& getLastReaderResult() const { return m_lastReaderResult; }
    void clearLastReaderResult() { m_lastReaderResult = {}; }

    // Callback for when reader result is set (detail view registers to update UI)
    using ReaderResultCallback = std::function<void(const ReaderResult&)>;
    void setReaderResultCallback(ReaderResultCallback cb) { m_readerResultCallback = std::move(cb); }

    // Get string for display
    static std::string getThemeString(AppTheme theme);
    static std::string getReadingModeString(ReadingMode mode);
    static std::string getPageScaleModeString(PageScaleMode mode);
    static std::string getDownloadModeString(DownloadMode mode);
    static std::string getDownloadQualityString(DownloadQuality quality);

private:
    Application() = default;
    ~Application() = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool m_initialized = false;
    bool m_isConnected = false;
    std::string m_serverUrl;
    std::string m_authUsername;
    std::string m_authPassword;
    int m_currentCategoryId = 0;
    std::set<int> m_recentLibraryAdditions;  // Manga IDs added to library this session
    AppSettings m_settings;
    ReaderResult m_lastReaderResult;
    ReaderResultCallback m_readerResultCallback;
};

} // namespace vitasuwayomi
