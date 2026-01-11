/**
 * VitaABS - Async utilities
 * Simple async task execution with UI thread callbacks
 */

#pragma once

#include <functional>
#include <thread>
#include <borealis.hpp>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#endif

namespace vitaabs {

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
        std::thread([task]() {
            task();
        }).detach();
    }
}
#else
inline void asyncRunLargeStack(std::function<void()> task) {
    std::thread([task]() {
        task();
    }).detach();
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
    std::thread([task, callback]() {
        T result = task();
        brls::sync([callback, result]() {
            callback(result);
        });
    }).detach();
}

/**
 * Execute a void task asynchronously and call a callback on the UI thread when done.
 *
 * @param task The task to run in background
 * @param callback Called on UI thread when task completes
 */
inline void asyncTask(std::function<void()> task, std::function<void()> callback) {
    std::thread([task, callback]() {
        task();
        brls::sync([callback]() {
            callback();
        });
    }).detach();
}

/**
 * Execute a task asynchronously without a callback
 *
 * @param task The task to run in background
 */
inline void asyncRun(std::function<void()> task) {
    std::thread([task]() {
        task();
    }).detach();
}

} // namespace vitaabs
