/**
 * VitaSuwayomi - Suwayomi Client for PlayStation Vita
 * Borealis-based Application
 */

#pragma once

#include <string>
#include <functional>
#include <mutex>
#include <set>

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

    // Library Settings
    bool updateOnStart = false;
    bool updateOnlyWifi = true;
    int defaultCategoryId = 0;
    std::set<int> hiddenCategoryIds;  // Categories hidden from library view

    // Download Settings
    bool downloadToServer = true;      // Download on server side vs local
    bool autoDownloadChapters = false;
    bool downloadOverWifiOnly = true;
    int maxConcurrentDownloads = 2;
    bool deleteAfterRead = false;

    // Network Settings
    int connectionTimeout = 30;        // seconds

    // Display Settings
    bool showUnreadBadge = true;
    bool showDownloadedBadge = true;
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
