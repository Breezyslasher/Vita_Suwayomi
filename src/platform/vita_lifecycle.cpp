/**
 * VitaSuwayomi - answering the system's request to quit (PS Vita only)
 */

#include "platform/vita_lifecycle.hpp"

#include <borealis.hpp>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <atomic>
#include <cstring>

// Not declared by this SDK's headers; the stubs are in libSceAppMgr_stub,
// which the app links already. psp2/appmgr.h is left out on purpose, so that
// a later SDK declaring these with its own struct types can't clash.
extern "C" {
int sceAppMgrReceiveEventNum(int* eventNum);
int sceAppMgrReceiveEvent(void* event);
}

namespace vitasuwayomi {
namespace lifecycle {

namespace {

// SceAppMgrEvent as firmware 3.60 fills it in.
struct AppEvent {
    int event;
    int appId;
    char param[56];
};
static_assert(sizeof(AppEvent) == 0x40, "SceAppMgrEvent is 0x40 bytes");

constexpr int kEventRequestQuit = 0x20000001;

constexpr SceUInt kTickUs = 250 * 1000;
// How long the normal way out gets after the request before the process is
// ended. Short while the UI thread hasn't taken the request, as then it is
// stuck (in a request to a server it can't reach, say); longer once it has,
// so a shutdown under way — the downloads hand-over, settings being
// rewritten — isn't cut off mid-write.
constexpr SceInt64 kQuitGraceUs    = 2 * 1000000;
constexpr SceInt64 kQuitShutdownUs = 10 * 1000000;

std::atomic<SceInt64> g_quitAskedAt{0};
std::atomic<bool> g_quitUnderway{false};

void drainAppEvents(SceInt64 now) {
    int n = 0;
    if (sceAppMgrReceiveEventNum(&n) < 0 || n <= 0) return;
    for (int i = 0; i < n; i++) {
        AppEvent ev;
        std::memset(&ev, 0, sizeof(ev));
        if (sceAppMgrReceiveEvent(&ev) < 0) break;
        if (ev.event != kEventRequestQuit || g_quitAskedAt.load() != 0) continue;
        brls::Logger::info("lifecycle: the system asked the app to quit; quitting");
        g_quitAskedAt.store(now);
        brls::sync([]() {
            g_quitUnderway.store(true);
            brls::Application::quit();
        });
    }
}

int watcherMain(SceSize, void*) {
    for (;;) {
        sceKernelDelayThread(kTickUs);
        const SceInt64 now = sceKernelGetSystemTimeWide();
        drainAppEvents(now);
        const SceInt64 askedAt = g_quitAskedAt.load();
        const bool underway = g_quitUnderway.load();
        if (askedAt != 0 && now - askedAt > (underway ? kQuitShutdownUs : kQuitGraceUs)) {
            brls::Logger::warning("lifecycle: still running {:.1f}s after the system asked to quit ({}); "
                                  "ending the process",
                                  (now - askedAt) / 1e6,
                                  underway ? "the shutdown did not finish" : "the UI never took the request");
            sceKernelExitProcess(0);
        }
    }
    return 0;
}

} // namespace

void startQuitWatcher() {
    static bool started = false;
    if (started) return;
    started = true;
    const SceUID thid = sceKernelCreateThread("VitaSuwayomiLifecycle", watcherMain, 0x10000100, 0x10000,
                                              0, 0, nullptr);
    const int rc = thid >= 0 ? sceKernelStartThread(thid, 0, nullptr) : thid;
    if (rc < 0) brls::Logger::error("lifecycle: could not start the watcher: 0x{:08X}", (unsigned)rc);
}

bool quitRequested() { return g_quitAskedAt.load() != 0; }

} // namespace lifecycle
} // namespace vitasuwayomi
