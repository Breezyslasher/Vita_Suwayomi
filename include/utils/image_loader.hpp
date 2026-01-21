/**
 * VitaSuwayomi - Asynchronous Image Loader
 */

#pragma once

#include <borealis.hpp>
#include "view/rotatable_image.hpp"
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
    using RotatableLoadCallback = std::function<void(RotatableImage*)>;

    // Set authentication credentials for image loading
    static void setAuthCredentials(const std::string& username, const std::string& password);

    // Get authentication credentials (for other components that need to download)
    static const std::string& getAuthUsername() { return s_authUsername; }
    static const std::string& getAuthPassword() { return s_authPassword; }

    // Load image asynchronously from URL (with thumbnail downscaling) - for brls::Image
    static void loadAsync(const std::string& url, LoadCallback callback, brls::Image* target);

    // Load full-size image asynchronously (no downscaling - for manga reader) - for brls::Image
    static void loadAsyncFullSize(const std::string& url, LoadCallback callback, brls::Image* target);

    // Load full-size image asynchronously for RotatableImage (custom rendering)
    static void loadAsyncFullSize(const std::string& url, RotatableLoadCallback callback, RotatableImage* target);

    // Load a specific segment of a tall image (for webtoon splitting)
    // segment: which segment (0-based), totalSegments: total number of segments
    static void loadAsyncFullSizeSegment(const std::string& url, int segment, int totalSegments,
                                         RotatableLoadCallback callback, RotatableImage* target);

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

    // Set max concurrent loads (default: 4)
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
    };

    // Pending load request for RotatableImage
    struct RotatableLoadRequest {
        std::string url;
        RotatableLoadCallback callback;
        RotatableImage* target;
        int segment = 0;        // Segment index (0 = first/only)
        int totalSegments = 1;  // Total segments (1 = no splitting)
    };

    static void processQueue();
    static void executeLoad(const LoadRequest& request);
    static void executeRotatableLoad(const RotatableLoadRequest& request);

    static std::map<std::string, std::vector<uint8_t>> s_cache;
    static std::mutex s_cacheMutex;
    static std::string s_authUsername;
    static std::string s_authPassword;

    // Concurrent load limiting
    static std::queue<LoadRequest> s_loadQueue;
    static std::queue<RotatableLoadRequest> s_rotatableLoadQueue;
    static std::mutex s_queueMutex;
    static std::atomic<int> s_activeLoads;
    static int s_maxConcurrentLoads;
    static int s_maxThumbnailSize;
};

} // namespace vitasuwayomi
