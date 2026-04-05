/**
 * VitaSuwayomi - Async utilities
 * Simple async task execution with UI thread callbacks
 *
 * Threading is delegated to the platform abstraction layer
 * (platform::launchThread / platform::launchLargeStackThread)
 * so no #ifdef platform code is needed here.
 */

#pragma once

#include <functional>
#include <borealis.hpp>
#include "platform/platform.hpp"

namespace vitasuwayomi {

// Kept for backward compatibility — delegates to platform::launchThread.
namespace detail {
inline void launchThread(std::function<void()> task) {
    platform::launchThread(std::move(task));
}
} // namespace detail

/// Run task with larger stack size — needed for file operations on Vita.
inline void asyncRunLargeStack(std::function<void()> task) {
    platform::launchLargeStackThread(std::move(task));
}

/**
 * Execute a task asynchronously and call a callback on the UI thread when done.
 */
template<typename T>
inline void asyncTask(std::function<T()> task, std::function<void(T)> callback) {
    platform::launchThread([task, callback]() {
        T result = task();
        brls::sync([callback, result]() {
            callback(result);
        });
    });
}

/**
 * Execute a void task asynchronously and call a callback on the UI thread when done.
 */
inline void asyncTask(std::function<void()> task, std::function<void()> callback) {
    platform::launchThread([task, callback]() {
        task();
        brls::sync([callback]() {
            callback();
        });
    });
}

/**
 * Execute a task asynchronously without a callback
 */
inline void asyncRun(std::function<void()> task) {
    platform::launchThread(std::move(task));
}

} // namespace vitasuwayomi
