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
#include <fstream>

// Vita I/O for local file loading
#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

// stb_image for JPEG/PNG decoding
// Note: stb_image.h should be available from borealis/extern or nanovg
// If not found, you may need to add it to the project
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace vitasuwayomi {

// Static member initialization
std::list<ImageLoader::CacheEntry> ImageLoader::s_cacheList;
std::map<std::string, std::list<ImageLoader::CacheEntry>::iterator> ImageLoader::s_cacheMap;
size_t ImageLoader::s_maxCacheSize = 200;  // LRU cache: 200 entries for large libraries
std::mutex ImageLoader::s_cacheMutex;
std::string ImageLoader::s_authUsername;
std::string ImageLoader::s_authPassword;
std::string ImageLoader::s_accessToken;
std::queue<ImageLoader::LoadRequest> ImageLoader::s_loadQueue;
std::queue<ImageLoader::RotatableLoadRequest> ImageLoader::s_rotatableLoadQueue;
std::mutex ImageLoader::s_queueMutex;
std::condition_variable ImageLoader::s_queueCV;
int ImageLoader::s_maxConcurrentLoads = 20;  // Worker thread count for concurrent downloads
int ImageLoader::s_maxThumbnailSize = 180;  // Smaller thumbnails for speed

// Worker thread pool
std::vector<std::thread> ImageLoader::s_workers;
std::atomic<bool> ImageLoader::s_workersStarted{false};
std::atomic<bool> ImageLoader::s_shutdownWorkers{false};

// Batched texture upload queue
std::queue<ImageLoader::PendingTextureUpdate> ImageLoader::s_pendingTextures;
std::mutex ImageLoader::s_pendingMutex;
std::atomic<bool> ImageLoader::s_pendingScheduled{false};

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
    if (!src || !dst || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
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

// Helper to create TGA from RGBA data
static std::vector<uint8_t> createTGAFromRGBA(const uint8_t* rgba, int width, int height) {
    if (width <= 0 || height <= 0 || !rgba) return {};
    size_t imageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    // Sanity check: reject absurdly large images (>256MB pixel data)
    if (imageSize > 256 * 1024 * 1024) return {};
    std::vector<uint8_t> tgaData(18 + imageSize);

    uint8_t* header = tgaData.data();
    memset(header, 0, 18);
    header[2] = 2;
    header[12] = width & 0xFF;
    header[13] = (width >> 8) & 0xFF;
    header[14] = height & 0xFF;
    header[15] = (height >> 8) & 0xFF;
    header[16] = 32;
    header[17] = 0x28;

    uint8_t* pixels = tgaData.data() + 18;
    for (int i = 0; i < width * height; i++) {
        pixels[i * 4 + 0] = rgba[i * 4 + 2];  // B
        pixels[i * 4 + 1] = rgba[i * 4 + 1];  // G
        pixels[i * 4 + 2] = rgba[i * 4 + 0];  // R
        pixels[i * 4 + 3] = rgba[i * 4 + 3];  // A
    }

    return tgaData;
}

// Get WebP image dimensions without decoding
static bool getWebPDimensions(const uint8_t* webpData, size_t webpSize, int& width, int& height) {
    return WebPGetInfo(webpData, webpSize, &width, &height) != 0;
}

// Calculate number of segments needed for a tall image
static int calculateSegments(int width, int height, int maxSize) {
    if (height <= maxSize) return 1;
    // Split based on height, keeping each segment within maxSize
    return (height + maxSize - 1) / maxSize;
}

// Convert WebP to TGA for a specific segment of a tall image
static std::vector<uint8_t> convertWebPtoTGASegment(const uint8_t* webpData, size_t webpSize,
                                                     int segment, int totalSegments, int maxSize) {
    std::vector<uint8_t> tgaData;

    if (totalSegments < 1 || segment < 0 || segment >= totalSegments) return tgaData;

    int width, height;
    if (!WebPGetInfo(webpData, webpSize, &width, &height)) {
        return tgaData;
    }

    if (width <= 0 || height <= 0) return tgaData;

    uint8_t* rgba = WebPDecodeRGBA(webpData, webpSize, &width, &height);
    if (!rgba) {
        return tgaData;
    }

    // Calculate segment bounds
    int segmentHeight = (height + totalSegments - 1) / totalSegments;
    int startY = segment * segmentHeight;
    int endY = std::min(startY + segmentHeight, height);
    int actualHeight = endY - startY;

    if (actualHeight <= 0 || startY >= height) {
        WebPFree(rgba);
        return tgaData;
    }

    // Scale down if still too wide
    int targetW = width;
    int targetH = actualHeight;
    if (width > maxSize) {
        float scale = (float)maxSize / width;
        targetW = maxSize;
        targetH = std::max(1, (int)(actualHeight * scale));
    }

    // Extract and optionally scale the segment
    std::vector<uint8_t> segmentRgba;
    if (targetW != width || targetH != actualHeight) {
        // Need to scale - extract segment first, then scale
        std::vector<uint8_t> extracted(width * actualHeight * 4);
        memcpy(extracted.data(), rgba + (startY * width * 4), width * actualHeight * 4);

        segmentRgba.resize(targetW * targetH * 4);
        downscaleRGBA(extracted.data(), width, actualHeight, segmentRgba.data(), targetW, targetH);
    } else {
        // Just extract the segment
        segmentRgba.resize(width * actualHeight * 4);
        memcpy(segmentRgba.data(), rgba + (startY * width * 4), width * actualHeight * 4);
    }

    WebPFree(rgba);

    brls::Logger::debug("ImageLoader: Segment {}/{} - {}x{} (from {}x{} startY={})",
                        segment + 1, totalSegments, targetW, targetH, width, height, startY);

    return createTGAFromRGBA(segmentRgba.data(), targetW, targetH);
}

// Convert JPEG/PNG to TGA for a specific segment of a tall image (using stb_image)
static std::vector<uint8_t> convertImageToTGASegment(const uint8_t* data, size_t dataSize,
                                                      int segment, int totalSegments, int maxSize) {
    std::vector<uint8_t> tgaData;

    if (totalSegments < 1 || segment < 0 || segment >= totalSegments) return tgaData;

    int width, height, channels;
    // Force 4 channels (RGBA)
    uint8_t* rgba = stbi_load_from_memory(data, static_cast<int>(dataSize), &width, &height, &channels, 4);
    if (!rgba) {
        brls::Logger::error("ImageLoader: stb_image failed to decode image");
        return tgaData;
    }

    if (width <= 0 || height <= 0) {
        stbi_image_free(rgba);
        return tgaData;
    }

    // Calculate segment bounds
    int segmentHeight = (height + totalSegments - 1) / totalSegments;
    int startY = segment * segmentHeight;
    int endY = std::min(startY + segmentHeight, height);
    int actualHeight = endY - startY;

    if (actualHeight <= 0 || startY >= height) {
        stbi_image_free(rgba);
        return tgaData;
    }

    // Scale down if still too wide
    int targetW = width;
    int targetH = actualHeight;
    if (width > maxSize) {
        float scale = (float)maxSize / width;
        targetW = maxSize;
        targetH = std::max(1, (int)(actualHeight * scale));
    }

    // Extract and optionally scale the segment
    std::vector<uint8_t> segmentRgba;
    if (targetW != width || targetH != actualHeight) {
        // Need to scale - extract segment first, then scale
        std::vector<uint8_t> extracted(width * actualHeight * 4);
        memcpy(extracted.data(), rgba + (startY * width * 4), width * actualHeight * 4);

        segmentRgba.resize(targetW * targetH * 4);
        downscaleRGBA(extracted.data(), width, actualHeight, segmentRgba.data(), targetW, targetH);
    } else {
        // Just extract the segment
        segmentRgba.resize(width * actualHeight * 4);
        memcpy(segmentRgba.data(), rgba + (startY * width * 4), width * actualHeight * 4);
    }

    stbi_image_free(rgba);

    brls::Logger::debug("ImageLoader: JPEG/PNG Segment {}/{} - {}x{} (from {}x{} startY={})",
                        segment + 1, totalSegments, targetW, targetH, width, height, startY);

    return createTGAFromRGBA(segmentRgba.data(), targetW, targetH);
}

// Convert JPEG/PNG to TGA with optional downscaling (using stb_image)
static std::vector<uint8_t> convertImageToTGA(const uint8_t* data, size_t dataSize, int maxSize) {
    std::vector<uint8_t> tgaData;

    int width, height, channels;
    // Force 4 channels (RGBA)
    uint8_t* rgba = stbi_load_from_memory(data, static_cast<int>(dataSize), &width, &height, &channels, 4);
    if (!rgba) {
        brls::Logger::error("ImageLoader: stb_image failed to decode image");
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

    tgaData = createTGAFromRGBA(finalRgba, targetW, targetH);

    stbi_image_free(rgba);
    return tgaData;
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

void ImageLoader::setAccessToken(const std::string& token) {
    s_accessToken = token;
}

// Helper for Base64 encoding
static std::string base64EncodeImage(const std::string& input) {
    static const char* b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            encoded.push_back(b64chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) encoded.push_back(b64chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (encoded.size() % 4) encoded.push_back('=');
    return encoded;
}

// Helper to apply authentication headers to HTTP client
static void applyAuthHeaders(HttpClient& client) {
    // Prefer JWT Bearer auth if access token is available
    if (!ImageLoader::getAccessToken().empty()) {
        client.setDefaultHeader("Authorization", "Bearer " + ImageLoader::getAccessToken());
    } else if (!ImageLoader::getAuthUsername().empty() && !ImageLoader::getAuthPassword().empty()) {
        // Fall back to Basic Auth
        std::string credentials = ImageLoader::getAuthUsername() + ":" + ImageLoader::getAuthPassword();
        client.setDefaultHeader("Authorization", "Basic " + base64EncodeImage(credentials));
    }
}

// Authenticated HTTP GET with automatic JWT token refresh on 401/403.
// If the request fails due to an expired token, refreshes via SuwayomiClient
// and retries once with the new token.
static HttpResponse authenticatedGet(const std::string& url, int maxRetries = 2) {
    HttpClient client;
    applyAuthHeaders(client);

    HttpResponse resp;
    bool success = false;
    bool tokenRefreshed = false;

    for (int attempt = 0; attempt <= maxRetries && !success; attempt++) {
        if (attempt > 0) {
            brls::Logger::debug("ImageLoader: Retry {} for {}", attempt, url);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000 * attempt));
        }

        resp = client.get(url);

        // Check for auth failure (401 Unauthorized or 403 Forbidden)
        if ((resp.statusCode == 401 || resp.statusCode == 403) && !tokenRefreshed) {
            brls::Logger::info("ImageLoader: Got {} for {}, attempting token refresh...",
                              resp.statusCode, url);
            auto& suwayomiClient = SuwayomiClient::getInstance();
            if (suwayomiClient.refreshToken()) {
                brls::Logger::info("ImageLoader: Token refreshed, retrying download");
                tokenRefreshed = true;
                // Re-create client with new token
                client = HttpClient();
                applyAuthHeaders(client);
                // Don't count this as a retry attempt
                attempt--;
                continue;
            } else {
                brls::Logger::warning("ImageLoader: Token refresh failed for {}", url);
            }
        }

        if (resp.success && !resp.body.empty()) {
            success = true;
        }
    }

    return resp;
}

void ImageLoader::setMaxConcurrentLoads(int max) {
    s_maxConcurrentLoads = std::max(1, std::min(max, 20));
}

void ImageLoader::setMaxThumbnailSize(int maxSize) {
    s_maxThumbnailSize = maxSize > 0 ? maxSize : 0;
}

void ImageLoader::cachePut(const std::string& url, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(s_cacheMutex);

    // If key already exists, move to front and update data
    auto it = s_cacheMap.find(url);
    if (it != s_cacheMap.end()) {
        s_cacheList.erase(it->second);
        s_cacheMap.erase(it);
    }

    // Evict oldest entries (back of list) if over capacity
    while (s_cacheList.size() >= s_maxCacheSize) {
        auto& oldest = s_cacheList.back();
        s_cacheMap.erase(oldest.url);
        s_cacheList.pop_back();
    }

    // Insert at front (most recently used)
    s_cacheList.push_front({url, data});
    s_cacheMap[url] = s_cacheList.begin();
}

bool ImageLoader::cacheGet(const std::string& url, std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(s_cacheMutex);

    auto it = s_cacheMap.find(url);
    if (it == s_cacheMap.end()) {
        return false;
    }

    // Move to front (mark as recently used)
    data = it->second->data;
    s_cacheList.splice(s_cacheList.begin(), s_cacheList, it->second);
    return true;
}

void ImageLoader::queueTextureUpdate(const std::vector<uint8_t>& data, brls::Image* target, LoadCallback callback,
                                     std::shared_ptr<bool> alive) {
    {
        std::lock_guard<std::mutex> lock(s_pendingMutex);
        s_pendingTextures.push({data, target, callback, alive});
    }

    // Schedule processing if not already scheduled
    bool expected = false;
    if (s_pendingScheduled.compare_exchange_strong(expected, true)) {
        brls::sync([]() {
            processPendingTextures();
        });
    }
}

void ImageLoader::processPendingTextures() {
    s_pendingScheduled = false;

    // Process up to MAX_TEXTURES_PER_FRAME textures this frame
    int processed = 0;
    while (processed < MAX_TEXTURES_PER_FRAME) {
        PendingTextureUpdate update;
        {
            std::lock_guard<std::mutex> lock(s_pendingMutex);
            if (s_pendingTextures.empty()) return;
            update = std::move(s_pendingTextures.front());
            s_pendingTextures.pop();
        }

        if (update.target) {
            // Skip if the owning view was destroyed while the image was downloading.
            // Without this check, writing to a freed brls::Image* causes a crash.
            if (update.alive && !*update.alive) {
                continue;
            }
            update.target->setImageFromMem(update.data.data(), update.data.size());
            if (update.callback) update.callback(update.target);
        }
        processed++;
    }

    // If more pending, schedule another batch for the next frame
    bool morePending = false;
    {
        std::lock_guard<std::mutex> lock(s_pendingMutex);
        morePending = !s_pendingTextures.empty();
    }
    if (morePending) {
        bool expected = false;
        if (s_pendingScheduled.compare_exchange_strong(expected, true)) {
            brls::sync([]() {
                processPendingTextures();
            });
        }
    }
}

void ImageLoader::executeLoad(const LoadRequest& request) {
    const std::string& url = request.url;
    brls::Image* target = request.target;
    LoadCallback callback = request.callback;
    std::shared_ptr<bool> alive = request.alive;

    // Check disk cache first (this runs on a background thread, so disk I/O is fine)
    if (Application::getInstance().getSettings().cacheCoverImages) {
        int mangaId = extractMangaIdFromUrl(url);
        if (mangaId > 0) {
            std::vector<uint8_t> diskData;
            if (LibraryCache::getInstance().loadCoverImage(mangaId, diskData)) {
                // Found in disk cache - add to memory LRU cache
                cachePut(url, diskData);
                // Queue for batched texture upload (prevents main thread freeze
                // when 50+ covers load from disk cache simultaneously)
                if (target) {
                    queueTextureUpdate(diskData, target, callback, alive);
                }
                return;
            }
        }
    }

    // Authenticated GET with automatic JWT refresh on 401/403
    HttpResponse resp = authenticatedGet(url, 2);

    if (!resp.success || resp.body.empty()) {
        brls::Logger::warning("ImageLoader: Failed to load {} (status {})", url, resp.statusCode);
        return;
    }

    // Check image format
    bool isWebP = false;
    bool isKnownFormat = false;

    if (resp.body.size() > 12) {
        unsigned char* data = reinterpret_cast<unsigned char*>(resp.body.data());

        if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
            isKnownFormat = true;  // JPEG
        }
        else if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
            isKnownFormat = true;  // PNG
        }
        else if (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46) {
            isKnownFormat = true;  // GIF
        }
        else if (data[0] == 0x42 && data[1] == 0x4D) {
            isKnownFormat = true;  // BMP
        }
        else if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46 &&
                 data[8] == 0x57 && data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50) {
            isWebP = true;
            isKnownFormat = true;
        }
    }

    // For unrecognized formats (.bin, etc.), try stb_image as fallback
    // stb_image can auto-detect JPEG, PNG, BMP, GIF, TGA, PSD, PIC
    if (!isKnownFormat && !isWebP && resp.body.size() > 4) {
        int testW, testH, testC;
        if (stbi_info_from_memory(
                reinterpret_cast<const unsigned char*>(resp.body.data()),
                static_cast<int>(resp.body.size()),
                &testW, &testH, &testC)) {
            isKnownFormat = true;
            brls::Logger::info("ImageLoader: stb_image detected unknown format image {}x{} for {}",
                              testW, testH, url);
        }
    }

    if (!isKnownFormat) {
        brls::Logger::warning("ImageLoader: Unrecognized image format for {}", url);
        return;
    }

    // Convert and downscale all image formats for thumbnails
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
        // Downscale JPEG/PNG thumbnails too (not just WebP)
        // This reduces memory usage and makes cache more effective
        imageData = convertImageToTGA(
            reinterpret_cast<const uint8_t*>(resp.body.data()),
            resp.body.size(),
            s_maxThumbnailSize
        );
        if (imageData.empty()) {
            // Fallback: use raw data if conversion fails
            imageData.assign(resp.body.begin(), resp.body.end());
        }
    }

    // Cache the image in memory using LRU eviction
    cachePut(url, imageData);

    // Save to disk cache if enabled
    if (Application::getInstance().getSettings().cacheCoverImages) {
        int mangaId = extractMangaIdFromUrl(url);
        if (mangaId > 0) {
            LibraryCache::getInstance().saveCoverImage(mangaId, imageData);
        }
    }

    // Queue for batched texture upload on main thread
    if (target) {
        queueTextureUpdate(imageData, target, callback, alive);
    }
}

void ImageLoader::ensureWorkersStarted() {
    bool expected = false;
    if (!s_workersStarted.compare_exchange_strong(expected, true)) {
        return;  // Already started
    }

    s_shutdownWorkers = false;
    int numWorkers = s_maxConcurrentLoads;
    brls::Logger::info("ImageLoader: Starting {} worker threads", numWorkers);

    for (int i = 0; i < numWorkers; i++) {
        s_workers.emplace_back(workerThreadFunc, i);
        s_workers.back().detach();
    }
}

void ImageLoader::workerThreadFunc(int workerId) {
    // Each worker has its own HttpClient for TCP connection reuse (HTTP keep-alive).
    // This avoids creating a new TCP connection for every cover download.
    HttpClient httpClient;
    applyAuthHeaders(httpClient);

    brls::Logger::debug("ImageLoader: Worker {} started", workerId);

    while (!s_shutdownWorkers) {
        LoadRequest request;
        RotatableLoadRequest rotatableRequest;
        bool hasRequest = false;
        bool isRotatable = false;

        {
            std::unique_lock<std::mutex> lock(s_queueMutex);
            // Wait for items to be queued (with timeout to allow shutdown check)
            s_queueCV.wait_for(lock, std::chrono::milliseconds(500), []() {
                return !s_loadQueue.empty() || !s_rotatableLoadQueue.empty() || s_shutdownWorkers;
            });

            if (s_shutdownWorkers) break;

            // Rotatable queue has priority (reader pages)
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

        if (!hasRequest) continue;

        // Refresh auth headers in case token was refreshed by another thread
        httpClient.clearDefaultHeaders();
        applyAuthHeaders(httpClient);

        if (isRotatable) {
            executeRotatableLoad(rotatableRequest);
        } else {
            executeLoad(request);
        }
    }

    brls::Logger::debug("ImageLoader: Worker {} exiting", workerId);
}

void ImageLoader::loadAsync(const std::string& url, LoadCallback callback, brls::Image* target) {
    loadAsync(url, callback, target, nullptr);
}

void ImageLoader::loadAsync(const std::string& url, LoadCallback callback, brls::Image* target,
                            std::shared_ptr<bool> alive) {
    if (url.empty() || !target) return;

    // Check memory cache first (LRU - promotes to front on hit)
    // This is fast (in-memory map lookup) and safe on the main thread
    {
        std::vector<uint8_t> cachedData;
        if (cacheGet(url, cachedData)) {
            // Route through batched texture queue even for memory cache hits.
            // Direct setImageFromMem() on the main thread causes a freeze when
            // many cells hit memory cache simultaneously (e.g., after grid rebuild).
            queueTextureUpdate(cachedData, target, callback, alive);
            return;
        }
    }

    // Disk cache and network downloads are handled on worker threads
    // via executeLoad() to avoid blocking the main thread with I/O
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_loadQueue.push({url, callback, target, true, alive});
    }
    s_queueCV.notify_one();

    // Start workers if not already running
    ensureWorkersStarted();
}

void ImageLoader::executeRotatableLoad(const RotatableLoadRequest& request) {
    const std::string& url = request.url;
    RotatableLoadCallback callback = request.callback;
    RotatableImage* target = request.target;
    int segment = request.segment;
    int totalSegments = request.totalSegments;
    std::shared_ptr<bool> alive = request.alive;

    // Early out if owner was destroyed before we started
    if (alive && !*alive) return;

    std::string imageBody;
    bool loadSuccess = false;

    // Check if this is a local file path (Vita paths start with ux0: or similar)
    bool isLocalFile = (url.find("ux0:") == 0 || url.find("ur0:") == 0 ||
                        url.find("uma0:") == 0 || url.find("/") == 0);

    if (isLocalFile) {
        // Load from local file
        brls::Logger::debug("ImageLoader: Loading local file: {}", url);

#ifdef __vita__
        SceUID fd = sceIoOpen(url.c_str(), SCE_O_RDONLY, 0);
        if (fd >= 0) {
            SceOff fileSize = sceIoLseek(fd, 0, SCE_SEEK_END);
            sceIoLseek(fd, 0, SCE_SEEK_SET);

            if (fileSize > 0 && fileSize < 50 * 1024 * 1024) {  // Max 50MB
                imageBody.resize(fileSize);
                SceSSize bytesRead = sceIoRead(fd, &imageBody[0], fileSize);
                if (bytesRead == fileSize) {
                    loadSuccess = true;
                    brls::Logger::debug("ImageLoader: Loaded {} bytes from local file", fileSize);
                }
            }
            sceIoClose(fd);
        } else {
            brls::Logger::error("ImageLoader: Failed to open local file: {}", url);
        }
#else
        // Non-Vita: use standard file I/O
        std::ifstream file(url, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            imageBody.resize(size);
            if (file.read(&imageBody[0], size)) {
                loadSuccess = true;
            }
        }
#endif
    } else {
        // Load from HTTP with automatic JWT refresh on 401/403
        HttpResponse resp = authenticatedGet(url, 2);
        if (resp.success && !resp.body.empty()) {
            imageBody = std::move(resp.body);
            loadSuccess = true;
        }
    }

    if (loadSuccess && !imageBody.empty()) {
        brls::Logger::debug("ImageLoader: Loaded {} bytes from {} for RotatableImage", imageBody.size(), url);

        // Check image format
        bool isWebP = false;
        bool isJpegOrPng = false;
        bool isValidImage = false;

        if (imageBody.size() > 12) {
            unsigned char* data = reinterpret_cast<unsigned char*>(imageBody.data());

            // JPEG
            if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
                isJpegOrPng = true;
                isValidImage = true;
            }
            // PNG
            else if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
                isJpegOrPng = true;
                isValidImage = true;
            }
            // GIF (pass through - typically small enough)
            else if (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46) {
                isValidImage = true;
            }
            // BMP
            else if (data[0] == 0x42 && data[1] == 0x4D) {
                isJpegOrPng = true;  // Treat like JPEG/PNG (stb_image can decode)
                isValidImage = true;
            }
            // WebP
            else if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46 &&
                     data[8] == 0x57 && data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50) {
                isWebP = true;
                isValidImage = true;
            }
        }

        // Fallback: try stb_image for unrecognized formats (.bin, TGA, etc.)
        if (!isValidImage && imageBody.size() > 4) {
            int testW, testH, testC;
            if (stbi_info_from_memory(
                    reinterpret_cast<const unsigned char*>(imageBody.data()),
                    static_cast<int>(imageBody.size()),
                    &testW, &testH, &testC)) {
                isJpegOrPng = true;  // stb_image can handle it
                isValidImage = true;
                brls::Logger::info("ImageLoader: stb_image detected unknown format {}x{} for {}", testW, testH, url);
            }
        }

        if (!isValidImage) {
            brls::Logger::warning("ImageLoader: Invalid image format for {}", url);
            return;
        }

        // Convert images to TGA with size limit for Vita GPU (max texture 2048x2048)
        // Webtoon images are often very tall and need to be split into segments
        const int MAX_TEXTURE_SIZE = 2048;
        std::vector<uint8_t> imageData;

        if (isWebP) {
            if (totalSegments > 1) {
                // Loading a specific segment of a tall WebP image
                imageData = convertWebPtoTGASegment(
                    reinterpret_cast<const uint8_t*>(imageBody.data()),
                    imageBody.size(),
                    segment,
                    totalSegments,
                    MAX_TEXTURE_SIZE
                );
                brls::Logger::info("ImageLoader: Loaded segment {}/{} of WebP", segment + 1, totalSegments);
            } else {
                // Normal WebP loading with downscaling if needed
                imageData = convertWebPtoTGA(
                    reinterpret_cast<const uint8_t*>(imageBody.data()),
                    imageBody.size(),
                    MAX_TEXTURE_SIZE
                );
            }
            if (imageData.empty()) {
                brls::Logger::error("ImageLoader: WebP conversion failed for {}", url);
                return;
            }
        } else if (isJpegOrPng) {
            if (totalSegments > 1) {
                // Loading a specific segment of a tall JPEG/PNG image
                imageData = convertImageToTGASegment(
                    reinterpret_cast<const uint8_t*>(imageBody.data()),
                    imageBody.size(),
                    segment,
                    totalSegments,
                    MAX_TEXTURE_SIZE
                );
                brls::Logger::info("ImageLoader: Loaded segment {}/{} of JPEG/PNG", segment + 1, totalSegments);
            } else {
                // Normal JPEG/PNG loading with downscaling if needed
                imageData = convertImageToTGA(
                    reinterpret_cast<const uint8_t*>(imageBody.data()),
                    imageBody.size(),
                    MAX_TEXTURE_SIZE
                );
            }
            if (imageData.empty()) {
                brls::Logger::error("ImageLoader: JPEG/PNG conversion failed for {}", url);
                return;
            }
        } else {
            // For GIF and other formats, pass through directly
            // NVG will handle loading
            imageData.assign(imageBody.begin(), imageBody.end());
            brls::Logger::debug("ImageLoader: Using image directly ({} bytes)", imageData.size());
        }

        // Cache the image using LRU (include segment in key if segmented)
        std::string cacheKey = url + "_full";
        if (totalSegments > 1) {
            cacheKey += "_seg" + std::to_string(segment);
        }
        cachePut(cacheKey, imageData);

        // Update UI on main thread (check alive flag to prevent use-after-free)
        brls::sync([imageData, callback, target, alive]() {
            if (alive && !*alive) return;  // Owner destroyed, skip
            if (target) {
                target->setImageFromMem(imageData.data(), imageData.size());
                if (callback) callback(target);
            }
        });
    } else {
        brls::Logger::error("ImageLoader: Failed to load {}", url);
    }
}

void ImageLoader::loadAsyncFullSize(const std::string& url, RotatableLoadCallback callback, RotatableImage* target,
                                    std::shared_ptr<bool> alive) {
    if (url.empty() || !target) return;
    if (alive && !*alive) return;

    // Check LRU cache first
    std::string cacheKey = url + "_full";
    {
        std::vector<uint8_t> cachedData;
        if (cacheGet(cacheKey, cachedData)) {
            target->setImageFromMem(cachedData.data(), cachedData.size());
            if (callback) callback(target);
            return;
        }
    }

    // Add to rotatable queue
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_rotatableLoadQueue.push({url, callback, target, 0, 1, alive});
    }

    s_queueCV.notify_one();
    ensureWorkersStarted();
}

void ImageLoader::loadAsyncFullSizeSegment(const std::string& url, int segment, int totalSegments,
                                            RotatableLoadCallback callback, RotatableImage* target,
                                            std::shared_ptr<bool> alive) {
    if (url.empty() || !target) return;
    if (alive && !*alive) return;

    // Validate segment parameters to prevent divide-by-zero and out-of-bounds
    if (totalSegments < 1) totalSegments = 1;
    if (segment < 0 || segment >= totalSegments) {
        brls::Logger::error("ImageLoader: Invalid segment {}/{}", segment, totalSegments);
        return;
    }

    // Check LRU cache first (with segment key)
    std::string cacheKey = url + "_full_seg" + std::to_string(segment);
    {
        std::vector<uint8_t> cachedData;
        if (cacheGet(cacheKey, cachedData)) {
            target->setImageFromMem(cachedData.data(), cachedData.size());
            if (callback) callback(target);
            return;
        }
    }

    // Add to rotatable queue with segment info
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_rotatableLoadQueue.push({url, callback, target, segment, totalSegments, alive});
    }

    s_queueCV.notify_one();
    ensureWorkersStarted();
}

void ImageLoader::preload(const std::string& url) {
    if (url.empty()) return;

    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_cacheMap.find(url) != s_cacheMap.end()) {
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_loadQueue.push({url, nullptr, nullptr, true});
    }

    s_queueCV.notify_one();
    ensureWorkersStarted();
}

bool ImageLoader::getImageDimensions(const std::string& url, int& width, int& height, int& suggestedSegments) {
    // Maximum texture size for Vita GPU
    const int MAX_TEXTURE_SIZE = 2048;

    // Default values
    width = 0;
    height = 0;
    suggestedSegments = 1;

    if (url.empty()) return false;

    std::string imageData;
    bool loadSuccess = false;

    // Check if this is a local file path
    bool isLocalFile = (url.find("ux0:") == 0 || url.find("ur0:") == 0 ||
                        url.find("uma0:") == 0 || url.find("/") == 0);

    if (isLocalFile) {
#ifdef __vita__
        SceUID fd = sceIoOpen(url.c_str(), SCE_O_RDONLY, 0);
        if (fd >= 0) {
            SceOff fileSize = sceIoLseek(fd, 0, SCE_SEEK_END);
            sceIoLseek(fd, 0, SCE_SEEK_SET);

            if (fileSize > 0 && fileSize < 50 * 1024 * 1024) {
                imageData.resize(fileSize);
                SceSSize bytesRead = sceIoRead(fd, &imageData[0], fileSize);
                if (bytesRead == fileSize) {
                    loadSuccess = true;
                }
            }
            sceIoClose(fd);
        }
#else
        std::ifstream file(url, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            imageData.resize(size);
            if (file.read(&imageData[0], size)) {
                loadSuccess = true;
            }
        }
#endif
    } else {
        // Load from HTTP with automatic JWT refresh on 401/403
        HttpResponse resp = authenticatedGet(url, 2);
        if (resp.success && !resp.body.empty()) {
            imageData = std::move(resp.body);
            loadSuccess = true;
        }
    }

    if (!loadSuccess || imageData.empty()) {
        brls::Logger::warning("ImageLoader::getImageDimensions: failed to fetch {}", url);
        return false;
    }

    // Check format and get dimensions
    if (imageData.size() < 12) return false;

    unsigned char* data = reinterpret_cast<unsigned char*>(imageData.data());

    // WebP
    if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46 &&
        data[8] == 0x57 && data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50) {
        if (WebPGetInfo(data, imageData.size(), &width, &height)) {
            suggestedSegments = calculateSegments(width, height, MAX_TEXTURE_SIZE);
            brls::Logger::info("ImageLoader: WebP {}x{} -> {} segments", width, height, suggestedSegments);
            return true;
        }
    }
    // PNG - dimensions at bytes 16-23
    else if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
        if (imageData.size() >= 24) {
            width = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
            height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
            suggestedSegments = calculateSegments(width, height, MAX_TEXTURE_SIZE);
            brls::Logger::info("ImageLoader: PNG {}x{} -> {} segments", width, height, suggestedSegments);
            return true;
        }
    }
    // JPEG - need to parse markers
    else if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        // Simple JPEG parser - look for SOF0/SOF2 markers
        size_t i = 2;
        while (i + 8 < imageData.size()) {
            if (data[i] != 0xFF) {
                i++;
                continue;
            }
            uint8_t marker = data[i + 1];
            // SOF0 (0xC0) or SOF2 (0xC2) contain dimensions
            if (marker == 0xC0 || marker == 0xC2) {
                height = (data[i + 5] << 8) | data[i + 6];
                width = (data[i + 7] << 8) | data[i + 8];
                suggestedSegments = calculateSegments(width, height, MAX_TEXTURE_SIZE);
                brls::Logger::info("ImageLoader: JPEG {}x{} -> {} segments", width, height, suggestedSegments);
                return true;
            }
            // Skip this marker segment
            if (marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7)) {
                i += 2;  // No length field
            } else {
                uint16_t len = (data[i + 2] << 8) | data[i + 3];
                i += 2 + len;
            }
        }
    }

    return false;
}

void ImageLoader::preloadFullSize(const std::string& url) {
    if (url.empty()) return;

    // Use same cache key as loadAsyncFullSize/executeRotatableLoad
    std::string cacheKey = url + "_full";

    // Check if already cached
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_cacheMap.find(cacheKey) != s_cacheMap.end()) {
            return;
        }
    }

    // Queue for full-size loading using rotatable queue (same as reader pages)
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_rotatableLoadQueue.push({url, nullptr, nullptr});  // No callback/target for preload
    }

    s_queueCV.notify_one();
    ensureWorkersStarted();
}

void ImageLoader::clearCache() {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_cacheList.clear();
    s_cacheMap.clear();
}

void ImageLoader::cancelAll() {
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        while (!s_loadQueue.empty()) {
            s_loadQueue.pop();
        }
        while (!s_rotatableLoadQueue.empty()) {
            s_rotatableLoadQueue.pop();
        }
    }
    // Also clear pending texture uploads
    {
        std::lock_guard<std::mutex> lock2(s_pendingMutex);
        while (!s_pendingTextures.empty()) {
            s_pendingTextures.pop();
        }
    }
}

} // namespace vitasuwayomi
