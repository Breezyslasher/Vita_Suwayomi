/**
 * VitaSuwayomi - Application implementation
 */

#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "utils/library_cache.hpp"
#include "utils/async.hpp"
#include "activity/login_activity.hpp"
#include "activity/main_activity.hpp"
#include "activity/reader_activity.hpp"
#include "platform/paths.hpp"
#include "utils/perf_overlay.hpp"

#include <borealis.hpp>
#include <filesystem>
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
static const std::string SETTINGS_PATH = platformPath("settings.json");
#endif

// Obfuscation helpers for storing sensitive fields (password, tokens) on disk.
// Uses XOR with a static key + base64 encoding. This is NOT cryptographic security
// (the key is in the binary), but prevents plain-text passwords in the config file.
static const char OBFUSCATION_KEY[] = "VitaSuwayomi$2025!SecretKey";
static const char OBFUSCATION_PREFIX[] = "enc:";

static std::string base64EncodeLocal(const std::vector<uint8_t>& input) {
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (uint8_t c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

static std::vector<uint8_t> base64DecodeLocal(const std::string& input) {
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    std::vector<uint8_t> out;
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::string obfuscate(const std::string& plaintext) {
    if (plaintext.empty()) return "";
    size_t keyLen = sizeof(OBFUSCATION_KEY) - 1;
    std::vector<uint8_t> xored(plaintext.size());
    for (size_t i = 0; i < plaintext.size(); i++) {
        xored[i] = static_cast<uint8_t>(plaintext[i]) ^ static_cast<uint8_t>(OBFUSCATION_KEY[i % keyLen]);
    }
    return std::string(OBFUSCATION_PREFIX) + base64EncodeLocal(xored);
}

static std::string deobfuscate(const std::string& stored) {
    if (stored.empty()) return "";
    // If not obfuscated (legacy plain text), return as-is for migration
    if (stored.substr(0, sizeof(OBFUSCATION_PREFIX) - 1) != OBFUSCATION_PREFIX) {
        return stored;
    }
    std::string b64 = stored.substr(sizeof(OBFUSCATION_PREFIX) - 1);
    std::vector<uint8_t> xored = base64DecodeLocal(b64);
    size_t keyLen = sizeof(OBFUSCATION_KEY) - 1;
    std::string plaintext(xored.size(), '\0');
    for (size_t i = 0; i < xored.size(); i++) {
        plaintext[i] = static_cast<char>(xored[i] ^ static_cast<uint8_t>(OBFUSCATION_KEY[i % keyLen]));
    }
    return plaintext;
}

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
#else
    std::error_code ec;
    std::filesystem::create_directories(PLATFORM_DATA_DIR, ec);
    if (ec) {
        brls::Logger::warning("Failed to create data dir {}: {}", PLATFORM_DATA_DIR, ec.message());
    }
#endif

    // Load saved settings
    brls::Logger::info("Loading saved settings...");
    bool loaded = loadSettings();
    brls::Logger::info("Settings load result: {}", loaded ? "success" : "failed/not found");

    // Apply settings
    applyTheme();
    applyLogLevel();
    PerfOverlay::getInstance().setEnabled(m_settings.showPerfOverlay);

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

        // Try to restore auth session
        AuthMode authMode = client.getAuthMode();
        if (authMode == AuthMode::SIMPLE_LOGIN) {
            // SIMPLE_LOGIN uses server-side sessions via POST /login.html.
            // Sessions expire, so always re-login with stored credentials.
            std::string username = getAuthUsername();
            std::string password = getAuthPassword();
            if (!username.empty() && !password.empty()) {
                brls::Logger::info("SIMPLE_LOGIN: re-establishing session...");
                if (client.login(username, password)) {
                    brls::Logger::info("SIMPLE_LOGIN: session established");
                    m_settings.sessionCookie = client.getSessionCookie();
                } else {
                    brls::Logger::warning("SIMPLE_LOGIN: session login failed");
                    client.setSessionCookie("");
                    m_settings.sessionCookie = "";
                }
            }
        } else if (authMode == AuthMode::UI_LOGIN && !client.getRefreshToken().empty()) {
            // UI_LOGIN uses JWT tokens - try to refresh them (they may be expired)
            brls::Logger::info("Attempting to refresh JWT tokens...");
            if (client.refreshToken()) {
                brls::Logger::info("Token refresh successful");
                m_settings.accessToken = client.getAccessToken();
                m_settings.refreshToken = client.getRefreshToken();
                m_settings.sessionCookie = client.getSessionCookie();
            } else {
                brls::Logger::warning("Token refresh failed, clearing stale tokens for basic auth fallback");
                client.setTokens("", "");
                m_settings.accessToken = "";
                m_settings.refreshToken = "";
            }
        }

        // Test connection with a PROTECTED query (not just aboutServer which is public)
        // This catches auth mode mismatches that testConnection() would miss
        bool authValid = false;
        if (client.testConnection()) {
            brls::Logger::info("Server reachable, validating auth with protected query...");

            if (authMode == AuthMode::NONE) {
                // No auth - if testConnection works, we're good
                authValid = true;
            } else if (client.validateAuthWithProtectedQuery()) {
                brls::Logger::info("Auth validated with protected query");
                authValid = true;
            } else {
                // Auth failed on protected query - try all auth modes automatically
                brls::Logger::warning("Protected query failed with saved mode {}, trying all modes...",
                    static_cast<int>(authMode));

                std::string username = getAuthUsername();
                std::string password = getAuthPassword();

                if (!username.empty() && !password.empty()) {
                    // First try a fresh login with the saved mode (cookies may have expired)
                    if (authMode == AuthMode::SIMPLE_LOGIN || authMode == AuthMode::UI_LOGIN) {
                        brls::Logger::info("Trying fresh login with saved mode {}", static_cast<int>(authMode));
                        client.setAuthMode(authMode);
                        client.logout();
                        if (client.login(username, password) && client.validateAuthWithProtectedQuery()) {
                            brls::Logger::info("Fresh login succeeded with saved mode {}", static_cast<int>(authMode));
                            m_settings.accessToken = client.getAccessToken();
                            m_settings.refreshToken = client.getRefreshToken();
                            m_settings.sessionCookie = client.getSessionCookie();
                            saveSettings();
                            authValid = true;
                        }
                    }

                    if (!authValid) {
                        // Try all modes: UI_LOGIN, SIMPLE_LOGIN, BASIC_AUTH
                        AuthMode tryModes[] = { AuthMode::UI_LOGIN, AuthMode::SIMPLE_LOGIN, AuthMode::BASIC_AUTH };
                        for (AuthMode tryMode : tryModes) {
                            if (tryMode == authMode) continue;  // Already tried with fresh login above

                            brls::Logger::info("Trying auth mode: {}", static_cast<int>(tryMode));
                            client.setAuthMode(tryMode);
                            client.logout();

                            if (tryMode == AuthMode::BASIC_AUTH) {
                                client.setAuthCredentials(username, password);
                            } else {
                                if (!client.login(username, password)) continue;
                            }

                            if (client.validateAuthWithProtectedQuery()) {
                                brls::Logger::info("Auth succeeded with mode {}", static_cast<int>(tryMode));
                                m_settings.authMode = static_cast<int>(tryMode);
                                m_settings.accessToken = client.getAccessToken();
                                m_settings.refreshToken = client.getRefreshToken();
                                m_settings.sessionCookie = client.getSessionCookie();
                                saveSettings();
                                authValid = true;
                                break;
                            }
                        }
                    }
                }

                if (!authValid) {
                    brls::Logger::error("All auth modes failed, showing login screen");
                }
            }
        } else {
            brls::Logger::warning("Server not reachable with current auth mode {}", static_cast<int>(authMode));

            // testConnection() can fail due to auth mode mismatch (not just network issues).
            // For example, if server switched from JWT to Basic auth and token clearing wasn't
            // enough, or if the auth mode changed in other ways.
            // Try all auth modes as fallback before giving up.
            std::string username = getAuthUsername();
            std::string password = getAuthPassword();

            if (!username.empty() && !password.empty()) {
                brls::Logger::info("Have credentials, trying auth mode fallback...");

                // First try a fresh login with the saved mode (cookies/tokens may have expired)
                if (authMode == AuthMode::SIMPLE_LOGIN || authMode == AuthMode::UI_LOGIN) {
                    brls::Logger::info("Trying fresh login with saved mode {}", static_cast<int>(authMode));
                    client.setAuthMode(authMode);
                    client.logout();
                    if (client.login(username, password) &&
                        client.testConnection() && client.validateAuthWithProtectedQuery()) {
                        brls::Logger::info("Fresh login succeeded with saved mode {}", static_cast<int>(authMode));
                        m_settings.accessToken = client.getAccessToken();
                        m_settings.refreshToken = client.getRefreshToken();
                        m_settings.sessionCookie = client.getSessionCookie();
                        saveSettings();
                        authValid = true;
                    }
                }

                if (!authValid) {
                    // Try BASIC_AUTH first since it's the most common fallback from JWT
                    AuthMode tryModes[] = { AuthMode::BASIC_AUTH, AuthMode::UI_LOGIN, AuthMode::SIMPLE_LOGIN, AuthMode::NONE };
                    for (AuthMode tryMode : tryModes) {
                        if (tryMode == authMode) continue;  // Already tried with fresh login above

                        brls::Logger::info("Trying auth mode: {}", static_cast<int>(tryMode));
                        client.setAuthMode(tryMode);
                        client.logout();

                        if (tryMode == AuthMode::BASIC_AUTH) {
                            client.setAuthCredentials(username, password);
                        } else if (tryMode == AuthMode::NONE) {
                            // No credentials needed
                        } else {
                            if (!client.login(username, password)) continue;
                        }

                        if (client.testConnection() && client.validateAuthWithProtectedQuery()) {
                            brls::Logger::info("Connection succeeded with auth mode {}", static_cast<int>(tryMode));
                            m_settings.authMode = static_cast<int>(tryMode);
                            m_settings.accessToken = client.getAccessToken();
                            m_settings.refreshToken = client.getRefreshToken();
                            m_settings.sessionCookie = client.getSessionCookie();
                            saveSettings();
                            authValid = true;
                            break;
                        }
                    }
                }

                if (!authValid) {
                    // Restore original auth mode so login screen starts from a known state
                    client.setAuthMode(authMode);
                    if (authMode == AuthMode::BASIC_AUTH) {
                        client.setAuthCredentials(username, password);
                    }
                    brls::Logger::error("All auth mode fallbacks failed");
                }
            }
        }

        if (authValid) {
            brls::Logger::info("Connection restored successfully");
            m_isConnected = true;

            // Apply server image settings on first run (convert to PNG/JPEG only)
            if (!m_settings.serverImageSettingsApplied) {
                brls::Logger::info("First run: applying server image settings...");
                if (SuwayomiClient::getInstance().applyServerImageSettings()) {
                    m_settings.serverImageSettingsApplied = true;
                    saveSettings();
                    brls::Logger::info("Server image settings applied successfully");
                } else {
                    brls::Logger::warning("Failed to apply server image settings, will retry next launch");
                }
            }

            // Sync offline reading progress to server in background
            DownloadsManager& dm = DownloadsManager::getInstance();
            if (!dm.getDownloads().empty()) {
                brls::Logger::info("Syncing offline reading progress to server...");
                vitasuwayomi::asyncRun([]() {
                    DownloadsManager::getInstance().syncProgressToServer();
                    brls::sync([]() {
                        brls::Logger::info("Offline reading progress synced to server");
                    });
                });
            }

            // Now that connection is confirmed, auto-resume incomplete downloads
            // This must happen AFTER connection test - otherwise downloads fail immediately
            dm.resumeDownloadsIfNeeded();

            pushMainActivity();
        } else {
            // Connection or auth failed - could be offline or auth expired
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

    // Sync perf overlay setting
    PerfOverlay::getInstance().setEnabled(m_settings.showPerfOverlay);

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
        case AppTheme::SYSTEM:
            // Default to dark for Vita
            variant = brls::ThemeVariant::DARK;
            break;
        default:
            // All custom themes use dark as base variant
            variant = brls::ThemeVariant::DARK;
            break;
    }

    brls::Application::getPlatform()->setThemeVariant(variant);
    brls::Logger::info("Applied theme: {}", getThemeString(m_settings.theme));
}

// ============================================================================
// Theme Color Palette System
// To add a new theme: 1) Add enum value to AppTheme in application.hpp
//                     2) Add a new ThemePalette entry below
//                     3) Update getThemeString() and applyTheme()
// ============================================================================

struct ThemePalette {
    NVGcolor accent;             // Primary accent (buttons, highlights)
    NVGcolor secondary;          // Secondary accent
    NVGcolor headerText;         // Section headers
    NVGcolor subtitle;           // Subtitle/detail text
    NVGcolor highlight;          // Focus highlight
    NVGcolor sidebar;            // Sidebar background tint
    NVGcolor cardBg;             // Card/cell background
    NVGcolor dialogBg;           // Dialog/overlay background
    NVGcolor teal;               // Teal accent (badges, status)
    NVGcolor status;             // Status text color
    NVGcolor description;        // Description/body text
    NVGcolor dimText;            // Dimmed text
    NVGcolor rowBg;              // List row background
    NVGcolor activeRowBg;        // Active/selected row bg
    NVGcolor inactiveRowBg;      // Inactive row bg
    NVGcolor trackingBtn;        // Tracking button
    NVGcolor ctaButton;          // Call-to-action button (read, retry, checkmarks)
    NVGcolor sectionHeaderBg;    // Section header background (extensions tab)
    NVGcolor readerBg;           // Reader/transition dark background
    NVGcolor errorOverlayBg;     // Error overlay background (translucent)
    NVGcolor focusedRowBg;       // Focused/hovered row highlight
    NVGcolor deepBg;             // Very deep/dark background
    NVGcolor separatorColor;     // Separator lines
    NVGcolor successText;        // Success/visible status text
    NVGcolor textColor;          // Primary text color
    NVGcolor buttonColor;        // Default button background
};

static const ThemePalette& getPalette(AppTheme theme) {
    // Default palette (used for System, Light, Dark)
    static const ThemePalette defaultPalette = {
        nvgRGB(100, 180, 255),          // accent: blue
        nvgRGB(100, 200, 100),          // secondary: green
        nvgRGB(100, 180, 255),          // headerText: blue
        nvgRGB(140, 140, 140),          // subtitle: gray
        nvgRGBA(0, 200, 200, 60),       // highlight: teal glow
        nvgRGBA(45, 45, 45, 255),       // sidebar: dark gray
        nvgRGBA(60, 60, 60, 255),       // cardBg: gray
        nvgRGBA(30, 30, 30, 255),       // dialogBg: dark
        nvgRGB(0, 150, 136),            // teal: standard teal
        nvgRGB(74, 159, 255),           // status: blue accent
        nvgRGB(192, 192, 192),          // description: light gray
        nvgRGB(128, 128, 128),          // dimText: gray
        nvgRGBA(40, 40, 40, 255),       // rowBg: dark
        nvgRGBA(0, 100, 80, 200),       // activeRowBg: teal
        nvgRGBA(50, 50, 50, 200),       // inactiveRowBg: gray
        nvgRGBA(103, 58, 183, 255),     // trackingBtn: purple
        nvgRGBA(0, 150, 136, 255),      // ctaButton: teal
        nvgRGB(0, 120, 110),            // sectionHeaderBg: dark teal
        nvgRGBA(20, 20, 30, 255),       // readerBg: dark blue-gray
        nvgRGBA(26, 26, 46, 200),       // errorOverlayBg: dark translucent
        nvgRGBA(60, 60, 80, 255),       // focusedRowBg: blue-gray
        nvgRGBA(20, 20, 20, 255),       // deepBg: near-black
        nvgRGBA(80, 80, 80, 100),       // separatorColor: gray line
        nvgRGB(100, 200, 150),          // successText: green
        nvgRGB(255, 255, 255),          // textColor: white
        nvgRGBA(66, 66, 66, 255),       // buttonColor: dark gray
    };

    // Neon Vaporwave / Miami palette
    static const ThemePalette vaporwavePalette = {
        nvgRGB(255, 50, 200),           // accent: hot pink / neon magenta
        nvgRGB(0, 255, 255),            // secondary: cyan / neon teal
        nvgRGB(0, 255, 200),            // headerText: neon mint/teal
        nvgRGB(180, 100, 255),          // subtitle: neon purple
        nvgRGBA(255, 50, 200, 80),      // highlight: hot pink glow
        nvgRGBA(20, 0, 40, 255),        // sidebar: deep purple-black
        nvgRGBA(30, 5, 50, 255),        // cardBg: dark purple
        nvgRGBA(20, 0, 40, 255),        // dialogBg: deep purple
        nvgRGB(0, 255, 200),            // teal: neon mint
        nvgRGB(0, 255, 255),            // status: neon cyan
        nvgRGB(200, 180, 255),          // description: light purple
        nvgRGB(140, 100, 180),          // dimText: muted purple
        nvgRGBA(25, 5, 45, 255),        // rowBg: dark purple
        nvgRGBA(60, 0, 100, 200),       // activeRowBg: purple highlight
        nvgRGBA(25, 5, 45, 200),        // inactiveRowBg: dark purple
        nvgRGBA(150, 0, 200, 255),      // trackingBtn: neon purple
        nvgRGBA(255, 50, 200, 255),     // ctaButton: hot pink
        nvgRGB(80, 0, 130),             // sectionHeaderBg: dark purple
        nvgRGBA(20, 0, 40, 255),        // readerBg: deep purple
        nvgRGBA(30, 0, 50, 200),        // errorOverlayBg: dark purple translucent
        nvgRGBA(50, 10, 80, 255),       // focusedRowBg: purple
        nvgRGBA(15, 0, 30, 255),        // deepBg: near-black purple
        nvgRGBA(100, 50, 150, 100),     // separatorColor: purple line
        nvgRGB(0, 255, 200),            // successText: neon mint
        nvgRGB(255, 255, 255),          // textColor: white (bright on dark)
        nvgRGBA(50, 10, 80, 255),       // buttonColor: purple
    };

    // Tachiyomi (default Komikku blue)
    static const ThemePalette tachiyomiPalette = {
        nvgRGB(0xB0, 0xC6, 0xFF),       // accent: light blue
        nvgRGB(0x7A, 0xDC, 0x77),       // secondary: green
        nvgRGB(0xB0, 0xC6, 0xFF),       // headerText: light blue
        nvgRGB(0xC5, 0xC6, 0xD0),       // subtitle: gray-blue
        nvgRGBA(0xB0, 0xC6, 0xFF, 60),  // highlight: blue glow
        nvgRGBA(0x1E, 0x1C, 0x22, 255), // sidebar: dark surface
        nvgRGBA(0x21, 0x1F, 0x26, 255), // cardBg: surface container
        nvgRGBA(0x1A, 0x18, 0x1D, 255), // dialogBg: surface lowest
        nvgRGB(0x7A, 0xDC, 0x77),       // teal: tertiary green
        nvgRGB(0xB0, 0xC6, 0xFF),       // status: blue
        nvgRGB(0xC5, 0xC6, 0xD0),       // description: on-surface-variant
        nvgRGB(0x8F, 0x90, 0x99),       // dimText: outline
        nvgRGBA(0x21, 0x1F, 0x26, 255), // rowBg: surface container
        nvgRGBA(0x00, 0x42, 0x9B, 200), // activeRowBg: primary container
        nvgRGBA(0x29, 0x27, 0x30, 200), // inactiveRowBg: surface high
        nvgRGBA(0x00, 0x42, 0x9B, 255), // trackingBtn: primary container
        nvgRGBA(0xB0, 0xC6, 0xFF, 255), // ctaButton: primary
        nvgRGB(0x30, 0x2E, 0x38),       // sectionHeaderBg: surface highest
        nvgRGBA(0x1B, 0x1B, 0x1F, 255), // readerBg: background
        nvgRGBA(0x1B, 0x1B, 0x1F, 200), // errorOverlayBg: bg translucent
        nvgRGBA(0x30, 0x2E, 0x38, 255), // focusedRowBg: surface highest
        nvgRGBA(0x1A, 0x18, 0x1D, 255), // deepBg: surface lowest
        nvgRGBA(0x44, 0x46, 0x4F, 100), // separatorColor: outline variant
        nvgRGB(0x7A, 0xDC, 0x77),       // successText: green
        nvgRGB(0xE3, 0xE2, 0xE6),       // textColor: on-surface
        nvgRGBA(0x29, 0x27, 0x30, 255), // buttonColor: surface high
    };

    // Catppuccin Mocha purple
    static const ThemePalette catppuccinPalette = {
        nvgRGB(0xCB, 0xA6, 0xF7),       // accent: mauve/purple
        nvgRGB(0xCB, 0xA6, 0xF7),       // secondary: same mauve
        nvgRGB(0xCB, 0xA6, 0xF7),       // headerText: mauve
        nvgRGB(0xCD, 0xD6, 0xF4),       // subtitle: text/subtext
        nvgRGBA(0xCB, 0xA6, 0xF7, 60),  // highlight: mauve glow
        nvgRGBA(0x1E, 0x1E, 0x2E, 255), // sidebar: base
        nvgRGBA(0x1E, 0x1E, 0x2E, 255), // cardBg: surface container
        nvgRGBA(0x18, 0x18, 0x25, 255), // dialogBg: crust
        nvgRGB(0xCB, 0xA6, 0xF7),       // teal: mauve
        nvgRGB(0xCB, 0xA6, 0xF7),       // status: mauve
        nvgRGB(0xCD, 0xD6, 0xF4),       // description: text
        nvgRGB(0x58, 0x5B, 0x70),       // dimText: overlay
        nvgRGBA(0x1E, 0x1E, 0x2E, 255), // rowBg: surface
        nvgRGBA(0xCB, 0xA6, 0xF7, 60),  // activeRowBg: mauve tint
        nvgRGBA(0x1E, 0x1E, 0x2E, 200), // inactiveRowBg: surface
        nvgRGBA(0xCB, 0xA6, 0xF7, 255), // trackingBtn: mauve
        nvgRGBA(0xCB, 0xA6, 0xF7, 255), // ctaButton: mauve
        nvgRGB(0x31, 0x32, 0x44),       // sectionHeaderBg: surface bright
        nvgRGBA(0x18, 0x18, 0x25, 255), // readerBg: background
        nvgRGBA(0x18, 0x18, 0x25, 200), // errorOverlayBg
        nvgRGBA(0x31, 0x32, 0x44, 255), // focusedRowBg: surface bright
        nvgRGBA(0x18, 0x18, 0x25, 255), // deepBg: crust
        nvgRGBA(0x58, 0x5B, 0x70, 100), // separatorColor: overlay
        nvgRGB(0xCB, 0xA6, 0xF7),       // successText: mauve
        nvgRGB(0xCD, 0xD6, 0xF4),       // textColor: text
        nvgRGBA(0x1E, 0x1E, 0x2E, 255), // buttonColor: surface
    };

    // Nord arctic blue
    static const ThemePalette nordPalette = {
        nvgRGB(0x88, 0xC0, 0xD0),       // accent: frost blue
        nvgRGB(0x81, 0xA1, 0xC1),       // secondary: frost darker
        nvgRGB(0x88, 0xC0, 0xD0),       // headerText: frost blue
        nvgRGB(0xEC, 0xEF, 0xF4),       // subtitle: snow storm
        nvgRGBA(0x88, 0xC0, 0xD0, 60),  // highlight: frost glow
        nvgRGBA(0x3E, 0x47, 0x56, 255), // sidebar: polar night
        nvgRGBA(0x41, 0x4C, 0x5C, 255), // cardBg: surface variant
        nvgRGBA(0x37, 0x3F, 0x4D, 255), // dialogBg: surface lowest
        nvgRGB(0x5E, 0x81, 0xAC),       // teal: tertiary frost
        nvgRGB(0x88, 0xC0, 0xD0),       // status: frost
        nvgRGB(0xEC, 0xEF, 0xF4),       // description: snow
        nvgRGB(0x6D, 0x71, 0x7B),       // dimText: outline
        nvgRGBA(0x41, 0x4C, 0x5C, 255), // rowBg: surface
        nvgRGBA(0x88, 0xC0, 0xD0, 60),  // activeRowBg: frost tint
        nvgRGBA(0x4E, 0x57, 0x66, 200), // inactiveRowBg: surface high
        nvgRGBA(0x5E, 0x81, 0xAC, 255), // trackingBtn: tertiary
        nvgRGBA(0x88, 0xC0, 0xD0, 255), // ctaButton: frost
        nvgRGB(0x50, 0x59, 0x68),       // sectionHeaderBg: surface highest
        nvgRGBA(0x2E, 0x34, 0x40, 255), // readerBg: polar night
        nvgRGBA(0x2E, 0x34, 0x40, 200), // errorOverlayBg
        nvgRGBA(0x50, 0x59, 0x68, 255), // focusedRowBg: surface highest
        nvgRGBA(0x37, 0x3F, 0x4D, 255), // deepBg: surface lowest
        nvgRGBA(0x90, 0x93, 0x9A, 100), // separatorColor: outline variant
        nvgRGB(0x5E, 0x81, 0xAC),       // successText: frost
        nvgRGB(0xEC, 0xEF, 0xF4),       // textColor: snow storm
        nvgRGBA(0x4E, 0x57, 0x66, 255), // buttonColor: surface high
    };

    // Tako warm orange
    static const ThemePalette takoPalette = {
        nvgRGB(0xF3, 0xB3, 0x75),       // accent: warm orange
        nvgRGB(0x66, 0x57, 0x7E),       // secondary: purple
        nvgRGB(0xF3, 0xB3, 0x75),       // headerText: orange
        nvgRGB(0xCB, 0xC4, 0xCE),       // subtitle: on-surface-variant
        nvgRGBA(0xF3, 0xB3, 0x75, 60),  // highlight: orange glow
        nvgRGBA(0x26, 0x26, 0x36, 255), // sidebar: surface low
        nvgRGBA(0x2A, 0x2A, 0x3C, 255), // cardBg: surface container
        nvgRGBA(0x20, 0x20, 0x2E, 255), // dialogBg: surface lowest
        nvgRGB(0x66, 0x57, 0x7E),       // teal: purple tertiary
        nvgRGB(0xF3, 0xB3, 0x75),       // status: orange
        nvgRGB(0xCB, 0xC4, 0xCE),       // description: on-surface-variant
        nvgRGB(0x95, 0x8F, 0x99),       // dimText: outline
        nvgRGBA(0x2A, 0x2A, 0x3C, 255), // rowBg: surface container
        nvgRGBA(0xF3, 0xB3, 0x75, 60),  // activeRowBg: orange tint
        nvgRGBA(0x30, 0x30, 0x44, 200), // inactiveRowBg: surface high
        nvgRGBA(0x66, 0x57, 0x7E, 255), // trackingBtn: purple
        nvgRGBA(0xF3, 0xB3, 0x75, 255), // ctaButton: orange
        nvgRGB(0x36, 0x36, 0x4D),       // sectionHeaderBg: surface highest
        nvgRGBA(0x21, 0x21, 0x2E, 255), // readerBg: background
        nvgRGBA(0x21, 0x21, 0x2E, 200), // errorOverlayBg
        nvgRGBA(0x36, 0x36, 0x4D, 255), // focusedRowBg: surface highest
        nvgRGBA(0x20, 0x20, 0x2E, 255), // deepBg: surface lowest
        nvgRGBA(0x95, 0x8F, 0x99, 100), // separatorColor: outline
        nvgRGB(0x66, 0x57, 0x7E),       // successText: purple
        nvgRGB(0xE3, 0xE0, 0xF2),       // textColor: on-surface
        nvgRGBA(0x30, 0x30, 0x44, 255), // buttonColor: surface high
    };

    // Midnight Dusk pink
    static const ThemePalette midnightDuskPalette = {
        nvgRGB(0xF0, 0x24, 0x75),       // accent: hot pink
        nvgRGB(0x55, 0x97, 0x1C),       // secondary: green
        nvgRGB(0xF0, 0x24, 0x75),       // headerText: pink
        nvgRGB(0xD6, 0xC1, 0xC4),       // subtitle: on-surface-variant
        nvgRGBA(0xF0, 0x24, 0x75, 60),  // highlight: pink glow
        nvgRGBA(0x25, 0x15, 0x22, 255), // sidebar: surface low
        nvgRGBA(0x28, 0x16, 0x24, 255), // cardBg: surface variant
        nvgRGBA(0x22, 0x13, 0x20, 255), // dialogBg: surface lowest
        nvgRGB(0x55, 0x97, 0x1C),       // teal: green tertiary
        nvgRGB(0xF0, 0x24, 0x75),       // status: pink
        nvgRGB(0xD6, 0xC1, 0xC4),       // description: on-surface-variant
        nvgRGB(0x9F, 0x8C, 0x8F),       // dimText: outline
        nvgRGBA(0x28, 0x16, 0x24, 255), // rowBg: surface
        nvgRGBA(0x66, 0x18, 0x3C, 200), // activeRowBg: secondary container
        nvgRGBA(0x2D, 0x1C, 0x2A, 200), // inactiveRowBg: surface high
        nvgRGBA(0xBD, 0x1C, 0x5C, 255), // trackingBtn: primary container
        nvgRGBA(0xF0, 0x24, 0x75, 255), // ctaButton: pink
        nvgRGB(0x2F, 0x1F, 0x2C),       // sectionHeaderBg: surface highest
        nvgRGBA(0x16, 0x15, 0x1D, 255), // readerBg: background
        nvgRGBA(0x16, 0x15, 0x1D, 200), // errorOverlayBg
        nvgRGBA(0x2F, 0x1F, 0x2C, 255), // focusedRowBg: surface highest
        nvgRGBA(0x22, 0x13, 0x20, 255), // deepBg
        nvgRGBA(0x9F, 0x8C, 0x8F, 100), // separatorColor
        nvgRGB(0x55, 0x97, 0x1C),       // successText: green
        nvgRGB(0xE5, 0xE1, 0xE5),       // textColor: on-surface
        nvgRGBA(0x2D, 0x1C, 0x2A, 255), // buttonColor: surface high
    };

    // Strawberry Daiquiri red-pink
    static const ThemePalette strawberryPalette = {
        nvgRGB(0xAF, 0xFB, 0x2B),       // accent: (note: parsed as 0xAF, 0xFB, 0x2B - light lime? actually it's #FFB2B8 pinkish)
        nvgRGB(0xED, 0x4A, 0x65),       // secondary: red
        nvgRGB(0xED, 0x4A, 0x65),       // headerText: red
        nvgRGB(0xE1, 0xBE, 0xC0),       // subtitle: on-surface-variant
        nvgRGBA(0xED, 0x4A, 0x65, 60),  // highlight: red glow
        nvgRGBA(0x30, 0x25, 0x25, 255), // sidebar: surface low
        nvgRGBA(0x32, 0x27, 0x27, 255), // cardBg: surface variant
        nvgRGBA(0x2C, 0x22, 0x22, 255), // dialogBg: surface lowest
        nvgRGB(0xE8, 0xC0, 0x8E),       // teal: warm tertiary
        nvgRGB(0xED, 0x4A, 0x65),       // status: red
        nvgRGB(0xE1, 0xBE, 0xC0),       // description
        nvgRGB(0xA9, 0x89, 0x8B),       // dimText: outline
        nvgRGBA(0x32, 0x27, 0x27, 255), // rowBg
        nvgRGBA(0xD5, 0x38, 0x55, 200), // activeRowBg: primary container
        nvgRGBA(0x3C, 0x2F, 0x2F, 200), // inactiveRowBg
        nvgRGBA(0xD5, 0x38, 0x55, 255), // trackingBtn
        nvgRGBA(0xED, 0x4A, 0x65, 255), // ctaButton
        nvgRGB(0x46, 0x37, 0x37),       // sectionHeaderBg
        nvgRGBA(0x20, 0x1A, 0x1A, 255), // readerBg
        nvgRGBA(0x20, 0x1A, 0x1A, 200), // errorOverlayBg
        nvgRGBA(0x46, 0x37, 0x37, 255), // focusedRowBg
        nvgRGBA(0x2C, 0x22, 0x22, 255), // deepBg
        nvgRGBA(0x59, 0x40, 0x42, 100), // separatorColor
        nvgRGB(0xE8, 0xC0, 0x8E),       // successText
        nvgRGB(0xF7, 0xDC, 0xDD),       // textColor
        nvgRGBA(0x3C, 0x2F, 0x2F, 255), // buttonColor
    };

    // Green Apple
    static const ThemePalette greenApplePalette = {
        nvgRGB(0x7A, 0xDB, 0x8F),       // accent: green
        nvgRGB(0xFF, 0xB3, 0xAC),       // secondary: coral/red
        nvgRGB(0x7A, 0xDB, 0x8F),       // headerText: green
        nvgRGB(0xBE, 0xCA, 0xBC),       // subtitle: on-surface-variant
        nvgRGBA(0x7A, 0xDB, 0x8F, 60),  // highlight: green glow
        nvgRGBA(0x18, 0x1D, 0x18, 255), // sidebar
        nvgRGBA(0x1C, 0x21, 0x1C, 255), // cardBg
        nvgRGBA(0x0A, 0x0F, 0x0B, 255), // dialogBg
        nvgRGB(0xFF, 0xB3, 0xAC),       // teal: coral tertiary
        nvgRGB(0x7A, 0xDB, 0x8F),       // status
        nvgRGB(0xBE, 0xCA, 0xBC),       // description
        nvgRGB(0x88, 0x94, 0x87),       // dimText
        nvgRGBA(0x1C, 0x21, 0x1C, 255), // rowBg
        nvgRGBA(0x01, 0x77, 0x37, 200), // activeRowBg: primary container
        nvgRGBA(0x26, 0x2B, 0x26, 200), // inactiveRowBg
        nvgRGBA(0x01, 0x77, 0x37, 255), // trackingBtn
        nvgRGBA(0x7A, 0xDB, 0x8F, 255), // ctaButton
        nvgRGB(0x31, 0x36, 0x30),       // sectionHeaderBg
        nvgRGBA(0x0F, 0x15, 0x10, 255), // readerBg
        nvgRGBA(0x0F, 0x15, 0x10, 200), // errorOverlayBg
        nvgRGBA(0x31, 0x36, 0x30, 255), // focusedRowBg
        nvgRGBA(0x0A, 0x0F, 0x0B, 255), // deepBg
        nvgRGBA(0x3F, 0x49, 0x3F, 100), // separatorColor
        nvgRGB(0xFF, 0xB3, 0xAC),       // successText: coral
        nvgRGB(0xDF, 0xE4, 0xDB),       // textColor
        nvgRGBA(0x26, 0x2B, 0x26, 255), // buttonColor
    };

    // Lavender purple
    static const ThemePalette lavenderPalette = {
        nvgRGB(0xA1, 0x77, 0xFF),       // accent: lavender
        nvgRGB(0xCD, 0xBD, 0xFF),       // secondary: light purple
        nvgRGB(0xA1, 0x77, 0xFF),       // headerText
        nvgRGB(0xCB, 0xC3, 0xD6),       // subtitle
        nvgRGBA(0xA1, 0x77, 0xFF, 60),  // highlight
        nvgRGBA(0x17, 0x15, 0x31, 255), // sidebar
        nvgRGBA(0x1D, 0x19, 0x3B, 255), // cardBg
        nvgRGBA(0x15, 0x13, 0x2D, 255), // dialogBg
        nvgRGB(0xCD, 0xBD, 0xFF),       // teal: light purple
        nvgRGB(0xA1, 0x77, 0xFF),       // status
        nvgRGB(0xCB, 0xC3, 0xD6),       // description
        nvgRGB(0x95, 0x8E, 0x9F),       // dimText
        nvgRGBA(0x1D, 0x19, 0x3B, 255), // rowBg
        nvgRGBA(0xA1, 0x77, 0xFF, 60),  // activeRowBg
        nvgRGBA(0x24, 0x1F, 0x41, 200), // inactiveRowBg
        nvgRGBA(0xA1, 0x77, 0xFF, 255), // trackingBtn
        nvgRGBA(0xA1, 0x77, 0xFF, 255), // ctaButton
        nvgRGB(0x28, 0x24, 0x46),       // sectionHeaderBg
        nvgRGBA(0x11, 0x11, 0x29, 255), // readerBg
        nvgRGBA(0x11, 0x11, 0x29, 200), // errorOverlayBg
        nvgRGBA(0x28, 0x24, 0x46, 255), // focusedRowBg
        nvgRGBA(0x15, 0x13, 0x2D, 255), // deepBg
        nvgRGBA(0x4A, 0x44, 0x53, 100), // separatorColor
        nvgRGB(0xCD, 0xBD, 0xFF),       // successText
        nvgRGB(0xE7, 0xE0, 0xEC),       // textColor
        nvgRGBA(0x24, 0x1F, 0x41, 255), // buttonColor
    };

    // Matrix neon green
    static const ThemePalette matrixPalette = {
        nvgRGB(0x00, 0xFF, 0x00),       // accent: neon green
        nvgRGB(0x00, 0xFF, 0x00),       // secondary: same green
        nvgRGB(0x00, 0xFF, 0x00),       // headerText: green
        nvgRGB(0x00, 0xCC, 0x00),       // subtitle: dimmer green
        nvgRGBA(0x00, 0xFF, 0x00, 60),  // highlight: green glow
        nvgRGBA(0x0A, 0x0A, 0x0A, 255), // sidebar: near-black
        nvgRGBA(0x21, 0x21, 0x21, 255), // cardBg: dark
        nvgRGBA(0x11, 0x11, 0x11, 255), // dialogBg: black
        nvgRGB(0xFF, 0xFF, 0xFF),       // teal: white (tertiary)
        nvgRGB(0x00, 0xFF, 0x00),       // status: green
        nvgRGB(0x00, 0xCC, 0x00),       // description: dimmer green
        nvgRGB(0x00, 0x88, 0x00),       // dimText: dark green
        nvgRGBA(0x21, 0x21, 0x21, 255), // rowBg
        nvgRGBA(0x00, 0xFF, 0x00, 40),  // activeRowBg: green tint
        nvgRGBA(0x21, 0x21, 0x21, 200), // inactiveRowBg
        nvgRGBA(0x00, 0xFF, 0x00, 255), // trackingBtn
        nvgRGBA(0x00, 0xFF, 0x00, 255), // ctaButton
        nvgRGB(0x1A, 0x1A, 0x1A),       // sectionHeaderBg
        nvgRGBA(0x11, 0x11, 0x11, 255), // readerBg
        nvgRGBA(0x11, 0x11, 0x11, 200), // errorOverlayBg
        nvgRGBA(0x2A, 0x2A, 0x2A, 255), // focusedRowBg
        nvgRGBA(0x08, 0x08, 0x08, 255), // deepBg
        nvgRGBA(0x00, 0xFF, 0x00, 40),  // separatorColor: green line
        nvgRGB(0x00, 0xFF, 0x00),       // successText
        nvgRGB(0xFF, 0xFF, 0xFF),       // textColor: white
        nvgRGBA(0x21, 0x21, 0x21, 255), // buttonColor
    };

    // Doom blood red
    static const ThemePalette doomPalette = {
        nvgRGB(0xFF, 0x00, 0x00),       // accent: red
        nvgRGB(0xFF, 0x00, 0x00),       // secondary: red
        nvgRGB(0xFF, 0x00, 0x00),       // headerText: red
        nvgRGB(0xCC, 0x00, 0x00),       // subtitle: darker red
        nvgRGBA(0xFF, 0x00, 0x00, 60),  // highlight: red glow
        nvgRGBA(0x0A, 0x0A, 0x0A, 255), // sidebar
        nvgRGBA(0x30, 0x30, 0x30, 255), // cardBg
        nvgRGBA(0x1B, 0x1B, 0x1B, 255), // dialogBg
        nvgRGB(0xBF, 0xBF, 0xBF),       // teal: gray tertiary
        nvgRGB(0xFF, 0x00, 0x00),       // status: red
        nvgRGB(0xCC, 0x00, 0x00),       // description: dark red
        nvgRGB(0x88, 0x00, 0x00),       // dimText: very dark red
        nvgRGBA(0x30, 0x30, 0x30, 255), // rowBg
        nvgRGBA(0xFF, 0x00, 0x00, 40),  // activeRowBg: red tint
        nvgRGBA(0x30, 0x30, 0x30, 200), // inactiveRowBg
        nvgRGBA(0xFF, 0x00, 0x00, 255), // trackingBtn
        nvgRGBA(0xFF, 0x00, 0x00, 255), // ctaButton
        nvgRGB(0x1A, 0x1A, 0x1A),       // sectionHeaderBg
        nvgRGBA(0x1B, 0x1B, 0x1B, 255), // readerBg
        nvgRGBA(0x1B, 0x1B, 0x1B, 200), // errorOverlayBg
        nvgRGBA(0x3A, 0x3A, 0x3A, 255), // focusedRowBg
        nvgRGBA(0x0A, 0x0A, 0x0A, 255), // deepBg
        nvgRGBA(0xFF, 0x00, 0x00, 40),  // separatorColor: red line
        nvgRGB(0xBF, 0xBF, 0xBF),       // successText: gray
        nvgRGB(0xFF, 0xFF, 0xFF),       // textColor
        nvgRGBA(0x30, 0x30, 0x30, 255), // buttonColor
    };

    // Mocha warm brown
    static const ThemePalette mochaPalette = {
        nvgRGB(0xFB, 0xB7, 0x7F),       // accent: warm orange (parsed from FFB77F)
        nvgRGB(0xF6, 0xBC, 0x70),       // secondary: golden
        nvgRGB(0xFB, 0xB7, 0x7F),       // headerText
        nvgRGB(0xD7, 0xC3, 0xB8),       // subtitle
        nvgRGBA(0xFB, 0xB7, 0x7F, 60),  // highlight
        nvgRGBA(0x22, 0x1A, 0x14, 255), // sidebar
        nvgRGBA(0x26, 0x1E, 0x18, 255), // cardBg
        nvgRGBA(0x14, 0x0D, 0x08, 255), // dialogBg
        nvgRGB(0xAE, 0xD1, 0x8D),       // teal: green tertiary
        nvgRGB(0xFB, 0xB7, 0x7F),       // status
        nvgRGB(0xD7, 0xC3, 0xB8),       // description
        nvgRGB(0x9F, 0x8D, 0x83),       // dimText
        nvgRGBA(0x26, 0x1E, 0x18, 255), // rowBg
        nvgRGBA(0x6C, 0x3A, 0x08, 200), // activeRowBg
        nvgRGBA(0x31, 0x28, 0x22, 200), // inactiveRowBg
        nvgRGBA(0x6C, 0x3A, 0x08, 255), // trackingBtn
        nvgRGBA(0xFB, 0xB7, 0x7F, 255), // ctaButton
        nvgRGB(0x3C, 0x33, 0x2D),       // sectionHeaderBg
        nvgRGBA(0x19, 0x12, 0x0C, 255), // readerBg
        nvgRGBA(0x19, 0x12, 0x0C, 200), // errorOverlayBg
        nvgRGBA(0x3C, 0x33, 0x2D, 255), // focusedRowBg
        nvgRGBA(0x14, 0x0D, 0x08, 255), // deepBg
        nvgRGBA(0x52, 0x44, 0x3C, 100), // separatorColor
        nvgRGB(0xAE, 0xD1, 0x8D),       // successText: green
        nvgRGB(0xF0, 0xDF, 0xD6),       // textColor
        nvgRGBA(0x31, 0x28, 0x22, 255), // buttonColor
    };

    // Sapphire blue
    static const ThemePalette sapphirePalette = {
        nvgRGB(0x1E, 0x88, 0xE5),       // accent: sapphire blue
        nvgRGB(0x1E, 0x88, 0xE5),       // secondary: same
        nvgRGB(0x1E, 0x88, 0xE5),       // headerText
        nvgRGB(0xBB, 0xBB, 0xBB),       // subtitle
        nvgRGBA(0x1E, 0x88, 0xE5, 60),  // highlight
        nvgRGBA(0x2A, 0x2A, 0x2A, 255), // sidebar
        nvgRGBA(0x42, 0x42, 0x42, 255), // cardBg
        nvgRGBA(0x21, 0x21, 0x21, 255), // dialogBg
        nvgRGB(0x21, 0x21, 0x21),       // teal: dark gray tertiary
        nvgRGB(0x1E, 0x88, 0xE5),       // status
        nvgRGB(0xBB, 0xBB, 0xBB),       // description
        nvgRGB(0x88, 0x88, 0x88),       // dimText
        nvgRGBA(0x42, 0x42, 0x42, 255), // rowBg
        nvgRGBA(0x1E, 0x88, 0xE5, 60),  // activeRowBg
        nvgRGBA(0x42, 0x42, 0x42, 200), // inactiveRowBg
        nvgRGBA(0x1E, 0x88, 0xE5, 255), // trackingBtn
        nvgRGBA(0x1E, 0x88, 0xE5, 255), // ctaButton
        nvgRGB(0x4A, 0x4A, 0x4A),       // sectionHeaderBg
        nvgRGBA(0x21, 0x21, 0x21, 255), // readerBg
        nvgRGBA(0x21, 0x21, 0x21, 200), // errorOverlayBg
        nvgRGBA(0x4A, 0x4A, 0x4A, 255), // focusedRowBg
        nvgRGBA(0x1A, 0x1A, 0x1A, 255), // deepBg
        nvgRGBA(0x1E, 0x88, 0xE5, 40),  // separatorColor: blue line
        nvgRGB(0x1E, 0x88, 0xE5),       // successText
        nvgRGB(0xFF, 0xFF, 0xFF),       // textColor
        nvgRGBA(0x42, 0x42, 0x42, 255), // buttonColor
    };

    // Cloudflare orange
    static const ThemePalette cloudflarePalette = {
        nvgRGB(0xF3, 0x80, 0x20),       // accent: cloudflare orange
        nvgRGB(0xF3, 0x80, 0x20),       // secondary: same
        nvgRGB(0xF3, 0x80, 0x20),       // headerText
        nvgRGB(0xBB, 0xBB, 0xBB),       // subtitle
        nvgRGBA(0xF3, 0x80, 0x20, 60),  // highlight
        nvgRGBA(0x2A, 0x2A, 0x2A, 255), // sidebar
        nvgRGBA(0x3F, 0x3F, 0x46, 255), // cardBg
        nvgRGBA(0x1B, 0x1B, 0x22, 255), // dialogBg
        nvgRGB(0x1B, 0x1B, 0x22),       // teal: dark tertiary
        nvgRGB(0xF3, 0x80, 0x20),       // status
        nvgRGB(0xBB, 0xBB, 0xBB),       // description
        nvgRGB(0x88, 0x88, 0x88),       // dimText
        nvgRGBA(0x3F, 0x3F, 0x46, 255), // rowBg
        nvgRGBA(0xF3, 0x80, 0x20, 60),  // activeRowBg
        nvgRGBA(0x3F, 0x3F, 0x46, 200), // inactiveRowBg
        nvgRGBA(0xF3, 0x80, 0x20, 255), // trackingBtn
        nvgRGBA(0xF3, 0x80, 0x20, 255), // ctaButton
        nvgRGB(0x4A, 0x4A, 0x4A),       // sectionHeaderBg
        nvgRGBA(0x1B, 0x1B, 0x22, 255), // readerBg
        nvgRGBA(0x1B, 0x1B, 0x22, 200), // errorOverlayBg
        nvgRGBA(0x4A, 0x4A, 0x4A, 255), // focusedRowBg
        nvgRGBA(0x15, 0x15, 0x1A, 255), // deepBg
        nvgRGBA(0xF3, 0x80, 0x20, 40),  // separatorColor: orange line
        nvgRGB(0xF3, 0x80, 0x20),       // successText
        nvgRGB(0xEF, 0xF2, 0xF5),       // textColor
        nvgRGBA(0x3F, 0x3F, 0x46, 255), // buttonColor
    };

    // Teal Turquoise
    static const ThemePalette tealTurquoisePalette = {
        nvgRGB(0x40, 0xE0, 0xD0),       // accent: turquoise
        nvgRGB(0xBF, 0x1F, 0x2F),       // secondary: red
        nvgRGB(0x40, 0xE0, 0xD0),       // headerText
        nvgRGB(0xBB, 0xCC, 0xCC),       // subtitle
        nvgRGBA(0x40, 0xE0, 0xD0, 60),  // highlight
        nvgRGBA(0x22, 0x2F, 0x31, 255), // sidebar
        nvgRGBA(0x23, 0x31, 0x33, 255), // cardBg
        nvgRGBA(0x20, 0x2C, 0x2E, 255), // dialogBg
        nvgRGB(0xBF, 0x1F, 0x2F),       // teal: red tertiary
        nvgRGB(0x40, 0xE0, 0xD0),       // status
        nvgRGB(0xBB, 0xCC, 0xCC),       // description
        nvgRGB(0x89, 0x93, 0x91),       // dimText
        nvgRGBA(0x23, 0x31, 0x33, 255), // rowBg
        nvgRGBA(0x40, 0xE0, 0xD0, 40),  // activeRowBg
        nvgRGBA(0x28, 0x38, 0x3A, 200), // inactiveRowBg
        nvgRGBA(0x40, 0xE0, 0xD0, 255), // trackingBtn
        nvgRGBA(0x40, 0xE0, 0xD0, 255), // ctaButton
        nvgRGB(0x2F, 0x42, 0x44),       // sectionHeaderBg
        nvgRGBA(0x20, 0x21, 0x25, 255), // readerBg
        nvgRGBA(0x20, 0x21, 0x25, 200), // errorOverlayBg
        nvgRGBA(0x2F, 0x42, 0x44, 255), // focusedRowBg
        nvgRGBA(0x20, 0x2C, 0x2E, 255), // deepBg
        nvgRGBA(0x89, 0x93, 0x91, 100), // separatorColor
        nvgRGB(0x40, 0xE0, 0xD0),       // successText
        nvgRGB(0xDF, 0xDE, 0xDA),       // textColor
        nvgRGBA(0x28, 0x38, 0x3A, 255), // buttonColor
    };

    // Tidal Wave ocean blue
    static const ThemePalette tidalWavePalette = {
        nvgRGB(0x5E, 0xD4, 0xFC),       // accent: sky blue
        nvgRGB(0x92, 0xF7, 0xBC),       // secondary: mint green
        nvgRGB(0x5E, 0xD4, 0xFC),       // headerText
        nvgRGB(0xBF, 0xC8, 0xCC),       // subtitle
        nvgRGBA(0x5E, 0xD4, 0xFC, 60),  // highlight
        nvgRGBA(0x07, 0x29, 0x47, 255), // sidebar
        nvgRGBA(0x08, 0x2B, 0x4B, 255), // cardBg
        nvgRGBA(0x07, 0x26, 0x42, 255), // dialogBg
        nvgRGB(0x92, 0xF7, 0xBC),       // teal: mint
        nvgRGB(0x5E, 0xD4, 0xFC),       // status
        nvgRGB(0xBF, 0xC8, 0xCC),       // description
        nvgRGB(0x8A, 0x92, 0x96),       // dimText
        nvgRGBA(0x08, 0x2B, 0x4B, 255), // rowBg
        nvgRGBA(0x00, 0x4D, 0x61, 200), // activeRowBg
        nvgRGBA(0x09, 0x32, 0x57, 200), // inactiveRowBg
        nvgRGBA(0x00, 0x4D, 0x61, 255), // trackingBtn
        nvgRGBA(0x5E, 0xD4, 0xFC, 255), // ctaButton
        nvgRGB(0x0A, 0x38, 0x61),       // sectionHeaderBg
        nvgRGBA(0x00, 0x1C, 0x3B, 255), // readerBg
        nvgRGBA(0x00, 0x1C, 0x3B, 200), // errorOverlayBg
        nvgRGBA(0x0A, 0x38, 0x61, 255), // focusedRowBg
        nvgRGBA(0x07, 0x26, 0x42, 255), // deepBg
        nvgRGBA(0x8A, 0x92, 0x96, 100), // separatorColor
        nvgRGB(0x92, 0xF7, 0xBC),       // successText: mint
        nvgRGB(0xD5, 0xE3, 0xFF),       // textColor
        nvgRGBA(0x09, 0x32, 0x57, 255), // buttonColor
    };

    // Yotsuba warm orange-red
    static const ThemePalette yotsubaPalette = {
        nvgRGB(0xFF, 0xB5, 0x9D),       // accent: peach/salmon
        nvgRGB(0xD7, 0xC6, 0x8D),       // secondary: golden
        nvgRGB(0xFF, 0xB5, 0x9D),       // headerText
        nvgRGB(0xD8, 0xC2, 0xBC),       // subtitle
        nvgRGBA(0xFF, 0xB5, 0x9D, 60),  // highlight
        nvgRGBA(0x31, 0x25, 0x21, 255), // sidebar
        nvgRGBA(0x33, 0x27, 0x23, 255), // cardBg
        nvgRGBA(0x2E, 0x22, 0x1F, 255), // dialogBg
        nvgRGB(0xD7, 0xC6, 0x8D),       // teal: golden
        nvgRGB(0xFF, 0xB5, 0x9D),       // status
        nvgRGB(0xD8, 0xC2, 0xBC),       // description
        nvgRGB(0xA0, 0x8C, 0x87),       // dimText
        nvgRGBA(0x33, 0x27, 0x23, 255), // rowBg
        nvgRGBA(0x86, 0x22, 0x00, 200), // activeRowBg
        nvgRGBA(0x41, 0x35, 0x31, 200), // inactiveRowBg
        nvgRGBA(0x86, 0x22, 0x00, 255), // trackingBtn
        nvgRGBA(0xFF, 0xB5, 0x9D, 255), // ctaButton
        nvgRGB(0x4C, 0x40, 0x3D),       // sectionHeaderBg
        nvgRGBA(0x21, 0x1A, 0x18, 255), // readerBg
        nvgRGBA(0x21, 0x1A, 0x18, 200), // errorOverlayBg
        nvgRGBA(0x4C, 0x40, 0x3D, 255), // focusedRowBg
        nvgRGBA(0x2E, 0x22, 0x1F, 255), // deepBg
        nvgRGBA(0xA0, 0x8C, 0x87, 100), // separatorColor
        nvgRGB(0xD7, 0xC6, 0x8D),       // successText: golden
        nvgRGB(0xED, 0xE0, 0xDD),       // textColor
        nvgRGBA(0x41, 0x35, 0x31, 255), // buttonColor
    };

    // Yin Yang white-on-dark
    static const ThemePalette yinYangPalette = {
        nvgRGB(0xFF, 0xFF, 0xFF),       // accent: white
        nvgRGB(0xFF, 0xFF, 0xFF),       // secondary: white
        nvgRGB(0xFF, 0xFF, 0xFF),       // headerText
        nvgRGB(0xD1, 0xD1, 0xD1),       // subtitle
        nvgRGBA(0xFF, 0xFF, 0xFF, 40),  // highlight
        nvgRGBA(0x2D, 0x2D, 0x2D, 255), // sidebar
        nvgRGBA(0x31, 0x31, 0x31, 255), // cardBg
        nvgRGBA(0x2A, 0x2A, 0x2A, 255), // dialogBg
        nvgRGB(0x00, 0x41, 0x9E),       // teal: blue accent from tertiary container
        nvgRGB(0xFF, 0xFF, 0xFF),       // status
        nvgRGB(0xD1, 0xD1, 0xD1),       // description
        nvgRGB(0x99, 0x99, 0x99),       // dimText
        nvgRGBA(0x31, 0x31, 0x31, 255), // rowBg
        nvgRGBA(0xFF, 0xFF, 0xFF, 30),  // activeRowBg: white tint
        nvgRGBA(0x38, 0x38, 0x38, 200), // inactiveRowBg
        nvgRGBA(0xFF, 0xFF, 0xFF, 255), // trackingBtn
        nvgRGBA(0xFF, 0xFF, 0xFF, 255), // ctaButton
        nvgRGB(0x3F, 0x3F, 0x3F),       // sectionHeaderBg
        nvgRGBA(0x1E, 0x1E, 0x1E, 255), // readerBg
        nvgRGBA(0x1E, 0x1E, 0x1E, 200), // errorOverlayBg
        nvgRGBA(0x3F, 0x3F, 0x3F, 255), // focusedRowBg
        nvgRGBA(0x2A, 0x2A, 0x2A, 255), // deepBg
        nvgRGBA(0x99, 0x99, 0x99, 80),  // separatorColor
        nvgRGB(0xFF, 0xFF, 0xFF),       // successText
        nvgRGB(0xE6, 0xE6, 0xE6),       // textColor
        nvgRGBA(0x38, 0x38, 0x38, 255), // buttonColor
    };

    // Monochrome pure B&W
    static const ThemePalette monochromePalette = {
        nvgRGB(0xFF, 0xFF, 0xFF),       // accent: white
        nvgRGB(0x77, 0x77, 0x77),       // secondary: gray
        nvgRGB(0xFF, 0xFF, 0xFF),       // headerText
        nvgRGB(0xAA, 0xAA, 0xAA),       // subtitle
        nvgRGBA(0xFF, 0xFF, 0xFF, 30),  // highlight
        nvgRGBA(0x00, 0x00, 0x00, 255), // sidebar: pure black
        nvgRGBA(0x00, 0x00, 0x00, 255), // cardBg: pure black
        nvgRGBA(0x00, 0x00, 0x00, 255), // dialogBg: pure black
        nvgRGB(0x77, 0x77, 0x77),       // teal: gray
        nvgRGB(0xFF, 0xFF, 0xFF),       // status: white
        nvgRGB(0xAA, 0xAA, 0xAA),       // description
        nvgRGB(0x77, 0x77, 0x77),       // dimText
        nvgRGBA(0x00, 0x00, 0x00, 255), // rowBg: black
        nvgRGBA(0xFF, 0xFF, 0xFF, 20),  // activeRowBg: white tint
        nvgRGBA(0x00, 0x00, 0x00, 200), // inactiveRowBg
        nvgRGBA(0xFF, 0xFF, 0xFF, 255), // trackingBtn
        nvgRGBA(0xFF, 0xFF, 0xFF, 255), // ctaButton
        nvgRGB(0x10, 0x10, 0x10),       // sectionHeaderBg
        nvgRGBA(0x00, 0x00, 0x00, 255), // readerBg
        nvgRGBA(0x00, 0x00, 0x00, 200), // errorOverlayBg
        nvgRGBA(0x10, 0x10, 0x10, 255), // focusedRowBg
        nvgRGBA(0x00, 0x00, 0x00, 255), // deepBg
        nvgRGBA(0xFF, 0xFF, 0xFF, 30),  // separatorColor
        nvgRGB(0xFF, 0xFF, 0xFF),       // successText
        nvgRGB(0xFF, 0xFF, 0xFF),       // textColor
        nvgRGBA(0x00, 0x00, 0x00, 255), // buttonColor
    };

    // Cotton Candy pink-teal
    static const ThemePalette cottonCandyPalette = {
        nvgRGB(0xFB, 0x3B, 0xB4),       // accent: pink (primary parsed)
        nvgRGB(0x80, 0xD4, 0xD8),       // secondary: teal
        nvgRGB(0xFB, 0x3B, 0xB4),       // headerText
        nvgRGB(0xD7, 0xC1, 0xC1),       // subtitle
        nvgRGBA(0xFB, 0x3B, 0xB4, 60),  // highlight
        nvgRGBA(0x22, 0x19, 0x1A, 255), // sidebar
        nvgRGBA(0x26, 0x1D, 0x1E, 255), // cardBg
        nvgRGBA(0x14, 0x0C, 0x0D, 255), // dialogBg
        nvgRGB(0xEB, 0xB5, 0xED),       // teal: light purple tertiary
        nvgRGB(0x80, 0xD4, 0xD8),       // status: teal
        nvgRGB(0xD7, 0xC1, 0xC1),       // description
        nvgRGB(0xA0, 0x8C, 0x8C),       // dimText
        nvgRGBA(0x26, 0x1D, 0x1E, 255), // rowBg
        nvgRGBA(0x73, 0x33, 0x36, 200), // activeRowBg
        nvgRGBA(0x31, 0x28, 0x28, 200), // inactiveRowBg
        nvgRGBA(0x73, 0x33, 0x36, 255), // trackingBtn
        nvgRGBA(0xFB, 0x3B, 0xB4, 255), // ctaButton
        nvgRGB(0x3D, 0x32, 0x33),       // sectionHeaderBg
        nvgRGBA(0x1A, 0x11, 0x11, 255), // readerBg
        nvgRGBA(0x1A, 0x11, 0x11, 200), // errorOverlayBg
        nvgRGBA(0x3D, 0x32, 0x33, 255), // focusedRowBg
        nvgRGBA(0x14, 0x0C, 0x0D, 255), // deepBg
        nvgRGBA(0x52, 0x43, 0x43, 100), // separatorColor
        nvgRGB(0xEB, 0xB5, 0xED),       // successText: purple
        nvgRGB(0xF0, 0xDE, 0xDF),       // textColor
        nvgRGBA(0x31, 0x28, 0x28, 255), // buttonColor
    };

    switch (theme) {
        case AppTheme::NEON_VAPORWAVE: return vaporwavePalette;
        case AppTheme::TACHIYOMI: return tachiyomiPalette;
        case AppTheme::CATPPUCCIN: return catppuccinPalette;
        case AppTheme::NORD: return nordPalette;
        case AppTheme::TAKO: return takoPalette;
        case AppTheme::MIDNIGHT_DUSK: return midnightDuskPalette;
        case AppTheme::STRAWBERRY: return strawberryPalette;
        case AppTheme::GREEN_APPLE: return greenApplePalette;
        case AppTheme::LAVENDER: return lavenderPalette;
        case AppTheme::MATRIX: return matrixPalette;
        case AppTheme::DOOM: return doomPalette;
        case AppTheme::MOCHA: return mochaPalette;
        case AppTheme::SAPPHIRE: return sapphirePalette;
        case AppTheme::CLOUDFLARE: return cloudflarePalette;
        case AppTheme::TEAL_TURQUOISE: return tealTurquoisePalette;
        case AppTheme::TIDAL_WAVE: return tidalWavePalette;
        case AppTheme::YOTSUBA: return yotsubaPalette;
        case AppTheme::YIN_YANG: return yinYangPalette;
        case AppTheme::MONOCHROME: return monochromePalette;
        case AppTheme::COTTON_CANDY: return cottonCandyPalette;
        default: return defaultPalette;
    }
}

NVGcolor Application::getAccentColor() const { return getPalette(m_settings.theme).accent; }
NVGcolor Application::getSecondaryColor() const { return getPalette(m_settings.theme).secondary; }
NVGcolor Application::getHeaderTextColor() const { return getPalette(m_settings.theme).headerText; }
NVGcolor Application::getSubtitleColor() const { return getPalette(m_settings.theme).subtitle; }
NVGcolor Application::getHighlightColor() const { return getPalette(m_settings.theme).highlight; }
NVGcolor Application::getSidebarColor() const { return getPalette(m_settings.theme).sidebar; }
NVGcolor Application::getCardBackground() const { return getPalette(m_settings.theme).cardBg; }
NVGcolor Application::getDialogBackground() const { return getPalette(m_settings.theme).dialogBg; }
NVGcolor Application::getTealColor() const { return getPalette(m_settings.theme).teal; }
NVGcolor Application::getStatusColor() const { return getPalette(m_settings.theme).status; }
NVGcolor Application::getDescriptionColor() const { return getPalette(m_settings.theme).description; }
NVGcolor Application::getDimTextColor() const { return getPalette(m_settings.theme).dimText; }
NVGcolor Application::getRowBackground() const { return getPalette(m_settings.theme).rowBg; }
NVGcolor Application::getActiveRowBackground() const { return getPalette(m_settings.theme).activeRowBg; }
NVGcolor Application::getInactiveRowBackground() const { return getPalette(m_settings.theme).inactiveRowBg; }
NVGcolor Application::getTrackingButtonColor() const { return getPalette(m_settings.theme).trackingBtn; }
NVGcolor Application::getCtaButtonColor() const { return getPalette(m_settings.theme).ctaButton; }
NVGcolor Application::getSectionHeaderBg() const { return getPalette(m_settings.theme).sectionHeaderBg; }
NVGcolor Application::getReaderBackground() const { return getPalette(m_settings.theme).readerBg; }
NVGcolor Application::getErrorOverlayBg() const { return getPalette(m_settings.theme).errorOverlayBg; }
NVGcolor Application::getFocusedRowBg() const { return getPalette(m_settings.theme).focusedRowBg; }
NVGcolor Application::getDeepBackground() const { return getPalette(m_settings.theme).deepBg; }
NVGcolor Application::getSeparatorColor() const { return getPalette(m_settings.theme).separatorColor; }
NVGcolor Application::getSuccessTextColor() const { return getPalette(m_settings.theme).successText; }
NVGcolor Application::getTextColor() const { return getPalette(m_settings.theme).textColor; }
NVGcolor Application::getButtonColor() const { return getPalette(m_settings.theme).buttonColor; }

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
        case AppTheme::NEON_VAPORWAVE: return "Neon Vaporwave";
        case AppTheme::TACHIYOMI: return "Tachiyomi";
        case AppTheme::CATPPUCCIN: return "Catppuccin";
        case AppTheme::NORD: return "Nord";
        case AppTheme::TAKO: return "Tako";
        case AppTheme::MIDNIGHT_DUSK: return "Midnight Dusk";
        case AppTheme::STRAWBERRY: return "Strawberry Daiquiri";
        case AppTheme::GREEN_APPLE: return "Green Apple";
        case AppTheme::LAVENDER: return "Lavender";
        case AppTheme::MATRIX: return "Matrix";
        case AppTheme::DOOM: return "Doom";
        case AppTheme::MOCHA: return "Mocha";
        case AppTheme::SAPPHIRE: return "Sapphire";
        case AppTheme::CLOUDFLARE: return "Cloudflare";
        case AppTheme::TEAL_TURQUOISE: return "Teal Turquoise";
        case AppTheme::TIDAL_WAVE: return "Tidal Wave";
        case AppTheme::YOTSUBA: return "Yotsuba";
        case AppTheme::YIN_YANG: return "Yin Yang";
        case AppTheme::MONOCHROME: return "Monochrome";
        case AppTheme::COTTON_CANDY: return "Cotton Candy";
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
        case PageScaleMode::ORIGINAL: return "Original (1:1)";
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

std::string Application::getDownloadQualityString(DownloadQuality quality) {
    switch (quality) {
        case DownloadQuality::ORIGINAL: return "Original";
        case DownloadQuality::HIGH: return "High (1280px)";
        case DownloadQuality::MEDIUM: return "Medium (960px)";
        case DownloadQuality::LOW: return "Low (720px)";
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
    if (static_cast<int>(m_settings.theme) < 0 || static_cast<int>(m_settings.theme) > 22) {
        m_settings.theme = AppTheme::DARK;
    }
    m_settings.showClock = extractBool("showClock", true);
    m_settings.debugLogging = extractBool("debugLogging", false);
    m_settings.showPerfOverlay = extractBool("showPerfOverlay", false);

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
    m_settings.goToEndOnPrevChapter = extractBool("goToEndOnPrevChapter", true);

    // Load webtoon settings
    m_settings.cropBorders = extractBool("cropBorders", false);
    m_settings.webtoonDetection = extractBool("webtoonDetection", true);
    m_settings.webtoonSidePadding = extractInt("webtoonSidePadding");
    if (m_settings.webtoonSidePadding < 0 || m_settings.webtoonSidePadding > 20) {
        m_settings.webtoonSidePadding = 0;
    }

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
    m_settings.downloadsOnlyMode = extractBool("downloadsOnlyMode", false);
    m_settings.librarySortMode = extractInt("librarySortMode");
    if (m_settings.librarySortMode < 0 || m_settings.librarySortMode > 10) m_settings.librarySortMode = 0;
    m_settings.chapterSortDescending = extractBool("chapterSortDescending", true);

    // Load download settings
    int downloadModeInt = extractInt("downloadMode");
    m_settings.downloadMode = static_cast<DownloadMode>(downloadModeInt);
    brls::Logger::info("loadSettings: downloadMode = {} (0=Server, 1=Local, 2=Both)", downloadModeInt);
    int downloadQualityInt = extractInt("downloadQuality");
    if (downloadQualityInt < 0 || downloadQualityInt > 3) downloadQualityInt = 0;
    m_settings.downloadQuality = static_cast<DownloadQuality>(downloadQualityInt);
    brls::Logger::info("loadSettings: downloadQuality = {} (0=Original, 1=High, 2=Medium, 3=Low)", downloadQualityInt);
    m_settings.autoDownloadChapters = extractBool("autoDownloadChapters", false);
    m_settings.deleteAfterRead = extractBool("deleteAfterRead", false);
    m_settings.autoResumeDownloads = extractBool("autoResumeDownloads", true);
    m_settings.pageCacheEnabled = extractBool("pageCacheEnabled", true);

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

    // Load source tags (sourceTags: { "sourceId": ["tag1", "tag2"], ... })
    m_settings.sourceTags.clear();
    size_t sourceTagsPos = content.find("\"sourceTags\"");
    if (sourceTagsPos != std::string::npos) {
        size_t objStart = content.find('{', sourceTagsPos + 12);
        if (objStart != std::string::npos) {
            // Find matching closing brace (handle nested arrays)
            int depth = 1;
            size_t objEnd = objStart + 1;
            while (objEnd < content.size() && depth > 0) {
                if (content[objEnd] == '{') depth++;
                else if (content[objEnd] == '}') depth--;
                objEnd++;
            }
            std::string tagsObj = content.substr(objStart + 1, objEnd - objStart - 2);

            // Parse each "sourceId": ["tag1", "tag2"] entry
            size_t searchPos = 0;
            while (searchPos < tagsObj.size()) {
                size_t keyStart = tagsObj.find('"', searchPos);
                if (keyStart == std::string::npos) break;
                size_t keyEnd = tagsObj.find('"', keyStart + 1);
                if (keyEnd == std::string::npos) break;
                std::string sourceId = tagsObj.substr(keyStart + 1, keyEnd - keyStart - 1);

                size_t arrStart = tagsObj.find('[', keyEnd);
                if (arrStart == std::string::npos) break;
                size_t arrEnd = tagsObj.find(']', arrStart);
                if (arrEnd == std::string::npos) break;

                std::string arrStr = tagsObj.substr(arrStart + 1, arrEnd - arrStart - 1);
                std::set<std::string> tags;
                size_t tPos = 0;
                while (tPos < arrStr.size()) {
                    size_t tStart = arrStr.find('"', tPos);
                    if (tStart == std::string::npos) break;
                    size_t tEnd = arrStr.find('"', tStart + 1);
                    if (tEnd == std::string::npos) break;
                    std::string tag = arrStr.substr(tStart + 1, tEnd - tStart - 1);
                    if (!tag.empty()) tags.insert(tag);
                    tPos = tEnd + 1;
                }
                if (!tags.empty()) {
                    m_settings.sourceTags[sourceId] = tags;
                }
                searchPos = arrEnd + 1;
            }
        }
    }

    // Load selected source tag filters
    m_settings.selectedSourceTagFilters.clear();
    size_t tagFilterPos = content.find("\"selectedSourceTagFilters\"");
    if (tagFilterPos != std::string::npos) {
        size_t arrStart = content.find('[', tagFilterPos);
        size_t arrEnd = content.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            std::string arrStr = content.substr(arrStart + 1, arrEnd - arrStart - 1);
            size_t tPos = 0;
            while (tPos < arrStr.size()) {
                size_t tStart = arrStr.find('"', tPos);
                if (tStart == std::string::npos) break;
                size_t tEnd = arrStr.find('"', tStart + 1);
                if (tEnd == std::string::npos) break;
                std::string tag = arrStr.substr(tStart + 1, tEnd - tStart - 1);
                if (!tag.empty()) m_settings.selectedSourceTagFilters.insert(tag);
                tPos = tEnd + 1;
            }
        }
    }

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

    // Load server image settings flag
    m_settings.serverImageSettingsApplied = extractBool("serverImageSettingsApplied", false);

    // Load library grid customization
    int displayModeInt = extractInt("libraryDisplayMode");
    if (displayModeInt >= 0 && displayModeInt <= 2) {
        m_settings.libraryDisplayMode = static_cast<LibraryDisplayMode>(displayModeInt);
    }
    int gridSizeInt = extractInt("libraryGridSize");
    if (gridSizeInt >= 0 && gridSizeInt <= 2) {
        m_settings.libraryGridSize = static_cast<LibraryGridSize>(gridSizeInt);
    }
    int listRowSizeInt = extractInt("listRowSize");
    if (listRowSizeInt >= 0 && listRowSizeInt <= 3) {
        m_settings.listRowSize = static_cast<ListRowSize>(listRowSizeInt);
    }
    int defaultSortInt = extractInt("defaultLibrarySortMode");
    if (defaultSortInt >= 0 && defaultSortInt <= 10) {
        m_settings.defaultLibrarySortMode = defaultSortInt;
    }
    int groupModeInt = extractInt("libraryGroupMode");
    if (groupModeInt >= 0 && groupModeInt <= 2) {
        m_settings.libraryGroupMode = static_cast<LibraryGroupMode>(groupModeInt);
    }
    brls::Logger::info("loadSettings: libraryDisplayMode={}, libraryGridSize={}, listRowSize={}, defaultSort={}, groupMode={}",
                       displayModeInt, gridSizeInt, listRowSizeInt, defaultSortInt, groupModeInt);

    // Load per-category sort modes
    m_settings.categorySortModes.clear();
    size_t catSortPos = content.find("\"categorySortModes\"");
    if (catSortPos != std::string::npos) {
        size_t openBrace = content.find('{', catSortPos);
        if (openBrace != std::string::npos) {
            size_t closeBrace = content.find('}', openBrace);
            if (closeBrace != std::string::npos) {
                std::string catSortJson = content.substr(openBrace + 1, closeBrace - openBrace - 1);
                // Parse each entry: "categoryId": sortMode
                size_t searchPos = 0;
                while (searchPos < catSortJson.length()) {
                    size_t quoteStart = catSortJson.find('"', searchPos);
                    if (quoteStart == std::string::npos) break;
                    size_t quoteEnd = catSortJson.find('"', quoteStart + 1);
                    if (quoteEnd == std::string::npos) break;
                    std::string catIdStr = catSortJson.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                    int categoryId = atoi(catIdStr.c_str());
                    size_t colonPos = catSortJson.find(':', quoteEnd);
                    if (colonPos != std::string::npos) {
                        int sortMode = atoi(catSortJson.c_str() + colonPos + 1);
                        if (sortMode >= -1 && sortMode <= 10) {
                            m_settings.categorySortModes[categoryId] = sortMode;
                        }
                    }
                    searchPos = quoteEnd + 1;
                }
                brls::Logger::debug("Loaded {} per-category sort modes", m_settings.categorySortModes.size());
            }
        }
    }

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

                            // Extract readerBackground
                            size_t rbPos = entryJson.find("\"readerBackground\"");
                            if (rbPos != std::string::npos) {
                                size_t colonPos = entryJson.find(':', rbPos);
                                if (colonPos != std::string::npos) {
                                    int val = atoi(entryJson.c_str() + colonPos + 1);
                                    if (val >= 0 && val <= 2) {
                                        settings.readerBackground = static_cast<ReaderBackground>(val);
                                    }
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

    // Load auth credentials (deobfuscate sensitive fields; handles legacy plain text)
    m_authUsername = extractString("authUsername");
    m_authPassword = deobfuscate(extractString("authPassword"));
    m_settings.authMode = extractInt("authMode");
    m_settings.accessToken = deobfuscate(extractString("accessToken"));
    m_settings.refreshToken = deobfuscate(extractString("refreshToken"));
    m_settings.sessionCookie = deobfuscate(extractString("sessionCookie"));

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

    // Restore session cookie for simple_login
    if (!m_settings.sessionCookie.empty()) {
        client.setSessionCookie(m_settings.sessionCookie);
        brls::Logger::info("Restored session cookie");
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

    // Auth credentials (obfuscated for security - not stored as plain text)
    json += "  \"authUsername\": \"" + m_authUsername + "\",\n";
    json += "  \"authPassword\": \"" + obfuscate(m_authPassword) + "\",\n";
    json += "  \"authMode\": " + std::to_string(m_settings.authMode) + ",\n";
    json += "  \"accessToken\": \"" + obfuscate(m_settings.accessToken) + "\",\n";
    json += "  \"refreshToken\": \"" + obfuscate(m_settings.refreshToken) + "\",\n";
    json += "  \"sessionCookie\": \"" + obfuscate(m_settings.sessionCookie) + "\",\n";

    // UI settings
    json += "  \"theme\": " + std::to_string(static_cast<int>(m_settings.theme)) + ",\n";
    json += "  \"showClock\": " + std::string(m_settings.showClock ? "true" : "false") + ",\n";
    json += "  \"debugLogging\": " + std::string(m_settings.debugLogging ? "true" : "false") + ",\n";
    json += "  \"showPerfOverlay\": " + std::string(m_settings.showPerfOverlay ? "true" : "false") + ",\n";

    // Reader settings
    json += "  \"readingMode\": " + std::to_string(static_cast<int>(m_settings.readingMode)) + ",\n";
    json += "  \"pageScaleMode\": " + std::to_string(static_cast<int>(m_settings.pageScaleMode)) + ",\n";
    json += "  \"readerBackground\": " + std::to_string(static_cast<int>(m_settings.readerBackground)) + ",\n";
    json += "  \"imageRotation\": " + std::to_string(m_settings.imageRotation) + ",\n";
    json += "  \"keepScreenOn\": " + std::string(m_settings.keepScreenOn ? "true" : "false") + ",\n";
    json += "  \"showPageNumber\": " + std::string(m_settings.showPageNumber ? "true" : "false") + ",\n";
    json += "  \"tapToNavigate\": " + std::string(m_settings.tapToNavigate ? "true" : "false") + ",\n";
    json += "  \"goToEndOnPrevChapter\": " + std::string(m_settings.goToEndOnPrevChapter ? "true" : "false") + ",\n";

    // Webtoon settings
    json += "  \"cropBorders\": " + std::string(m_settings.cropBorders ? "true" : "false") + ",\n";
    json += "  \"webtoonDetection\": " + std::string(m_settings.webtoonDetection ? "true" : "false") + ",\n";
    json += "  \"webtoonSidePadding\": " + std::to_string(m_settings.webtoonSidePadding) + ",\n";

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
    json += "  \"downloadsOnlyMode\": " + std::string(m_settings.downloadsOnlyMode ? "true" : "false") + ",\n";
    json += "  \"librarySortMode\": " + std::to_string(m_settings.librarySortMode) + ",\n";
    json += "  \"chapterSortDescending\": " + std::string(m_settings.chapterSortDescending ? "true" : "false") + ",\n";

    // Download settings
    json += "  \"downloadMode\": " + std::to_string(static_cast<int>(m_settings.downloadMode)) + ",\n";
    json += "  \"downloadQuality\": " + std::to_string(static_cast<int>(m_settings.downloadQuality)) + ",\n";
    json += "  \"autoDownloadChapters\": " + std::string(m_settings.autoDownloadChapters ? "true" : "false") + ",\n";
    json += "  \"deleteAfterRead\": " + std::string(m_settings.deleteAfterRead ? "true" : "false") + ",\n";
    json += "  \"autoResumeDownloads\": " + std::string(m_settings.autoResumeDownloads ? "true" : "false") + ",\n";
    json += "  \"pageCacheEnabled\": " + std::string(m_settings.pageCacheEnabled ? "true" : "false") + ",\n";

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

    // Source tags
    json += "  \"sourceTags\": {";
    {
        bool firstSource = true;
        for (const auto& [sourceId, tags] : m_settings.sourceTags) {
            if (tags.empty()) continue;
            if (!firstSource) json += ", ";
            firstSource = false;
            json += "\"" + sourceId + "\": [";
            bool firstTag = true;
            for (const auto& tag : tags) {
                if (!firstTag) json += ", ";
                firstTag = false;
                json += "\"" + tag + "\"";
            }
            json += "]";
        }
    }
    json += "},\n";

    // Selected source tag filters
    json += "  \"selectedSourceTagFilters\": [";
    {
        bool first = true;
        for (const auto& tag : m_settings.selectedSourceTagFilters) {
            if (!first) json += ", ";
            first = false;
            json += "\"" + tag + "\"";
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

    // Server image settings flag
    json += "  \"serverImageSettingsApplied\": " + std::string(m_settings.serverImageSettingsApplied ? "true" : "false") + ",\n";

    // Library grid customization
    json += "  \"libraryDisplayMode\": " + std::to_string(static_cast<int>(m_settings.libraryDisplayMode)) + ",\n";
    json += "  \"libraryGridSize\": " + std::to_string(static_cast<int>(m_settings.libraryGridSize)) + ",\n";
    json += "  \"listRowSize\": " + std::to_string(static_cast<int>(m_settings.listRowSize)) + ",\n";
    json += "  \"defaultLibrarySortMode\": " + std::to_string(m_settings.defaultLibrarySortMode) + ",\n";
    json += "  \"libraryGroupMode\": " + std::to_string(static_cast<int>(m_settings.libraryGroupMode)) + ",\n";

    // Per-category sort modes
    json += "  \"categorySortModes\": {";
    bool firstCatSort = true;
    for (const auto& pair : m_settings.categorySortModes) {
        if (!firstCatSort) json += ",";
        firstCatSort = false;
        json += "\"" + std::to_string(pair.first) + "\": " + std::to_string(pair.second);
    }
    json += "},\n";

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
        json += "\"isWebtoonFormat\": " + std::string(pair.second.isWebtoonFormat ? "true" : "false") + ", ";
        json += "\"readerBackground\": " + std::to_string(static_cast<int>(pair.second.readerBackground)) + "}";
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
