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
#include <new>
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

// NanoSVG for SVG parsing and rasterization (header-only, lightweight)
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

// FFmpeg for AVIF/HEIF decoding (libraries already linked)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace vitasuwayomi {

// Static member initialization
std::list<ImageLoader::CacheEntry> ImageLoader::s_cacheList;
std::map<std::string, std::list<ImageLoader::CacheEntry>::iterator> ImageLoader::s_cacheMap;
size_t ImageLoader::s_maxCacheSize = 30;  // LRU cache: 30 entries to limit PS Vita memory usage
size_t ImageLoader::s_currentCacheMemory = 0;
static const size_t MAX_CACHE_MEMORY = 20 * 1024 * 1024;  // 20MB max cache memory (reduced from 25MB to prevent OOM with animated WebP pages)
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

// Memory pressure tracking: when decode operations fail (OOM, unsupported
// features, etc.), set a cooldown to let the system recover before attempting
// more decodes that could push the Vita past its memory limit.
static std::atomic<int> s_oomCooldownFrames{0};
static constexpr int OOM_COOLDOWN_DURATION = 60;  // ~1 second at 60fps
// Track consecutive decode failures to detect memory-pressure patterns even
// when the individual failure codes are not VP8_STATUS_OUT_OF_MEMORY.
static std::atomic<int> s_consecutiveDecodeFailures{0};
static constexpr int MAX_CONSECUTIVE_FAILURES_BEFORE_COOLDOWN = 3;

// Serialize heavy decode operations so only one worker decodes at a time.
// This prevents 3 workers from each allocating multi-MB decode buffers
// concurrently, which can push the Vita past its memory limit.
static std::mutex s_decodeMutex;

// Serialize animated WebP processing.  Each animated WebP cover is 2+ MB —
// with 3 worker threads, simultaneous downloads exhaust the Vita's memory.
// When one worker is processing an animated WebP, others that encounter one
// re-queue it and move on to the next (non-animated) image in the queue.
static std::mutex s_animatedWebPMutex;

// Signal that an OOM condition occurred during image decode.
// This sets a cooldown period during which new thumbnail loads are skipped
// to let the system recover before allocating more decode buffers.
static void signalOOM(const char* context) {
    int prev = s_oomCooldownFrames.load();
    if (prev < OOM_COOLDOWN_DURATION) {
        s_oomCooldownFrames.store(OOM_COOLDOWN_DURATION);
        brls::Logger::error("ImageLoader: OOM in {} - entering {}-frame cooldown", context, OOM_COOLDOWN_DURATION);
    }
}

// Track a decode failure. After MAX_CONSECUTIVE_FAILURES_BEFORE_COOLDOWN
// consecutive failures, trigger the OOM cooldown since the system is likely
// under memory pressure even if individual errors are not explicit OOM.
static void trackDecodeFailure(const char* context) {
    int fails = s_consecutiveDecodeFailures.fetch_add(1) + 1;
    if (fails >= MAX_CONSECUTIVE_FAILURES_BEFORE_COOLDOWN) {
        signalOOM(context);
    }
}

// Reset the consecutive failure counter on a successful decode.
static void trackDecodeSuccess() {
    s_consecutiveDecodeFailures.store(0);
}

// Check whether we are in an OOM cooldown period (decrements the counter).
static bool isUnderMemoryPressure() {
    int frames = s_oomCooldownFrames.load();
    if (frames > 0) {
        s_oomCooldownFrames.fetch_sub(1);
        return true;
    }
    return false;
}

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
    std::vector<uint8_t> tgaData;
    try {
        tgaData.resize(18 + imageSize);
    } catch (const std::bad_alloc&) {
        signalOOM("createTGAFromRGBA");
        return {};
    }

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

// Validate RIFF container integrity for WebP data.
// Returns true if the data looks like a complete (or at least decodable) WebP.
// Sets isTruncated=true if the RIFF header declares more data than we have.
static bool validateWebPContainer(const uint8_t* data, size_t size, bool& isTruncated) {
    isTruncated = false;
    if (size < 12) return false;

    // Check RIFF magic
    if (data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F') return false;
    // Check WEBP magic
    if (data[8] != 'W' || data[9] != 'E' || data[10] != 'B' || data[11] != 'P') return false;

    // RIFF container declares file size as (data[4..7] little-endian) + 8 bytes for RIFF header
    uint32_t declaredSize = static_cast<uint32_t>(data[4])
                          | (static_cast<uint32_t>(data[5]) << 8)
                          | (static_cast<uint32_t>(data[6]) << 16)
                          | (static_cast<uint32_t>(data[7]) << 24);
    uint64_t expectedTotal = static_cast<uint64_t>(declaredSize) + 8;

    if (size < expectedTotal) {
        // We have less data than the RIFF header claims — truncated download
        isTruncated = true;
        double pct = (size * 100.0) / expectedTotal;
        brls::Logger::warning("ImageLoader: WebP truncated - have {} of {} bytes ({:.0f}%)",
                              size, expectedTotal, pct);
        // Still return true — libwebp may be able to partially decode it
    }
    return true;
}

// Extract the first frame from an animated WebP by parsing the RIFF/ANMF chunk
// structure.  Returns a standalone (non-animated) WebP file that can be decoded
// with the regular WebPDecode API, or empty on failure.
// This avoids needing libwebpdemux or FFmpeg for animated WebP support.
static std::vector<uint8_t> extractFirstFrameFromAnimatedWebP(const uint8_t* data, size_t dataSize) {
    // Minimum: RIFF header (12) + at least one chunk header (8) + ANMF header (16)
    if (dataSize < 36) return {};

    // Verify RIFF WEBP header
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WEBP", 4) != 0) return {};

    // Scan top-level chunks starting after "RIFF size WEBP" (offset 12)
    size_t offset = 12;
    while (offset + 8 <= dataSize) {
        uint32_t chunkSize = static_cast<uint32_t>(data[offset + 4])
                           | (static_cast<uint32_t>(data[offset + 5]) << 8)
                           | (static_cast<uint32_t>(data[offset + 6]) << 16)
                           | (static_cast<uint32_t>(data[offset + 7]) << 24);

        if (memcmp(data + offset, "ANMF", 4) == 0) {
            // Found first ANMF chunk
            size_t payloadStart = offset + 8;
            if (payloadStart + 16 > dataSize) return {};  // need 16-byte frame header

            // ANMF payload: 16-byte header (x,y,w,h,duration,flags) + frame bitstream
            const uint8_t* frameData = data + payloadStart + 16;
            size_t frameDataSize = chunkSize > 16 ? chunkSize - 16 : 0;

            // Clamp to available data if truncated
            if (payloadStart + 16 + frameDataSize > dataSize) {
                frameDataSize = dataSize - payloadStart - 16;
            }
            if (frameDataSize < 8) return {};

            // Build a minimal RIFF WEBP container around the frame bitstream.
            // The frame data contains VP8/VP8L (and optional ALPH) sub-chunks
            // that form a valid non-animated WebP payload.
            uint32_t riffPayload = static_cast<uint32_t>(4 + frameDataSize);  // "WEBP" + data
            std::vector<uint8_t> result(12 + frameDataSize);
            memcpy(result.data(),     "RIFF", 4);
            result[4] = riffPayload & 0xFF;
            result[5] = (riffPayload >> 8) & 0xFF;
            result[6] = (riffPayload >> 16) & 0xFF;
            result[7] = (riffPayload >> 24) & 0xFF;
            memcpy(result.data() + 8, "WEBP", 4);
            memcpy(result.data() + 12, frameData, frameDataSize);

            brls::Logger::info("ImageLoader: Extracted first frame from animated WebP ({} bytes -> {} bytes)",
                               dataSize, result.size());
            return result;
        }

        // Advance to next chunk (RIFF chunks are padded to even size)
        size_t advance = 8 + chunkSize + (chunkSize & 1);
        if (advance == 0) break;  // prevent infinite loop on corrupted data
        offset += advance;
    }

    brls::Logger::warning("ImageLoader: No ANMF chunk found in animated WebP ({} bytes)", dataSize);
    return {};
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
    // Reduce memory usage during segment decode
    config.options.bypass_filtering = 1;
    config.options.no_fancy_upsampling = 1;

    // Scale if needed
    if (targetW != width || targetH != actualHeight) {
        config.options.use_scaling = 1;
        config.options.scaled_width = targetW;
        config.options.scaled_height = targetH;
    }

    config.output.colorspace = MODE_RGBA;

    VP8StatusCode decStatus = WebPDecode(webpData, webpSize, &config);
    if (decStatus != VP8_STATUS_OK) {
        if (decStatus == VP8_STATUS_OUT_OF_MEMORY) {
            signalOOM("WebPDecode segment");
        }
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
    try {
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
    } catch (const std::bad_alloc&) {
        stbi_image_free(rgba);
        signalOOM("convertImageToTGASegment");
        return tgaData;
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

    try {
        if (targetW != width || targetH != height) {
            scaledRgba.resize(targetW * targetH * 4);
            downscaleRGBA(rgba, width, height, scaledRgba.data(), targetW, targetH);
            finalRgba = scaledRgba.data();
        }

        tgaData = createTGAFromRGBA(finalRgba, targetW, targetH);
    } catch (const std::bad_alloc&) {
        stbi_image_free(rgba);
        signalOOM("convertImageToTGA");
        return {};
    }

    stbi_image_free(rgba);
    return tgaData;
}

// Check whether a VP8StatusCode indicates a non-recoverable error that will
// fail regardless of colorspace, resolution, or retry strategy.
static bool isNonRecoverableWebPStatus(VP8StatusCode status) {
    return status == VP8_STATUS_UNSUPPORTED_FEATURE ||
           status == VP8_STATUS_INVALID_PARAM;
}

// Internal helper: attempt a single WebP decode at the given target dimensions.
// Uses bypass_filtering and no_fancy_upsampling to reduce memory and improve
// reliability on PS Vita's constrained hardware.
// Returns empty vector on failure; sets *outStatus to the libwebp status code.
static std::vector<uint8_t> tryWebPDecode(const uint8_t* webpData, size_t webpSize,
                                           int srcW, int srcH, int targetW, int targetH,
                                           VP8StatusCode* outStatus = nullptr) {
    bool needsScaling = (targetW != srcW || targetH != srcH);

    if (needsScaling) {
        // --- Attempt 1: Scaled RGBA decode ---
        WebPDecoderConfig config;
        if (!WebPInitDecoderConfig(&config)) return {};

        config.options.use_scaling = 1;
        config.options.scaled_width = targetW;
        config.options.scaled_height = targetH;
        // Reduce memory usage and improve robustness for thumbnails:
        // bypass_filtering skips loop-filtering (slightly lower quality, much less memory)
        // no_fancy_upsampling uses simpler chroma upsampling (less memory, faster)
        config.options.bypass_filtering = 1;
        config.options.no_fancy_upsampling = 1;
        config.output.colorspace = MODE_RGBA;

        VP8StatusCode decStatus = WebPDecode(webpData, webpSize, &config);
        if (outStatus) *outStatus = decStatus;
        if (decStatus == VP8_STATUS_OK) {
            auto tgaData = createTGAFromRGBA(config.output.u.RGBA.rgba, targetW, targetH);
            WebPFreeDecBuffer(&config.output);
            trackDecodeSuccess();
            brls::Logger::info("ImageLoader: WebP RGBA decode OK {}x{}->{}x{}", srcW, srcH, targetW, targetH);
            return tgaData;
        }

        if (decStatus == VP8_STATUS_OUT_OF_MEMORY) {
            signalOOM("WebPDecode RGBA scaled");
        }
        WebPFreeDecBuffer(&config.output);

        // Non-recoverable errors (unsupported feature, invalid param) won't
        // be fixed by switching colorspace or resolution — bail out immediately
        // to avoid wasting memory on futile retries.
        if (isNonRecoverableWebPStatus(decStatus)) {
            brls::Logger::warning("ImageLoader: WebP non-recoverable error (status={}) for {}x{}->{}x{}, skipping retries",
                                  static_cast<int>(decStatus), srcW, srcH, targetW, targetH);
            return {};
        }

        brls::Logger::warning("ImageLoader: WebP scaled RGBA failed (status={}) for {}x{}->{}x{}, trying RGB",
                              static_cast<int>(decStatus), srcW, srcH, targetW, targetH);

        // --- Attempt 2: Scaled RGB decode (25% less memory) ---
        if (!WebPInitDecoderConfig(&config)) return {};
        config.options.use_scaling = 1;
        config.options.scaled_width = targetW;
        config.options.scaled_height = targetH;
        config.options.bypass_filtering = 1;
        config.options.no_fancy_upsampling = 1;
        config.output.colorspace = MODE_RGB;

        decStatus = WebPDecode(webpData, webpSize, &config);
        if (outStatus) *outStatus = decStatus;
        if (decStatus == VP8_STATUS_OK) {
            // Convert RGB to RGBA for TGA
            uint8_t* rgb = config.output.u.RGBA.rgba;
            size_t pixelCount = static_cast<size_t>(targetW) * static_cast<size_t>(targetH);
            std::vector<uint8_t> rgbaVec;
            try {
                rgbaVec.resize(pixelCount * 4);
            } catch (const std::bad_alloc&) {
                WebPFreeDecBuffer(&config.output);
                signalOOM("WebP RGB->RGBA alloc");
                return {};
            }
            for (size_t i = 0; i < pixelCount; i++) {
                rgbaVec[i * 4 + 0] = rgb[i * 3 + 0];
                rgbaVec[i * 4 + 1] = rgb[i * 3 + 1];
                rgbaVec[i * 4 + 2] = rgb[i * 3 + 2];
                rgbaVec[i * 4 + 3] = 255;
            }
            WebPFreeDecBuffer(&config.output);
            trackDecodeSuccess();
            brls::Logger::info("ImageLoader: WebP RGB decode OK {}x{}->{}x{}", srcW, srcH, targetW, targetH);
            return createTGAFromRGBA(rgbaVec.data(), targetW, targetH);
        }

        if (decStatus == VP8_STATUS_OUT_OF_MEMORY) {
            signalOOM("WebPDecode RGB scaled");
        }
        WebPFreeDecBuffer(&config.output);
        return {};
    }

    // No scaling needed — decode at full resolution
    uint8_t* rgba = WebPDecodeRGBA(webpData, webpSize, &srcW, &srcH);
    if (!rgba) {
        if (outStatus) *outStatus = VP8_STATUS_BITSTREAM_ERROR;
        return {};
    }
    if (outStatus) *outStatus = VP8_STATUS_OK;
    trackDecodeSuccess();
    auto tgaData = createTGAFromRGBA(rgba, srcW, srcH);
    WebPFree(rgba);
    return tgaData;
}

// Helper function to convert WebP to TGA format with downscaling.
// Uses WebPDecode with scaled output for large images to avoid allocating
// the full-resolution buffer (e.g., 800x15000 = 48MB RGBA).
// Includes multiple fallback strategies:
//   1. Validate RIFF container for truncation detection
//   2. Decode at target size with bypass_filtering for robustness
//   3. On failure, retry at half the target size
//   4. Try RGB colorspace (25% less memory) at each size
static std::vector<uint8_t> convertWebPtoTGA(const uint8_t* webpData, size_t webpSize, int maxSize) {
    // Validate RIFF container integrity before spending time on decode
    bool isTruncated = false;
    if (!validateWebPContainer(webpData, webpSize, isTruncated)) {
        brls::Logger::error("ImageLoader: Invalid WebP container (dataSize={})", webpSize);
        return {};
    }

    // Use WebPGetFeatures for detailed error info
    WebPBitstreamFeatures features;
    VP8StatusCode status = WebPGetFeatures(webpData, webpSize, &features);
    if (status != VP8_STATUS_OK) {
        brls::Logger::error("ImageLoader: WebPGetFeatures failed (status={}, dataSize={}, truncated={})",
                            static_cast<int>(status), webpSize, isTruncated);
        return {};
    }

    int width = features.width;
    int height = features.height;
    brls::Logger::debug("ImageLoader: WebP {}x{} hasAlpha={} format={} hasAnimation={} truncated={}",
                        width, height, features.has_alpha, features.format, features.has_animation, isTruncated);

    if (width <= 0 || height <= 0) {
        brls::Logger::error("ImageLoader: WebP has invalid dimensions {}x{}", width, height);
        return {};
    }

    // Animated WebP cannot be decoded by the simple WebPDecode API — it returns
    // VP8_STATUS_UNSUPPORTED_FEATURE.  Extract the first frame from the ANMF
    // chunk and decode that as a standalone static WebP image.
    if (features.has_animation) {
        brls::Logger::info("ImageLoader: Animated WebP detected ({}x{}, {}KB) — extracting first frame",
                           width, height, webpSize / 1024);
        auto firstFrame = extractFirstFrameFromAnimatedWebP(webpData, webpSize);
        if (firstFrame.empty()) {
            brls::Logger::error("ImageLoader: Failed to extract first frame from animated WebP");
            trackDecodeFailure("WebP animated frame extract");
            return {};
        }
        // Recursively decode the extracted (non-animated) first frame
        return convertWebPtoTGA(firstFrame.data(), firstFrame.size(), maxSize);
    }

    // Calculate target dimensions
    int targetW = width;
    int targetH = height;
    if (maxSize > 0 && (width > maxSize || height > maxSize)) {
        float scale = (float)maxSize / std::max(width, height);
        targetW = std::max(1, (int)(width * scale));
        targetH = std::max(1, (int)(height * scale));
    }

    // Attempt 1: Decode at requested target size
    VP8StatusCode lastStatus = VP8_STATUS_OK;
    auto result = tryWebPDecode(webpData, webpSize, width, height, targetW, targetH, &lastStatus);
    if (!result.empty()) return result;

    // Non-recoverable errors (unsupported feature, invalid param) won't be
    // helped by reducing resolution — skip the half-size retry to avoid
    // wasting memory on the Vita's constrained hardware.
    if (isNonRecoverableWebPStatus(lastStatus)) {
        brls::Logger::error("ImageLoader: WebP libwebp non-recoverable (status={}) for {}x{} ({}KB, truncated={})",
                            static_cast<int>(lastStatus), width, height, webpSize / 1024, isTruncated);
        trackDecodeFailure("WebP non-recoverable");
        return {};
    }

    // Attempt 2: Retry at half the target size (reduces decode memory pressure).
    // Particularly helps with truncated data or borderline-OOM situations.
    int halfW = std::max(1, targetW / 2);
    int halfH = std::max(1, targetH / 2);
    if (halfW != targetW || halfH != targetH) {
        brls::Logger::warning("ImageLoader: WebP retrying at half size {}x{} (was {}x{})",
                              halfW, halfH, targetW, targetH);
        result = tryWebPDecode(webpData, webpSize, width, height, halfW, halfH);
        if (!result.empty()) return result;
    }

    brls::Logger::error("ImageLoader: WebP libwebp decode failed for {}x{} ({}KB, truncated={})",
                        width, height, webpSize / 1024, isTruncated);
    trackDecodeFailure("WebP all attempts");
    return {};
}

// Convert SVG to TGA with rasterization via nanosvg
// SVG is rasterized at the target size for crisp rendering
static std::vector<uint8_t> convertSVGtoTGA(const uint8_t* data, size_t dataSize, int maxSize) {
    // nsvgParse modifies the input string, so make a mutable copy
    std::string svgStr(reinterpret_cast<const char*>(data), dataSize);

    NSVGimage* image = nsvgParse(&svgStr[0], "px", 96.0f);
    if (!image) {
        brls::Logger::error("ImageLoader: nsvgParse failed");
        return {};
    }

    if (image->width <= 0 || image->height <= 0) {
        brls::Logger::error("ImageLoader: SVG has invalid dimensions {}x{}", image->width, image->height);
        nsvgDelete(image);
        return {};
    }

    // Calculate rasterization scale to fit within maxSize
    float scale = 1.0f;
    int w = static_cast<int>(image->width);
    int h = static_cast<int>(image->height);

    if (maxSize > 0 && (w > maxSize || h > maxSize)) {
        scale = static_cast<float>(maxSize) / std::max(w, h);
        w = std::max(1, static_cast<int>(w * scale));
        h = std::max(1, static_cast<int>(h * scale));
    }

    // Sanity check dimensions for Vita memory
    if (static_cast<size_t>(w) * h * 4 > 256 * 1024 * 1024) {
        brls::Logger::error("ImageLoader: SVG rasterized size too large {}x{}", w, h);
        nsvgDelete(image);
        return {};
    }

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        brls::Logger::error("ImageLoader: nsvgCreateRasterizer failed");
        nsvgDelete(image);
        return {};
    }

    std::vector<uint8_t> rgba(w * h * 4);
    nsvgRasterize(rast, image, 0, 0, scale, rgba.data(), w, h, w * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    brls::Logger::info("ImageLoader: SVG rasterized to {}x{} (scale={})", w, h, scale);
    return createTGAFromRGBA(rgba.data(), w, h);
}

// Detect if data is SVG (text-based XML with <svg element)
static bool isSVGData(const uint8_t* data, size_t size) {
    if (size < 4) return false;

    // Skip BOM if present
    size_t offset = 0;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        offset = 3;  // UTF-8 BOM
    }

    // Skip leading whitespace
    while (offset < size && (data[offset] == ' ' || data[offset] == '\t' ||
           data[offset] == '\r' || data[offset] == '\n')) {
        offset++;
    }

    // Check for XML declaration or SVG tag
    size_t remaining = size - offset;
    if (remaining >= 5 && memcmp(data + offset, "<?xml", 5) == 0) return true;
    if (remaining >= 4 && memcmp(data + offset, "<svg", 4) == 0) return true;
    // Also check for SVG with namespace prefix
    if (remaining >= 5 && memcmp(data + offset, "<SVG", 4) == 0) return true;

    return false;
}

// Detect AVIF format (ISOBMFF container with 'avif'/'avis'/'mif1' brand)
static bool isAVIFData(const uint8_t* data, size_t size) {
    if (size < 12) return false;
    // ISOBMFF: bytes 4-7 = 'ftyp', bytes 8-11 = brand
    if (data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p') {
        if (memcmp(data + 8, "avif", 4) == 0) return true;
        if (memcmp(data + 8, "avis", 4) == 0) return true;
        // mif1 can be either AVIF or HEIF - check compatible brands
        if (memcmp(data + 8, "mif1", 4) == 0) {
            // Read box size and scan compatible brands for 'avif'
            uint32_t boxSize = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
            if (boxSize > size) boxSize = static_cast<uint32_t>(size);
            for (uint32_t i = 16; i + 4 <= boxSize; i += 4) {
                if (memcmp(data + i, "avif", 4) == 0) return true;
            }
        }
    }
    return false;
}

// Detect HEIF/HEIC format (ISOBMFF container with 'heic'/'heix'/'hevc'/'heim' brand)
static bool isHEIFData(const uint8_t* data, size_t size) {
    if (size < 12) return false;
    if (data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p') {
        if (memcmp(data + 8, "heic", 4) == 0) return true;
        if (memcmp(data + 8, "heix", 4) == 0) return true;
        if (memcmp(data + 8, "hevc", 4) == 0) return true;
        if (memcmp(data + 8, "heim", 4) == 0) return true;
        if (memcmp(data + 8, "heis", 4) == 0) return true;
        // mif1 without avif brand = likely HEIF
        if (memcmp(data + 8, "mif1", 4) == 0) {
            uint32_t boxSize = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
            if (boxSize > size) boxSize = static_cast<uint32_t>(size);
            for (uint32_t i = 16; i + 4 <= boxSize; i += 4) {
                if (memcmp(data + i, "heic", 4) == 0 || memcmp(data + i, "hevc", 4) == 0) return true;
            }
        }
    }
    return false;
}

// Custom AVIOContext read callback for FFmpeg memory-based I/O
struct FFmpegMemoryBuffer {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

static int ffmpegReadPacket(void* opaque, uint8_t* buf, int buf_size) {
    FFmpegMemoryBuffer* mb = static_cast<FFmpegMemoryBuffer*>(opaque);
    size_t remaining = mb->size - mb->pos;
    if (remaining <= 0) return AVERROR_EOF;
    size_t toRead = std::min(static_cast<size_t>(buf_size), remaining);
    memcpy(buf, mb->data + mb->pos, toRead);
    mb->pos += toRead;
    return static_cast<int>(toRead);
}

static int64_t ffmpegSeek(void* opaque, int64_t offset, int whence) {
    FFmpegMemoryBuffer* mb = static_cast<FFmpegMemoryBuffer*>(opaque);
    switch (whence) {
        case SEEK_SET:
            mb->pos = static_cast<size_t>(offset);
            break;
        case SEEK_CUR:
            mb->pos += static_cast<size_t>(offset);
            break;
        case SEEK_END:
            mb->pos = mb->size + static_cast<size_t>(offset);
            break;
        case AVSEEK_SIZE:
            return static_cast<int64_t>(mb->size);
        default:
            return -1;
    }
    if (mb->pos > mb->size) mb->pos = mb->size;
    return static_cast<int64_t>(mb->pos);
}

// Decode AVIF or HEIF image using FFmpeg and convert to TGA
// Works by creating a memory-based AVIOContext, using avformat to demux,
// then avcodec to decode the single image frame, and swscale to convert to RGBA.
static std::vector<uint8_t> convertFFmpegImageToTGA(const uint8_t* data, size_t dataSize, int maxSize, const char* formatName) {
    std::vector<uint8_t> result;

    // Allocate I/O buffer for AVIOContext
    const int ioBufferSize = 32768;
    uint8_t* ioBuffer = static_cast<uint8_t*>(av_malloc(ioBufferSize));
    if (!ioBuffer) {
        brls::Logger::error("ImageLoader: av_malloc failed for {} I/O buffer", formatName);
        return result;
    }

    FFmpegMemoryBuffer memBuf = {data, dataSize, 0};

    AVIOContext* avioCtx = avio_alloc_context(ioBuffer, ioBufferSize, 0, &memBuf,
                                               ffmpegReadPacket, nullptr, ffmpegSeek);
    if (!avioCtx) {
        av_free(ioBuffer);
        brls::Logger::error("ImageLoader: avio_alloc_context failed for {}", formatName);
        return result;
    }

    AVFormatContext* fmtCtx = avformat_alloc_context();
    if (!fmtCtx) {
        avio_context_free(&avioCtx);
        brls::Logger::error("ImageLoader: avformat_alloc_context failed for {}", formatName);
        return result;
    }
    fmtCtx->pb = avioCtx;

    // Open input from memory
    int ret = avformat_open_input(&fmtCtx, nullptr, nullptr, nullptr);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        brls::Logger::error("ImageLoader: avformat_open_input failed for {}: {}", formatName, errbuf);
        avformat_close_input(&fmtCtx);
        avio_context_free(&avioCtx);
        return result;
    }

    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        brls::Logger::error("ImageLoader: avformat_find_stream_info failed for {}", formatName);
        avformat_close_input(&fmtCtx);
        avio_context_free(&avioCtx);
        return result;
    }

    // Find the video/image stream
    int streamIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            streamIdx = static_cast<int>(i);
            break;
        }
    }
    if (streamIdx < 0) {
        brls::Logger::error("ImageLoader: No video stream found in {} data", formatName);
        avformat_close_input(&fmtCtx);
        avio_context_free(&avioCtx);
        return result;
    }

    // Open codec
    AVCodecParameters* codecPar = fmtCtx->streams[streamIdx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        brls::Logger::error("ImageLoader: No decoder found for {} (codec_id={})", formatName, static_cast<int>(codecPar->codec_id));
        avformat_close_input(&fmtCtx);
        avio_context_free(&avioCtx);
        return result;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        brls::Logger::error("ImageLoader: avcodec_alloc_context3 failed for {}", formatName);
        avformat_close_input(&fmtCtx);
        avio_context_free(&avioCtx);
        return result;
    }

    avcodec_parameters_to_context(codecCtx, codecPar);
    ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        brls::Logger::error("ImageLoader: avcodec_open2 failed for {}", formatName);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        avio_context_free(&avioCtx);
        return result;
    }

    // Read and decode one frame
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool decoded = false;

    while (av_read_frame(fmtCtx, pkt) >= 0 && !decoded) {
        if (pkt->stream_index == streamIdx) {
            ret = avcodec_send_packet(codecCtx, pkt);
            if (ret >= 0) {
                ret = avcodec_receive_frame(codecCtx, frame);
                if (ret >= 0) {
                    decoded = true;
                }
            }
        }
        av_packet_unref(pkt);
    }

    // Flush decoder if needed
    if (!decoded) {
        avcodec_send_packet(codecCtx, nullptr);
        if (avcodec_receive_frame(codecCtx, frame) >= 0) {
            decoded = true;
        }
    }

    if (!decoded || frame->width <= 0 || frame->height <= 0) {
        brls::Logger::error("ImageLoader: Failed to decode {} frame", formatName);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        avio_context_free(&avioCtx);
        return result;
    }

    int srcW = frame->width;
    int srcH = frame->height;
    int dstW = srcW;
    int dstH = srcH;

    // Downscale if needed
    if (maxSize > 0 && (srcW > maxSize || srcH > maxSize)) {
        float scale = static_cast<float>(maxSize) / std::max(srcW, srcH);
        dstW = std::max(1, static_cast<int>(srcW * scale));
        dstH = std::max(1, static_cast<int>(srcH * scale));
    }

    // Convert to RGBA using swscale
    SwsContext* swsCtx = sws_getContext(srcW, srcH, static_cast<AVPixelFormat>(frame->format),
                                         dstW, dstH, AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsCtx) {
        brls::Logger::error("ImageLoader: sws_getContext failed for {}", formatName);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        avio_context_free(&avioCtx);
        return result;
    }

    std::vector<uint8_t> rgba;
    try {
        rgba.resize(static_cast<size_t>(dstW) * dstH * 4);
    } catch (const std::bad_alloc&) {
        sws_freeContext(swsCtx);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        avio_context_free(&avioCtx);
        signalOOM("FFmpeg RGBA alloc");
        return result;
    }
    uint8_t* dstSlice[1] = {rgba.data()};
    int dstStride[1] = {dstW * 4};

    sws_scale(swsCtx, frame->data, frame->linesize, 0, srcH, dstSlice, dstStride);

    sws_freeContext(swsCtx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    avio_context_free(&avioCtx);

    brls::Logger::info("ImageLoader: {} decoded {}x{} -> {}x{}", formatName, srcW, srcH, dstW, dstH);
    return createTGAFromRGBA(rgba.data(), dstW, dstH);
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

    if (!sessionCookie.empty() || !token.empty()) {
        // Build cookie header with all available cookies for cross-mode compatibility
        std::string cookies;
        if (!sessionCookie.empty()) {
            cookies = sessionCookie;
        }
        if (!token.empty()) {
            if (!cookies.empty()) cookies += "; ";
            cookies += "suwayomi-server-token=" + token;
            client.setDefaultHeader("Authorization", "Bearer " + token);
        }
        client.setDefaultHeader("Cookie", cookies);
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
            // Skip empty data to avoid passing garbage to NVG
            if (update.data.empty()) {
                continue;
            }
            brls::Logger::debug("ImageLoader: Uploading texture {} bytes to target {:p}",
                                update.data.size(), static_cast<void*>(update.target));
            update.target->setImageFromMem(update.data.data(), update.data.size());
            brls::Logger::debug("ImageLoader: Texture upload OK");
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
            // Skip empty data to avoid passing garbage to NVG
            if (!update.isSegmented && update.data.empty()) {
                continue;
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

    // Early out if the owning cell was destroyed before we started processing.
    // This prevents wasting worker time (and curl resources) on stale requests
    // queued before a grid rebuild (e.g., switching from BY_SOURCE to NO_GROUPING).
    if (alive && !*alive) return;

    // Skip thumbnail loads while the system is recovering from OOM.
    // Thumbnails are non-critical and can be loaded later when scrolled to.
    if (isUnderMemoryPressure()) {
        brls::Logger::debug("ImageLoader: Skipping thumbnail load (OOM cooldown) for {}", url);
        return;
    }

    // Check disk cache first (this runs on a background thread, so disk I/O is fine)
    if (Application::getInstance().getSettings().cacheCoverImages) {
        int mangaId = extractMangaIdFromUrl(url);
        if (mangaId > 0) {
            std::vector<uint8_t> diskData;
            if (LibraryCache::getInstance().loadCoverImage(mangaId, diskData)) {
                // Validate cached data looks like a valid TGA (our output format).
                // Previously, raw undecoded image data could be cached by the
                // fallback path, causing repeated "Cannot set texture: 0" errors
                // and potential OOM crashes when loading many covers.
                bool validTGA = diskData.size() > 18 &&
                                diskData[0] == 0 && diskData[1] == 0 &&
                                diskData[2] == 2 && diskData[16] == 32;
                if (!validTGA) {
                    // Corrupt/non-TGA cached data - delete it and re-download
                    brls::Logger::warning("ImageLoader: Removing invalid cached cover for manga {}", mangaId);
                    LibraryCache::getInstance().deleteCoverImage(mangaId);
                } else {
                    // Found valid TGA in disk cache - add to memory LRU cache
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
    }

    // Skip network requests when offline. Thumbnails that aren't in disk cache
    // cannot be loaded without a server connection. Without this check, hundreds
    // of doomed HTTP requests (each with retries) flood the worker threads,
    // exhaust curl handles / file descriptors, and crash the Vita.
    if (!Application::getInstance().isConnected()) {
        return;
    }

    // Re-check alive flag after disk cache I/O
    if (alive && !*alive) return;

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
    bool isSVG = false;
    bool isAVIF = false;
    bool isHEIF = false;
    bool isKnownFormat = false;

    const uint8_t* bodyData = reinterpret_cast<const uint8_t*>(resp.body.data());
    size_t bodySize = resp.body.size();

    if (bodySize > 12) {
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
        else if (isAVIFData(bodyData, bodySize)) {
            isAVIF = true;
            isKnownFormat = true;
            brls::Logger::info("ImageLoader: Detected AVIF format for {}", url);
        }
        else if (isHEIFData(bodyData, bodySize)) {
            isHEIF = true;
            isKnownFormat = true;
            brls::Logger::info("ImageLoader: Detected HEIF/HEIC format for {}", url);
        }
    }

    // Check for SVG (text-based, needs different detection)
    if (!isKnownFormat && isSVGData(bodyData, bodySize)) {
        isSVG = true;
        isKnownFormat = true;
        brls::Logger::info("ImageLoader: Detected SVG format for {}", url);
    }

    // For unrecognized formats (.bin, etc.), try stb_image as fallback
    // stb_image can auto-detect JPEG, PNG, BMP, GIF, TGA, PSD, PIC
    if (!isKnownFormat && bodySize > 4) {
        int testW, testH, testC;
        if (stbi_info_from_memory(bodyData, static_cast<int>(bodySize), &testW, &testH, &testC)) {
            isKnownFormat = true;
            brls::Logger::info("ImageLoader: stb_image detected unknown format image {}x{} for {}",
                              testW, testH, url);
        }
    }

    if (!isKnownFormat) {
        brls::Logger::warning("ImageLoader: Unrecognized image format for {}", url);
        return;
    }

    // For animated WebP, extract the small first frame BEFORE copying the full
    // response body.  Animated WebP covers can be 2+ MB — copying that into
    // bodyVec would briefly require 4+ MB (resp.body + bodyVec) which fragments
    // the Vita's limited heap and can cause cascading OOM crashes.
    bool isAnimatedWebP = false;
    if (isWebP && bodySize > 30) {
        WebPBitstreamFeatures features;
        if (WebPGetFeatures(bodyData, bodySize, &features) == VP8_STATUS_OK) {
            isAnimatedWebP = features.has_animation;
        }
    }

    std::vector<uint8_t> bodyVec;
    if (isAnimatedWebP) {
        // Only one animated WebP can be processed at a time.  Each one is 2+ MB;
        // with 3 workers holding them simultaneously the Vita runs out of memory.
        // If another worker is already processing one, free our body immediately
        // and re-queue this URL so it gets processed later.
        std::unique_lock<std::mutex> animLock(s_animatedWebPMutex, std::try_to_lock);
        if (!animLock.owns_lock()) {
            brls::Logger::info("ImageLoader: Deferring animated WebP (another in progress), re-queuing: {}", url);
            // Free the 2+ MB body before re-queuing to reclaim memory immediately
            std::string urlCopy = url;
            LoadCallback cbCopy = callback;
            brls::Image* tgtCopy = target;
            auto aliveCopy = alive;
            { std::string().swap(resp.body); }
            {
                std::lock_guard<std::mutex> lock(s_queueMutex);
                s_loadQueue.push({urlCopy, cbCopy, tgtCopy, true, aliveCopy});
            }
            s_queueCV.notify_one();
            return;
        }

        // We hold the animated WebP lock — safe to process.
        // Extract first frame directly from resp.body (avoids duplicating the
        // full multi-MB animated WebP into bodyVec).
        brls::Logger::info("ImageLoader: Animated WebP ({}KB), extracting first frame before copy: {}",
                           bodySize / 1024, url);
        auto firstFrame = extractFirstFrameFromAnimatedWebP(bodyData, bodySize);
        { std::string().swap(resp.body); }  // free 2+ MB immediately
        bodyData = nullptr;
        bodySize = 0;

        if (firstFrame.empty()) {
            brls::Logger::warning("ImageLoader: Failed to extract frame from animated WebP: {}", url);
            return;
        }
        bodyVec = std::move(firstFrame);  // ~70 KB instead of 2+ MB
        // animLock released when bodyVec processing continues below
    } else {
        // Move the HTTP response body into a local vector so we hold a compact copy.
        // The `resp` object (with its string, headers, etc.) is freed here, reducing
        // peak memory during the decode step that follows.
        try {
            bodyVec.assign(bodyData, bodyData + bodySize);
        } catch (const std::bad_alloc&) {
            signalOOM("executeLoad bodyVec alloc");
            return;
        }
        { std::string().swap(resp.body); }  // release resp.body memory now
    }
    const uint8_t* decData = bodyVec.data();
    size_t decSize = bodyVec.size();

    // Serialize decodes: only one worker thread decodes at a time.
    // This prevents 3 concurrent WebP/stb/FFmpeg decode buffers from
    // pushing the Vita past its ~128MB user-space memory limit.
    std::lock_guard<std::mutex> decodeLock(s_decodeMutex);

    // Re-check OOM cooldown after acquiring the lock (another thread may have
    // encountered OOM while we were waiting).
    if (isUnderMemoryPressure()) {
        brls::Logger::debug("ImageLoader: Skipping decode (OOM cooldown) for {}", url);
        return;
    }

    // Convert and downscale all image formats for thumbnails
    std::vector<uint8_t> imageData;
    if (isSVG) {
        imageData = convertSVGtoTGA(decData, decSize, s_maxThumbnailSize);
        if (imageData.empty()) {
            brls::Logger::error("ImageLoader: SVG conversion failed for {}", url);
            return;
        }
    } else if (isAVIF) {
        imageData = convertFFmpegImageToTGA(decData, decSize, s_maxThumbnailSize, "AVIF");
        if (imageData.empty()) {
            brls::Logger::error("ImageLoader: AVIF conversion failed for {}", url);
            return;
        }
    } else if (isHEIF) {
        imageData = convertFFmpegImageToTGA(decData, decSize, s_maxThumbnailSize, "HEIF");
        if (imageData.empty()) {
            brls::Logger::error("ImageLoader: HEIF conversion failed for {}", url);
            return;
        }
    } else if (isWebP) {
        // For animated WebP, the first frame was already extracted BEFORE the
        // decode mutex (to avoid holding 2+ MB in memory).  bodyVec now contains
        // a small (~70 KB) standalone static WebP — just decode it normally.
        {
            imageData = convertWebPtoTGA(decData, decSize, s_maxThumbnailSize);
            if (imageData.empty()) {
                // Fallback: try FFmpeg decoder for WebP — it wraps libwebp but with
                // additional error recovery and can handle some corrupted/truncated
                // data that the raw libwebp API rejects.
                brls::Logger::warning("ImageLoader: libwebp failed, trying FFmpeg fallback for {}", url);
                imageData = convertFFmpegImageToTGA(decData, decSize, s_maxThumbnailSize, "WebP");
            }
            if (imageData.empty()) {
                // Last resort: try stb_image in case the server mis-labeled the format
                // (e.g., a JPEG served with a .webp URL or wrong Content-Type)
                brls::Logger::warning("ImageLoader: FFmpeg WebP fallback failed, trying stb_image for {}", url);
                imageData = convertImageToTGA(decData, decSize, s_maxThumbnailSize);
            }
            if (imageData.empty()) {
                brls::Logger::error("ImageLoader: All WebP decode attempts failed for {}", url);
                // Signal memory pressure after exhausting all fallbacks.
                // Even though the final error may not be OOM, the repeated decode
                // attempts (libwebp, FFmpeg, stb_image) allocate and free large
                // buffers that fragment the Vita's limited heap.  Entering cooldown
                // gives the allocator time to consolidate before processing more
                // thumbnails, preventing cascading failures that lead to crashes.
                signalOOM("WebP all fallbacks exhausted");
                return;
            }
        }
    } else {
        // Downscale JPEG/PNG thumbnails too (not just WebP)
        // This reduces memory usage and makes cache more effective
        imageData = convertImageToTGA(decData, decSize, s_maxThumbnailSize);
        if (imageData.empty()) {
            // Image decode failed - skip this image entirely.
            // Do NOT fall back to raw data: it wastes memory (no downscaling),
            // gets cached (persisting the problem), and will fail again in NVG
            // causing "Cannot set texture: 0" errors and potential OOM crashes
            // on the Vita when many covers fail simultaneously.
            return;
        }
    }

    brls::Logger::info("ImageLoader: Decode OK ({} bytes TGA) for {}", imageData.size(), url);

    // Release download buffer if still held (non-animated paths)
    if (!bodyVec.empty()) {
        bodyVec.clear();
        bodyVec.shrink_to_fit();
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
    brls::Logger::info("ImageLoader: Queued texture for {}", url);
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

        // Top-level safety net: catch any std::bad_alloc that slipped past
        // the per-function try-catch blocks.  Without this, an uncaught
        // exception terminates the whole app on PS Vita with stack corruption.
        try {
            if (isRotatable) {
                executeRotatableLoad(rotatableRequest);
            } else {
                executeLoad(request);
            }
        } catch (const std::bad_alloc&) {
            signalOOM("worker top-level");
        } catch (...) {
            brls::Logger::error("ImageLoader: Unknown exception in worker {}", workerId);
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

    // Skip preload-only requests (no target, no callback) while under memory
    // pressure.  This prevents the reader's 3-page preload from piling up
    // downloads and decode buffers while the system is already struggling.
    // Loads with a target/callback are the current page and must proceed.
    if (!target && !callback && isUnderMemoryPressure()) {
        brls::Logger::debug("ImageLoader: Skipping preload (OOM cooldown) for {}", url);
        return;
    }

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
        // Skip network requests when offline to avoid flooding workers with
        // doomed HTTP requests that exhaust curl handles and crash the Vita.
        if (!Application::getInstance().isConnected()) {
            return;
        }

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
        bool isSVG = false;
        bool isAVIF = false;
        bool isHEIF = false;
        bool isValidImage = false;

        const uint8_t* imgData = reinterpret_cast<const uint8_t*>(imageBody.data());
        size_t imgSize = imageBody.size();

        if (imgSize > 18) {
            unsigned char* data = reinterpret_cast<unsigned char*>(imageBody.data());

            // TGA (uncompressed true-color, 32-bit BGRA) - pass through directly to
            // NanoVG without decode/re-encode since it's already in GPU-ready format.
            // Check header: ID=0, colormap=0, type=2, bpp=32.
            if (data[0] == 0 && data[1] == 0 && data[2] == 2 && data[16] == 32) {
                int tgaW = data[12] | (data[13] << 8);
                int tgaH = data[14] | (data[15] << 8);
                size_t expectedSize = 18 + static_cast<size_t>(tgaW) * tgaH * 4;
                if (tgaW > 0 && tgaH > 0 && imgSize >= expectedSize) {
                    isTGA = true;
                    isValidImage = true;
                }
            }
        }

        if (!isValidImage && imgSize > 12) {
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
            // AVIF (ISOBMFF with avif/avis/mif1 brand)
            else if (isAVIFData(imgData, imgSize)) {
                isAVIF = true;
                isValidImage = true;
                brls::Logger::info("ImageLoader: Detected AVIF format for {}", url);
            }
            // HEIF/HEIC (ISOBMFF with heic/heix/hevc brand)
            else if (isHEIFData(imgData, imgSize)) {
                isHEIF = true;
                isValidImage = true;
                brls::Logger::info("ImageLoader: Detected HEIF/HEIC format for {}", url);
            }
        }

        // Check for SVG (text-based format)
        if (!isValidImage && isSVGData(imgData, imgSize)) {
            isSVG = true;
            isValidImage = true;
            brls::Logger::info("ImageLoader: Detected SVG format for {}", url);
        }

        // Fallback: try stb_image for unrecognized formats (.bin, etc.)
        if (!isValidImage && imgSize > 4) {
            int testW, testH, testC;
            if (stbi_info_from_memory(imgData, static_cast<int>(imgSize), &testW, &testH, &testC)) {
                isJpegOrPng = true;  // stb_image can handle it
                isValidImage = true;
                brls::Logger::info("ImageLoader: stb_image detected unknown format {}x{} for {}", testW, testH, url);
            }
        }

        if (!isValidImage) {
            brls::Logger::warning("ImageLoader: Invalid image format for {}", url);
            return;
        }

        // For animated WebP in the reader path, extract the first frame BEFORE
        // the decode mutex.  Animated WebP pages can be 2+ MB each — when
        // preloading multiple pages, keeping the full animated data in imageBody
        // while waiting for the decode lock causes memory spikes that crash the
        // Vita.  Serialize extraction with s_animatedWebPMutex so only one
        // worker holds a full animated WebP body at a time.
        if (isWebP && imageBody.size() > 30) {
            WebPBitstreamFeatures animFeatures;
            if (WebPGetFeatures(reinterpret_cast<const uint8_t*>(imageBody.data()),
                                imageBody.size(), &animFeatures) == VP8_STATUS_OK &&
                animFeatures.has_animation) {
                // Serialize animated WebP extraction across all workers
                std::lock_guard<std::mutex> animLock(s_animatedWebPMutex);

                brls::Logger::info("ImageLoader: Animated WebP reader page ({}KB), extracting first frame: {}",
                                   imageBody.size() / 1024, url);
                auto firstFrame = extractFirstFrameFromAnimatedWebP(
                    reinterpret_cast<const uint8_t*>(imageBody.data()), imageBody.size());
                if (!firstFrame.empty()) {
                    brls::Logger::info("ImageLoader: Extracted first frame from animated WebP ({} bytes -> {} bytes)",
                                       imageBody.size(), firstFrame.size());
                    // Replace imageBody with the small first frame
                    imageBody.assign(reinterpret_cast<const char*>(firstFrame.data()), firstFrame.size());
                    std::vector<uint8_t>().swap(firstFrame);  // free immediately
                } else {
                    brls::Logger::warning("ImageLoader: Failed to extract frame from animated WebP reader page: {}", url);
                    // Continue with original data — convertWebPtoTGA will try extraction again
                }
            }
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

        // Serialize decodes across all worker threads to prevent concurrent
        // multi-MB decode buffers from exhausting the Vita's memory.
        std::lock_guard<std::mutex> decodeLock(s_decodeMutex);

        // Re-check memory pressure and alive flag after acquiring the lock
        if (isUnderMemoryPressure()) {
            brls::Logger::debug("ImageLoader: Skipping rotatable decode (OOM cooldown) for {}", url);
            return;
        }
        if (alive && !*alive) return;

        // SVG, AVIF, and HEIF are decoded to TGA directly (no auto-split support yet)
        // They go through convertSVGtoTGA / convertFFmpegImageToTGA which handle sizing
        if (isSVG) {
            std::vector<uint8_t> imageData = convertSVGtoTGA(imgData, imgSize, MAX_TEXTURE_SIZE);
            if (imageData.empty()) {
                brls::Logger::error("ImageLoader: SVG conversion failed for {}", url);
                return;
            }
            std::string cacheKey = url + "_full";
            cachePut(cacheKey, imageData);
            auto decodeEndTime = std::chrono::steady_clock::now();
            auto decodeMs = std::chrono::duration_cast<std::chrono::milliseconds>(decodeEndTime - decodeStartTime).count();
            auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(decodeEndTime - loadStartTime).count();
            brls::Logger::info("ImageLoader: [TIMING] SVG decode took {}ms, total {}ms for {}", decodeMs, totalMs, url);
            if (target || callback) {
                queueRotatableTextureUpdate(imageData, target, callback, alive);
            }
            return;
        }

        if (isAVIF || isHEIF) {
            const char* fmtName = isAVIF ? "AVIF" : "HEIF";
            std::vector<uint8_t> imageData = convertFFmpegImageToTGA(imgData, imgSize, MAX_TEXTURE_SIZE, fmtName);
            if (imageData.empty()) {
                brls::Logger::error("ImageLoader: {} conversion failed for {}", fmtName, url);
                return;
            }
            std::string cacheKey = url + "_full";
            cachePut(cacheKey, imageData);
            auto decodeEndTime = std::chrono::steady_clock::now();
            auto decodeMs = std::chrono::duration_cast<std::chrono::milliseconds>(decodeEndTime - decodeStartTime).count();
            auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(decodeEndTime - loadStartTime).count();
            brls::Logger::info("ImageLoader: [TIMING] {} decode took {}ms, total {}ms for {}", fmtName, decodeMs, totalMs, url);
            if (target || callback) {
                queueRotatableTextureUpdate(imageData, target, callback, alive);
            }
            return;
        }

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
                    // Skip auto-split for animated WebP — segmented crop decode
                    // doesn't work on animated containers.  The normal single-texture
                    // path in convertWebPtoTGA handles animated WebP via frame extraction.
                    if (!feat.has_animation) {
                        origW = feat.width;
                        origH = feat.height;
                    }
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
                    if (isUnderMemoryPressure()) {
                        brls::Logger::warning("ImageLoader: Aborting auto-split at seg {}/{} (OOM cooldown)", seg, autoSegments);
                        allOK = false;
                        break;
                    }

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
                // WebP decode failed - try FFmpeg fallback (wraps libwebp with
                // better error recovery for corrupted/truncated data)
                brls::Logger::warning("ImageLoader: WebP conversion failed for {}, trying FFmpeg fallback", url);
                imageData = convertFFmpegImageToTGA(
                    reinterpret_cast<const uint8_t*>(imageBody.data()),
                    imageBody.size(),
                    MAX_TEXTURE_SIZE,
                    "WebP"
                );
            }
            if (imageData.empty()) {
                // Last resort: try stb_image in case format was
                // misidentified or stb can handle this particular encoding
                brls::Logger::warning("ImageLoader: FFmpeg WebP fallback failed for {}, trying stb_image", url);
                imageData = convertImageToTGA(
                    reinterpret_cast<const uint8_t*>(imageBody.data()),
                    imageBody.size(),
                    MAX_TEXTURE_SIZE
                );
                if (imageData.empty()) {
                    brls::Logger::error("ImageLoader: All decode attempts failed for {}", url);
                    signalOOM("WebP full-size all fallbacks exhausted");
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

        // Free the raw downloaded image data now that decode is complete.
        // This releases the compressed image buffer (can be 2+ MB for WebP)
        // before we cache the decoded TGA and queue the texture upload,
        // reducing peak memory usage on the Vita.
        { std::string().swap(imageBody); }

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

    // SVG - parse with nanosvg to get dimensions
    if (isSVGData(data, imageData.size())) {
        std::string svgCopy(imageData);
        NSVGimage* svgImg = nsvgParse(&svgCopy[0], "px", 96.0f);
        if (svgImg && svgImg->width > 0 && svgImg->height > 0) {
            width = static_cast<int>(svgImg->width);
            height = static_cast<int>(svgImg->height);
            suggestedSegments = 1;  // SVG is rasterized at target size, no splitting needed
            brls::Logger::info("ImageLoader: SVG {}x{}", width, height);
            nsvgDelete(svgImg);
            return true;
        }
        if (svgImg) nsvgDelete(svgImg);
    }

    // AVIF/HEIF - use stb_image fallback for dimension check, or try FFmpeg probing
    if (isAVIFData(data, imageData.size()) || isHEIFData(data, imageData.size())) {
        // For AVIF/HEIF, dimensions are embedded in the ISOBMFF container.
        // Use a lightweight approach: parse ispe box for dimensions.
        // The 'ispe' (image spatial extents) box contains width and height as uint32.
        for (size_t i = 0; i + 12 < imageData.size(); i++) {
            if (data[i] == 'i' && data[i+1] == 's' && data[i+2] == 'p' && data[i+3] == 'e') {
                // ispe box: 4 bytes version/flags, then 4 bytes width, 4 bytes height
                if (i + 4 + 4 + 4 + 4 <= imageData.size()) {
                    size_t off = i + 4 + 4;  // skip box type + version/flags
                    width = (data[off] << 24) | (data[off+1] << 16) | (data[off+2] << 8) | data[off+3];
                    height = (data[off+4] << 24) | (data[off+5] << 16) | (data[off+6] << 8) | data[off+7];
                    if (width > 0 && height > 0) {
                        suggestedSegments = calculateSegments(width, height, MAX_TEXTURE_SIZE);
                        brls::Logger::info("ImageLoader: AVIF/HEIF {}x{} -> {} segments", width, height, suggestedSegments);
                        return true;
                    }
                }
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
