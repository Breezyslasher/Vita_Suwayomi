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
#include <cmath>
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
size_t ImageLoader::s_maxCacheSize = 30;  // LRU cache: 30 entries to limit PS Vita memory usage
size_t ImageLoader::s_currentCacheMemory = 0;
static const size_t MAX_CACHE_MEMORY = 25 * 1024 * 1024;  // 25MB max cache memory (Vita has ~192MB total)
std::mutex ImageLoader::s_cacheMutex;
std::string ImageLoader::s_authUsername;
std::string ImageLoader::s_authPassword;
std::string ImageLoader::s_accessToken;
std::string ImageLoader::s_sessionCookie;
std::mutex ImageLoader::s_authMutex;
std::queue<ImageLoader::LoadRequest> ImageLoader::s_loadQueue;
std::queue<ImageLoader::RotatableLoadRequest> ImageLoader::s_rotatableLoadQueue;
std::set<std::string> ImageLoader::s_pendingFullSizeUrls;
std::mutex ImageLoader::s_queueMutex;
std::condition_variable ImageLoader::s_queueCV;
int ImageLoader::s_maxConcurrentLoads = 3;  // Worker thread count - kept low for PS Vita memory limits
int ImageLoader::s_maxThumbnailSize = 180;  // Smaller thumbnails for speed

// Worker thread pool
std::vector<std::thread> ImageLoader::s_workers;
std::atomic<bool> ImageLoader::s_workersStarted{false};
std::atomic<bool> ImageLoader::s_shutdownWorkers{false};

// Batched texture upload queue (thumbnails / brls::Image)
std::queue<ImageLoader::PendingTextureUpdate> ImageLoader::s_pendingTextures;
std::mutex ImageLoader::s_pendingMutex;
std::atomic<bool> ImageLoader::s_pendingScheduled{false};

// Batched texture upload queue (reader pages / RotatableImage)
std::queue<ImageLoader::PendingRotatableTextureUpdate> ImageLoader::s_pendingRotatableTextures;
std::mutex ImageLoader::s_pendingRotatableMutex;
std::atomic<bool> ImageLoader::s_pendingRotatableScheduled{false};

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
// Uses WebPDecode with crop + scale to avoid allocating the full image buffer
static std::vector<uint8_t> convertWebPtoTGASegment(const uint8_t* webpData, size_t webpSize,
                                                     int segment, int totalSegments, int maxSize) {
    if (totalSegments < 1 || segment < 0 || segment >= totalSegments) return {};

    WebPBitstreamFeatures features;
    VP8StatusCode status = WebPGetFeatures(webpData, webpSize, &features);
    if (status != VP8_STATUS_OK) {
        brls::Logger::error("ImageLoader: WebPGetFeatures failed in segment decode (status={})",
                            static_cast<int>(status));
        return {};
    }

    int width = features.width;
    int height = features.height;
    if (width <= 0 || height <= 0) return {};

    // Calculate segment bounds
    int segmentHeight = (height + totalSegments - 1) / totalSegments;
    int startY = segment * segmentHeight;
    int endY = std::min(startY + segmentHeight, height);
    int actualHeight = endY - startY;

    if (actualHeight <= 0 || startY >= height) return {};

    // Calculate target dimensions for the segment
    int targetW = width;
    int targetH = actualHeight;
    if (width > maxSize) {
        float scale = (float)maxSize / width;
        targetW = maxSize;
        targetH = std::max(1, (int)(actualHeight * scale));
    }

    // Use WebPDecode with crop + scale to avoid full image allocation
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) return {};

    // Crop to the segment region
    config.options.use_cropping = 1;
    config.options.crop_left = 0;
    config.options.crop_top = startY;
    config.options.crop_width = width;
    config.options.crop_height = actualHeight;

    // Scale if needed
    if (targetW != width || targetH != actualHeight) {
        config.options.use_scaling = 1;
        config.options.scaled_width = targetW;
        config.options.scaled_height = targetH;
    }

    config.output.colorspace = MODE_RGBA;

    VP8StatusCode decStatus = WebPDecode(webpData, webpSize, &config);
    if (decStatus != VP8_STATUS_OK) {
        brls::Logger::error("ImageLoader: WebP segment crop+scale failed (status={}) for {}x{} seg {}/{}",
                            static_cast<int>(decStatus), width, height, segment + 1, totalSegments);
        WebPFreeDecBuffer(&config.output);
        return {};
    }

    brls::Logger::debug("ImageLoader: Segment {}/{} - {}x{} (from {}x{} startY={})",
                        segment + 1, totalSegments, targetW, targetH, width, height, startY);

    auto tgaData = createTGAFromRGBA(config.output.u.RGBA.rgba, targetW, targetH);
    WebPFreeDecBuffer(&config.output);
    return tgaData;
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
// Uses WebPDecode with scaled output for large images to avoid allocating
// the full-resolution buffer (e.g., 800x15000 = 48MB RGBA).
static std::vector<uint8_t> convertWebPtoTGA(const uint8_t* webpData, size_t webpSize, int maxSize) {
    // Use WebPGetFeatures for detailed error info instead of just WebPGetInfo
    WebPBitstreamFeatures features;
    VP8StatusCode status = WebPGetFeatures(webpData, webpSize, &features);
    if (status != VP8_STATUS_OK) {
        brls::Logger::error("ImageLoader: WebPGetFeatures failed (status={}, dataSize={})",
                            static_cast<int>(status), webpSize);
        return {};
    }

    int width = features.width;
    int height = features.height;
    brls::Logger::debug("ImageLoader: WebP {}x{} hasAlpha={} format={}",
                        width, height, features.has_alpha, features.format);

    if (width <= 0 || height <= 0) {
        brls::Logger::error("ImageLoader: WebP has invalid dimensions {}x{}", width, height);
        return {};
    }

    // Calculate target dimensions
    int targetW = width;
    int targetH = height;
    if (maxSize > 0 && (width > maxSize || height > maxSize)) {
        float scale = (float)maxSize / std::max(width, height);
        targetW = std::max(1, (int)(width * scale));
        targetH = std::max(1, (int)(height * scale));
    }

    bool needsScaling = (targetW != width || targetH != height);

    // For large images that need downscaling, use WebPDecode with built-in scaling.
    // This avoids allocating the full-resolution buffer (which can be 48MB+ for
    // tall webtoon strips like 800x15000).
    if (needsScaling) {
        WebPDecoderConfig config;
        if (!WebPInitDecoderConfig(&config)) {
            brls::Logger::error("ImageLoader: WebPInitDecoderConfig failed");
            return {};
        }

        // Configure scaled output - decoder will only allocate target-size buffer
        config.options.use_scaling = 1;
        config.options.scaled_width = targetW;
        config.options.scaled_height = targetH;
        config.output.colorspace = MODE_RGBA;

        VP8StatusCode decStatus = WebPDecode(webpData, webpSize, &config);
        if (decStatus != VP8_STATUS_OK) {
            brls::Logger::warning("ImageLoader: WebP scaled RGBA decode failed (status={}) for {}x{}->{}x{}, trying RGB",
                                  static_cast<int>(decStatus), width, height, targetW, targetH);

            // Try RGB (uses 25% less memory)
            WebPFreeDecBuffer(&config.output);
            if (!WebPInitDecoderConfig(&config)) {
                return {};
            }
            config.options.use_scaling = 1;
            config.options.scaled_width = targetW;
            config.options.scaled_height = targetH;
            config.output.colorspace = MODE_RGB;

            decStatus = WebPDecode(webpData, webpSize, &config);
            if (decStatus != VP8_STATUS_OK) {
                brls::Logger::error("ImageLoader: WebP scaled decode completely failed (status={}) for {}x{} ({}KB)",
                                    static_cast<int>(decStatus), width, height, webpSize / 1024);
                WebPFreeDecBuffer(&config.output);
                return {};
            }

            // Convert RGB to RGBA for TGA
            uint8_t* rgb = config.output.u.RGBA.rgba;
            size_t pixelCount = static_cast<size_t>(targetW) * static_cast<size_t>(targetH);
            std::vector<uint8_t> rgbaVec(pixelCount * 4);
            for (size_t i = 0; i < pixelCount; i++) {
                rgbaVec[i * 4 + 0] = rgb[i * 3 + 0];
                rgbaVec[i * 4 + 1] = rgb[i * 3 + 1];
                rgbaVec[i * 4 + 2] = rgb[i * 3 + 2];
                rgbaVec[i * 4 + 3] = 255;
            }
            WebPFreeDecBuffer(&config.output);

            brls::Logger::info("ImageLoader: WebP scaled RGB decode OK {}x{}->{}x{}", width, height, targetW, targetH);
            return createTGAFromRGBA(rgbaVec.data(), targetW, targetH);
        }

        // Scaled RGBA decode succeeded
        auto tgaData = createTGAFromRGBA(config.output.u.RGBA.rgba, targetW, targetH);
        WebPFreeDecBuffer(&config.output);

        brls::Logger::info("ImageLoader: WebP scaled RGBA decode OK {}x{}->{}x{}", width, height, targetW, targetH);
        return tgaData;
    }

    // Image fits within maxSize - decode at full resolution (no scaling needed)
    uint8_t* rgba = WebPDecodeRGBA(webpData, webpSize, &width, &height);
    if (!rgba) {
        brls::Logger::error("ImageLoader: WebPDecodeRGBA failed for {}x{} ({}KB)",
                            width, height, webpSize / 1024);
        return {};
    }

    auto tgaData = createTGAFromRGBA(rgba, width, height);
    WebPFree(rgba);
    return tgaData;
}

void ImageLoader::setAuthCredentials(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(s_authMutex);
    s_authUsername = username;
    s_authPassword = password;
}

void ImageLoader::setAccessToken(const std::string& token) {
    std::lock_guard<std::mutex> lock(s_authMutex);
    s_accessToken = token;
}

std::string ImageLoader::getAuthUsername() {
    std::lock_guard<std::mutex> lock(s_authMutex);
    return s_authUsername;
}

std::string ImageLoader::getAuthPassword() {
    std::lock_guard<std::mutex> lock(s_authMutex);
    return s_authPassword;
}

std::string ImageLoader::getAccessToken() {
    std::lock_guard<std::mutex> lock(s_authMutex);
    return s_accessToken;
}

void ImageLoader::setSessionCookie(const std::string& cookie) {
    std::lock_guard<std::mutex> lock(s_authMutex);
    s_sessionCookie = cookie;
}

std::string ImageLoader::getSessionCookie() {
    std::lock_guard<std::mutex> lock(s_authMutex);
    return s_sessionCookie;
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
    // Read auth state once into local variables to avoid TOCTOU races
    std::string sessionCookie = ImageLoader::getSessionCookie();
    std::string token = ImageLoader::getAccessToken();

    if (!sessionCookie.empty()) {
        // SIMPLE_LOGIN: session cookie from POST /login.html
        client.setDefaultHeader("Cookie", sessionCookie);
    } else if (!token.empty()) {
        // UI_LOGIN: JWT Bearer token
        client.setDefaultHeader("Authorization", "Bearer " + token);
        client.setDefaultHeader("Cookie", "suwayomi-server-token=" + token);
    } else {
        // BASIC_AUTH fallback
        std::string username = ImageLoader::getAuthUsername();
        std::string password = ImageLoader::getAuthPassword();
        if (!username.empty() && !password.empty()) {
            std::string credentials = username + ":" + password;
            client.setDefaultHeader("Authorization", "Basic " + base64EncodeImage(credentials));
        }
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
            bool refreshed = suwayomiClient.refreshToken();
            // Always re-apply auth headers: even if OUR refresh failed,
            // another thread may have already refreshed the token successfully.
            tokenRefreshed = true;
            client.clearDefaultHeaders();
            applyAuthHeaders(client);
            if (refreshed) {
                brls::Logger::info("ImageLoader: Token refreshed, retrying download");
            } else {
                brls::Logger::warning("ImageLoader: Token refresh failed for {}, retrying with current token", url);
            }
            // Don't count this as a retry attempt
            attempt--;
            continue;
        }

        if (resp.success && !resp.body.empty()) {
            success = true;
        }
    }

    return resp;
}

void ImageLoader::setMaxConcurrentLoads(int max) {
    s_maxConcurrentLoads = std::max(1, std::min(max, 6));
}

void ImageLoader::setMaxThumbnailSize(int maxSize) {
    s_maxThumbnailSize = maxSize > 0 ? maxSize : 0;
}

void ImageLoader::cachePut(const std::string& url, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(s_cacheMutex);

    // If key already exists, remove old entry and track memory
    auto it = s_cacheMap.find(url);
    if (it != s_cacheMap.end()) {
        s_currentCacheMemory -= it->second->data.size();
        s_cacheList.erase(it->second);
        s_cacheMap.erase(it);
    }

    // Evict oldest entries if over count limit OR memory limit
    while (s_cacheList.size() >= s_maxCacheSize ||
           (s_currentCacheMemory + data.size() > MAX_CACHE_MEMORY && !s_cacheList.empty())) {
        auto& oldest = s_cacheList.back();
        s_currentCacheMemory -= oldest.data.size();
        s_cacheMap.erase(oldest.url);
        s_cacheList.pop_back();
    }

    // Insert at front (most recently used)
    s_cacheList.push_front({url, data});
    s_cacheMap[url] = s_cacheList.begin();
    s_currentCacheMemory += data.size();
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

void ImageLoader::queueRotatableTextureUpdate(const std::vector<uint8_t>& data, RotatableImage* target,
                                               RotatableLoadCallback callback, std::shared_ptr<bool> alive) {
    {
        std::lock_guard<std::mutex> lock(s_pendingRotatableMutex);
        PendingRotatableTextureUpdate update;
        update.data = data;
        update.target = target;
        update.callback = callback;
        update.alive = alive;
        update.isSegmented = false;
        s_pendingRotatableTextures.push(std::move(update));
    }

    // Schedule processing if not already scheduled
    bool expected = false;
    if (s_pendingRotatableScheduled.compare_exchange_strong(expected, true)) {
        brls::sync([]() {
            processPendingRotatableTextures();
        });
    }
}

void ImageLoader::queueRotatableSegmentUpdate(std::vector<std::vector<uint8_t>> segDatas, int origW, int origH,
                                               std::vector<int> segHeights, RotatableImage* target,
                                               RotatableLoadCallback callback, std::shared_ptr<bool> alive) {
    {
        std::lock_guard<std::mutex> lock(s_pendingRotatableMutex);
        PendingRotatableTextureUpdate update;
        update.segmentDatas = std::move(segDatas);
        update.origW = origW;
        update.origH = origH;
        update.segHeights = std::move(segHeights);
        update.target = target;
        update.callback = callback;
        update.alive = alive;
        update.isSegmented = true;
        s_pendingRotatableTextures.push(std::move(update));
    }

    bool expected = false;
    if (s_pendingRotatableScheduled.compare_exchange_strong(expected, true)) {
        brls::sync([]() {
            processPendingRotatableTextures();
        });
    }
}

void ImageLoader::processPendingRotatableTextures() {
    s_pendingRotatableScheduled = false;

    int processed = 0;
    int queueSize = 0;
    {
        std::lock_guard<std::mutex> lock(s_pendingRotatableMutex);
        queueSize = static_cast<int>(s_pendingRotatableTextures.size());
    }
    if (queueSize > 0) {
        brls::Logger::debug("ImageLoader: [TIMING] Processing rotatable textures ({} queued, max {} per frame)",
                           queueSize, MAX_ROTATABLE_TEXTURES_PER_FRAME);
    }

    while (processed < MAX_ROTATABLE_TEXTURES_PER_FRAME) {
        PendingRotatableTextureUpdate update;
        {
            std::lock_guard<std::mutex> lock(s_pendingRotatableMutex);
            if (s_pendingRotatableTextures.empty()) return;
            update = std::move(s_pendingRotatableTextures.front());
            s_pendingRotatableTextures.pop();
        }

        if (update.target) {
            if (update.alive && !*update.alive) {
                continue;  // Owner destroyed, skip
            }
            auto uploadStart = std::chrono::steady_clock::now();
            if (update.isSegmented) {
                update.target->setImageSegments(update.segmentDatas, update.origW, update.origH, update.segHeights);
            } else {
                update.target->setImageFromMem(update.data.data(), update.data.size());
            }
            auto uploadEnd = std::chrono::steady_clock::now();
            auto uploadMs = std::chrono::duration_cast<std::chrono::milliseconds>(uploadEnd - uploadStart).count();
            brls::Logger::debug("ImageLoader: [TIMING] GPU upload took {}ms ({})",
                               uploadMs, update.isSegmented ? "segmented" : "single");
            if (update.callback) update.callback(update.target);
        }
        processed++;
    }

    // If more pending, schedule another batch for the next frame
    bool morePending = false;
    {
        std::lock_guard<std::mutex> lock(s_pendingRotatableMutex);
        morePending = !s_pendingRotatableTextures.empty();
    }
    if (morePending) {
        bool expected = false;
        if (s_pendingRotatableScheduled.compare_exchange_strong(expected, true)) {
            brls::sync([]() {
                processPendingRotatableTextures();
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

    // Check alive flag after download - skip decode if owner was destroyed
    if (alive && !*alive) return;

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

    // Remove URL from the pending dedup set now that a worker has picked it up.
    // This must happen before any early return so new requests for this URL can
    // be queued if the current load is skipped (e.g., owner destroyed).
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_pendingFullSizeUrls.erase(url);
    }

    // Early out if owner was destroyed before we started
    if (alive && !*alive) return;

    auto loadStartTime = std::chrono::steady_clock::now();

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
        auto ioEndTime = std::chrono::steady_clock::now();
        auto ioMs = std::chrono::duration_cast<std::chrono::milliseconds>(ioEndTime - loadStartTime).count();
        brls::Logger::info("ImageLoader: [TIMING] I/O took {}ms for {} ({} bytes, {})",
                          ioMs, url, imageBody.size(), isLocalFile ? "local" : "network");

        // Check alive flag again after download - if the reader was closed while
        // we were downloading, skip the expensive decode/conversion to save memory
        if (alive && !*alive) {
            brls::Logger::debug("ImageLoader: Skipping decode for {} (owner destroyed during download)", url);
            return;
        }

        auto decodeStartTime = std::chrono::steady_clock::now();

        // Check image format
        bool isWebP = false;
        bool isJpegOrPng = false;
        bool isTGA = false;
        bool isValidImage = false;

        if (imageBody.size() > 18) {
            unsigned char* data = reinterpret_cast<unsigned char*>(imageBody.data());

            // TGA (uncompressed true-color, 32-bit BGRA) - pass through directly to
            // NanoVG without decode/re-encode since it's already in GPU-ready format.
            // Check header: ID=0, colormap=0, type=2, bpp=32.
            if (data[0] == 0 && data[1] == 0 && data[2] == 2 && data[16] == 32) {
                int tgaW = data[12] | (data[13] << 8);
                int tgaH = data[14] | (data[15] << 8);
                size_t expectedSize = 18 + static_cast<size_t>(tgaW) * tgaH * 4;
                if (tgaW > 0 && tgaH > 0 && imageBody.size() >= expectedSize) {
                    isTGA = true;
                    isValidImage = true;
                }
            }
        }

        if (!isValidImage && imageBody.size() > 12) {
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

        // Fallback: try stb_image for unrecognized formats (.bin, etc.)
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

        // TGA pass-through: already in GPU-ready format, skip decode/conversion
        if (isTGA) {
            brls::Logger::debug("ImageLoader: TGA pass-through ({} bytes) for {}", imageBody.size(), url);
            std::vector<uint8_t> imageData(imageBody.begin(), imageBody.end());

            std::string cacheKey = url + "_full";
            if (totalSegments > 1) {
                cacheKey += "_seg" + std::to_string(segment);
            }
            cachePut(cacheKey, imageData);

            {
                auto decodeEndTime = std::chrono::steady_clock::now();
                auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(decodeEndTime - loadStartTime).count();
                brls::Logger::info("ImageLoader: [TIMING] TGA pass-through, total {}ms for {}", totalMs, url);
            }

            if (target || callback) {
                queueRotatableTextureUpdate(imageData, target, callback, alive);
            }
            return;
        }

        // Convert images to TGA with size limit for Vita GPU (max texture 2048x2048)
        const int MAX_TEXTURE_SIZE = 2048;

        // Auto-split tall images to preserve width quality.
        // Without splitting, an 800x15000 image gets scaled to 109x2048 (proportional),
        // which looks extremely blurry when displayed at screen width.
        // By splitting into vertical segments, each segment keeps full image width.
        if (totalSegments == 1 && (isWebP || isJpegOrPng)) {
            int origW = 0, origH = 0;

            if (isWebP) {
                WebPBitstreamFeatures feat;
                if (WebPGetFeatures(reinterpret_cast<const uint8_t*>(imageBody.data()),
                                    imageBody.size(), &feat) == VP8_STATUS_OK) {
                    origW = feat.width;
                    origH = feat.height;
                }
            } else {
                int c;
                stbi_info_from_memory(reinterpret_cast<const unsigned char*>(imageBody.data()),
                                      static_cast<int>(imageBody.size()), &origW, &origH, &c);
            }

            if (origW > 0 && origH > MAX_TEXTURE_SIZE) {
                int autoSegments = (origH + MAX_TEXTURE_SIZE - 1) / MAX_TEXTURE_SIZE;

                // VRAM budget: cap total texture memory per image to prevent GPU exhaustion.
                // The segment function scales both dims proportionally when width > maxSize,
                // so total VRAM = maxSize² * origH / origW * 4.
                // Solve for maxSize: maxSize = sqrt(budget * origW / (origH * 4))
                const int MAX_VRAM_PER_IMAGE = 16 * 1024 * 1024;  // 16MB
                int segMaxSize = MAX_TEXTURE_SIZE;
                long long totalUnscaledBytes = (long long)origW * origH * 4;
                if (totalUnscaledBytes > MAX_VRAM_PER_IMAGE) {
                    float maxSizeF = std::sqrt((float)MAX_VRAM_PER_IMAGE * origW / ((float)origH * 4.0f));
                    segMaxSize = std::max(256, std::min((int)maxSizeF, MAX_TEXTURE_SIZE));
                    brls::Logger::info("ImageLoader: VRAM budget {}x{} -> maxWidth={}", origW, origH, segMaxSize);
                }

                brls::Logger::info("ImageLoader: Auto-splitting {}x{} into {} segments (maxSize={})",
                                   origW, origH, autoSegments, segMaxSize);

                std::vector<std::vector<uint8_t>> segmentDatas;
                std::vector<int> segSrcHeights;
                bool allOK = true;

                for (int seg = 0; seg < autoSegments && allOK; seg++) {
                    if (alive && !*alive) return;  // Owner destroyed during processing

                    std::vector<uint8_t> segData;
                    if (isWebP) {
                        segData = convertWebPtoTGASegment(
                            reinterpret_cast<const uint8_t*>(imageBody.data()),
                            imageBody.size(), seg, autoSegments, segMaxSize);
                    } else {
                        segData = convertImageToTGASegment(
                            reinterpret_cast<const uint8_t*>(imageBody.data()),
                            imageBody.size(), seg, autoSegments, segMaxSize);
                    }
                    if (segData.empty()) {
                        allOK = false;
                        break;
                    }

                    // Cache each segment
                    std::string segCK = url + "_full_autoseg" + std::to_string(seg);
                    cachePut(segCK, segData);
                    segmentDatas.push_back(std::move(segData));

                    // Track source height for proportional display
                    int segH = (origH + autoSegments - 1) / autoSegments;
                    int startY = seg * segH;
                    int endY = std::min(startY + segH, origH);
                    segSrcHeights.push_back(endY - startY);
                }

                if (allOK && !segmentDatas.empty()) {
                    // Store compact metadata under the main cache key so that
                    // loadAsyncFullSize can reconstruct from cached segments
                    // without re-reading the file from disk.
                    // Format: "ASEG" + uint16 count + uint16 origW + uint16 origH + uint16[] segHeights
                    std::vector<uint8_t> meta;
                    meta.resize(4 + 2 + 2 + 2 + 2 * autoSegments);
                    meta[0] = 'A'; meta[1] = 'S'; meta[2] = 'E'; meta[3] = 'G';
                    uint16_t cnt = static_cast<uint16_t>(autoSegments);
                    uint16_t mw = static_cast<uint16_t>(std::min(origW, 65535));
                    uint16_t mh = static_cast<uint16_t>(std::min(origH, 65535));
                    memcpy(&meta[4], &cnt, 2);
                    memcpy(&meta[6], &mw, 2);
                    memcpy(&meta[8], &mh, 2);
                    for (int s = 0; s < autoSegments; s++) {
                        uint16_t sh = static_cast<uint16_t>(segSrcHeights[s]);
                        memcpy(&meta[10 + s * 2], &sh, 2);
                    }
                    cachePut(url + "_full", meta);

                    {
                        auto decodeEndTime = std::chrono::steady_clock::now();
                        auto decodeMs = std::chrono::duration_cast<std::chrono::milliseconds>(decodeEndTime - decodeStartTime).count();
                        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(decodeEndTime - loadStartTime).count();
                        brls::Logger::info("ImageLoader: [TIMING] Decode took {}ms (auto-split {}x{} -> {} segs), total {}ms for {}",
                                          decodeMs, origW, origH, autoSegments, totalMs, url);
                    }

                    if (target || callback) {
                        queueRotatableSegmentUpdate(std::move(segmentDatas), origW, origH,
                                                    std::move(segSrcHeights), target, callback, alive);
                    }
                    return;  // Done - skip normal single-texture path
                }

                // Auto-split failed, fall through to normal proportional scaling
                brls::Logger::warning("ImageLoader: Auto-split failed for {}x{}, using proportional scaling", origW, origH);
            }
        }

        // Normal single-texture path (images that fit in one texture, or auto-split fallback)
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
                // WebP decode failed - fall back to stb_image in case format was
                // misidentified or stb can handle this particular encoding
                brls::Logger::warning("ImageLoader: WebP conversion failed for {}, trying stb_image fallback", url);
                imageData = convertImageToTGA(
                    reinterpret_cast<const uint8_t*>(imageBody.data()),
                    imageBody.size(),
                    MAX_TEXTURE_SIZE
                );
                if (imageData.empty()) {
                    brls::Logger::error("ImageLoader: All decode attempts failed for {}", url);
                    return;
                }
                brls::Logger::info("ImageLoader: stb_image fallback succeeded for {}", url);
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

        {
            auto decodeEndTime = std::chrono::steady_clock::now();
            auto decodeMs = std::chrono::duration_cast<std::chrono::milliseconds>(decodeEndTime - decodeStartTime).count();
            auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(decodeEndTime - loadStartTime).count();
            brls::Logger::info("ImageLoader: [TIMING] Decode took {}ms, total {}ms for {}", decodeMs, totalMs, url);
        }

        // Queue for batched texture upload on the main thread.
        // Skip entirely for preload-only requests (no target/callback)
        // to avoid copying large image data into the queue unnecessarily.
        if (target || callback) {
            queueRotatableTextureUpdate(imageData, target, callback, alive);
        }
    } else {
        auto failTime = std::chrono::steady_clock::now();
        auto failMs = std::chrono::duration_cast<std::chrono::milliseconds>(failTime - loadStartTime).count();
        brls::Logger::error("ImageLoader: Failed to load {} (after {}ms)", url, failMs);
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
            // Check for auto-segment metadata marker (stored when tall images are split)
            if (cachedData.size() >= 10 &&
                cachedData[0] == 'A' && cachedData[1] == 'S' &&
                cachedData[2] == 'E' && cachedData[3] == 'G') {
                // Reconstruct from cached segments
                uint16_t cnt, mw, mh;
                memcpy(&cnt, &cachedData[4], 2);
                memcpy(&mw, &cachedData[6], 2);
                memcpy(&mh, &cachedData[8], 2);
                int segCount = cnt;
                int origW = mw, origH = mh;

                std::vector<std::vector<uint8_t>> segDatas;
                std::vector<int> segHeights;
                bool allFound = true;

                for (int s = 0; s < segCount && allFound; s++) {
                    std::string segCK = url + "_full_autoseg" + std::to_string(s);
                    std::vector<uint8_t> segData;
                    if (cacheGet(segCK, segData)) {
                        segDatas.push_back(std::move(segData));
                        if (static_cast<size_t>(10 + s * 2 + 2) <= cachedData.size()) {
                            uint16_t sh;
                            memcpy(&sh, &cachedData[10 + s * 2], 2);
                            segHeights.push_back(sh);
                        }
                    } else {
                        allFound = false;  // Segment evicted, need full reload
                    }
                }

                if (allFound && !segDatas.empty()) {
                    // Route through batched queue to avoid GPU stalls when multiple
                    // cache hits happen in the same frame (e.g. scrolling back)
                    queueRotatableSegmentUpdate(std::move(segDatas), origW, origH,
                                                std::move(segHeights), target, callback);
                    return;
                }
                // Segments evicted from cache, fall through to full reload
                brls::Logger::debug("ImageLoader: Auto-seg cache partial miss for {}, reloading", url);
            } else {
                // Normal single-texture cache hit - route through batched queue
                queueRotatableTextureUpdate(cachedData, target, callback);
                return;
            }
        }
    }

    // Add to rotatable queue with dedup — if the URL is already queued or being
    // processed by a worker, skip to avoid wasting a worker thread on duplicate work.
    // The first load will cache the result; subsequent requests will hit the cache.
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        if (s_pendingFullSizeUrls.count(url) > 0) {
            brls::Logger::debug("ImageLoader: Skipping duplicate loadAsyncFullSize for {}", url);
            return;  // Already queued or being processed
        }
        s_pendingFullSizeUrls.insert(url);
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
            queueRotatableTextureUpdate(cachedData, target, callback, alive);
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

    // Check if already queued or being processed to avoid duplicate disk I/O and decode work.
    // On Vita's memory card, concurrent reads are serialized at the hardware level,
    // so duplicate reads waste time and starve the worker pool.
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        if (s_pendingFullSizeUrls.count(url)) {
            return;  // Already queued or being processed
        }
        s_pendingFullSizeUrls.insert(url);
        s_rotatableLoadQueue.push({url, nullptr, nullptr});  // No callback/target for preload
    }

    s_queueCV.notify_one();
    ensureWorkersStarted();
}

void ImageLoader::clearCache() {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_cacheList.clear();
    s_cacheMap.clear();
    s_currentCacheMemory = 0;
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
        s_pendingFullSizeUrls.clear();
    }
    // Also clear pending texture uploads (both thumbnail and reader page queues)
    {
        std::lock_guard<std::mutex> lock2(s_pendingMutex);
        while (!s_pendingTextures.empty()) {
            s_pendingTextures.pop();
        }
    }
    {
        std::lock_guard<std::mutex> lock3(s_pendingRotatableMutex);
        while (!s_pendingRotatableTextures.empty()) {
            s_pendingRotatableTextures.pop();
        }
    }
}

} // namespace vitasuwayomi
