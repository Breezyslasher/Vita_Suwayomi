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
std::mutex ImageLoader::s_tokenMutex;
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

// Helper function to downscale RGBA image using box filtering (area averaging)
// Produces much better quality than nearest-neighbor for manga/webtoon images
static void downscaleRGBA(const uint8_t* src, int srcW, int srcH,
                          uint8_t* dst, int dstW, int dstH) {
    float scaleX = (float)srcW / dstW;
    float scaleY = (float)srcH / dstH;

    for (int y = 0; y < dstH; y++) {
        for (int x = 0; x < dstW; x++) {
            // Box filter: average all source pixels that map to this destination pixel
            int sx0 = (int)(x * scaleX);
            int sy0 = (int)(y * scaleY);
            int sx1 = std::min((int)((x + 1) * scaleX), srcW);
            int sy1 = std::min((int)((y + 1) * scaleY), srcH);

            // Ensure at least one pixel
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            if (sx1 > srcW) sx1 = srcW;
            if (sy1 > srcH) sy1 = srcH;

            unsigned int sumR = 0, sumG = 0, sumB = 0, sumA = 0;
            int count = 0;

            for (int sy = sy0; sy < sy1; sy++) {
                for (int sx = sx0; sx < sx1; sx++) {
                    int srcIdx = (sy * srcW + sx) * 4;
                    sumR += src[srcIdx + 0];
                    sumG += src[srcIdx + 1];
                    sumB += src[srcIdx + 2];
                    sumA += src[srcIdx + 3];
                    count++;
                }
            }

            int dstIdx = (y * dstW + x) * 4;
            dst[dstIdx + 0] = static_cast<uint8_t>(sumR / count);
            dst[dstIdx + 1] = static_cast<uint8_t>(sumG / count);
            dst[dstIdx + 2] = static_cast<uint8_t>(sumB / count);
            dst[dstIdx + 3] = static_cast<uint8_t>(sumA / count);
        }
    }
}

// Helper to create TGA from RGBA data
static std::vector<uint8_t> createTGAFromRGBA(const uint8_t* rgba, int width, int height) {
    size_t imageSize = width * height * 4;
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
    std::lock_guard<std::mutex> lock(s_tokenMutex);
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
    std::shared_ptr<bool> alive = request.alive;

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
            // Call callback on failure so caller can clean up loading state
            brls::sync([callback, target, alive]() {
                if (alive && !*alive) return;
                if (callback) callback(target, false);
            });
            return;
        }

        // Convert images to TGA for Vita GPU compatibility
        const int MAX_TEXTURE_SIZE = 4096;
        std::vector<uint8_t> imageData;

        if (isWebP) {
            imageData = convertWebPtoTGA(
                reinterpret_cast<const uint8_t*>(imageBody.data()),
                imageBody.size(),
                MAX_TEXTURE_SIZE
            );
            if (imageData.empty()) {
                brls::Logger::error("ImageLoader: WebP conversion failed for {}", url);
                brls::sync([callback, target, alive]() {
                    if (alive && !*alive) return;
                    if (callback) callback(target, false);
                });
                return;
            }
        } else if (isJpegOrPng) {
            imageData = convertImageToTGA(
                reinterpret_cast<const uint8_t*>(imageBody.data()),
                imageBody.size(),
                MAX_TEXTURE_SIZE
            );
            if (imageData.empty()) {
                brls::Logger::error("ImageLoader: JPEG/PNG conversion failed for {}", url);
                brls::sync([callback, target, alive]() {
                    if (alive && !*alive) return;
                    if (callback) callback(target, false);
                });
                return;
            }
        } else {
            // For GIF and other formats, pass through directly
            imageData.assign(imageBody.begin(), imageBody.end());
            brls::Logger::debug("ImageLoader: Using image directly ({} bytes)", imageData.size());
        }

        // Cache the image using LRU
        std::string cacheKey = url + "_full";
        cachePut(cacheKey, imageData);

        // Update UI on main thread
        brls::sync([imageData, callback, target, alive]() {
            if (target) {
                // Skip if the owning view was destroyed while the image was downloading
                if (alive && !*alive) return;
                target->setImageFromMem(imageData.data(), imageData.size());
                if (callback) callback(target, true);
            }
        });
    } else {
        brls::Logger::error("ImageLoader: Failed to load {}", url);
        // Call callback on failure so caller can clean up loading state and retry
        brls::sync([callback, target, alive]() {
            if (alive && !*alive) return;
            if (callback) callback(target, false);
        });
    }
}

void ImageLoader::loadAsyncFullSize(const std::string& url, RotatableLoadCallback callback, RotatableImage* target,
                                    std::shared_ptr<bool> alive) {
    if (url.empty() || !target) return;

    // Check LRU cache first
    std::string cacheKey = url + "_full";
    {
        std::vector<uint8_t> cachedData;
        if (cacheGet(cacheKey, cachedData)) {
            target->setImageFromMem(cachedData.data(), cachedData.size());
            if (callback) callback(target, true);
            return;
        }
    }

    // Add to rotatable queue
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_rotatableLoadQueue.push({url, callback, target, alive});
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
