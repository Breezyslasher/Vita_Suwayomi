/**
 * VitaSuwayomi - Async utilities
 * Simple async task execution with UI thread callbacks
 */

#pragma once

#include <functional>
#include <thread>
#include <borealis.hpp>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#endif

#if defined(__SWITCH__)
#include <pthread.h>
#endif

namespace vitasuwayomi {

// Helper: launch a detached thread with adequate stack size.
// On Switch libnx the default pthread stack is too small for curl+mbedTLS,
// so we explicitly request 512 KB. On other platforms std::thread is fine.
namespace detail {

#if defined(__SWITCH__)
inline void launchThread(std::function<void()> task) {
    auto* taskPtr = new std::function<void()>(std::move(task));
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 0x80000); // 512 KB
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
        // Fallback
        brls::Logger::error("pthread_create failed ({}), using std::thread", rc);
        auto t = std::move(*taskPtr);
        delete taskPtr;
        std::thread([t]() { t(); }).detach();
    }
}
#else
inline void launchThread(std::function<void()> task) {
    std::thread([task]() {
        task();
    }).detach();
}
#endif

} // namespace detail

#ifdef __vita__
// Vita-specific thread wrapper with configurable stack size
struct VitaThreadData {
    std::function<void()> task;
};

inline int vitaThreadEntry(SceSize args, void* argp) {
    (void)args;
    VitaThreadData* data = *static_cast<VitaThreadData**>(argp);
    if (data && data->task) {
        data->task();
    }
    delete data;
    return sceKernelExitDeleteThread(0);
}

// Run task with larger stack size (256KB) - needed for file operations
inline void asyncRunLargeStack(std::function<void()> task) {
    VitaThreadData* data = new VitaThreadData();
    data->task = std::move(task);

    SceUID thid = sceKernelCreateThread("asyncLargeStack", vitaThreadEntry,
                                         0x10000100, 0x40000, 0, 0, NULL);  // 256KB stack
    if (thid >= 0) {
        VitaThreadData* dataPtr = data;
        sceKernelStartThread(thid, sizeof(dataPtr), &dataPtr);
    } else {
        // Fallback to regular thread if creation fails
        delete data;
        detail::launchThread(std::move(task));
    }
}
#else
inline void asyncRunLargeStack(std::function<void()> task) {
    detail::launchThread(std::move(task));
}
#endif

/**
 * Execute a task asynchronously and call a callback on the UI thread when done.
 *
 * @param task The task to run in background (should not touch UI)
 * @param callback Called on UI thread when task completes
 */
template<typename T>
inline void asyncTask(std::function<T()> task, std::function<void(T)> callback) {
    detail::launchThread([task, callback]() {
        T result = task();
        brls::sync([callback, result]() {
            callback(result);
        });
    });
}

/**
 * Execute a void task asynchronously and call a callback on the UI thread when done.
 *
 * @param task The task to run in background
 * @param callback Called on UI thread when task completes
 */
inline void asyncTask(std::function<void()> task, std::function<void()> callback) {
    detail::launchThread([task, callback]() {
        task();
        brls::sync([callback]() {
            callback();
        });
    });
}

/**
 * Execute a task asynchronously without a callback
 *
 * @param task The task to run in background
 */
inline void asyncRun(std::function<void()> task) {
    detail::launchThread(std::move(task));
}

} // namespace vitasuwayomi
