/**
 * Platform implementation: Desktop (Linux, macOS, Windows)
 */

#include "platform/platform.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <filesystem>
#include <sys/stat.h>

namespace platform {

// ── Paths ──────────────────────────────────────────────────────────────

const std::string& dataDir() {
    static std::string s_dir;
    if (s_dir.empty()) {
        const char* home = std::getenv("HOME");
        if (home && *home) {
            s_dir = std::string(home) + "/.local/share/VitaSuwayomi";
        } else {
            s_dir = "./VitaSuwayomi";
        }
    }
    return s_dir;
}

std::string path(const char* relative) {
    return dataDir() + "/" + relative;
}

std::string path(const std::string& relative) {
    return dataDir() + "/" + relative;
}

bool isLocalPath(const std::string& url) {
    if (url.empty()) return false;
    return url[0] == '/' || url.find(dataDir()) == 0;
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
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool deleteFile(const std::string& path) {
    return std::remove(path.c_str()) == 0;
}

int64_t fileSize(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.good()) return -1;
    return f.tellg();
}

bool createDir(const std::string& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) return true;
    std::filesystem::create_directory(path, ec);
    return !ec;
}

bool createDirRecursive(const std::string& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) return true;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

bool removeDir(const std::string& path) {
    return rmdir(path.c_str()) == 0;
}

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> result;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        result.emplace_back(entry.path().filename().string());
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
