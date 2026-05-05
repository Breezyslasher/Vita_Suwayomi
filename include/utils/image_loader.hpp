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
#include <set>
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

    // Set session cookie for SIMPLE_LOGIN mode
    static void setSessionCookie(const std::string& cookie);

    // Get authentication credentials (thread-safe copies for use from worker threads)
    static std::string getAuthUsername();
    static std::string getAuthPassword();
    static std::string getAccessToken();
    static std::string getSessionCookie();

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

    // Load a specific segment of a tall image (for webtoon splitting)
    // segment: which segment (0-based), totalSegments: total number of segments
    static void loadAsyncFullSizeSegment(const std::string& url, int segment, int totalSegments,
                                         RotatableLoadCallback callback, RotatableImage* target,
                                         std::shared_ptr<bool> alive = nullptr);

    // Preload image to cache without displaying
    static void preload(const std::string& url);

    // Preload full-size image to cache (for manga reader)
    static void preloadFullSize(const std::string& url);

    // Get image dimensions and suggested segment count for a URL (downloads image temporarily)
    // Returns true if dimensions were obtained, false on error
    // suggestedSegments will be > 1 if the image is taller than MAX_TEXTURE_SIZE (2048)
    static bool getImageDimensions(const std::string& url, int& width, int& height, int& suggestedSegments);

    // Clear image cache
    static void clearCache();

    // Cancel all pending loads
    static void cancelAll();

    // Set max concurrent loads (default: 6)
    static void setMaxConcurrentLoads(int max);

    // Set max thumbnail size for downscaling (default: 200)
    static void setMaxThumbnailSize(int maxSize);

    // While true, processPendingTextures() defers GPU uploads to a later
    // frame. Used by RecyclingGrid during fast scrolling so texture
    // uploads (which take ~15-20ms each on Vita) don't stall the scroll
    // frame. Pending textures continue to queue up and are flushed when
    // the flag is cleared.
    static void setDeferTextureUploads(bool defer);

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
        int segment = 0;        // Segment index (0 = first/only)
        int totalSegments = 1;  // Total segments (1 = no splitting)
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
    static size_t s_currentCacheMemory;
    static std::mutex s_cacheMutex;

    // LRU cache helpers
    static void cachePut(const std::string& url, const std::vector<uint8_t>& data);
    static bool cacheGet(const std::string& url, std::vector<uint8_t>& data);
    static std::string s_authUsername;
    static std::string s_authPassword;
    static std::string s_accessToken;    // JWT access token for Bearer auth
    static std::string s_sessionCookie;  // Session cookie for SIMPLE_LOGIN
    static std::mutex s_authMutex;       // Protects s_authUsername/Password/AccessToken/SessionCookie

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
    static std::set<std::string> s_pendingFullSizeUrls;  // URLs queued or being processed (dedup)
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
    static std::atomic<bool> s_deferTextureUploads;
    static constexpr int MAX_TEXTURES_PER_FRAME = 1;  // Limit GPU uploads per frame (each upload stalls Vita GPU for ~15-20ms)

    // Queue a texture for batched upload on the main thread
    static void queueTextureUpdate(const std::vector<uint8_t>& data, brls::Image* target, LoadCallback callback,
                                   std::shared_ptr<bool> alive = nullptr);
    // Process a batch of pending texture uploads (called on main thread)
    static void processPendingTextures();

    // Batched texture upload queue for RotatableImage (webtoon/manga reader pages).
    // Same concept as PendingTextureUpdate but for full-size reader images.
    // Limits GPU uploads to MAX_ROTATABLE_TEXTURES_PER_FRAME per frame to prevent
    // the "group loading" appearance where multiple images pop in simultaneously.
    struct PendingRotatableTextureUpdate {
        // Single-texture path
        std::vector<uint8_t> data;
        // Multi-segment path (auto-split tall images)
        std::vector<std::vector<uint8_t>> segmentDatas;
        int origW = 0;
        int origH = 0;
        std::vector<int> segHeights;
        bool isSegmented = false;  // true = use segmentDatas, false = use data
        RotatableImage* target = nullptr;
        RotatableLoadCallback callback;
        std::shared_ptr<bool> alive;
    };
    static std::queue<PendingRotatableTextureUpdate> s_pendingRotatableTextures;
    static std::mutex s_pendingRotatableMutex;
    static std::atomic<bool> s_pendingRotatableScheduled;
    static constexpr int MAX_ROTATABLE_TEXTURES_PER_FRAME = 1;  // 1 per frame to avoid stutter during scroll

    // Queue a RotatableImage texture for batched upload (single-texture path)
    static void queueRotatableTextureUpdate(const std::vector<uint8_t>& data, RotatableImage* target,
                                            RotatableLoadCallback callback, std::shared_ptr<bool> alive = nullptr);
    // Queue a RotatableImage texture for batched upload (multi-segment path)
    static void queueRotatableSegmentUpdate(std::vector<std::vector<uint8_t>> segDatas, int origW, int origH,
                                            std::vector<int> segHeights, RotatableImage* target,
                                            RotatableLoadCallback callback, std::shared_ptr<bool> alive = nullptr);
    // Process a batch of pending RotatableImage texture uploads (called on main thread)
    static void processPendingRotatableTextures();
};

} // namespace vitasuwayomi
