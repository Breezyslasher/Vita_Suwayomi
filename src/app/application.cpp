/**
 * VitaSuwayomi - Application implementation
 */

#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "activity/login_activity.hpp"
#include "activity/main_activity.hpp"
#include "activity/player_activity.hpp"

#include <borealis.hpp>
#include <fstream>
#include <cstring>
#include <cmath>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

namespace vitasuwayomi {

static const char* SETTINGS_PATH = "ux0:data/VitaSuwayomi/settings.json";

Application& Application::getInstance() {
    static Application instance;
    return instance;
}

bool Application::init() {
    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    brls::Logger::info("VitaSuwayomi {} initializing...", VITA_ABS_VERSION);

#ifdef __vita__
    // Create data directory
    int ret = sceIoMkdir("ux0:data/VitaSuwayomi", 0777);
    brls::Logger::debug("sceIoMkdir result: {:#x}", ret);
#endif

    // Load saved settings
    brls::Logger::info("Loading saved settings...");
    bool loaded = loadSettings();
    brls::Logger::info("Settings load result: {}", loaded ? "success" : "failed/not found");

    // Apply settings
    applyTheme();
    applyLogLevel();

    m_initialized = true;
    return true;
}

void Application::run() {
    brls::Logger::info("Application::run - isLoggedIn={}, serverUrl={}",
                       isLoggedIn(), m_serverUrl.empty() ? "(empty)" : m_serverUrl);

    // Initialize downloads manager to check for offline content
    DownloadsManager::getInstance().init();

    // Check if we have saved login credentials
    if (isLoggedIn() && !m_serverUrl.empty()) {
        brls::Logger::info("Restoring saved session...");
        // Verify connection and go to main
        AudiobookshelfClient::getInstance().setAuthToken(m_authToken);
        AudiobookshelfClient::getInstance().setServerUrl(m_serverUrl);

        // Validate token before proceeding
        if (AudiobookshelfClient::getInstance().validateToken()) {
            brls::Logger::info("Restored session, token valid");
            pushMainActivity();
        } else {
            // Token validation failed - could be offline
            // Check if we have downloads, if so go to main activity (offline mode)
            auto downloads = DownloadsManager::getInstance().getDownloads();
            if (!downloads.empty()) {
                brls::Logger::info("Offline with {} downloads, going to main activity", downloads.size());
                pushMainActivity();
            } else {
                brls::Logger::error("Saved token invalid and no downloads, showing login");
                pushLoginActivity();
            }
        }
    } else {
        // No saved session - check if we have downloads for offline mode
        auto downloads = DownloadsManager::getInstance().getDownloads();
        if (!downloads.empty()) {
            brls::Logger::info("No session but {} downloads exist, going to main activity", downloads.size());
            pushMainActivity();
        } else {
            brls::Logger::info("No saved session, showing login screen");
            pushLoginActivity();
        }
    }

    // Main loop handled by Borealis
    while (brls::Application::mainLoop()) {
        // Application keeps running
    }
}

void Application::shutdown() {
    saveSettings();
    m_initialized = false;
    brls::Logger::info("VitaSuwayomi shutting down");
}

void Application::pushLoginActivity() {
    brls::Application::pushActivity(new LoginActivity());
}

void Application::pushMainActivity() {
    brls::Application::pushActivity(new MainActivity());
}

void Application::pushPlayerActivity(const std::string& itemId, const std::string& episodeId,
                                      float startTime) {
    brls::Application::pushActivity(new PlayerActivity(itemId, episodeId, startTime));
}

void Application::pushPlayerActivityWithFile(const std::string& itemId, const std::string& episodeId,
                                              const std::string& preDownloadedPath, float startTime) {
    brls::Application::pushActivity(new PlayerActivity(itemId, episodeId, preDownloadedPath, startTime));
}

void Application::applyTheme() {
    brls::ThemeVariant variant;

    switch (m_settings.theme) {
        case AppTheme::LIGHT:
            variant = brls::ThemeVariant::LIGHT;
            break;
        case AppTheme::DARK:
            variant = brls::ThemeVariant::DARK;
            break;
        case AppTheme::SYSTEM:
        default:
            // Default to dark for Vita
            variant = brls::ThemeVariant::DARK;
            break;
    }

    brls::Application::getPlatform()->setThemeVariant(variant);
    brls::Logger::info("Applied theme: {}", getThemeString(m_settings.theme));
}

void Application::applyLogLevel() {
    if (m_settings.debugLogging) {
        brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        brls::Logger::info("Debug logging enabled");
    } else {
        brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
        brls::Logger::info("Debug logging disabled");
    }
}

std::string Application::getAudioQualityString(AudioQuality quality) {
    switch (quality) {
        case AudioQuality::ORIGINAL: return "Original (Direct Play)";
        case AudioQuality::HIGH: return "High (320 kbps)";
        case AudioQuality::MEDIUM: return "Medium (192 kbps)";
        case AudioQuality::LOW: return "Low (128 kbps)";
        case AudioQuality::VERY_LOW: return "Very Low (64 kbps)";
        default: return "Unknown";
    }
}

std::string Application::getThemeString(AppTheme theme) {
    switch (theme) {
        case AppTheme::SYSTEM: return "System";
        case AppTheme::LIGHT: return "Light";
        case AppTheme::DARK: return "Dark";
        default: return "Unknown";
    }
}

std::string Application::getPlaybackSpeedString(PlaybackSpeed speed) {
    switch (speed) {
        case PlaybackSpeed::SPEED_0_5X: return "0.5x";
        case PlaybackSpeed::SPEED_0_75X: return "0.75x";
        case PlaybackSpeed::SPEED_1X: return "1x (Normal)";
        case PlaybackSpeed::SPEED_1_25X: return "1.25x";
        case PlaybackSpeed::SPEED_1_5X: return "1.5x";
        case PlaybackSpeed::SPEED_1_75X: return "1.75x";
        case PlaybackSpeed::SPEED_2X: return "2x";
        default: return "Unknown";
    }
}

std::string Application::getSleepTimerString(SleepTimer timer) {
    switch (timer) {
        case SleepTimer::OFF: return "Off";
        case SleepTimer::MINUTES_5: return "5 minutes";
        case SleepTimer::MINUTES_10: return "10 minutes";
        case SleepTimer::MINUTES_15: return "15 minutes";
        case SleepTimer::MINUTES_30: return "30 minutes";
        case SleepTimer::MINUTES_45: return "45 minutes";
        case SleepTimer::MINUTES_60: return "60 minutes";
        case SleepTimer::END_OF_CHAPTER: return "End of Chapter";
        default: return "Unknown";
    }
}

float Application::getPlaybackSpeedValue(PlaybackSpeed speed) {
    switch (speed) {
        case PlaybackSpeed::SPEED_0_5X: return 0.5f;
        case PlaybackSpeed::SPEED_0_75X: return 0.75f;
        case PlaybackSpeed::SPEED_1X: return 1.0f;
        case PlaybackSpeed::SPEED_1_25X: return 1.25f;
        case PlaybackSpeed::SPEED_1_5X: return 1.5f;
        case PlaybackSpeed::SPEED_1_75X: return 1.75f;
        case PlaybackSpeed::SPEED_2X: return 2.0f;
        default: return 1.0f;
    }
}

std::string Application::formatTime(float seconds) {
    if (seconds < 0) seconds = 0;

    int totalSeconds = (int)seconds;
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int secs = totalSeconds % 60;

    char buffer[32];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, secs);
    } else {
        snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, secs);
    }
    return buffer;
}

std::string Application::formatDuration(float seconds) {
    if (seconds < 0) seconds = 0;

    int totalSeconds = (int)seconds;
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;

    char buffer[64];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%dh %dm", hours, minutes);
    } else {
        snprintf(buffer, sizeof(buffer), "%d min", minutes);
    }
    return buffer;
}

bool Application::loadSettings() {
#ifdef __vita__
    brls::Logger::debug("loadSettings: Opening {}", SETTINGS_PATH);

    SceUID fd = sceIoOpen(SETTINGS_PATH, SCE_O_RDONLY, 0);
    if (fd < 0) {
        brls::Logger::debug("No settings file found (error: {:#x})", fd);
        return false;
    }

    // Get file size
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    brls::Logger::debug("loadSettings: File size = {}", size);

    if (size <= 0 || size > 16384) {
        brls::Logger::error("loadSettings: Invalid file size");
        sceIoClose(fd);
        return false;
    }

    std::string content;
    content.resize(size);
    sceIoRead(fd, &content[0], size);
    sceIoClose(fd);

    brls::Logger::debug("loadSettings: Read {} bytes", content.length());

    // Simple JSON parsing for strings
    auto extractString = [&content](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return "";
        pos += search.length();
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        if (pos >= content.length() || content[pos] != '"') return "";
        pos++;
        size_t end = content.find("\"", pos);
        if (end == std::string::npos) return "";
        return content.substr(pos, end - pos);
    };

    // Parse integers
    auto extractInt = [&content](const std::string& key) -> int {
        std::string search = "\"" + key + "\":";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return 0;
        pos += search.length();
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        size_t end = content.find_first_of(",}\n", pos);
        if (end == std::string::npos) return 0;
        return atoi(content.substr(pos, end - pos).c_str());
    };

    // Parse booleans
    auto extractBool = [&content](const std::string& key, bool defaultVal = false) -> bool {
        std::string search = "\"" + key + "\":";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return defaultVal;
        pos += search.length();
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        return (content.substr(pos, 4) == "true");
    };

    // Load authentication
    m_authToken = extractString("authToken");
    m_serverUrl = extractString("serverUrl");
    m_username = extractString("username");
    m_currentLibraryId = extractString("currentLibraryId");

    brls::Logger::info("loadSettings: authToken={}, serverUrl={}, username={}",
                       m_authToken.empty() ? "(empty)" : "(set)",
                       m_serverUrl.empty() ? "(empty)" : m_serverUrl,
                       m_username.empty() ? "(empty)" : m_username);

    // Load UI settings
    m_settings.theme = static_cast<AppTheme>(extractInt("theme"));
    m_settings.showClock = extractBool("showClock", true);
    m_settings.animationsEnabled = extractBool("animationsEnabled", true);
    m_settings.debugLogging = extractBool("debugLogging", true);

    // Load content display settings
    m_settings.showCollections = extractBool("showCollections", true);
    m_settings.showSeries = extractBool("showSeries", true);
    m_settings.showAuthors = extractBool("showAuthors", true);
    m_settings.showProgress = extractBool("showProgress", true);
    m_settings.showOnlyDownloaded = extractBool("showOnlyDownloaded", false);

    // Load playback settings
    m_settings.autoPlayNext = extractBool("autoPlayNext", false);
    m_settings.resumePlayback = extractBool("resumePlayback", true);
    m_settings.playbackSpeed = static_cast<PlaybackSpeed>(extractInt("playbackSpeed"));
    m_settings.sleepTimer = static_cast<SleepTimer>(extractInt("sleepTimer"));
    m_settings.seekInterval = extractInt("seekInterval");
    if (m_settings.seekInterval <= 0) m_settings.seekInterval = 30;
    m_settings.longSeekInterval = extractInt("longSeekInterval");
    if (m_settings.longSeekInterval <= 0) m_settings.longSeekInterval = 300;

    // Load podcast settings
    m_settings.podcastAutoComplete = static_cast<AutoCompleteThreshold>(extractInt("podcastAutoComplete"));

    // Load audio settings
    m_settings.audioQuality = static_cast<AudioQuality>(extractInt("audioQuality"));
    m_settings.boostVolume = extractBool("boostVolume", false);
    m_settings.volumeBoostDb = extractInt("volumeBoostDb");

    // Load chapter settings
    m_settings.showChapterList = extractBool("showChapterList", true);
    m_settings.skipChapterTransitions = extractBool("skipChapterTransitions", false);

    // Load bookmark settings
    m_settings.autoBookmark = extractBool("autoBookmark", true);

    // Load network settings
    m_settings.connectionTimeout = extractInt("connectionTimeout");
    if (m_settings.connectionTimeout <= 0) m_settings.connectionTimeout = 180;
    m_settings.downloadOverWifiOnly = extractBool("downloadOverWifiOnly", false);

    // Load download settings
    m_settings.autoStartDownloads = extractBool("autoStartDownloads", true);
    m_settings.maxConcurrentDownloads = extractInt("maxConcurrentDownloads");
    if (m_settings.maxConcurrentDownloads <= 0) m_settings.maxConcurrentDownloads = 1;
    m_settings.deleteAfterFinish = extractBool("deleteAfterFinish", false);
    m_settings.syncProgressOnConnect = extractBool("syncProgressOnConnect", true);

    // Load streaming/temp file settings
    m_settings.saveToDownloads = extractBool("saveToDownloads", false);
    m_settings.maxTempFiles = extractInt("maxTempFiles");
    if (m_settings.maxTempFiles <= 0) m_settings.maxTempFiles = 5;
    m_settings.maxTempSizeMB = extractInt("maxTempSizeMB");
    if (m_settings.maxTempSizeMB <= 0) m_settings.maxTempSizeMB = 500;

    // Load player UI settings
    m_settings.showDownloadProgress = extractBool("showDownloadProgress", true);

    // Load sleep/power settings
    m_settings.preventSleep = extractBool("preventSleep", true);
    m_settings.pauseOnHeadphoneDisconnect = extractBool("pauseOnHeadphoneDisconnect", true);

    brls::Logger::info("Settings loaded successfully");
    return !m_authToken.empty();
#else
    return false;
#endif
}

bool Application::saveSettings() {
#ifdef __vita__
    brls::Logger::info("saveSettings: Saving to {}", SETTINGS_PATH);
    brls::Logger::debug("saveSettings: authToken={}, serverUrl={}, username={}",
                        m_authToken.empty() ? "(empty)" : "(set)",
                        m_serverUrl.empty() ? "(empty)" : m_serverUrl,
                        m_username.empty() ? "(empty)" : m_username);

    // Create JSON content
    std::string json = "{\n";

    // Authentication
    json += "  \"authToken\": \"" + m_authToken + "\",\n";
    json += "  \"serverUrl\": \"" + m_serverUrl + "\",\n";
    json += "  \"username\": \"" + m_username + "\",\n";
    json += "  \"currentLibraryId\": \"" + m_currentLibraryId + "\",\n";

    // UI settings
    json += "  \"theme\": " + std::to_string(static_cast<int>(m_settings.theme)) + ",\n";
    json += "  \"showClock\": " + std::string(m_settings.showClock ? "true" : "false") + ",\n";
    json += "  \"animationsEnabled\": " + std::string(m_settings.animationsEnabled ? "true" : "false") + ",\n";
    json += "  \"debugLogging\": " + std::string(m_settings.debugLogging ? "true" : "false") + ",\n";

    // Content display settings
    json += "  \"showCollections\": " + std::string(m_settings.showCollections ? "true" : "false") + ",\n";
    json += "  \"showSeries\": " + std::string(m_settings.showSeries ? "true" : "false") + ",\n";
    json += "  \"showAuthors\": " + std::string(m_settings.showAuthors ? "true" : "false") + ",\n";
    json += "  \"showProgress\": " + std::string(m_settings.showProgress ? "true" : "false") + ",\n";
    json += "  \"showOnlyDownloaded\": " + std::string(m_settings.showOnlyDownloaded ? "true" : "false") + ",\n";

    // Playback settings
    json += "  \"autoPlayNext\": " + std::string(m_settings.autoPlayNext ? "true" : "false") + ",\n";
    json += "  \"resumePlayback\": " + std::string(m_settings.resumePlayback ? "true" : "false") + ",\n";
    json += "  \"playbackSpeed\": " + std::to_string(static_cast<int>(m_settings.playbackSpeed)) + ",\n";
    json += "  \"sleepTimer\": " + std::to_string(static_cast<int>(m_settings.sleepTimer)) + ",\n";
    json += "  \"seekInterval\": " + std::to_string(m_settings.seekInterval) + ",\n";
    json += "  \"longSeekInterval\": " + std::to_string(m_settings.longSeekInterval) + ",\n";

    // Podcast settings
    json += "  \"podcastAutoComplete\": " + std::to_string(static_cast<int>(m_settings.podcastAutoComplete)) + ",\n";

    // Audio settings
    json += "  \"audioQuality\": " + std::to_string(static_cast<int>(m_settings.audioQuality)) + ",\n";
    json += "  \"boostVolume\": " + std::string(m_settings.boostVolume ? "true" : "false") + ",\n";
    json += "  \"volumeBoostDb\": " + std::to_string(m_settings.volumeBoostDb) + ",\n";

    // Chapter settings
    json += "  \"showChapterList\": " + std::string(m_settings.showChapterList ? "true" : "false") + ",\n";
    json += "  \"skipChapterTransitions\": " + std::string(m_settings.skipChapterTransitions ? "true" : "false") + ",\n";

    // Bookmark settings
    json += "  \"autoBookmark\": " + std::string(m_settings.autoBookmark ? "true" : "false") + ",\n";

    // Network settings
    json += "  \"connectionTimeout\": " + std::to_string(m_settings.connectionTimeout) + ",\n";
    json += "  \"downloadOverWifiOnly\": " + std::string(m_settings.downloadOverWifiOnly ? "true" : "false") + ",\n";

    // Download settings
    json += "  \"autoStartDownloads\": " + std::string(m_settings.autoStartDownloads ? "true" : "false") + ",\n";
    json += "  \"maxConcurrentDownloads\": " + std::to_string(m_settings.maxConcurrentDownloads) + ",\n";
    json += "  \"deleteAfterFinish\": " + std::string(m_settings.deleteAfterFinish ? "true" : "false") + ",\n";
    json += "  \"syncProgressOnConnect\": " + std::string(m_settings.syncProgressOnConnect ? "true" : "false") + ",\n";

    // Streaming/temp file settings
    json += "  \"saveToDownloads\": " + std::string(m_settings.saveToDownloads ? "true" : "false") + ",\n";
    json += "  \"maxTempFiles\": " + std::to_string(m_settings.maxTempFiles) + ",\n";
    json += "  \"maxTempSizeMB\": " + std::to_string(m_settings.maxTempSizeMB) + ",\n";

    // Player UI settings
    json += "  \"showDownloadProgress\": " + std::string(m_settings.showDownloadProgress ? "true" : "false") + ",\n";

    // Sleep/power settings
    json += "  \"preventSleep\": " + std::string(m_settings.preventSleep ? "true" : "false") + ",\n";
    json += "  \"pauseOnHeadphoneDisconnect\": " + std::string(m_settings.pauseOnHeadphoneDisconnect ? "true" : "false") + "\n";

    json += "}\n";

    SceUID fd = sceIoOpen(SETTINGS_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) {
        brls::Logger::error("Failed to open settings file for writing: {:#x}", fd);
        return false;
    }

    int written = sceIoWrite(fd, json.c_str(), json.length());
    sceIoClose(fd);

    if (written == (int)json.length()) {
        brls::Logger::info("Settings saved successfully ({} bytes)", written);
        return true;
    } else {
        brls::Logger::error("Failed to write settings: only {} of {} bytes written", written, json.length());
        return false;
    }
#else
    return false;
#endif
}

void Application::setBackgroundDownloadProgress(const BackgroundDownloadProgress& progress) {
    std::lock_guard<std::mutex> lock(m_bgDownloadMutex);
    m_bgDownloadProgress = progress;
}

BackgroundDownloadProgress Application::getBackgroundDownloadProgress() const {
    std::lock_guard<std::mutex> lock(m_bgDownloadMutex);
    return m_bgDownloadProgress;
}

void Application::clearBackgroundDownloadProgress() {
    std::lock_guard<std::mutex> lock(m_bgDownloadMutex);
    m_bgDownloadProgress = BackgroundDownloadProgress();
}

} // namespace vitasuwayomi
