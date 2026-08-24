/**
 * VitaSuwayomi - In-app updater
 *
 * Checks the GitHub releases feed for a newer build and installs it in place
 * (per-platform), or falls back to opening the release page in a browser.
 *
 * Public surface is intentionally tiny; everything else is file-local.
 * See docs/in-app-updates.md for the design and per-platform mechanisms.
 */

#pragma once

namespace app_update {

// Remember argv[0] once from main() before borealis starts. Some platforms
// (Switch) need it to know which file to overwrite; a no-op elsewhere.
void setSelfPath(const char* argv0);

// Check GitHub for a newer release.
//   manual = true  → the user pressed a "Check for updates" cell: report
//                    "up to date" and re-offer a previously skipped release.
//   manual = false → the silent startup check: stay quiet when up to date or
//                    on a release the user chose to skip.
void checkForUpdates(bool manual);

} // namespace app_update
