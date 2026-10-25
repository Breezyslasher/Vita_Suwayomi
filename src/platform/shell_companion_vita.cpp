/**
 * VitaSuwayomi - vita-shell-companion bridge (PS Vita only)
 */

#include "platform/shell_companion.hpp"

#include <borealis.hpp>

#include "shell_companion.h"
#include "platform/platform.hpp"
#include "utils/vita_install.hpp"

#include <chrono>
#include <cstdio>

namespace vitasuwayomi {
namespace shellcomp {

namespace {

constexpr const char* kTitleId = "VSWY00001";
constexpr const char* kDataDir = "ux0:data/VitaSuwayomi";
constexpr const char* kName = "Suwayomi";
constexpr const char* kBundledPlugin = "app0:module/shell_companion.suprx";

bool s_open = false;
std::string s_openInfo = "not started";
bool s_takeBackReached = false;
bool s_pages = false;
std::chrono::steady_clock::time_point s_lastProgress;
bool s_progressShown = false;

} // namespace

void startup() {
    if (scInit(kTitleId, kDataDir, kName, SC_FEAT_DOWNLOADS | SC_FEAT_UPDATE, kBundledPlugin) != 0) {
        s_openInfo = "scInit failed";
        brls::Logger::error("ShellCompanion: scInit failed");
        return;
    }

    // Before anything reads or writes the download files: a plugin still
    // carrying on our downloads from the last run stops after the piece it is
    // writing. Non-zero just means no plugin is in SceShell yet, so nothing
    // was carried on.
    const int tb = scDownloadsTakeBack();
    s_takeBackReached = (tb == 0);
    brls::Logger::info("ShellCompanion: scDownloadsTakeBack -> 0x{:08X} ({})", (unsigned)tb,
                       s_takeBackReached ? "a plugin was there" : "no plugin in SceShell yet");

    char what[256] = {0};
    const int rc = scOpen(what, sizeof(what));
    s_open = (rc == 0);
    s_openInfo = what[0] ? what : (s_open ? "open" : "not open");
    if (s_open) {
        brls::Logger::info("ShellCompanion: open ({})", s_openInfo);
        // The plugin in SceShell is the first one loaded this boot, maybe an
        // older one some other app shipped. "pages" downloads came in 0.2.0;
        // a plugin before that never answers.
        ScPluginInfo info;
        if (scPluginInfo(&info) == 0) {
            s_pages = info.versionCode >= 200;
            brls::Logger::info("ShellCompanion: plugin {} (protocol {}, features 0x{:X} of which ours 0x{:X}, caps {})",
                               info.version, info.protocol, info.features, info.granted, info.caps);
        } else {
            brls::Logger::info("ShellCompanion: plugin older than 0.2.0: downloads go a page an item");
        }
    } else {
        brls::Logger::warning("ShellCompanion: scOpen -> 0x{:08X} ({})", (unsigned)rc, s_openInfo);
    }
}

bool isOpen() { return s_open; }

bool installUpdate(const std::string& vpkPath, std::string& err) {
    if (!s_open) { err = "the shell companion plugin isn't loaded"; return false; }

    const std::string dir = platform::path(SC_UPDATE_DIR);
    if (vita::prepareVpk(vpkPath, dir, err) != 0) return false;

    // A result left by an earlier update would be read as this one's.
    char resultPath[256];
    if (scPath(SC_UPDATE_RESULT, resultPath, sizeof(resultPath)) == 0) platform::deleteFile(resultPath);

    const int rc = scUpdateInstall();
    if (rc != 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "scUpdateInstall failed (0x%08X)", (unsigned)rc);
        err = buf;
        vita::removeTree(dir);
        return false;
    }
    brls::Logger::info("ShellCompanion: update handed to the plugin ({})", dir);
    return true;
}

std::string takeUpdateResult(bool& failed) {
    failed = false;
    int result = 0;
    char why[256] = {0};
    if (scReadUpdateResult(&result, why, sizeof(why)) != 0) return {};

    char resultPath[256];
    if (scPath(SC_UPDATE_RESULT, resultPath, sizeof(resultPath)) == 0) platform::deleteFile(resultPath);
    // The installer consumes the folder when it succeeds; after a failure it
    // is left behind, and it is as big as the app.
    vita::removeTree(platform::path(SC_UPDATE_DIR));

    if (result == 0) {
        brls::Logger::info("ShellCompanion: update installed");
        return "Updated to " VITA_SUWAYOMI_DISPLAY_VERSION;
    }
    failed = true;
    char buf[400];
    std::snprintf(buf, sizeof(buf), "The update could not be installed (0x%08X)%s%s", (unsigned)result,
                  why[0] ? ": " : "", why);
    brls::Logger::error("ShellCompanion: {}", buf);
    return buf;
}

const std::string& openInfo() { return s_openInfo; }

bool takeBackReached() { return s_takeBackReached; }

bool pagesDownloads() { return s_open && s_pages; }

void downloadProgress(const std::string& line1, const std::string& line2, uint32_t permille) {
    if (!s_open) return;
    const auto now = std::chrono::steady_clock::now();
    if (s_progressShown && now - s_lastProgress < std::chrono::seconds(1)) return;
    s_lastProgress = now;
    s_progressShown = true;
    scDownloadProgress(line1.c_str(), line2.c_str(), permille > 1000 ? 1000 : permille);
}

void downloadProgressEnd(const std::string& line1, const std::string& line2) {
    if (!s_open || !s_progressShown) return;
    s_progressShown = false;
    scDownloadProgressEnd(line1.c_str(), line2.c_str());
}

bool downloadPauseAsked() { return s_open && scDownloadPauseAsked() == 1; }

bool handOverDownloads(const std::string& headers, const std::vector<HandOverItem>& items,
                       std::string& err) {
    if (!s_open) { err = "the shell companion plugin isn't loaded"; return false; }
    if (items.empty()) { err = "nothing to hand over"; return false; }
    const size_t n = items.size() < kMaxHandOverItems ? items.size() : kMaxHandOverItems;
    std::vector<ScDownload> list(n);
    for (size_t i = 0; i < n; i++) {
        ScDownload& d = list[i];
        d.id = items[i].id.c_str();
        d.file = items[i].file.c_str();
        d.size = 0;                       // a page's size isn't known up front
        d.title = items[i].title.c_str();
        d.kind = items[i].pages.empty() ? "url" : "pages";
        d.args[0] = items[i].url.c_str();
        d.args[1] = items[i].pages.empty() ? nullptr : items[i].pages.c_str();
        d.args[2] = nullptr;
    }
    if (scWriteDownloads(1, headers.c_str(), list.data(), (uint32_t)n) != 0) {
        err = "could not write " SC_DOWNLOADS;
        return false;
    }
    const int rc = scDownloadsStart();
    if (rc != 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "scDownloadsStart failed (0x%08X)", (unsigned)rc);
        err = buf;
        return false;
    }
    brls::Logger::info("ShellCompanion: {} downloads handed over", n);
    return true;
}

bool readDownloadsStatus(bool& running, std::vector<DownloadStatusItem>& items) {
    items.clear();
    running = false;
    std::vector<ScDownloadStatus> raw(kMaxHandOverItems);
    int run = 0, paused = 0;
    uint32_t count = 0;
    if (scReadDownloadsStatus(&run, &paused, raw.data(), (uint32_t)raw.size(), &count) != 0) return false;
    running = run != 0;
    for (uint32_t i = 0; i < count && i < raw.size(); i++) {
        DownloadStatusItem it;
        it.id = raw[i].id;
        it.state = raw[i].state;
        it.bytes = raw[i].bytes;
        it.pagesDone = raw[i].pagesDone;
        it.pageCount = raw[i].pageCount;
        items.push_back(std::move(it));
    }
    return true;
}

bool hasDownloadList() {
    char p[256];
    return scPath(SC_DOWNLOADS, p, sizeof(p)) == 0 && platform::fileExists(p);
}

void clearDownloadFiles() {
    char p[256];
    if (scPath(SC_DOWNLOADS, p, sizeof(p)) == 0) platform::deleteFile(p);
    if (scPath(SC_DOWNLOADS_STATUS, p, sizeof(p)) == 0) platform::deleteFile(p);
}

} // namespace shellcomp
} // namespace vitasuwayomi
