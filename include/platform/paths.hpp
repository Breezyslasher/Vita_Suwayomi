#pragma once

/**
 * Platform-specific path abstraction.
 *
 * This header now delegates to the platform abstraction layer.
 * Legacy names (PLATFORM_DATA_DIR, platformPath, isPlatformLocalPath)
 * are kept as thin wrappers for backward compatibility.
 */

#include "platform/platform.hpp"

// Legacy constant — code that needs compile-time paths should migrate to
// platform::dataDir().  This macro evaluates at runtime on Android/Desktop.
#define PLATFORM_DATA_DIR (platform::dataDir().c_str())

inline std::string platformPath(const char* relative) {
    return platform::path(relative);
}

inline std::string platformPath(const std::string& relative) {
    return platform::path(relative);
}

inline bool isPlatformLocalPath(const std::string& url) {
    return platform::isLocalPath(url);
}
