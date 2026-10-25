/**
 * VitaSuwayomi - answering the system's request to quit (PS Vita only)
 *
 * Ported from VitaPlex's watcher (src/utils/background_audio_psv.cpp).
 */

#pragma once

#if defined(__vita__) || defined(__PSV__)

#include <functional>

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

// Run on the watcher's thread as soon as the system asks the app to quit,
// before the UI thread is told: the system gives little time when it closes
// the app to start a game, and the UI thread may be slow to get to it while
// the app is in the background. Set before startQuitWatcher.
void setOnQuitRequested(std::function<void()> fn);

// Whether the system asked the app to quit (rather than the user choosing to).
bool quitRequested();

} // namespace lifecycle
} // namespace vitasuwayomi

#endif
