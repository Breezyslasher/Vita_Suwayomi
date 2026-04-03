/**
 * Platform implementation: Android
 */

#include "platform/platform.hpp"

#include <SDL2/SDL.h>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

namespace platform {

// ── Paths ──────────────────────────────────────────────────────────────

const std::string& dataDir() {
    static std::string s_dataDir;
    if (s_dataDir.empty()) {
        const char* internalPath = SDL_AndroidGetInternalStoragePath();
        if (internalPath && internalPath[0] != '\0') {
            s_dataDir = std::string(internalPath) + "/VitaSuwayomi";
        } else {
            s_dataDir = "/sdcard/VitaSuwayomi";
        }
    }
    return s_dataDir;
}

std::string path(const char* relative) {
    return dataDir() + "/" + relative;
}

std::string path(const std::string& relative) {
    return dataDir() + "/" + relative;
}

bool isLocalPath(const std::string& url) {
    if (url.empty()) return false;
    return url[0] == '/';
}

// ── File I/O ───────────────────────────────────────────────────────────

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    auto size = file.tellg();
    if (size <= 0) return {};
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) return {};
    return data;
}

bool writeFile(const std::string& path, const void* data, size_t size) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    file.write(static_cast<const char*>(data), size);
    return file.good();
}

bool writeFile(const std::string& path, const std::string& content) {
    return writeFile(path, content.data(), content.size());
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool deleteFile(const std::string& path) {
    return std::remove(path.c_str()) == 0;
}

int64_t fileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return st.st_size;
    }
    return -1;
}

bool createDir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return true;
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

bool createDirRecursive(const std::string& path) {
    if (path.empty()) return false;
    if (createDir(path)) return true;
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0) return false;
    if (!createDirRecursive(path.substr(0, pos))) return false;
    return createDir(path);
}

bool removeDir(const std::string& path) {
    return rmdir(path.c_str()) == 0;
}

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> result;
    DIR* d = opendir(dir.c_str());
    if (d) {
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            result.emplace_back(entry->d_name);
        }
        closedir(d);
    }
    return result;
}

bool writeFileStreamed(const std::string& filePath, std::function<bool(WriteCallback)> writer) {
    FILE* fp = fopen(filePath.c_str(), "wb");
    if (!fp) return false;

    bool success = writer([fp](const char* data, size_t size) -> bool {
        return fwrite(data, 1, size, fp) == size;
    });

    fclose(fp);
    if (!success) std::remove(filePath.c_str());
    return success;
}

// ── Threading ──────────────────────────────────────────────────────────

void launchThread(std::function<void()> task) {
    std::thread([task]() { task(); }).detach();
}

void launchLargeStackThread(std::function<void()> task) {
    launchThread(std::move(task));
}

bool condWaitFor(std::mutex& mtx, std::unique_lock<std::mutex>& lock,
                 int milliseconds, std::function<bool()> predicate) {
    static std::condition_variable cv;
    return cv.wait_for(lock, std::chrono::milliseconds(milliseconds), predicate);
}

// ── Display / Image Constraints ────────────────────────────────────────

const ImageConstraints& imageConstraints() {
    static const ImageConstraints c{
        .coverWidth = 300,
        .coverHeight = 420,
        .gridCellWidth = 185,
        .gridCellHeight = 260,
    };
    return c;
}

} // namespace platform
