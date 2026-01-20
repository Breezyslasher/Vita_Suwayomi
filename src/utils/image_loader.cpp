/**
 * VitaSuwayomi - Asynchronous Image Loader implementation
 * With concurrent load limiting and image downscaling for PS Vita memory constraints
 * Non-blocking design with staggered loading to prevent UI freezes
 */

#include "utils/image_loader.hpp"
#include "utils/http_client.hpp"
#include "utils/library_cache.hpp"
#include "app/suwayomi_client.hpp"
#include "app/application.hpp"

// WebP decoding support
#include <webp/decode.h>
#include <cstring>
#include <thread>
#include <chrono>

namespace vitasuwayomi {

// Static member initialization
std::map<std::string, std::vector<uint8_t>> ImageLoader::s_cache;
std::mutex ImageLoader::s_cacheMutex;
std::string ImageLoader::s_authUsername;
std::string ImageLoader::s_authPassword;
std::queue<ImageLoader::LoadRequest> ImageLoader::s_loadQueue;
std::queue<ImageLoader::RotatableLoadRequest> ImageLoader::s_rotatableLoadQueue;
std::mutex ImageLoader::s_queueMutex;
std::atomic<int> ImageLoader::s_activeLoads{0};
int ImageLoader::s_maxConcurrentLoads = 2;  // Reduced for Vita stability
int ImageLoader::s_maxThumbnailSize = 180;  // Smaller thumbnails for speed

// Flag to track if queue processor is running
static std::atomic<bool> s_processorRunning{false};

// Helper to extract manga ID from thumbnail URL
// URLs look like: http://server/api/v1/manga/123/thumbnail or /api/v1/manga/123/thumbnail
static int extractMangaIdFromUrl(const std::string& url) {
    // Look for /manga/XXX/ pattern
    size_t pos = url.find("/manga/");
    if (pos != std::string::npos) {
        pos += 7;  // Skip "/manga/"
        size_t endPos = url.find('/', pos);
        if (endPos == std::string::npos) {
            endPos = url.length();
        }
        std::string idStr = url.substr(pos, endPos - pos);
        try {
            return std::stoi(idStr);
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

// Helper function to downscale RGBA image
static void downscaleRGBA(const uint8_t* src, int srcW, int srcH,
                          uint8_t* dst, int dstW, int dstH) {
    float scaleX = (float)srcW / dstW;
    float scaleY = (float)srcH / dstH;

    for (int y = 0; y < dstH; y++) {
        for (int x = 0; x < dstW; x++) {
            int srcX = (int)(x * scaleX);
            int srcY = (int)(y * scaleY);
            if (srcX >= srcW) srcX = srcW - 1;
            if (srcY >= srcH) srcY = srcH - 1;

            int srcIdx = (srcY * srcW + srcX) * 4;
            int dstIdx = (y * dstW + x) * 4;

            dst[dstIdx + 0] = src[srcIdx + 0];
            dst[dstIdx + 1] = src[srcIdx + 1];
            dst[dstIdx + 2] = src[srcIdx + 2];
            dst[dstIdx + 3] = src[srcIdx + 3];
        }
    }
}

// Helper function to convert WebP to TGA format with downscaling
static std::vector<uint8_t> convertWebPtoTGA(const uint8_t* webpData, size_t webpSize, int maxSize) {
    std::vector<uint8_t> tgaData;

    int width, height;
    if (!WebPGetInfo(webpData, webpSize, &width, &height)) {
        return tgaData;
    }

    uint8_t* rgba = WebPDecodeRGBA(webpData, webpSize, &width, &height);
    if (!rgba) {
        return tgaData;
    }

    int targetW = width;
    int targetH = height;

    if (maxSize > 0 && (width > maxSize || height > maxSize)) {
        float scale = (float)maxSize / std::max(width, height);
        targetW = std::max(1, (int)(width * scale));
        targetH = std::max(1, (int)(height * scale));
    }

    uint8_t* finalRgba = rgba;
    std::vector<uint8_t> scaledRgba;

    if (targetW != width || targetH != height) {
        scaledRgba.resize(targetW * targetH * 4);
        downscaleRGBA(rgba, width, height, scaledRgba.data(), targetW, targetH);
        finalRgba = scaledRgba.data();
    }

    size_t imageSize = targetW * targetH * 4;
    tgaData.resize(18 + imageSize);

    uint8_t* header = tgaData.data();
    memset(header, 0, 18);
    header[2] = 2;
    header[12] = targetW & 0xFF;
    header[13] = (targetW >> 8) & 0xFF;
    header[14] = targetH & 0xFF;
    header[15] = (targetH >> 8) & 0xFF;
    header[16] = 32;
    header[17] = 0x28;

    uint8_t* pixels = tgaData.data() + 18;
    for (int i = 0; i < targetW * targetH; i++) {
        pixels[i * 4 + 0] = finalRgba[i * 4 + 2];  // B
        pixels[i * 4 + 1] = finalRgba[i * 4 + 1];  // G
        pixels[i * 4 + 2] = finalRgba[i * 4 + 0];  // R
        pixels[i * 4 + 3] = finalRgba[i * 4 + 3];  // A
    }

    WebPFree(rgba);
    return tgaData;
}

void ImageLoader::setAuthCredentials(const std::string& username, const std::string& password) {
    s_authUsername = username;
    s_authPassword = password;
}

void ImageLoader::setMaxConcurrentLoads(int max) {
    s_maxConcurrentLoads = std::max(1, std::min(max, 4));
}

void ImageLoader::setMaxThumbnailSize(int maxSize) {
    s_maxThumbnailSize = maxSize > 0 ? maxSize : 0;
}

void ImageLoader::executeLoad(const LoadRequest& request) {
    const std::string& url = request.url;
    brls::Image* target = request.target;
    LoadCallback callback = request.callback;

    HttpClient client;

    // Add authentication if needed
    if (!s_authUsername.empty() && !s_authPassword.empty()) {
        std::string credentials = s_authUsername + ":" + s_authPassword;
        static const char* b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        int val = 0, valb = -6;
        for (unsigned char c : credentials) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                encoded.push_back(b64chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) encoded.push_back(b64chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (encoded.size() % 4) encoded.push_back('=');
        client.setDefaultHeader("Authorization", "Basic " + encoded);
    }

    HttpResponse resp = client.get(url);

    if (!resp.success || resp.body.empty()) {
        return;
    }

    // Check image format
    bool isWebP = false;
    bool isValidImage = false;

    if (resp.body.size() > 12) {
        unsigned char* data = reinterpret_cast<unsigned char*>(resp.body.data());

        if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
            isValidImage = true;  // JPEG
        }
        else if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
            isValidImage = true;  // PNG
        }
        else if (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46) {
            isValidImage = true;  // GIF
        }
        else if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46 &&
                 data[8] == 0x57 && data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50) {
            isWebP = true;
            isValidImage = true;
        }
    }

    if (!isValidImage) {
        return;
    }

    // Convert WebP if needed
    std::vector<uint8_t> imageData;
    if (isWebP) {
        imageData = convertWebPtoTGA(
            reinterpret_cast<const uint8_t*>(resp.body.data()),
            resp.body.size(),
            s_maxThumbnailSize
        );
        if (imageData.empty()) {
            return;
        }
    } else {
        imageData.assign(resp.body.begin(), resp.body.end());
    }

    // Cache the image in memory
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_cache.size() > 40) {
            s_cache.clear();
        }
        s_cache[url] = imageData;
    }

    // Save to disk cache if enabled
    if (Application::getInstance().getSettings().cacheCoverImages) {
        int mangaId = extractMangaIdFromUrl(url);
        if (mangaId > 0) {
            LibraryCache::getInstance().saveCoverImage(mangaId, imageData);
        }
    }

    // Update UI on main thread
    if (target) {
        brls::sync([imageData, callback, target]() {
            target->setImageFromMem(imageData.data(), imageData.size());
            if (callback) callback(target);
        });
    }
}

void ImageLoader::processQueue() {
    // Only allow one processor thread at a time
    bool expected = false;
    if (!s_processorRunning.compare_exchange_strong(expected, true)) {
        return;  // Another processor is already running
    }

    // Start a single background thread that processes the entire queue
    std::thread([]() {
        while (true) {
            // Check if we can process more
            if (s_activeLoads >= s_maxConcurrentLoads) {
                // Wait a bit before checking again
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            // Get next request from either queue
            LoadRequest request;
            RotatableLoadRequest rotatableRequest;
            bool hasRequest = false;
            bool isRotatable = false;

            {
                std::lock_guard<std::mutex> lock(s_queueMutex);
                // Check rotatable queue first (reader pages have priority)
                if (!s_rotatableLoadQueue.empty()) {
                    rotatableRequest = s_rotatableLoadQueue.front();
                    s_rotatableLoadQueue.pop();
                    hasRequest = true;
                    isRotatable = true;
                } else if (!s_loadQueue.empty()) {
                    request = s_loadQueue.front();
                    s_loadQueue.pop();
                    hasRequest = true;
                }
            }

            if (!hasRequest) {
                // Both queues are empty, exit processor
                s_processorRunning = false;
                return;
            }

            s_activeLoads++;

            // Execute load in a new thread
            if (isRotatable) {
                std::thread([rotatableRequest]() {
                    executeRotatableLoad(rotatableRequest);
                    s_activeLoads--;
                }).detach();
            } else {
                std::thread([request]() {
                    executeLoad(request);
                    s_activeLoads--;
                }).detach();
            }

            // Small delay between starting loads to prevent overwhelming the system
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }).detach();
}

void ImageLoader::loadAsync(const std::string& url, LoadCallback callback, brls::Image* target) {
    if (url.empty() || !target) return;

    // Check memory cache first
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(url);
        if (it != s_cache.end()) {
            target->setImageFromMem(it->second.data(), it->second.size());
            if (callback) callback(target);
            return;
        }
    }

    // Check disk cache if enabled
    if (Application::getInstance().getSettings().cacheCoverImages) {
        int mangaId = extractMangaIdFromUrl(url);
        if (mangaId > 0) {
            std::vector<uint8_t> diskData;
            if (LibraryCache::getInstance().loadCoverImage(mangaId, diskData)) {
                // Found in disk cache - add to memory cache and display
                {
                    std::lock_guard<std::mutex> lock(s_cacheMutex);
                    if (s_cache.size() > 40) {
                        s_cache.clear();
                    }
                    s_cache[url] = diskData;
                }
                target->setImageFromMem(diskData.data(), diskData.size());
                if (callback) callback(target);
                return;
            }
        }
    }

    // Add to queue for network download
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_loadQueue.push({url, callback, target, true});
    }

    // Start processor if not already running
    processQueue();
}

void ImageLoader::executeRotatableLoad(const RotatableLoadRequest& request) {
    const std::string& url = request.url;
    RotatableLoadCallback callback = request.callback;
    RotatableImage* target = request.target;

    HttpClient client;

    // Add authentication if credentials are set
    if (!s_authUsername.empty() && !s_authPassword.empty()) {
        std::string credentials = s_authUsername + ":" + s_authPassword;
        static const char* b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        int val = 0, valb = -6;
        for (unsigned char c : credentials) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                encoded.push_back(b64chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) encoded.push_back(b64chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (encoded.size() % 4) encoded.push_back('=');
        client.setDefaultHeader("Authorization", "Basic " + encoded);
    }

    HttpResponse resp = client.get(url);

    if (resp.success && !resp.body.empty()) {
        brls::Logger::debug("ImageLoader: Loaded {} bytes from {} for RotatableImage", resp.body.size(), url);

        // Check image format
        bool isWebP = false;
        bool isValidImage = false;

        if (resp.body.size() > 12) {
            unsigned char* data = reinterpret_cast<unsigned char*>(resp.body.data());

            // JPEG, PNG, GIF
            if ((data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) ||
                (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) ||
                (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46)) {
                isValidImage = true;
            }
            // WebP
            else if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46 &&
                     data[8] == 0x57 && data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50) {
                isWebP = true;
                isValidImage = true;
            }
        }

        if (!isValidImage) {
            brls::Logger::warning("ImageLoader: Invalid image format for {}", url);
            return;
        }

        // Convert WebP to TGA if needed (no downscaling for manga pages)
        std::vector<uint8_t> imageData;
        if (isWebP) {
            imageData = convertWebPtoTGA(
                reinterpret_cast<const uint8_t*>(resp.body.data()),
                resp.body.size(),
                0  // No downscaling
            );
            if (imageData.empty()) {
                brls::Logger::error("ImageLoader: WebP conversion failed for {}", url);
                return;
            }
        } else {
            imageData.assign(resp.body.begin(), resp.body.end());
        }

        // Cache the image
        std::string cacheKey = url + "_full";
        {
            std::lock_guard<std::mutex> lock(s_cacheMutex);
            if (s_cache.size() > 50) {
                s_cache.clear();
            }
            s_cache[cacheKey] = imageData;
        }

        // Update UI on main thread
        brls::sync([imageData, callback, target]() {
            if (target) {
                target->setImageFromMem(imageData.data(), imageData.size());
                if (callback) callback(target);
            }
        });
    } else {
        brls::Logger::error("ImageLoader: Failed to load {}: {}", url, resp.error);
    }
}

void ImageLoader::loadAsyncFullSize(const std::string& url, RotatableLoadCallback callback, RotatableImage* target) {
    if (url.empty() || !target) return;

    // Check cache first
    std::string cacheKey = url + "_full";
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(cacheKey);
        if (it != s_cache.end()) {
            target->setImageFromMem(it->second.data(), it->second.size());
            if (callback) callback(target);
            return;
        }
    }

    // Add to rotatable queue
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_rotatableLoadQueue.push({url, callback, target});
    }

    processQueue();
}

void ImageLoader::preload(const std::string& url) {
    if (url.empty()) return;

    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_cache.find(url) != s_cache.end()) {
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_loadQueue.push({url, nullptr, nullptr, true});
    }

    processQueue();
}

void ImageLoader::preloadFullSize(const std::string& url) {
    if (url.empty()) return;

    // Check if already cached
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_cache.find(url) != s_cache.end()) {
            return;
        }
    }

    // Queue for full-size loading (no downscaling)
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_loadQueue.push({url, nullptr, nullptr, true});  // fullSize = true
    }

    processQueue();
}

void ImageLoader::clearCache() {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_cache.clear();
}

void ImageLoader::cancelAll() {
    std::lock_guard<std::mutex> lock(s_queueMutex);
    while (!s_loadQueue.empty()) {
        s_loadQueue.pop();
    }
}

} // namespace vitasuwayomi
