/**
 * Platform implementation: Nintendo Switch
 */

#include "platform/platform.hpp"

#include <fstream>
#include <thread>
#include <mutex>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>

namespace platform {

// ── Paths ──────────────────────────────────────────────────────────────

static constexpr const char* DATA_DIR = "sdmc:/VitaSuwayomi";

const std::string& dataDir() {
    static const std::string s(DATA_DIR);
    return s;
}

std::string path(const char* relative) {
    return std::string(DATA_DIR) + "/" + relative;
}

std::string path(const std::string& relative) {
    return std::string(DATA_DIR) + "/" + relative;
}

bool isLocalPath(const std::string& url) {
    if (url.empty()) return false;
    return url.find("sdmc:/") == 0 || url[0] == '/';
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
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.good()) return -1;
    return f.tellg();
}

bool createDir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return true;
    return mkdir(path.c_str(), 0777) == 0 || errno == EEXIST;
}

bool createDirRecursive(const std::string& path) {
    // Simple recursive mkdir
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
    auto* taskPtr = new std::function<void()>(std::move(task));
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 0x80000); // 512 KB for curl+mbedTLS
    int rc = pthread_create(&tid, &attr, [](void* arg) -> void* {
        auto* fn = static_cast<std::function<void()>*>(arg);
        (*fn)();
        delete fn;
        return nullptr;
    }, taskPtr);
    pthread_attr_destroy(&attr);
    if (rc == 0) {
        pthread_detach(tid);
    } else {
        auto t = std::move(*taskPtr);
        delete taskPtr;
        std::thread([t]() { t(); }).detach();
    }
}

void launchLargeStackThread(std::function<void()> task) {
    launchThread(std::move(task)); // Already uses 512KB stack
}

bool condWaitFor(std::mutex& mtx, std::unique_lock<std::mutex>& lock,
                 int milliseconds, std::function<bool()> predicate) {
    // On Switch/libnx, pthread_cond_timedwait returns ENOSYS.
    // Poll instead of using condition_variable::wait_for().
    if (predicate()) return true;
    lock.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds > 50 ? 50 : milliseconds));
    lock.lock();
    return predicate();
}

// ── Display / Image Constraints ────────────────────────────────────────

const ImageConstraints& imageConstraints() {
    static const ImageConstraints c{
        .coverWidth = 210,
        .coverHeight = 294,
        .gridCellWidth = 143,
        .gridCellHeight = 200,
    };
    return c;
}

} // namespace platform
