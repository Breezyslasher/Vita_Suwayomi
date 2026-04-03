/**
 * VitaSuwayomi - Platform abstraction layer
 *
 * Provides a clean interface for all platform-varying functionality.
 * Each platform has its own .cpp implementing these functions,
 * selected at build time via CMakeLists.txt.
 *
 * Supported platforms:
 *   PS Vita    – platform_vita.cpp
 *   Switch     – platform_switch.cpp
 *   PS4        – platform_ps4.cpp
 *   Android    – platform_android.cpp
 *   Desktop    – platform_desktop.cpp
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace platform {

// ── Paths ──────────────────────────────────────────────────────────────

/// Root data directory for all persistent storage.
/// e.g. "ux0:data/VitaSuwayomi", "sdmc:/VitaSuwayomi", "$HOME/.local/share/VitaSuwayomi"
const std::string& dataDir();

/// Build a full path rooted at the data directory.
/// e.g. platformPath("downloads") → "ux0:data/VitaSuwayomi/downloads"
std::string path(const char* relative);
std::string path(const std::string& relative);

/// Returns true when a URL looks like a local file path on this platform.
bool isLocalPath(const std::string& url);

// ── File I/O ───────────────────────────────────────────────────────────

/// Read an entire file into memory. Returns empty vector on failure.
std::vector<uint8_t> readFile(const std::string& path);

/// Write raw bytes to a file (creates/truncates). Returns true on success.
bool writeFile(const std::string& path, const void* data, size_t size);

/// Write a string to a file (creates/truncates). Returns true on success.
bool writeFile(const std::string& path, const std::string& content);

/// Check if a file or directory exists.
bool fileExists(const std::string& path);

/// Delete a single file. Returns true on success.
bool deleteFile(const std::string& path);

/// Get file size in bytes. Returns -1 if file doesn't exist.
int64_t fileSize(const std::string& path);

/// Create a single directory (not recursive). Returns true on success or already exists.
bool createDir(const std::string& path);

/// Create directories recursively. Returns true on success.
bool createDirRecursive(const std::string& path);

/// Remove an empty directory. Returns true on success.
bool removeDir(const std::string& path);

/// List file names in a directory. Returns names only (not full paths).
std::vector<std::string> listDir(const std::string& dir);

/// Download callback: receives chunks of data, returns false to abort.
using WriteCallback = std::function<bool(const char* data, size_t size)>;

/// Open a file for streamed writing and invoke the writer callback.
/// Used by HTTP download-to-file. Deletes partial file on failure.
bool writeFileStreamed(const std::string& path, std::function<bool(WriteCallback)> writer);

// ── Threading ──────────────────────────────────────────────────────────

/// Launch a detached background thread with platform-appropriate stack size.
/// Switch: 512KB stack (for curl+mbedTLS). Vita: SDK thread. Others: std::thread.
void launchThread(std::function<void()> task);

/// Launch a thread with a large stack (256KB+ on Vita, 512KB on Switch).
void launchLargeStackThread(std::function<void()> task);

/// Platform-safe condition variable wait with timeout.
/// On Switch, polls instead of using wait_for (pthread_cond_timedwait is ENOSYS).
/// Returns true if predicate became true, false on timeout.
bool condWaitFor(std::mutex& mtx, std::unique_lock<std::mutex>& lock,
                 int milliseconds, std::function<bool()> predicate);

// ── Display / Image Constraints ────────────────────────────────────────

struct ImageConstraints {
    int coverWidth;      // Cover thumbnail width in pixels
    int coverHeight;     // Cover thumbnail height in pixels
    int gridCellWidth;   // Grid cell width for layout
    int gridCellHeight;  // Grid cell height for layout
};

/// Get platform-appropriate image sizing constraints.
const ImageConstraints& imageConstraints();

} // namespace platform
