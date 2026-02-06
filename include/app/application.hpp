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
    DARK = 2
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

// Reader color filter modes
enum class ColorFilterMode {
    NONE = 0,
    SEPIA = 1,
    NIGHT = 2,       // Dim/dark mode
    BLUE_LIGHT = 3   // Blue light filter (warm)
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

// Download mode options
enum class DownloadMode {
    SERVER_ONLY = 0,    // Download to server queue only
    LOCAL_ONLY = 1,     // Download to local device only
    BOTH = 2            // Download to both server and local
};

// Per-manga reader settings (overrides defaults when set)
struct MangaReaderSettings {
    ReadingMode readingMode = ReadingMode::RIGHT_TO_LEFT;
    PageScaleMode pageScaleMode = PageScaleMode::FIT_SCREEN;
    int imageRotation = 0;  // 0, 90, 180, or 270 degrees
    bool cropBorders = false;
    int webtoonSidePadding = 0;
    bool isWebtoonFormat = false;  // Treat as webtoon (vertical scroll, page splitting)
};

// Application settings structure
struct AppSettings {
    // UI Settings
    AppTheme theme = AppTheme::DARK;
    bool showClock = true;
    bool animationsEnabled = true;
    bool debugLogging = false;

    // Reader Settings
    ReadingMode readingMode = ReadingMode::RIGHT_TO_LEFT;
    PageScaleMode pageScaleMode = PageScaleMode::FIT_SCREEN;
    ReaderBackground readerBackground = ReaderBackground::BLACK;
    int imageRotation = 0;  // 0, 90, 180, or 270 degrees
    bool keepScreenOn = true;
    bool showPageNumber = true;
    bool tapToNavigate = true;

    // Webtoon Settings (also applies to vertical mode)
    bool cropBorders = false;           // Auto-crop white/black borders from pages
    bool webtoonDetection = true;       // Auto-detect webtoon format (aspect ratio based)
    int webtoonSidePadding = 0;         // Side padding percentage (0-20%)

    // Reader Color Filters
    ColorFilterMode colorFilter = ColorFilterMode::NONE;
    int brightness = 100;               // Brightness level (0-100%)
    int colorFilterIntensity = 50;      // Filter intensity (0-100%)

    // Auto-Chapter Advance
    bool autoChapterAdvance = false;    // Automatically advance to next chapter
    int autoAdvanceDelay = 3;           // Seconds to wait before advancing (0-10)
    bool showAdvanceCountdown = true;   // Show countdown before advancing

    // Library Settings
    bool updateOnStart = false;
    bool updateOnlyWifi = true;
    int defaultCategoryId = 0;
    std::set<int> hiddenCategoryIds;  // Categories hidden from library view
    bool cacheLibraryData = true;     // Cache manga info for faster loading
    bool cacheCoverImages = true;     // Cache cover images to disk
    int librarySortMode = 0;          // Library sort mode (0=A-Z, 1=Z-A, 2=Unread desc, 3=Unread asc, 4=Recently added)
    bool chapterSortDescending = true; // Chapter sort order (true=newest first)

    // Library Grid Customization
    LibraryDisplayMode libraryDisplayMode = LibraryDisplayMode::GRID_NORMAL;
    LibraryGridSize libraryGridSize = LibraryGridSize::MEDIUM;

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
    bool autoDownloadChapters = false;
    bool downloadOverWifiOnly = true;
    int maxConcurrentDownloads = 2;
    bool deleteAfterRead = false;
    bool autoResumeDownloads = true;  // Auto-resume queued downloads on app restart

    // Source/Browse Settings
    std::set<std::string> enabledSourceLanguages;  // Empty = all languages, otherwise filter by these (e.g. "en", "multi")
    bool showNsfwSources = false;

    // Network Settings
    int connectionTimeout = 30;        // seconds

    // Display Settings
    bool showUnreadBadge = true;
    bool showDownloadedBadge = true;

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

    // Application settings access
    AppSettings& getSettings() { return m_settings; }
    const AppSettings& getSettings() const { return m_settings; }

    // Apply theme
    void applyTheme();

    // Apply log level based on settings
    void applyLogLevel();

    // Get string for display
    static std::string getThemeString(AppTheme theme);
    static std::string getReadingModeString(ReadingMode mode);
    static std::string getPageScaleModeString(PageScaleMode mode);
    static std::string getDownloadModeString(DownloadMode mode);

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
    AppSettings m_settings;
};

} // namespace vitasuwayomi
