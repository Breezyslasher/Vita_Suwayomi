/**
 * VitaSuwayomi - vita-shell-companion bridge (PS Vita only)
 */

#include "platform/shell_companion.hpp"

#include <borealis.hpp>

#include "shell_companion.h"
#include "platform/platform.hpp"
#include "utils/vita_install.hpp"

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
    brls::Logger::info("ShellCompanion: scDownloadsTakeBack -> {}", tb);

    char what[256] = {0};
    const int rc = scOpen(what, sizeof(what));
    s_open = (rc == 0);
    s_openInfo = what[0] ? what : (s_open ? "open" : "not open");
    if (s_open) {
        brls::Logger::info("ShellCompanion: open ({})", s_openInfo);
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

} // namespace shellcomp
} // namespace vitasuwayomi
