/**
 * VitaSuwayomi - vita-shell-companion bridge (PS Vita only)
 *
 * The app's one connection to the SceShell plugin from
 * lib/vita-shell-companion (docs/PROTOCOL.md there), which carries chapter
 * downloads on after the app closes and installs self-updates. Everything
 * else in the app goes through here rather than the C client directly.
 */

#pragma once

#if defined(__vita__) || defined(__PSV__)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vitasuwayomi {
namespace shellcomp {

// At start-up, before the downloads manager touches its files: who the app
// is (scInit), the plugin told to stop any downloads it carried on for us
// (scDownloadsTakeBack: only reaches a plugin already in SceShell, never
// loads one), then the plugin found or loaded and the app registered
// (scOpen). Logs what happened.
void startup();

// Whether scOpen succeeded, so the plugin's features can be used.
bool isOpen();

// What scOpen reported, for the log and Settings.
const std::string& openInfo();

// ── Self-update (SC_FEAT_UPDATE) ─────────────────────────────────────────────

// The downloaded update VPK unpacked to <data>/sc-update with its head.bin
// forged, and the plugin asked to install it once the app has quit
// (scUpdateInstall). The caller must quit at once on true. On false nothing
// was sent, `err` says why, and the caller can fall back to the updater stub.
bool installUpdate(const std::string& vpkPath, std::string& err);

// What the plugin wrote after installing an update (sc-update-result.txt),
// read at start-up and turned into a line for the user: empty when there was
// no update. Read once: the file and any leftover sc-update folder are
// removed.
std::string takeUpdateResult(bool& failed);

// ── Background downloads (SC_FEAT_DOWNLOADS) ─────────────────────────────────

// Whether startup's scDownloadsTakeBack reached a plugin (one in SceShell
// from earlier this boot). Only then can it still be writing our files.
bool takeBackReached();

// The plugin's progress notification while the app downloads itself, so the
// same notification carries on once it closes. At most once a second; the
// lines are UTF-8. permille 0-1000.
void downloadProgress(const std::string& line1, const std::string& line2, uint32_t permille);
// The notification ended, with these last two lines.
void downloadProgressEnd(const std::string& line1, const std::string& line2);
// Whether "Pause downloads?" was answered Yes while the app downloads.
bool downloadPauseAsked();

struct HandOverItem {
    std::string id;     // the app's own id, read back from the status
    std::string file;   // where the plugin writes it
    std::string title;  // for the notification
    std::string url;    // for "pages", with {page} where the page number goes
    // A "pages" item (a chapter): "count" or "count,first"; file and url are
    // then templates. Empty for a "url" item.
    std::string pages;
};
// The list written (sc-downloads.txt) and handed to the plugin to carry on
// once the app has gone (scDownloadsStart). `headers` are "Name: value"
// lines, \r\n between, sent with every request. At most kMaxHandOverItems.
bool handOverDownloads(const std::string& headers, const std::vector<HandOverItem>& items,
                       std::string& err);

struct DownloadStatusItem {
    std::string id;
    std::string state;  // going, waiting, done, gone or failed
    uint64_t bytes = 0;
    uint32_t pagesDone = 0;  // "pages": those whole, from the first
    uint32_t pageCount = 0;
};
// The plugin's sc-downloads-status.txt: false when there is none.
bool readDownloadsStatus(bool& running, std::vector<DownloadStatusItem>& items);
// Whether a hand-over list (sc-downloads.txt) is there.
bool hasDownloadList();
// The hand-over's files removed, once the app has taken the work back.
void clearDownloadFiles();

// Whether the plugin takes "pages" items (0.2.0 on): a chapter an item.
bool pagesDownloads();
// The most pages one "pages" item can carry (the plugin's SC_MAX_PAGES).
constexpr size_t kMaxItemPages = 1000;

// The most items one hand-over list can carry (the plugin's SC_MAX_DOWNLOADS).
constexpr size_t kMaxHandOverItems = 200;

} // namespace shellcomp
} // namespace vitasuwayomi

#endif
