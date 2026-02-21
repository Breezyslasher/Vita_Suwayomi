/**
 * VitaSuwayomi - Asynchronous Image Loader
 */

#pragma once

#include <borealis.hpp>
#include "view/rotatable_image.hpp"
#include <string>
#include <functional>
#include <map>
#include <list>
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <vector>
#include <thread>

namespace vitasuwayomi {

class ImageLoader {
public:
    using LoadCallback = std::function<void(brls::Image*)>;
    using RotatableLoadCallback = std::function<void(RotatableImage*)>;

    // Set authentication credentials for image loading
    static void setAuthCredentials(const std::string& username, const std::string& password);

    // Set JWT access token for Bearer auth
    static void setAccessToken(const std::string& token);

    // Get authentication credentials (for other components that need to download)
    static const std::string& getAuthUsername() { return s_authUsername; }
    static const std::string& getAuthPassword() { return s_authPassword; }
    static const std::string& getAccessToken() { return s_accessToken; }

    // Load image asynchronously from URL (with thumbnail downscaling) - for brls::Image
    static void loadAsync(const std::string& url, LoadCallback callback, brls::Image* target);

    // Load image asynchronously with lifetime tracking - alive flag prevents
    // writing to destroyed targets when the owning view is destroyed during loading
    static void loadAsync(const std::string& url, LoadCallback callback, brls::Image* target,
                          std::shared_ptr<bool> alive);

    // Load full-size image asynchronously (no downscaling - for manga reader) - for brls::Image
    static void loadAsyncFullSize(const std::string& url, LoadCallback callback, brls::Image* target);

    // Load full-size image asynchronously for RotatableImage (custom rendering)
    static void loadAsyncFullSize(const std::string& url, RotatableLoadCallback callback, RotatableImage* target,
                                  std::shared_ptr<bool> alive = nullptr);

    // Preload image to cache without displaying
    static void preload(const std::string& url);

    // Preload full-size image to cache (for manga reader)
    static void preloadFullSize(const std::string& url);

    // Clear image cache
    static void clearCache();

    // Cancel all pending loads
    static void cancelAll();

    // Set max concurrent loads (default: 6)
    static void setMaxConcurrentLoads(int max);

    // Set max thumbnail size for downscaling (default: 200)
    static void setMaxThumbnailSize(int maxSize);

private:
    // Pending load request for brls::Image
    struct LoadRequest {
        std::string url;
        LoadCallback callback;
        brls::Image* target;
        bool fullSize;  // true = no downscaling
        std::shared_ptr<bool> alive;  // If set and *alive==false, skip (owner destroyed)
    };

    // Pending load request for RotatableImage
    struct RotatableLoadRequest {
        std::string url;
        RotatableLoadCallback callback;
        RotatableImage* target;
        std::shared_ptr<bool> alive;  // If set and *alive==false, skip (owner destroyed)
    };

    static void executeLoad(const LoadRequest& request);
    static void executeRotatableLoad(const RotatableLoadRequest& request);

    // LRU cache: list stores entries in access order (most recent at front)
    // map provides O(1) lookup by URL
    struct CacheEntry {
        std::string url;
        std::vector<uint8_t> data;
    };
    static std::list<CacheEntry> s_cacheList;
    static std::map<std::string, std::list<CacheEntry>::iterator> s_cacheMap;
    static size_t s_maxCacheSize;
    static std::mutex s_cacheMutex;

    // LRU cache helpers
    static void cachePut(const std::string& url, const std::vector<uint8_t>& data);
    static bool cacheGet(const std::string& url, std::vector<uint8_t>& data);
    static std::string s_authUsername;
    static std::string s_authPassword;
    static std::string s_accessToken;  // JWT access token for Bearer auth

    // Worker thread pool - persistent threads that reuse HTTP connections
    // Each worker has its own HttpClient for TCP connection reuse (keep-alive)
    static void workerThreadFunc(int workerId);
    static void ensureWorkersStarted();
    static std::vector<std::thread> s_workers;
    static std::atomic<bool> s_workersStarted;
    static std::atomic<bool> s_shutdownWorkers;

    // Shared queue for worker threads
    static std::queue<LoadRequest> s_loadQueue;
    static std::queue<RotatableLoadRequest> s_rotatableLoadQueue;
    static std::mutex s_queueMutex;
    static std::condition_variable s_queueCV;  // Wake workers when items are queued
    static int s_maxConcurrentLoads;
    static int s_maxThumbnailSize;

    // Batched texture upload queue - prevents main thread freeze from
    // GPU texture uploads arriving simultaneously.
    // Background threads push completed images here instead of calling brls::sync() directly.
    // A single scheduled callback processes a few textures per frame.
    struct PendingTextureUpdate {
        std::vector<uint8_t> data;
        brls::Image* target;
        LoadCallback callback;
        std::shared_ptr<bool> alive;  // If set and *alive==false, skip (owner destroyed)
    };
    static std::queue<PendingTextureUpdate> s_pendingTextures;
    static std::mutex s_pendingMutex;
    static std::atomic<bool> s_pendingScheduled;
    static constexpr int MAX_TEXTURES_PER_FRAME = 6;  // Limit GPU uploads per frame

    // Queue a texture for batched upload on the main thread
    static void queueTextureUpdate(const std::vector<uint8_t>& data, brls::Image* target, LoadCallback callback,
                                   std::shared_ptr<bool> alive = nullptr);
    // Process a batch of pending texture uploads (called on main thread)
    static void processPendingTextures();
};

} // namespace vitasuwayomi
