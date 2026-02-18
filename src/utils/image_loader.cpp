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
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace vitasuwayomi {

// Static member initialization
std::map<std::string, std::vector<uint8_t>> ImageLoader::s_cache;
std::mutex ImageLoader::s_cacheMutex;
std::string ImageLoader::s_authUsername;
std::string ImageLoader::s_authPassword;
std::string ImageLoader::s_accessToken;
std::queue<ImageLoader::LoadRequest> ImageLoader::s_loadQueue;
std::queue<ImageLoader::RotatableLoadRequest> ImageLoader::s_rotatableLoadQueue;
std::mutex ImageLoader::s_queueMutex;
std::atomic<int> ImageLoader::s_activeLoads{0};
int ImageLoader::s_maxConcurrentLoads = 4;  // Increased for faster library loading
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

    int width, height;
    if (!WebPGetInfo(webpData, webpSize, &width, &height)) {
        return tgaData;
    }

    uint8_t* rgba = WebPDecodeRGBA(webpData, webpSize, &width, &height);
    if (!rgba) {
        return tgaData;
    }

    // Calculate segment bounds
    int segmentHeight = (height + totalSegments - 1) / totalSegments;
    int startY = segment * segmentHeight;
    int endY = std::min(startY + segmentHeight, height);
    int actualHeight = endY - startY;

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

    int width, height, channels;
    // Force 4 channels (RGBA)
    uint8_t* rgba = stbi_load_from_memory(data, static_cast<int>(dataSize), &width, &height, &channels, 4);
    if (!rgba) {
        brls::Logger::error("ImageLoader: stb_image failed to decode image");
        return tgaData;
    }

    // Calculate segment bounds
    int segmentHeight = (height + totalSegments - 1) / totalSegments;
    int startY = segment * segmentHeight;
    int endY = std::min(startY + segmentHeight, height);
    int actualHeight = endY - startY;

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

// Helper to apply authentication headers to HTTP client
static void applyAuthHeaders(HttpClient& client) {
    // Prefer JWT Bearer auth if access token is available
    if (!ImageLoader::getAccessToken().empty()) {
        client.setDefaultHeader("Authorization", "Bearer " + ImageLoader::getAccessToken());
    } else if (!ImageLoader::getAuthUsername().empty() && !ImageLoader::getAuthPassword().empty()) {
        // Fall back to Basic Auth
        std::string credentials = ImageLoader::getAuthUsername() + ":" + ImageLoader::getAuthPassword();
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
    applyAuthHeaders(client);

    // Retry logic for slow/unreliable proxy servers
    const int maxRetries = 2;
    HttpResponse resp;
    bool success = false;

    for (int attempt = 0; attempt <= maxRetries && !success; attempt++) {
        if (attempt > 0) {
            brls::Logger::debug("ImageLoader: Retry {} for {}", attempt, url);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000 * attempt));  // 1s, 2s
        }

        resp = client.get(url);
        if (resp.success && !resp.body.empty()) {
            success = true;
        }
    }

    if (!success || resp.body.empty()) {
        brls::Logger::warning("ImageLoader: Failed to load {} after {} attempts", url, maxRetries + 1);
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

    // Cache the image in memory (larger cache for big libraries)
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_cache.size() > 80) {
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
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
                    if (s_cache.size() > 80) {
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
    int segment = request.segment;
    int totalSegments = request.totalSegments;

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
        // Load from HTTP
        HttpClient client;

        // Add authentication if needed
        applyAuthHeaders(client);

        HttpResponse resp = client.get(url);
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

        // Cache the image (include segment in key if segmented)
        std::string cacheKey = url + "_full";
        if (totalSegments > 1) {
            cacheKey += "_seg" + std::to_string(segment);
        }
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
        brls::Logger::error("ImageLoader: Failed to load {}", url);
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
        s_rotatableLoadQueue.push({url, callback, target, 0, 1});  // segment=0, totalSegments=1
    }

    processQueue();
}

void ImageLoader::loadAsyncFullSizeSegment(const std::string& url, int segment, int totalSegments,
                                            RotatableLoadCallback callback, RotatableImage* target) {
    if (url.empty() || !target) return;

    // Check cache first (with segment key)
    std::string cacheKey = url + "_full_seg" + std::to_string(segment);
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(cacheKey);
        if (it != s_cache.end()) {
            target->setImageFromMem(it->second.data(), it->second.size());
            if (callback) callback(target);
            return;
        }
    }

    // Add to rotatable queue with segment info
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_rotatableLoadQueue.push({url, callback, target, segment, totalSegments});
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
        HttpClient client;

        // Add authentication if needed
        applyAuthHeaders(client);

        HttpResponse resp = client.get(url);
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
        if (s_cache.find(cacheKey) != s_cache.end()) {
            return;
        }
    }

    // Queue for full-size loading using rotatable queue (same as reader pages)
    {
        std::lock_guard<std::mutex> lock(s_queueMutex);
        s_rotatableLoadQueue.push({url, nullptr, nullptr});  // No callback/target for preload
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
    while (!s_rotatableLoadQueue.empty()) {
        s_rotatableLoadQueue.pop();
    }
}

} // namespace vitasuwayomi
