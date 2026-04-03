/**
 * Platform implementation: PS Vita
 */

#include "platform/platform.hpp"

#include <mutex>
#include <thread>
#include <condition_variable>
#include <cstring>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/kernel/threadmgr.h>

namespace platform {

// ── Paths ──────────────────────────────────────────────────────────────

static constexpr const char* DATA_DIR = "ux0:data/VitaSuwayomi";

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
    return url.find("ux0:") == 0 ||
           url.find("ur0:") == 0 ||
           url.find("uma0:") == 0 ||
           url.find("imc0:") == 0 ||
           url[0] == '/';
}

// ── File I/O ───────────────────────────────────────────────────────────

std::vector<uint8_t> readFile(const std::string& path) {
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return {};

    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    if (size <= 0 || size > 64 * 1024 * 1024) {
        sceIoClose(fd);
        return {};
    }

    std::vector<uint8_t> data(static_cast<size_t>(size));
    SceSSize bytesRead = sceIoRead(fd, data.data(), static_cast<SceSize>(size));
    sceIoClose(fd);

    if (bytesRead != static_cast<SceSSize>(size)) return {};
    return data;
}

bool writeFile(const std::string& path, const void* data, size_t size) {
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return false;
    SceSSize written = sceIoWrite(fd, data, static_cast<SceSize>(size));
    sceIoClose(fd);
    return written == static_cast<SceSSize>(size);
}

bool writeFile(const std::string& path, const std::string& content) {
    return writeFile(path, content.data(), content.size());
}

bool fileExists(const std::string& path) {
    SceIoStat stat;
    return sceIoGetstat(path.c_str(), &stat) >= 0;
}

bool deleteFile(const std::string& path) {
    return sceIoRemove(path.c_str()) >= 0;
}

int64_t fileSize(const std::string& path) {
    SceIoStat stat;
    if (sceIoGetstat(path.c_str(), &stat) >= 0) {
        return stat.st_size;
    }
    return -1;
}

bool createDir(const std::string& path) {
    SceIoStat stat;
    if (sceIoGetstat(path.c_str(), &stat) >= 0) return true;
    int ret = sceIoMkdir(path.c_str(), 0777);
    return ret >= 0 || ret == 0x80010011; // Success or already exists
}

bool createDirRecursive(const std::string& path) {
    if (path.empty()) return false;
    if (createDir(path)) return true;
    // Find parent separator (handle both / and : in Vita paths)
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0) return false;
    if (!createDirRecursive(path.substr(0, pos))) return false;
    return createDir(path);
}

bool removeDir(const std::string& path) {
    return sceIoRmdir(path.c_str()) >= 0;
}

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> result;
    SceUID dfd = sceIoDopen(dir.c_str());
    if (dfd >= 0) {
        SceIoDirent entry;
        while (sceIoDread(dfd, &entry) > 0) {
            result.emplace_back(entry.d_name);
        }
        sceIoDclose(dfd);
    }
    return result;
}

bool writeFileStreamed(const std::string& filePath, std::function<bool(WriteCallback)> writer) {
    SceUID fd = sceIoOpen(filePath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return false;

    bool success = writer([fd](const char* data, size_t size) -> bool {
        SceSSize written = sceIoWrite(fd, data, static_cast<SceSize>(size));
        return written == static_cast<SceSSize>(size);
    });

    sceIoClose(fd);
    if (!success) sceIoRemove(filePath.c_str());
    return success;
}

// ── Threading ──────────────────────────────────────────────────────────

struct VitaThreadData {
    std::function<void()> task;
};

static int vitaThreadEntry(SceSize args, void* argp) {
    (void)args;
    VitaThreadData* data = *static_cast<VitaThreadData**>(argp);
    if (data && data->task) {
        data->task();
    }
    delete data;
    return sceKernelExitDeleteThread(0);
}

void launchThread(std::function<void()> task) {
    std::thread([task]() { task(); }).detach();
}

void launchLargeStackThread(std::function<void()> task) {
    VitaThreadData* data = new VitaThreadData();
    data->task = std::move(task);

    SceUID thid = sceKernelCreateThread("asyncLargeStack", vitaThreadEntry,
                                        0x10000100, 0x40000, 0, 0, NULL); // 256KB
    if (thid >= 0) {
        VitaThreadData* dataPtr = data;
        sceKernelStartThread(thid, sizeof(dataPtr), &dataPtr);
    } else {
        delete data;
        launchThread(data->task);
    }
}

bool condWaitFor(std::mutex& mtx, std::unique_lock<std::mutex>& lock,
                 int milliseconds, std::function<bool()> predicate) {
    static std::condition_variable cv;
    return cv.wait_for(lock, std::chrono::milliseconds(milliseconds), predicate);
}

// ── Display / Image Constraints ────────────────────────────────────────

const ImageConstraints& imageConstraints() {
    static const ImageConstraints c{
        .coverWidth = 120,
        .coverHeight = 168,
        .gridCellWidth = 96,
        .gridCellHeight = 134,
    };
    return c;
}

} // namespace platform
