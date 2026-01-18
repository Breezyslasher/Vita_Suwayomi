/**
 * VitaSuwayomi - Asynchronous Image Loader implementation
 * With concurrent load limiting and image downscaling for PS Vita memory constraints
 * Non-blocking design to prevent UI freezes
 */

#include "utils/image_loader.hpp"
#include "utils/http_client.hpp"
#include "app/suwayomi_client.hpp"

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
static std::vector<uint8_t> convertWebPtoTGA(const uint8_t* webpData, size_t webpSize, int maxSize) {
    std::vector<uint8_t> tgaData;

    int width, height;
    if (!WebPGetInfo(webpData, webpSize, &width, &height)) {
        brls::Logger::error("ImageLoader: Failed to get WebP info");
        return tgaData;
    }

    uint8_t* rgba = WebPDecodeRGBA(webpData, webpSize, &width, &height);
    if (!rgba) {
        brls::Logger::error("ImageLoader: Failed to decode WebP");
        return tgaData;
    }

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
    }

    uint8_t* finalRgba = rgba;
    std::vector<uint8_t> scaledRgba;

    if (needsDownscale) {
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
    for (int y = 0; y < targetH; y++) {
        for (int x = 0; x < targetW; x++) {
            int srcIdx = (y * targetW + x) * 4;
            int dstIdx = (y * targetW + x) * 4;
            pixels[dstIdx + 0] = finalRgba[srcIdx + 2];
            pixels[dstIdx + 1] = finalRgba[srcIdx + 1];
            pixels[dstIdx + 2] = finalRgba[srcIdx + 0];
            pixels[dstIdx + 3] = finalRgba[srcIdx + 3];
        }
    }

    WebPFree(rgba);
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
    // Get one request from the queue without blocking
    LoadRequest request;
    bool hasRequest = false;

    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        if (!s_loadQueue.empty() && s_activeLoads < s_maxConcurrentLoads) {
            request = s_loadQueue.front();
            s_loadQueue.pop();
            s_activeLoads++;
            hasRequest = true;
        }
    }

    if (hasRequest) {
        // Execute on background thread - don't use brls::async to avoid blocking
        std::thread([request]() {
            executeLoad(request);
            s_activeLoads--;

            // Schedule next load on main thread to avoid race conditions
            brls::sync([]() {
                processQueue();
            });
        }).detach();
    }
}

void ImageLoader::executeLoad(const LoadRequest& request) {
    const std::string& url = request.url;
    LoadCallback callback = request.callback;
    brls::Image* target = request.target;

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
        brls::Logger::error("ImageLoader: Failed to load {}: {}", url, resp.error);
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
        brls::Logger::warning("ImageLoader: Invalid image format for {}", url);
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
            brls::Logger::error("ImageLoader: WebP conversion failed for {}", url);
            return;
        }
    } else {
        imageData.assign(resp.body.begin(), resp.body.end());
    }

    // Cache the image
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_cache.size() > 50) {
            s_cache.clear();
        }
        s_cache[url] = imageData;
    }

    // Update UI on main thread - only if target is valid
    if (target) {
        brls::sync([imageData, callback, target]() {
            target->setImageFromMem(imageData.data(), imageData.size());
            if (callback) callback(target);
        });
    }
}

void ImageLoader::loadAsync(const std::string& url, LoadCallback callback, brls::Image* target) {
    if (url.empty() || !target) return;

    // Check cache first (on main thread, fast)
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(url);
        if (it != s_cache.end()) {
            target->setImageFromMem(it->second.data(), it->second.size());
            if (callback) callback(target);
            return;
        }
    }

    // Add to queue
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_loadQueue.push({url, callback, target});
    }

    // Start processing (non-blocking)
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
        s_loadQueue.push({url, nullptr, nullptr});
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
