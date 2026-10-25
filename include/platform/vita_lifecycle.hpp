/**
 * VitaSuwayomi - answering the system's request to quit (PS Vita only)
 *
 * Ported from VitaPlex's watcher (src/utils/background_audio_psv.cpp).
 */

#pragma once

#if defined(__vita__) || defined(__PSV__)

namespace vitasuwayomi {
namespace lifecycle {

// A thread that drains SceAppMgr's events. When the system asks the app to
// quit — starting a game over it, for one — the main loop is told to end, so
// main's normal way out runs (downloads handed to the shell companion,
// settings saved). A non-game package that ignores the request leaves the
// console waiting on it. If the UI thread hasn't taken the request in 2 s,
// or the shutdown it began hasn't finished in 10 s, the watcher ends the
// process itself.
void startQuitWatcher();

// Whether the system asked the app to quit (rather than the user choosing to).
bool quitRequested();

} // namespace lifecycle
} // namespace vitasuwayomi

#endif
