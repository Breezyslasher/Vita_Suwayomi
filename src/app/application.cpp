/**
 * VitaSuwayomi - Application implementation
 */

#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "utils/library_cache.hpp"
#include "activity/login_activity.hpp"
#include "activity/main_activity.hpp"
#include "activity/reader_activity.hpp"

#include <borealis.hpp>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

namespace vitasuwayomi {

#ifdef __vita__
static const char* SETTINGS_PATH = "ux0:data/VitaSuwayomi/settings.json";
static const char* SETTINGS_DIR = "ux0:data/VitaSuwayomi";
#else
static const char* SETTINGS_PATH = "./VitaSuwayomi_settings.json";
#endif

Application& Application::getInstance() {
    static Application instance;
    return instance;
}

bool Application::init() {
    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    brls::Logger::info("VitaSuwayomi {} initializing...", VITA_SUWAYOMI_VERSION);

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

    // Initialize library cache
    LibraryCache::getInstance().init();

    m_initialized = true;
    return true;
}

void Application::run() {
    brls::Logger::info("Application::run - isConnected={}, serverUrl={}",
                       isConnected(), m_serverUrl.empty() ? "(empty)" : m_serverUrl);

    // Initialize downloads manager
    DownloadsManager::getInstance().init();

    // Check if we have saved server connection
    if (!m_serverUrl.empty()) {
        brls::Logger::info("Restoring saved connection...");
        SuwayomiClient& client = SuwayomiClient::getInstance();
        client.setServerUrl(m_serverUrl);

        // If we have JWT tokens, try to refresh them first (they may be expired)
        AuthMode authMode = client.getAuthMode();
        if ((authMode == AuthMode::SIMPLE_LOGIN || authMode == AuthMode::UI_LOGIN) &&
            !client.getRefreshToken().empty()) {
            brls::Logger::info("Attempting to refresh JWT tokens...");
            if (client.refreshToken()) {
                brls::Logger::info("Token refresh successful");
                // Update stored tokens
                m_settings.accessToken = client.getAccessToken();
                m_settings.refreshToken = client.getRefreshToken();
            } else {
                brls::Logger::warning("Token refresh failed, will try basic connection test");
            }
        }

        // Test connection before proceeding
        if (client.testConnection()) {
            brls::Logger::info("Connection restored successfully");
            m_isConnected = true;
            pushMainActivity();
        } else {
            // Connection failed - could be offline or auth expired
            // Check if we have downloads, if so go to main activity (offline mode)
            auto downloads = DownloadsManager::getInstance().getDownloads();
            if (!downloads.empty()) {
                brls::Logger::info("Offline with {} downloads, going to main activity", downloads.size());
                pushMainActivity();
            } else {
                brls::Logger::error("Connection failed and no downloads, showing login");
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

void Application::pushReaderActivity(int mangaId, int chapterIndex, const std::string& mangaTitle) {
    brls::Application::pushActivity(new ReaderActivity(mangaId, chapterIndex, mangaTitle));
}

void Application::pushReaderActivityAtPage(int mangaId, int chapterIndex, int startPage,
                                            const std::string& mangaTitle) {
    brls::Application::pushActivity(new ReaderActivity(mangaId, chapterIndex, startPage, mangaTitle));
}

void Application::pushMangaDetailView(int mangaId) {
    // TODO: Push manga detail view
    brls::Logger::info("Push manga detail view for manga {}", mangaId);
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

std::string Application::getThemeString(AppTheme theme) {
    switch (theme) {
        case AppTheme::SYSTEM: return "System";
        case AppTheme::LIGHT: return "Light";
        case AppTheme::DARK: return "Dark";
        default: return "Unknown";
    }
}

std::string Application::getReadingModeString(ReadingMode mode) {
    switch (mode) {
        case ReadingMode::LEFT_TO_RIGHT: return "Left to Right";
        case ReadingMode::RIGHT_TO_LEFT: return "Right to Left (Manga)";
        case ReadingMode::VERTICAL: return "Vertical";
        case ReadingMode::WEBTOON: return "Webtoon";
        default: return "Unknown";
    }
}

std::string Application::getPageScaleModeString(PageScaleMode mode) {
    switch (mode) {
        case PageScaleMode::FIT_SCREEN: return "Fit Screen";
        case PageScaleMode::FIT_WIDTH: return "Fit Width";
        case PageScaleMode::FIT_HEIGHT: return "Fit Height";
        case PageScaleMode::ORIGINAL: return "Original";
        default: return "Unknown";
    }
}

std::string Application::getDownloadModeString(DownloadMode mode) {
    switch (mode) {
        case DownloadMode::SERVER_ONLY: return "Server Only";
        case DownloadMode::LOCAL_ONLY: return "Local Only";
        case DownloadMode::BOTH: return "Both";
        default: return "Unknown";
    }
}

bool Application::loadSettings() {
    brls::Logger::debug("loadSettings: Opening {}", SETTINGS_PATH);

    std::string content;

#ifdef __vita__
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

    content.resize(size);
    sceIoRead(fd, &content[0], size);
    sceIoClose(fd);
#else
    // Non-Vita: use standard C++ file I/O
    std::ifstream file(SETTINGS_PATH);
    if (!file.is_open()) {
        brls::Logger::debug("No settings file found");
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    file.close();
#endif

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

    // Load connection info
    m_serverUrl = extractString("serverUrl");
    m_currentCategoryId = extractInt("currentCategoryId");

    brls::Logger::info("loadSettings: serverUrl={}",
                       m_serverUrl.empty() ? "(empty)" : m_serverUrl);

    // Load UI settings
    m_settings.theme = static_cast<AppTheme>(extractInt("theme"));
    m_settings.showClock = extractBool("showClock", true);
    m_settings.debugLogging = extractBool("debugLogging", false);

    // Load reader settings
    m_settings.readingMode = static_cast<ReadingMode>(extractInt("readingMode"));
    m_settings.pageScaleMode = static_cast<PageScaleMode>(extractInt("pageScaleMode"));
    m_settings.readerBackground = static_cast<ReaderBackground>(extractInt("readerBackground"));
    m_settings.imageRotation = extractInt("imageRotation");
    // Validate rotation (must be 0, 90, 180, or 270)
    if (m_settings.imageRotation != 0 && m_settings.imageRotation != 90 &&
        m_settings.imageRotation != 180 && m_settings.imageRotation != 270) {
        m_settings.imageRotation = 0;
    }
    m_settings.keepScreenOn = extractBool("keepScreenOn", true);
    m_settings.showPageNumber = extractBool("showPageNumber", true);
    m_settings.tapToNavigate = extractBool("tapToNavigate", true);

    // Load webtoon settings
    m_settings.cropBorders = extractBool("cropBorders", false);
    m_settings.webtoonDetection = extractBool("webtoonDetection", true);
    m_settings.webtoonSidePadding = extractInt("webtoonSidePadding");
    if (m_settings.webtoonSidePadding < 0 || m_settings.webtoonSidePadding > 20) {
        m_settings.webtoonSidePadding = 0;
    }

    // Load auto-chapter advance settings
    m_settings.autoChapterAdvance = extractBool("autoChapterAdvance", false);
    m_settings.autoAdvanceDelay = extractInt("autoAdvanceDelay");
    if (m_settings.autoAdvanceDelay < 0 || m_settings.autoAdvanceDelay > 10) {
        m_settings.autoAdvanceDelay = 3;
    }
    m_settings.showAdvanceCountdown = extractBool("showAdvanceCountdown", true);

    // Load library settings
    m_settings.updateOnStart = extractBool("updateOnStart", false);
    m_settings.updateOnlyWifi = extractBool("updateOnlyWifi", true);
    m_settings.defaultCategoryId = extractInt("defaultCategoryId");

    // Load hidden categories (comma-separated IDs)
    m_settings.hiddenCategoryIds.clear();
    std::string hiddenCatsStr = extractString("hiddenCategoryIds");
    if (!hiddenCatsStr.empty()) {
        std::stringstream ss(hiddenCatsStr);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (!token.empty()) {
                int catId = atoi(token.c_str());
                m_settings.hiddenCategoryIds.insert(catId);
            }
        }
        brls::Logger::debug("Loaded {} hidden categories", m_settings.hiddenCategoryIds.size());
    }

    // Load cache settings
    m_settings.cacheLibraryData = extractBool("cacheLibraryData", true);
    m_settings.cacheCoverImages = extractBool("cacheCoverImages", true);
    m_settings.librarySortMode = extractInt("librarySortMode");
    if (m_settings.librarySortMode < 0 || m_settings.librarySortMode > 10) m_settings.librarySortMode = 0;
    m_settings.chapterSortDescending = extractBool("chapterSortDescending", true);

    // Load download settings
    int downloadModeInt = extractInt("downloadMode");
    m_settings.downloadMode = static_cast<DownloadMode>(downloadModeInt);
    brls::Logger::info("loadSettings: downloadMode = {} (0=Server, 1=Local, 2=Both)", downloadModeInt);
    m_settings.autoDownloadChapters = extractBool("autoDownloadChapters", false);
    m_settings.downloadOverWifiOnly = extractBool("downloadOverWifiOnly", true);
    m_settings.maxConcurrentDownloads = extractInt("maxConcurrentDownloads");
    if (m_settings.maxConcurrentDownloads <= 0) m_settings.maxConcurrentDownloads = 2;
    m_settings.deleteAfterRead = extractBool("deleteAfterRead", false);
    m_settings.autoResumeDownloads = extractBool("autoResumeDownloads", true);

    // Load browse/source settings
    m_settings.showNsfwSources = extractBool("showNsfwSources", false);
    m_settings.enabledSourceLanguages.clear();
    size_t langArrayPos = content.find("\"enabledSourceLanguages\"");
    if (langArrayPos != std::string::npos) {
        size_t arrayStart = content.find('[', langArrayPos);
        size_t arrayEnd = content.find(']', arrayStart);
        if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
            std::string langArray = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
            // Parse language codes like "en", "multi", etc
            size_t pos = 0;
            while (pos < langArray.length()) {
                size_t quoteStart = langArray.find('"', pos);
                if (quoteStart == std::string::npos) break;
                size_t quoteEnd = langArray.find('"', quoteStart + 1);
                if (quoteEnd == std::string::npos) break;
                std::string lang = langArray.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                if (!lang.empty()) {
                    m_settings.enabledSourceLanguages.insert(lang);
                }
                pos = quoteEnd + 1;
            }
        }
    }
    brls::Logger::info("loadSettings: enabledSourceLanguages count = {}", m_settings.enabledSourceLanguages.size());

    // Load network settings
    m_settings.localServerUrl = extractString("localServerUrl");
    m_settings.remoteServerUrl = extractString("remoteServerUrl");
    m_settings.useRemoteUrl = extractBool("useRemoteUrl", false);
    m_settings.autoSwitchOnFailure = extractBool("autoSwitchOnFailure", false);
    m_settings.connectionTimeout = extractInt("connectionTimeout");
    if (m_settings.connectionTimeout <= 0) m_settings.connectionTimeout = 30;

    brls::Logger::info("loadSettings: localUrl={}, remoteUrl={}, useRemote={}, autoSwitch={}",
                       m_settings.localServerUrl.empty() ? "(empty)" : m_settings.localServerUrl,
                       m_settings.remoteServerUrl.empty() ? "(empty)" : m_settings.remoteServerUrl,
                       m_settings.useRemoteUrl ? "true" : "false",
                       m_settings.autoSwitchOnFailure ? "true" : "false");

    // Load display settings
    m_settings.showUnreadBadge = extractBool("showUnreadBadge", true);

    // Load library grid customization
    int displayModeInt = extractInt("libraryDisplayMode");
    if (displayModeInt >= 0 && displayModeInt <= 2) {
        m_settings.libraryDisplayMode = static_cast<LibraryDisplayMode>(displayModeInt);
    }
    int gridSizeInt = extractInt("libraryGridSize");
    if (gridSizeInt >= 0 && gridSizeInt <= 2) {
        m_settings.libraryGridSize = static_cast<LibraryGridSize>(gridSizeInt);
    }
    brls::Logger::info("loadSettings: libraryDisplayMode={}, libraryGridSize={}",
                       displayModeInt, gridSizeInt);

    // Load search history settings
    m_settings.maxSearchHistory = extractInt("maxSearchHistory");
    if (m_settings.maxSearchHistory <= 0 || m_settings.maxSearchHistory > 100) {
        m_settings.maxSearchHistory = 20;
    }
    m_settings.searchHistory.clear();
    size_t histArrayPos = content.find("\"searchHistory\"");
    if (histArrayPos != std::string::npos) {
        size_t arrayStart = content.find('[', histArrayPos);
        size_t arrayEnd = content.find(']', arrayStart);
        if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
            std::string histArray = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
            // Parse search queries
            size_t pos = 0;
            while (pos < histArray.length()) {
                size_t quoteStart = histArray.find('"', pos);
                if (quoteStart == std::string::npos) break;
                // Find end quote, accounting for escaped quotes
                size_t quoteEnd = quoteStart + 1;
                while (quoteEnd < histArray.length()) {
                    if (histArray[quoteEnd] == '"' && histArray[quoteEnd - 1] != '\\') break;
                    quoteEnd++;
                }
                if (quoteEnd >= histArray.length()) break;
                std::string query = histArray.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                // Unescape
                std::string unescaped;
                for (size_t i = 0; i < query.length(); i++) {
                    if (query[i] == '\\' && i + 1 < query.length()) {
                        if (query[i + 1] == '"' || query[i + 1] == '\\') {
                            unescaped += query[i + 1];
                            i++;
                            continue;
                        }
                    }
                    unescaped += query[i];
                }
                if (!unescaped.empty()) {
                    m_settings.searchHistory.push_back(unescaped);
                }
                pos = quoteEnd + 1;
            }
        }
    }
    brls::Logger::debug("Loaded {} search history entries", m_settings.searchHistory.size());

    // Load reading statistics
    m_settings.totalChaptersRead = extractInt("totalChaptersRead");
    m_settings.totalMangaCompleted = extractInt("totalMangaCompleted");
    m_settings.currentStreak = extractInt("currentStreak");
    m_settings.longestStreak = extractInt("longestStreak");

    // Extract int64 for timestamps - need special handling
    auto extractInt64 = [&content](const std::string& key) -> int64_t {
        std::string search = "\"" + key + "\":";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return 0;
        pos += search.length();
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        size_t end = content.find_first_of(",}\n", pos);
        if (end == std::string::npos) return 0;
        return std::stoll(content.substr(pos, end - pos));
    };

    m_settings.lastReadDate = extractInt64("lastReadDate");
    m_settings.totalReadingTime = extractInt64("totalReadingTime");

    // Load per-manga reader settings
    m_settings.mangaReaderSettings.clear();
    size_t mangaSettingsPos = content.find("\"mangaReaderSettings\"");
    if (mangaSettingsPos != std::string::npos) {
        size_t openBrace = content.find('{', mangaSettingsPos + 21);
        if (openBrace != std::string::npos) {
            // Find matching close brace
            int braceCount = 1;
            size_t pos = openBrace + 1;
            size_t closeBrace = std::string::npos;
            while (pos < content.length() && braceCount > 0) {
                if (content[pos] == '{') braceCount++;
                else if (content[pos] == '}') braceCount--;
                if (braceCount == 0) closeBrace = pos;
                pos++;
            }

            if (closeBrace != std::string::npos) {
                std::string mangaJson = content.substr(openBrace + 1, closeBrace - openBrace - 1);

                // Parse each manga entry: "123": {...}
                size_t searchPos = 0;
                while (searchPos < mangaJson.length()) {
                    size_t quoteStart = mangaJson.find('"', searchPos);
                    if (quoteStart == std::string::npos) break;

                    size_t quoteEnd = mangaJson.find('"', quoteStart + 1);
                    if (quoteEnd == std::string::npos) break;

                    std::string mangaIdStr = mangaJson.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                    int mangaId = atoi(mangaIdStr.c_str());

                    if (mangaId > 0) {
                        size_t entryBrace = mangaJson.find('{', quoteEnd);
                        size_t entryEnd = mangaJson.find('}', entryBrace);
                        if (entryBrace != std::string::npos && entryEnd != std::string::npos) {
                            std::string entryJson = mangaJson.substr(entryBrace, entryEnd - entryBrace + 1);

                            MangaReaderSettings settings;

                            // Extract readingMode
                            size_t rmPos = entryJson.find("\"readingMode\"");
                            if (rmPos != std::string::npos) {
                                size_t colonPos = entryJson.find(':', rmPos);
                                if (colonPos != std::string::npos) {
                                    int val = atoi(entryJson.c_str() + colonPos + 1);
                                    settings.readingMode = static_cast<ReadingMode>(val);
                                }
                            }

                            // Extract pageScaleMode
                            size_t psPos = entryJson.find("\"pageScaleMode\"");
                            if (psPos != std::string::npos) {
                                size_t colonPos = entryJson.find(':', psPos);
                                if (colonPos != std::string::npos) {
                                    int val = atoi(entryJson.c_str() + colonPos + 1);
                                    settings.pageScaleMode = static_cast<PageScaleMode>(val);
                                }
                            }

                            // Extract imageRotation
                            size_t irPos = entryJson.find("\"imageRotation\"");
                            if (irPos != std::string::npos) {
                                size_t colonPos = entryJson.find(':', irPos);
                                if (colonPos != std::string::npos) {
                                    settings.imageRotation = atoi(entryJson.c_str() + colonPos + 1);
                                }
                            }

                            // Extract cropBorders
                            size_t cbPos = entryJson.find("\"cropBorders\"");
                            if (cbPos != std::string::npos) {
                                size_t colonPos = entryJson.find(':', cbPos);
                                if (colonPos != std::string::npos) {
                                    size_t valueStart = colonPos + 1;
                                    while (valueStart < entryJson.length() &&
                                           (entryJson[valueStart] == ' ' || entryJson[valueStart] == '\t')) {
                                        valueStart++;
                                    }
                                    settings.cropBorders = (entryJson.substr(valueStart, 4) == "true");
                                }
                            }

                            // Extract webtoonSidePadding
                            size_t wspPos = entryJson.find("\"webtoonSidePadding\"");
                            if (wspPos != std::string::npos) {
                                size_t colonPos = entryJson.find(':', wspPos);
                                if (colonPos != std::string::npos) {
                                    settings.webtoonSidePadding = atoi(entryJson.c_str() + colonPos + 1);
                                    if (settings.webtoonSidePadding < 0 || settings.webtoonSidePadding > 20) {
                                        settings.webtoonSidePadding = 0;
                                    }
                                }
                            }

                            // Extract isWebtoonFormat
                            size_t iwfPos = entryJson.find("\"isWebtoonFormat\"");
                            if (iwfPos != std::string::npos) {
                                size_t colonPos = entryJson.find(':', iwfPos);
                                if (colonPos != std::string::npos) {
                                    size_t valueStart = colonPos + 1;
                                    while (valueStart < entryJson.length() &&
                                           (entryJson[valueStart] == ' ' || entryJson[valueStart] == '\t')) {
                                        valueStart++;
                                    }
                                    settings.isWebtoonFormat = (entryJson.substr(valueStart, 4) == "true");
                                }
                            }

                            m_settings.mangaReaderSettings[mangaId] = settings;
                            searchPos = entryEnd + 1;
                        } else {
                            break;
                        }
                    } else {
                        searchPos = quoteEnd + 1;
                    }
                }

                brls::Logger::debug("Loaded {} per-manga reader settings", m_settings.mangaReaderSettings.size());
            }
        }
    }

    // Load auth credentials (stored separately for security)
    m_authUsername = extractString("authUsername");
    m_authPassword = extractString("authPassword");
    m_settings.authMode = extractInt("authMode");
    m_settings.accessToken = extractString("accessToken");
    m_settings.refreshToken = extractString("refreshToken");

    // Apply auth credentials and mode to SuwayomiClient
    SuwayomiClient& client = SuwayomiClient::getInstance();
    client.setAuthMode(static_cast<AuthMode>(m_settings.authMode));

    if (!m_authUsername.empty() && !m_authPassword.empty()) {
        client.setAuthCredentials(m_authUsername, m_authPassword);
        brls::Logger::info("Restored auth credentials for user: {}", m_authUsername);
    }

    // Restore tokens for JWT-based auth
    if (!m_settings.accessToken.empty() || !m_settings.refreshToken.empty()) {
        client.setTokens(m_settings.accessToken, m_settings.refreshToken);
        brls::Logger::info("Restored auth tokens");
    }

    brls::Logger::info("Settings loaded successfully");
    return true;  // Return true if we successfully read the file
}

bool Application::saveSettings() {
    brls::Logger::info("saveSettings: Saving to {}", SETTINGS_PATH);
    brls::Logger::debug("saveSettings: serverUrl={}",
                        m_serverUrl.empty() ? "(empty)" : m_serverUrl);

    // Create JSON content
    std::string json = "{\n";

    // Connection info
    json += "  \"serverUrl\": \"" + m_serverUrl + "\",\n";
    json += "  \"currentCategoryId\": " + std::to_string(m_currentCategoryId) + ",\n";

    // Auth credentials (stored for persistence)
    json += "  \"authUsername\": \"" + m_authUsername + "\",\n";
    json += "  \"authPassword\": \"" + m_authPassword + "\",\n";
    json += "  \"authMode\": " + std::to_string(m_settings.authMode) + ",\n";
    json += "  \"accessToken\": \"" + m_settings.accessToken + "\",\n";
    json += "  \"refreshToken\": \"" + m_settings.refreshToken + "\",\n";

    // UI settings
    json += "  \"theme\": " + std::to_string(static_cast<int>(m_settings.theme)) + ",\n";
    json += "  \"showClock\": " + std::string(m_settings.showClock ? "true" : "false") + ",\n";
    json += "  \"debugLogging\": " + std::string(m_settings.debugLogging ? "true" : "false") + ",\n";

    // Reader settings
    json += "  \"readingMode\": " + std::to_string(static_cast<int>(m_settings.readingMode)) + ",\n";
    json += "  \"pageScaleMode\": " + std::to_string(static_cast<int>(m_settings.pageScaleMode)) + ",\n";
    json += "  \"readerBackground\": " + std::to_string(static_cast<int>(m_settings.readerBackground)) + ",\n";
    json += "  \"imageRotation\": " + std::to_string(m_settings.imageRotation) + ",\n";
    json += "  \"keepScreenOn\": " + std::string(m_settings.keepScreenOn ? "true" : "false") + ",\n";
    json += "  \"showPageNumber\": " + std::string(m_settings.showPageNumber ? "true" : "false") + ",\n";
    json += "  \"tapToNavigate\": " + std::string(m_settings.tapToNavigate ? "true" : "false") + ",\n";

    // Webtoon settings
    json += "  \"cropBorders\": " + std::string(m_settings.cropBorders ? "true" : "false") + ",\n";
    json += "  \"webtoonDetection\": " + std::string(m_settings.webtoonDetection ? "true" : "false") + ",\n";
    json += "  \"webtoonSidePadding\": " + std::to_string(m_settings.webtoonSidePadding) + ",\n";

    // Auto-chapter advance settings
    json += "  \"autoChapterAdvance\": " + std::string(m_settings.autoChapterAdvance ? "true" : "false") + ",\n";
    json += "  \"autoAdvanceDelay\": " + std::to_string(m_settings.autoAdvanceDelay) + ",\n";
    json += "  \"showAdvanceCountdown\": " + std::string(m_settings.showAdvanceCountdown ? "true" : "false") + ",\n";

    // Library settings
    json += "  \"updateOnStart\": " + std::string(m_settings.updateOnStart ? "true" : "false") + ",\n";
    json += "  \"updateOnlyWifi\": " + std::string(m_settings.updateOnlyWifi ? "true" : "false") + ",\n";
    json += "  \"defaultCategoryId\": " + std::to_string(m_settings.defaultCategoryId) + ",\n";

    // Hidden categories (stored as comma-separated IDs)
    std::string hiddenCatsStr;
    for (int catId : m_settings.hiddenCategoryIds) {
        if (!hiddenCatsStr.empty()) hiddenCatsStr += ",";
        hiddenCatsStr += std::to_string(catId);
    }
    json += "  \"hiddenCategoryIds\": \"" + hiddenCatsStr + "\",\n";

    // Cache settings
    json += "  \"cacheLibraryData\": " + std::string(m_settings.cacheLibraryData ? "true" : "false") + ",\n";
    json += "  \"cacheCoverImages\": " + std::string(m_settings.cacheCoverImages ? "true" : "false") + ",\n";
    json += "  \"librarySortMode\": " + std::to_string(m_settings.librarySortMode) + ",\n";
    json += "  \"chapterSortDescending\": " + std::string(m_settings.chapterSortDescending ? "true" : "false") + ",\n";

    // Download settings
    json += "  \"downloadMode\": " + std::to_string(static_cast<int>(m_settings.downloadMode)) + ",\n";
    json += "  \"autoDownloadChapters\": " + std::string(m_settings.autoDownloadChapters ? "true" : "false") + ",\n";
    json += "  \"downloadOverWifiOnly\": " + std::string(m_settings.downloadOverWifiOnly ? "true" : "false") + ",\n";
    json += "  \"maxConcurrentDownloads\": " + std::to_string(m_settings.maxConcurrentDownloads) + ",\n";
    json += "  \"deleteAfterRead\": " + std::string(m_settings.deleteAfterRead ? "true" : "false") + ",\n";
    json += "  \"autoResumeDownloads\": " + std::string(m_settings.autoResumeDownloads ? "true" : "false") + ",\n";

    // Browse/Source settings
    json += "  \"showNsfwSources\": " + std::string(m_settings.showNsfwSources ? "true" : "false") + ",\n";
    json += "  \"enabledSourceLanguages\": [";
    {
        bool first = true;
        for (const auto& lang : m_settings.enabledSourceLanguages) {
            if (!first) json += ", ";
            first = false;
            json += "\"" + lang + "\"";
        }
    }
    json += "],\n";

    // Network settings
    json += "  \"localServerUrl\": \"" + m_settings.localServerUrl + "\",\n";
    json += "  \"remoteServerUrl\": \"" + m_settings.remoteServerUrl + "\",\n";
    json += "  \"useRemoteUrl\": " + std::string(m_settings.useRemoteUrl ? "true" : "false") + ",\n";
    json += "  \"autoSwitchOnFailure\": " + std::string(m_settings.autoSwitchOnFailure ? "true" : "false") + ",\n";
    json += "  \"connectionTimeout\": " + std::to_string(m_settings.connectionTimeout) + ",\n";

    // Display settings
    json += "  \"showUnreadBadge\": " + std::string(m_settings.showUnreadBadge ? "true" : "false") + ",\n";

    // Library grid customization
    json += "  \"libraryDisplayMode\": " + std::to_string(static_cast<int>(m_settings.libraryDisplayMode)) + ",\n";
    json += "  \"libraryGridSize\": " + std::to_string(static_cast<int>(m_settings.libraryGridSize)) + ",\n";

    // Search history
    json += "  \"maxSearchHistory\": " + std::to_string(m_settings.maxSearchHistory) + ",\n";
    json += "  \"searchHistory\": [";
    {
        bool first = true;
        for (const auto& query : m_settings.searchHistory) {
            if (!first) json += ", ";
            first = false;
            // Escape any quotes in search queries
            std::string escaped;
            for (char c : query) {
                if (c == '"') escaped += "\\\"";
                else if (c == '\\') escaped += "\\\\";
                else escaped += c;
            }
            json += "\"" + escaped + "\"";
        }
    }
    json += "],\n";

    // Reading statistics
    json += "  \"totalChaptersRead\": " + std::to_string(m_settings.totalChaptersRead) + ",\n";
    json += "  \"totalMangaCompleted\": " + std::to_string(m_settings.totalMangaCompleted) + ",\n";
    json += "  \"currentStreak\": " + std::to_string(m_settings.currentStreak) + ",\n";
    json += "  \"longestStreak\": " + std::to_string(m_settings.longestStreak) + ",\n";
    json += "  \"lastReadDate\": " + std::to_string(m_settings.lastReadDate) + ",\n";
    json += "  \"totalReadingTime\": " + std::to_string(m_settings.totalReadingTime) + ",\n";

    // Per-manga reader settings
    json += "  \"mangaReaderSettings\": {";
    bool firstManga = true;
    for (const auto& pair : m_settings.mangaReaderSettings) {
        if (!firstManga) json += ",";
        firstManga = false;
        json += "\n    \"" + std::to_string(pair.first) + "\": {";
        json += "\"readingMode\": " + std::to_string(static_cast<int>(pair.second.readingMode)) + ", ";
        json += "\"pageScaleMode\": " + std::to_string(static_cast<int>(pair.second.pageScaleMode)) + ", ";
        json += "\"imageRotation\": " + std::to_string(pair.second.imageRotation) + ", ";
        json += "\"cropBorders\": " + std::string(pair.second.cropBorders ? "true" : "false") + ", ";
        json += "\"webtoonSidePadding\": " + std::to_string(pair.second.webtoonSidePadding) + ", ";
        json += "\"isWebtoonFormat\": " + std::string(pair.second.isWebtoonFormat ? "true" : "false") + "}";
    }
    if (!m_settings.mangaReaderSettings.empty()) json += "\n  ";
    json += "}\n";

    json += "}\n";

#ifdef __vita__
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
    // Non-Vita: use standard C++ file I/O
    std::ofstream file(SETTINGS_PATH);
    if (!file.is_open()) {
        brls::Logger::error("Failed to open settings file for writing");
        return false;
    }
    file << json;
    file.close();
    brls::Logger::info("Settings saved successfully ({} bytes)", json.length());
    return true;
#endif
}

std::string Application::getActiveServerUrl() const {
    if (m_settings.useRemoteUrl && !m_settings.remoteServerUrl.empty()) {
        return m_settings.remoteServerUrl;
    }
    if (!m_settings.localServerUrl.empty()) {
        return m_settings.localServerUrl;
    }
    // Fall back to current server URL if no local/remote configured
    return m_serverUrl;
}

std::string Application::getAlternateServerUrl() const {
    // Return the URL that is NOT currently active
    if (m_settings.useRemoteUrl) {
        // Currently using remote, so alternate is local
        return m_settings.localServerUrl;
    } else {
        // Currently using local, so alternate is remote
        return m_settings.remoteServerUrl;
    }
}

bool Application::tryAlternateUrl() {
    // Only try if auto-switch is enabled and both URLs are configured
    if (!m_settings.autoSwitchOnFailure || !hasBothUrls()) {
        return false;
    }

    std::string alternateUrl = getAlternateServerUrl();
    if (alternateUrl.empty()) {
        return false;
    }

    brls::Logger::info("Auto-switch: Trying alternate URL: {}", alternateUrl);

    // Test the alternate URL
    SuwayomiClient& client = SuwayomiClient::getInstance();
    std::string originalUrl = client.getServerUrl();
    client.setServerUrl(alternateUrl);

    if (client.testConnection()) {
        // Alternate URL works - switch to it
        if (m_settings.useRemoteUrl) {
            // Was using remote, now switch to local
            m_settings.useRemoteUrl = false;
            m_serverUrl = m_settings.localServerUrl;
            brls::Logger::info("Auto-switch: Switched to local URL");
        } else {
            // Was using local, now switch to remote
            m_settings.useRemoteUrl = true;
            m_serverUrl = m_settings.remoteServerUrl;
            brls::Logger::info("Auto-switch: Switched to remote URL");
        }
        saveSettings();
        return true;
    }

    // Alternate URL also failed - restore original
    client.setServerUrl(originalUrl);
    brls::Logger::warning("Auto-switch: Alternate URL also failed");
    return false;
}

void Application::switchToLocalUrl() {
    if (!m_settings.localServerUrl.empty()) {
        m_settings.useRemoteUrl = false;
        m_serverUrl = m_settings.localServerUrl;
        SuwayomiClient::getInstance().setServerUrl(m_serverUrl);
        brls::Logger::info("Switched to local URL: {}", m_serverUrl);
        saveSettings();
    }
}

void Application::switchToRemoteUrl() {
    if (!m_settings.remoteServerUrl.empty()) {
        m_settings.useRemoteUrl = true;
        m_serverUrl = m_settings.remoteServerUrl;
        SuwayomiClient::getInstance().setServerUrl(m_serverUrl);
        brls::Logger::info("Switched to remote URL: {}", m_serverUrl);
        saveSettings();
    }
}

void Application::updateReadingStatistics(bool chapterCompleted, bool mangaCompleted) {
    // Get current date (days since epoch for streak calculation)
    int64_t currentTime = static_cast<int64_t>(std::time(nullptr));
    int64_t currentDay = currentTime / (24 * 60 * 60);  // Days since epoch
    int64_t lastDay = m_settings.lastReadDate / (24 * 60 * 60);

    // Update chapter count
    if (chapterCompleted) {
        m_settings.totalChaptersRead++;
        brls::Logger::info("Statistics: chapters read = {}", m_settings.totalChaptersRead);
    }

    // Update manga completed count
    if (mangaCompleted) {
        m_settings.totalMangaCompleted++;
        brls::Logger::info("Statistics: manga completed = {}", m_settings.totalMangaCompleted);
    }

    // Update reading streak
    if (m_settings.lastReadDate == 0) {
        // First time reading
        m_settings.currentStreak = 1;
    } else if (currentDay == lastDay) {
        // Same day - no streak change
    } else if (currentDay == lastDay + 1) {
        // Consecutive day - increment streak
        m_settings.currentStreak++;
    } else {
        // Streak broken - reset to 1
        m_settings.currentStreak = 1;
    }

    // Update longest streak
    if (m_settings.currentStreak > m_settings.longestStreak) {
        m_settings.longestStreak = m_settings.currentStreak;
    }

    // Update last read date
    m_settings.lastReadDate = currentTime;

    // Save settings
    saveSettings();
}

void Application::syncStatisticsFromServer() {
    SuwayomiClient& client = SuwayomiClient::getInstance();

    // Fetch library manga to count completed manga
    std::vector<Manga> libraryManga;
    if (client.fetchLibraryManga(libraryManga)) {
        int chaptersRead = 0;
        int mangaCompleted = 0;

        for (const auto& manga : libraryManga) {
            // Count chapters read (total - unread)
            int readChapters = manga.chapterCount - manga.unreadCount;
            if (readChapters > 0) {
                chaptersRead += readChapters;
            }

            // Count completed manga (no unread chapters and has chapters)
            if (manga.unreadCount == 0 && manga.chapterCount > 0) {
                mangaCompleted++;
            }
        }

        // Update stats if server has higher counts (don't decrease)
        if (chaptersRead > m_settings.totalChaptersRead) {
            m_settings.totalChaptersRead = chaptersRead;
            brls::Logger::info("Statistics synced from server: {} chapters read", chaptersRead);
        }
        if (mangaCompleted > m_settings.totalMangaCompleted) {
            m_settings.totalMangaCompleted = mangaCompleted;
            brls::Logger::info("Statistics synced from server: {} manga completed", mangaCompleted);
        }

        saveSettings();
    }
}

} // namespace vitasuwayomi
