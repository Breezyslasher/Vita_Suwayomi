/**
 * VitaSuwayomi - Asynchronous Image Loader implementation
 * With concurrent load limiting and image downscaling for PS Vita memory constraints
 */

#include "utils/image_loader.hpp"
#include "utils/http_client.hpp"
#include "app/suwayomi_client.hpp"

// WebP decoding support
#include <webp/decode.h>
#include <cstring>

namespace vitasuwayomi {

// Static member initialization
std::map<std::string, std::vector<uint8_t>> ImageLoader::s_cache;
std::mutex ImageLoader::s_cacheMutex;
std::string ImageLoader::s_authUsername;
std::string ImageLoader::s_authPassword;
std::queue<ImageLoader::LoadRequest> ImageLoader::s_loadQueue;
std::mutex ImageLoader::s_queueMutex;
std::atomic<int> ImageLoader::s_activeLoads{0};
int ImageLoader::s_maxConcurrentLoads = 4;  // Limit concurrent loads for Vita
int ImageLoader::s_maxThumbnailSize = 200;  // Max dimension for thumbnails

// Helper function to downscale RGBA image using simple box filter
static void downscaleRGBA(const uint8_t* src, int srcW, int srcH,
                          uint8_t* dst, int dstW, int dstH) {
    float scaleX = (float)srcW / dstW;
    float scaleY = (float)srcH / dstH;

    for (int y = 0; y < dstH; y++) {
        for (int x = 0; x < dstW; x++) {
            // Simple point sampling for speed
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

// Helper function to convert WebP to TGA format with optional downscaling
// Adds a 1-pixel transparent border to prevent GPU edge clamping artifacts
static std::vector<uint8_t> convertWebPtoTGA(const uint8_t* webpData, size_t webpSize, int maxSize) {
    std::vector<uint8_t> tgaData;

    // Get WebP image info
    int width, height;
    if (!WebPGetInfo(webpData, webpSize, &width, &height)) {
        brls::Logger::error("ImageLoader: Failed to get WebP info");
        return tgaData;
    }

    brls::Logger::debug("ImageLoader: Decoding WebP {}x{}", width, height);

    // Decode WebP to RGBA
    uint8_t* rgba = WebPDecodeRGBA(webpData, webpSize, &width, &height);
    if (!rgba) {
        brls::Logger::error("ImageLoader: Failed to decode WebP");
        return tgaData;
    }

    // Calculate target size with aspect ratio preservation
    int targetW = width;
    int targetH = height;
    bool needsDownscale = false;

    if (maxSize > 0 && (width > maxSize || height > maxSize)) {
        float scale = (float)maxSize / std::max(width, height);
        targetW = (int)(width * scale);
        targetH = (int)(height * scale);
        if (targetW < 1) targetW = 1;
        if (targetH < 1) targetH = 1;
        needsDownscale = true;
        brls::Logger::debug("ImageLoader: Downscaling from {}x{} to {}x{}", width, height, targetW, targetH);
    }

    // Downscale if needed
    uint8_t* finalRgba = rgba;
    std::vector<uint8_t> scaledRgba;

    if (needsDownscale) {
        scaledRgba.resize(targetW * targetH * 4);
        downscaleRGBA(rgba, width, height, scaledRgba.data(), targetW, targetH);
        finalRgba = scaledRgba.data();
    }

    // Add 1-pixel border by duplicating edge pixels to prevent GPU edge clamping artifacts
    // When GPU samples at texture edges during scaling, it clamps to edge pixels.
    // By duplicating edge pixels outward, the clamping uses the correct edge color.
    const int BORDER = 1;
    int paddedW = targetW + (BORDER * 2);
    int paddedH = targetH + (BORDER * 2);

    // Create TGA file in memory with padded dimensions
    size_t imageSize = paddedW * paddedH * 4;
    tgaData.resize(18 + imageSize);

    // TGA header
    uint8_t* header = tgaData.data();
    memset(header, 0, 18);
    header[2] = 2;          // Uncompressed true-color image
    header[12] = paddedW & 0xFF;
    header[13] = (paddedW >> 8) & 0xFF;
    header[14] = paddedH & 0xFF;
    header[15] = (paddedH >> 8) & 0xFF;
    header[16] = 32;        // 32 bits per pixel (RGBA)
    header[17] = 0x28;      // Image descriptor: top-left origin + 8 alpha bits

    uint8_t* pixels = tgaData.data() + 18;

    // Helper lambda to copy a pixel from source RGBA to dest BGRA
    auto copyPixel = [&](int srcX, int srcY, int dstX, int dstY) {
        // Clamp source coordinates to valid range
        srcX = std::max(0, std::min(srcX, targetW - 1));
        srcY = std::max(0, std::min(srcY, targetH - 1));

        int srcIdx = (srcY * targetW + srcX) * 4;
        int dstIdx = (dstY * paddedW + dstX) * 4;
        pixels[dstIdx + 0] = finalRgba[srcIdx + 2];  // B
        pixels[dstIdx + 1] = finalRgba[srcIdx + 1];  // G
        pixels[dstIdx + 2] = finalRgba[srcIdx + 0];  // R
        pixels[dstIdx + 3] = finalRgba[srcIdx + 3];  // A
    };

    // Copy entire padded area, clamping source coordinates to duplicate edges
    for (int dstY = 0; dstY < paddedH; dstY++) {
        for (int dstX = 0; dstX < paddedW; dstX++) {
            // Map destination to source (offset by border, then clamp)
            int srcX = dstX - BORDER;
            int srcY = dstY - BORDER;
            copyPixel(srcX, srcY, dstX, dstY);
        }
    }

    // Free WebP decoded data
    WebPFree(rgba);

    brls::Logger::debug("ImageLoader: Converted WebP to TGA {}x{} (padded to {}x{}, {} bytes)",
                        targetW, targetH, paddedW, paddedH, tgaData.size());
    return tgaData;
}

void ImageLoader::setAuthCredentials(const std::string& username, const std::string& password) {
    s_authUsername = username;
    s_authPassword = password;
}

void ImageLoader::setMaxConcurrentLoads(int max) {
    s_maxConcurrentLoads = max > 0 ? max : 1;
}

void ImageLoader::setMaxThumbnailSize(int maxSize) {
    s_maxThumbnailSize = maxSize > 0 ? maxSize : 0;
}

void ImageLoader::processQueue() {
    std::lock_guard<std::mutex> lock(s_queueMutex);

    // Process queue while we have capacity
    while (!s_loadQueue.empty() && s_activeLoads < s_maxConcurrentLoads) {
        LoadRequest request = s_loadQueue.front();
        s_loadQueue.pop();
        s_activeLoads++;

        // Execute load asynchronously
        brls::async([request]() {
            executeLoad(request);
            s_activeLoads--;
            // Try to process more from queue
            processQueue();
        });
    }
}

void ImageLoader::executeLoad(const LoadRequest& request) {
    const std::string& url = request.url;
    LoadCallback callback = request.callback;
    brls::Image* target = request.target;
    bool fullSize = request.fullSize;

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
        brls::Logger::debug("ImageLoader: Loaded {} bytes from {}", resp.body.size(), url);

        // Check if it's WebP and convert to TGA
        bool isWebP = false;
        bool isValidImage = false;

        if (resp.body.size() > 12) {
            unsigned char* data = reinterpret_cast<unsigned char*>(resp.body.data());

            // JPEG: FF D8 FF
            if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
                isValidImage = true;
            }
            // PNG: 89 50 4E 47
            else if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
                isValidImage = true;
            }
            // GIF: 47 49 46
            else if (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46) {
                isValidImage = true;
            }
            // WebP: RIFF....WEBP
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

        // Convert WebP to TGA if needed (with optional downscaling)
        std::vector<uint8_t> imageData;
        if (isWebP) {
            // Only downscale for thumbnails, not full-size manga pages
            int maxSize = fullSize ? 0 : s_maxThumbnailSize;
            imageData = convertWebPtoTGA(
                reinterpret_cast<const uint8_t*>(resp.body.data()),
                resp.body.size(),
                maxSize
            );
            if (imageData.empty()) {
                brls::Logger::error("ImageLoader: WebP conversion failed for {}", url);
                return;
            }
        } else {
            imageData.assign(resp.body.begin(), resp.body.end());
        }

        // Cache the image (with size limit)
        {
            std::lock_guard<std::mutex> lock(s_cacheMutex);
            // More aggressive cache eviction for Vita memory
            if (s_cache.size() > 50) {
                brls::Logger::debug("ImageLoader: Cache full, clearing {} entries", s_cache.size());
                s_cache.clear();
            }
            // Use different cache key for full-size images
            std::string cacheKey = fullSize ? (url + "_full") : url;
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

void ImageLoader::loadAsync(const std::string& url, LoadCallback callback, brls::Image* target) {
    if (url.empty() || !target) return;

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(url);
        if (it != s_cache.end()) {
            target->setImageFromMem(it->second.data(), it->second.size());
            if (callback) callback(target);
            return;
        }
    }

    // Add to queue (with thumbnail downscaling)
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_loadQueue.push({url, callback, target, false});
    }

    // Try to process queue
    processQueue();
}

void ImageLoader::loadAsyncFullSize(const std::string& url, LoadCallback callback, brls::Image* target) {
    if (url.empty() || !target) return;

    // Check cache first (use different cache key for full size)
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

    // Add to queue (without downscaling)
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_loadQueue.push({url, callback, target, true});
    }

    // Try to process queue
    processQueue();
}

void ImageLoader::preload(const std::string& url) {
    if (url.empty()) return;

    // Check if already cached
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_cache.find(url) != s_cache.end()) {
            return;
        }
    }

    // Add to queue with null target (with downscaling)
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_loadQueue.push({url, nullptr, nullptr, false});
    }

    processQueue();
}

void ImageLoader::preloadFullSize(const std::string& url) {
    if (url.empty()) return;

    // Check if already cached
    std::string cacheKey = url + "_full";
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_cache.find(cacheKey) != s_cache.end()) {
            return;
        }
    }

    // Add to queue with null target (without downscaling)
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_loadQueue.push({url, nullptr, nullptr, true});
    }

    processQueue();
}

void ImageLoader::clearCache() {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_cache.clear();
    brls::Logger::info("ImageLoader: Cache cleared");
}

void ImageLoader::cancelAll() {
    std::lock_guard<std::mutex> lock(s_queueMutex);
    // Clear the queue
    while (!s_loadQueue.empty()) {
        s_loadQueue.pop();
    }
    brls::Logger::info("ImageLoader: Cancelled {} pending loads", s_loadQueue.size());
}

} // namespace vitasuwayomi
