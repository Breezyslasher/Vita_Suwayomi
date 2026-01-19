/**
 * VitaSuwayomi - Asynchronous Image Loader
 */

#pragma once

#include <borealis.hpp>
#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>

namespace vitasuwayomi {

class ImageLoader {
public:
    using LoadCallback = std::function<void(brls::Image*)>;

    // Set authentication credentials for image loading
    static void setAuthCredentials(const std::string& username, const std::string& password);

    // Load image asynchronously from URL (with thumbnail downscaling)
    static void loadAsync(const std::string& url, LoadCallback callback, brls::Image* target);

    // Load full-size image asynchronously (no downscaling - for manga reader)
    static void loadAsyncFullSize(const std::string& url, LoadCallback callback, brls::Image* target);

    // Preload image to cache without displaying
    static void preload(const std::string& url);

    // Preload full-size image to cache (for manga reader)
    static void preloadFullSize(const std::string& url);

    // Clear image cache
    static void clearCache();

    // Cancel all pending loads
    static void cancelAll();

    // Set max concurrent loads (default: 4)
    static void setMaxConcurrentLoads(int max);

    // Set max thumbnail size for downscaling (default: 200)
    static void setMaxThumbnailSize(int maxSize);

private:
    // Pending load request
    struct LoadRequest {
        std::string url;
        LoadCallback callback;
        brls::Image* target;
        bool fullSize;  // true = no downscaling
    };

    static void processQueue();
    static void executeLoad(const LoadRequest& request);

    static std::map<std::string, std::vector<uint8_t>> s_cache;
    static std::mutex s_cacheMutex;
    static std::string s_authUsername;
    static std::string s_authPassword;

    // Concurrent load limiting
    static std::queue<LoadRequest> s_loadQueue;
    static std::mutex s_queueMutex;
    static std::atomic<int> s_activeLoads;
    static int s_maxConcurrentLoads;
    static int s_maxThumbnailSize;
};

} // namespace vitasuwayomi
