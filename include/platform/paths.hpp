#pragma once

/**
 * Platform-specific data directory abstraction.
 *
 * All persistent data (settings, cache, downloads, logs) is rooted under
 * PLATFORM_DATA_DIR. Use the helper macros / inline functions below instead
 * of hard-coding "ux0:" paths anywhere in the codebase.
 *
 * Supported platforms:
 *   PS Vita  – ux0:data/VitaSuwayomi
 *   Switch   – sdmc:/VitaSuwayomi
 *   Desktop  – ./VitaSuwayomi   (next to the executable)
 */

#include <string>

#if defined(__vita__)
    static constexpr const char* PLATFORM_DATA_DIR = "ux0:data/VitaSuwayomi";
#elif defined(__SWITCH__)
    static constexpr const char* PLATFORM_DATA_DIR = "sdmc:/VitaSuwayomi";
#elif defined(__ANDROID__)
    // App-private internal storage; does not require runtime storage permission.
    static constexpr const char* PLATFORM_DATA_DIR = "/data/data/com.vitasuwayomi.app/files/VitaSuwayomi";
#else
    static constexpr const char* PLATFORM_DATA_DIR = "./VitaSuwayomi";
#endif

/**
 * Build a full path rooted at the platform data directory.
 * Example: platformPath("downloads") -> "ux0:data/VitaSuwayomi/downloads"
 */
inline std::string platformPath(const char* relative) {
    return std::string(PLATFORM_DATA_DIR) + "/" + relative;
}

inline std::string platformPath(const std::string& relative) {
    return std::string(PLATFORM_DATA_DIR) + "/" + relative;
}

/**
 * Returns true when a URL / path string looks like a local file on the
 * current platform, rather than an HTTP(S) URL.
 *
 * Vita  : paths start with ux0:, ur0:, uma0:, imc0:, or absolute /
 * Switch: paths start with sdmc:/ or absolute /
 * Desktop: absolute paths (/) or paths under the data dir
 */
inline bool isPlatformLocalPath(const std::string& url) {
    if (url.empty()) return false;
#if defined(__vita__)
    return url.find("ux0:") == 0 ||
           url.find("ur0:") == 0 ||
           url.find("uma0:") == 0 ||
           url.find("imc0:") == 0 ||
           url[0] == '/';
#elif defined(__SWITCH__)
    return url.find("sdmc:/") == 0 || url[0] == '/';
#else
    // Desktop: absolute path or anything under our data dir
    return url[0] == '/' ||
           url.find(PLATFORM_DATA_DIR) == 0;
#endif
}
