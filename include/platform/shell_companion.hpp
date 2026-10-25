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

#include <string>

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

} // namespace shellcomp
} // namespace vitasuwayomi

#endif
