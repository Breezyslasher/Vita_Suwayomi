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
#include <cctype>
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

// Helper to extract manga ID, chapter index, and page index from a reader page URL
// URLs look like: http://server/api/v1/manga/622/chapter/75/page/0
// Returns true if all three values were extracted
static bool extractPageUrlIds(const std::string& url, int& mangaId, int& chapterIdx, int& pageIdx) {
    mangaId = chapterIdx = pageIdx = -1;

    // Try HTTP-style URL first: /manga/X/chapter/Y/page/Z
    size_t mPos = url.find("/manga/");
    if (mPos != std::string::npos) {
        mPos += 7;
        size_t mEnd = url.find('/', mPos);
        if (mEnd == std::string::npos) return false;

        size_t cPos = url.find("/chapter/", mEnd);
        if (cPos == std::string::npos) return false;
        cPos += 9;
        size_t cEnd = url.find('/', cPos);
        if (cEnd == std::string::npos) return false;

        size_t pPos = url.find("/page/", cEnd);
        if (pPos == std::string::npos) return false;
        pPos += 6;
        size_t pEnd = url.find('/', pPos);
        if (pEnd == std::string::npos) pEnd = url.length();

        try {
            mangaId = std::stoi(url.substr(mPos, mEnd - mPos));
            chapterIdx = std::stoi(url.substr(cPos, cEnd - cPos));
            pageIdx = std::stoi(url.substr(pPos, pEnd - pPos));
            return true;
        } catch (...) {
            return false;
        }
    }

    // Try local download path: .../manga_X/chapter_Y/page_Z.ext
    size_t lmPos = url.find("manga_");
    if (lmPos != std::string::npos) {
        lmPos += 6;
        size_t lmEnd = url.find('/', lmPos);
        if (lmEnd == std::string::npos) return false;

        size_t lcPos = url.find("chapter_", lmEnd);
        if (lcPos == std::string::npos) return false;
        lcPos += 8;
        size_t lcEnd = url.find('/', lcPos);
        if (lcEnd == std::string::npos) return false;

        size_t lpPos = url.find("page_", lcEnd);
        if (lpPos == std::string::npos) return false;
        lpPos += 5;
        // Page index ends at '.' (extension) or end of string
        size_t lpEnd = url.find('.', lpPos);
        if (lpEnd == std::string::npos) lpEnd = url.length();

        try {
            mangaId = std::stoi(url.substr(lmPos, lmEnd - lmPos));
            chapterIdx = std::stoi(url.substr(lcPos, lcEnd - lcPos));
            pageIdx = std::stoi(url.substr(lpPos, lpEnd - lpPos));
            return true;
        } catch (...) {
            return false;
        }
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
    // Use 16.16 fixed-point instead of per-pixel float math
    const uint32_t fxScaleX = ((uint32_t)srcW << 16) / (uint32_t)dstW;
    const uint32_t fxScaleY = ((uint32_t)srcH << 16) / (uint32_t)dstH;

    for (int y = 0; y < dstH; y++) {
        int srcY = (int)((uint32_t)y * fxScaleY >> 16);
        if (srcY >= srcH) srcY = srcH - 1;
        const uint8_t* srcRow = src + (size_t)srcY * srcW * 4;
        uint8_t* dstRow = dst + (size_t)y * dstW * 4;
        for (int x = 0; x < dstW; x++) {
            int srcX = (int)((uint32_t)x * fxScaleX >> 16);
            if (srcX >= srcW) srcX = srcW - 1;
            memcpy(dstRow + x * 4, srcRow + srcX * 4, 4);
        }
    }
}

// Helper to write a TGA header into the first 18 bytes of a buffer
static void writeTGAHeader(uint8_t* header, int width, int height) {
    memset(header, 0, 18);
    header[2] = 2;
    header[12] = width & 0xFF;
    header[13] = (width >> 8) & 0xFF;
    header[14] = height & 0xFF;
    header[15] = (height >> 8) & 0xFF;
    header[16] = 32;
    header[17] = 0x28;
}

// Fused downscale + RGBA→BGRA swizzle in a single pass (avoids separate
// downscaleRGBA + createTGAFromRGBA which touches every pixel twice).
// Writes directly into a TGA buffer (18-byte header + BGRA pixel data).
static std::vector<uint8_t> downscaleRGBAtoTGA(const uint8_t* src, int srcW, int srcH,
                                                int dstW, int dstH) {
    if (!src || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return {};
    size_t imageSize = (size_t)dstW * dstH * 4;
    if (imageSize > 256 * 1024 * 1024) return {};
    std::vector<uint8_t> tga;
    try {
        tga.resize(18 + imageSize);
    } catch (const std::bad_alloc&) {
        signalOOM("downscaleRGBAtoTGA");
        return {};
    }
    writeTGAHeader(tga.data(), dstW, dstH);

    const uint32_t fxScaleX = ((uint32_t)srcW << 16) / (uint32_t)dstW;
    const uint32_t fxScaleY = ((uint32_t)srcH << 16) / (uint32_t)dstH;
    uint8_t* dst = tga.data() + 18;

    for (int y = 0; y < dstH; y++) {
        int srcY = (int)((uint32_t)y * fxScaleY >> 16);
        if (srcY >= srcH) srcY = srcH - 1;
        const uint8_t* srcRow = src + (size_t)srcY * srcW * 4;
        uint8_t* dstRow = dst + (size_t)y * dstW * 4;
        for (int x = 0; x < dstW; x++) {
            int srcX = (int)((uint32_t)x * fxScaleX >> 16);
            if (srcX >= srcW) srcX = srcW - 1;
            const uint8_t* sp = srcRow + srcX * 4;
            uint8_t* dp = dstRow + x * 4;
            dp[0] = sp[2];  // B
            dp[1] = sp[1];  // G
            dp[2] = sp[0];  // R
            dp[3] = sp[3];  // A
        }
    }
    return tga;
}

// Fused segment-extract + downscale + RGBA→BGRA swizzle in one pass.
// Reads directly from the full RGBA buffer at a vertical offset, avoiding
// the intermediate memcpy of segment rows.
static std::vector<uint8_t> downscaleSegmentRGBAtoTGA(const uint8_t* src, int srcW,
                                                       int startY, int segH,
                                                       int dstW, int dstH) {
    if (!src || srcW <= 0 || segH <= 0 || dstW <= 0 || dstH <= 0) return {};
    size_t imageSize = (size_t)dstW * dstH * 4;
    if (imageSize > 256 * 1024 * 1024) return {};
    std::vector<uint8_t> tga;
    try {
        tga.resize(18 + imageSize);
    } catch (const std::bad_alloc&) {
        signalOOM("downscaleSegmentRGBAtoTGA");
        return {};
    }
    writeTGAHeader(tga.data(), dstW, dstH);

    const uint32_t fxScaleX = ((uint32_t)srcW << 16) / (uint32_t)dstW;
    const uint32_t fxScaleY = ((uint32_t)segH << 16) / (uint32_t)dstH;
    uint8_t* dst = tga.data() + 18;

    for (int y = 0; y < dstH; y++) {
        int srcY = startY + (int)((uint32_t)y * fxScaleY >> 16);
        const uint8_t* srcRow = src + (size_t)srcY * srcW * 4;
        uint8_t* dstRow = dst + (size_t)y * dstW * 4;
        for (int x = 0; x < dstW; x++) {
            int srcX = (int)((uint32_t)x * fxScaleX >> 16);
            if (srcX >= srcW) srcX = srcW - 1;
            const uint8_t* sp = srcRow + srcX * 4;
            uint8_t* dp = dstRow + x * 4;
            dp[0] = sp[2];  // B
            dp[1] = sp[1];  // G
            dp[2] = sp[0];  // R
            dp[3] = sp[3];  // A
        }
    }
    return tga;
}

// Helper to create TGA from RGBA data (swizzles R<->B for TGA's BGRA order)
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

    writeTGAHeader(tgaData.data(), width, height);

    uint8_t* pixels = tgaData.data() + 18;
    for (int i = 0; i < width * height; i++) {
        pixels[i * 4 + 0] = rgba[i * 4 + 2];  // B
        pixels[i * 4 + 1] = rgba[i * 4 + 1];  // G
        pixels[i * 4 + 2] = rgba[i * 4 + 0];  // R
        pixels[i * 4 + 3] = rgba[i * 4 + 3];  // A
    }

    return tgaData;
}

// Helper to create TGA from BGRA data (direct memcpy — no swizzle needed)
static std::vector<uint8_t> createTGAFromBGRA(const uint8_t* bgra, int width, int height) {
    if (width <= 0 || height <= 0 || !bgra) return {};
    size_t imageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    if (imageSize > 256 * 1024 * 1024) return {};
    std::vector<uint8_t> tgaData;
    try {
        tgaData.resize(18 + imageSize);
    } catch (const std::bad_alloc&) {
        signalOOM("createTGAFromBGRA");
        return {};
    }

    writeTGAHeader(tgaData.data(), width, height);
    memcpy(tgaData.data() + 18, bgra, imageSize);

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

// Detect whether a GIF is animated (has more than one frame).
// Scans for multiple Image Descriptor blocks (0x2C) or a Netscape loop extension.
static bool isAnimatedGIF(const uint8_t* data, size_t size) {
    if (size < 13 || memcmp(data, "GIF", 3) != 0) return false;

    int imageDescriptorCount = 0;
    size_t pos = 13;  // Skip header (6) + logical screen descriptor (7)

    // Skip Global Color Table if present
    uint8_t packed = data[10];
    if (packed & 0x80) {
        int gctSize = 3 * (1 << ((packed & 0x07) + 1));
        pos += gctSize;
    }

    while (pos < size) {
        uint8_t blockType = data[pos];
        pos++;

        if (blockType == 0x2C) {
            // Image Descriptor
            imageDescriptorCount++;
            if (imageDescriptorCount >= 2) return true;

            if (pos + 9 > size) break;
            uint8_t imgPacked = data[pos + 8];
            pos += 9;

            // Skip Local Color Table if present
            if (imgPacked & 0x80) {
                int lctSize = 3 * (1 << ((imgPacked & 0x07) + 1));
                pos += lctSize;
            }

            // Skip LZW minimum code size
            if (pos >= size) break;
            pos++;  // LZW min code size

            // Skip sub-blocks
            while (pos < size) {
                uint8_t subBlockSize = data[pos];
                pos++;
                if (subBlockSize == 0) break;
                if (pos + subBlockSize > size) { pos = size; break; }
                pos += subBlockSize;
            }
        } else if (blockType == 0x21) {
            // Extension block
            if (pos >= size) break;
            uint8_t label = data[pos];
            pos++;

            // Check for Netscape loop extension (indicates animation)
            if (label == 0xFF && pos + 1 < size) {
                uint8_t extLen = data[pos];
                if (extLen == 11 && pos + 12 < size &&
                    memcmp(data + pos + 1, "NETSCAPE2.0", 11) == 0) {
                    return true;
                }
            }

            // Skip sub-blocks
            while (pos < size) {
                uint8_t subBlockSize = data[pos];
                pos++;
                if (subBlockSize == 0) break;
                if (pos + subBlockSize > size) { pos = size; break; }
                pos += subBlockSize;
            }
        } else if (blockType == 0x3B) {
            // Trailer - end of GIF
            break;
        } else {
            // Unknown block, bail
            break;
        }
    }

    return false;
}

// Extract just the first frame from an animated GIF by copying the header,
// global color table, and the first image descriptor block.  This produces a
// valid single-frame GIF that stb_image can decode, and discards the remaining
// frames which would otherwise waste memory on the Vita.
static std::vector<uint8_t> extractFirstFrameFromGIF(const uint8_t* data, size_t size) {
    if (size < 13 || memcmp(data, "GIF", 3) != 0) return {};

    std::vector<uint8_t> result;
    try {
        // Reserve a reasonable upper bound — first frame is typically a small
        // fraction of a multi-frame GIF.
        result.reserve(std::min(size, (size_t)(512 * 1024)));
    } catch (...) {}

    // Copy header (6 bytes) + logical screen descriptor (7 bytes)
    size_t pos = 0;
    result.insert(result.end(), data, data + 13);
    pos = 13;

    // Copy Global Color Table if present
    uint8_t packed = data[10];
    if (packed & 0x80) {
        int gctSize = 3 * (1 << ((packed & 0x07) + 1));
        if (pos + gctSize > size) return {};
        result.insert(result.end(), data + pos, data + pos + gctSize);
        pos += gctSize;
    }

    // Scan blocks until we find and copy the first image descriptor
    bool foundImage = false;
    while (pos < size && !foundImage) {
        uint8_t blockType = data[pos];

        if (blockType == 0x2C) {
            // Image Descriptor — copy it and its data
            if (pos + 10 > size) break;
            size_t imgStart = pos;
            pos++;  // block type

            uint8_t imgPacked = data[pos + 8];
            pos += 9;  // 9-byte image descriptor

            // Local Color Table
            size_t lctBytes = 0;
            if (imgPacked & 0x80) {
                lctBytes = 3 * (1 << ((imgPacked & 0x07) + 1));
                if (pos + lctBytes > size) break;
                pos += lctBytes;
            }

            // LZW minimum code size
            if (pos >= size) break;
            pos++;

            // Sub-blocks (image data)
            while (pos < size) {
                uint8_t subBlockSize = data[pos];
                pos++;
                if (subBlockSize == 0) break;
                if (pos + subBlockSize > size) { pos = size; break; }
                pos += subBlockSize;
            }

            // Copy everything from imgStart to pos (the full first image block)
            if (pos <= size) {
                result.insert(result.end(), data + imgStart, data + pos);
                foundImage = true;
            }
        } else if (blockType == 0x21) {
            // Extension block — copy it (may be a Graphic Control Extension
            // needed for the first frame's transparency/delay settings)
            size_t extStart = pos;
            pos++;  // block type
            if (pos >= size) break;
            pos++;  // extension label

            // Skip sub-blocks to find extent
            while (pos < size) {
                uint8_t subBlockSize = data[pos];
                pos++;
                if (subBlockSize == 0) break;
                if (pos + subBlockSize > size) { pos = size; break; }
                pos += subBlockSize;
            }

            if (pos <= size) {
                result.insert(result.end(), data + extStart, data + pos);
            }
        } else if (blockType == 0x3B) {
            break;
        } else {
            break;
        }
    }

    if (!foundImage) return {};

    // Append GIF trailer
    result.push_back(0x3B);

    brls::Logger::info("ImageLoader: Extracted first frame from animated GIF ({} bytes -> {} bytes)",
                       size, result.size());
    return result;
}

// Iterative GIF first-frame decoder — completely bypasses both FFmpeg and stb_image.
// FFmpeg's GIF decoder may not be available in all Vita builds (e.g. switchfin packages
// only include h264/aac/mp3), and stb_image's GIF LZW decoder (stbi__out_gif_code) is
// recursive with up to 4096 levels of recursion that overflows Vita worker thread stacks.
// This decoder parses the GIF binary format and uses an iterative LZW decompressor.
// Input: raw GIF file data (full file or extracted first frame).
// Output: TGA image data ready for texture upload, or empty vector on failure.
static std::vector<uint8_t> decodeGIFToTGA(const uint8_t* data, size_t size, int maxSize) {
    if (size < 13 || memcmp(data, "GIF", 3) != 0) return {};

    // Parse Logical Screen Descriptor
    int screenWidth  = data[6] | (data[7] << 8);
    int screenHeight = data[8] | (data[9] << 8);
    uint8_t packed   = data[10];
    // data[11] = background color index, data[12] = pixel aspect ratio

    bool hasGCT = (packed & 0x80) != 0;
    int gctSize = hasGCT ? (1 << ((packed & 0x07) + 1)) : 0;

    // Read Global Color Table
    uint8_t gct[256 * 3] = {};
    size_t pos = 13;
    if (hasGCT) {
        size_t gctBytes = static_cast<size_t>(gctSize) * 3;
        if (pos + gctBytes > size) return {};
        memcpy(gct, data + pos, gctBytes);
        pos += gctBytes;
    }

    // Scan for Graphic Control Extension (transparency) and first Image Descriptor
    int transparentIndex = -1;
    bool hasTransparency = false;

    while (pos < size) {
        uint8_t blockType = data[pos];

        if (blockType == 0x21) {
            // Extension block
            if (pos + 2 > size) return {};
            uint8_t label = data[pos + 1];
            pos += 2;

            if (label == 0xF9 && pos + 1 < size) {
                // Graphic Control Extension
                uint8_t blockSize = data[pos];
                if (blockSize >= 4 && pos + 1 + blockSize <= size) {
                    uint8_t gcPacked = data[pos + 1];
                    hasTransparency = (gcPacked & 0x01) != 0;
                    if (hasTransparency) {
                        transparentIndex = data[pos + 4];
                    }
                }
            }
            // Skip sub-blocks
            while (pos < size) {
                uint8_t sb = data[pos++];
                if (sb == 0) break;
                pos += sb;
                if (pos > size) return {};
            }
        } else if (blockType == 0x2C) {
            // Image Descriptor — decode this frame
            break;
        } else if (blockType == 0x3B) {
            return {};  // Trailer, no image found
        } else {
            return {};  // Unknown block
        }
    }

    if (pos >= size || data[pos] != 0x2C) return {};

    // Parse Image Descriptor
    if (pos + 10 > size) return {};
    int imgLeft   = data[pos + 1] | (data[pos + 2] << 8);
    int imgTop    = data[pos + 3] | (data[pos + 4] << 8);
    int imgWidth  = data[pos + 5] | (data[pos + 6] << 8);
    int imgHeight = data[pos + 7] | (data[pos + 8] << 8);
    uint8_t imgPacked = data[pos + 9];
    pos += 10;

    bool hasLCT = (imgPacked & 0x80) != 0;
    bool interlaced = (imgPacked & 0x40) != 0;
    int lctSize = hasLCT ? (1 << ((imgPacked & 0x07) + 1)) : 0;

    // Read Local Color Table (overrides GCT for this frame)
    uint8_t lct[256 * 3] = {};
    const uint8_t* colorTable = gct;
    int colorTableSize = gctSize;
    if (hasLCT) {
        size_t lctBytes = static_cast<size_t>(lctSize) * 3;
        if (pos + lctBytes > size) return {};
        memcpy(lct, data + pos, lctBytes);
        pos += lctBytes;
        colorTable = lct;
        colorTableSize = lctSize;
    }

    if (colorTableSize == 0) return {};  // No color table

    // Sanity check dimensions
    if (imgWidth <= 0 || imgHeight <= 0 || imgWidth > 16384 || imgHeight > 16384) return {};
    if (screenWidth <= 0 || screenHeight <= 0) {
        screenWidth = imgWidth;
        screenHeight = imgHeight;
    }

    long long pixelBytes = (long long)screenWidth * screenHeight * 4;
    if (pixelBytes > 64 * 1024 * 1024) {
        brls::Logger::warning("ImageLoader: GIF too large ({}x{} = {}MB), skipping",
                              screenWidth, screenHeight, pixelBytes / (1024 * 1024));
        return {};
    }

    // LZW Minimum Code Size
    if (pos >= size) return {};
    int lzwMinCodeSize = data[pos++];
    if (lzwMinCodeSize < 2 || lzwMinCodeSize > 12) return {};

    // Collect all sub-block data into a contiguous buffer
    std::vector<uint8_t> lzwData;
    try {
        lzwData.reserve(size - pos);  // upper bound
    } catch (...) {}
    while (pos < size) {
        uint8_t sb = data[pos++];
        if (sb == 0) break;
        if (pos + sb > size) return {};
        lzwData.insert(lzwData.end(), data + pos, data + pos + sb);
        pos += sb;
    }

    // --- Iterative LZW Decoder ---
    const int clearCode = 1 << lzwMinCodeSize;
    const int endCode   = clearCode + 1;
    const int maxCode   = 4096;

    // Code table: each entry stores (prefix, suffix) for chain reconstruction
    struct LZWEntry {
        uint16_t prefix;    // Previous code (or 0xFFFF for single-char entries)
        uint8_t  suffix;    // Last byte of this code's string
        uint8_t  firstChar; // First byte of this code's string (cached for O(1) lookup)
        uint16_t length;    // Total string length (for stack allocation)
    };
    std::vector<LZWEntry> table(maxCode);

    // Output pixel indices
    std::vector<uint8_t> pixels;
    try {
        pixels.reserve(static_cast<size_t>(imgWidth) * imgHeight);
    } catch (...) {}

    // Temp buffer for iterative string output (max LZW string = 4096 bytes)
    uint8_t stringBuf[4096];

    // Bit reader state
    int bitPos = 0;
    int totalBits = static_cast<int>(lzwData.size()) * 8;

    auto readBits = [&](int nBits) -> int {
        if (bitPos + nBits > totalBits) return endCode;
        int byteIdx = bitPos >> 3;
        int bitIdx  = bitPos & 7;
        // Read up to 24 bits from 3 bytes
        uint32_t val = lzwData[byteIdx];
        if (byteIdx + 1 < (int)lzwData.size()) val |= (uint32_t)lzwData[byteIdx + 1] << 8;
        if (byteIdx + 2 < (int)lzwData.size()) val |= (uint32_t)lzwData[byteIdx + 2] << 16;
        val = (val >> bitIdx) & ((1u << nBits) - 1);
        bitPos += nBits;
        return static_cast<int>(val);
    };

    int codeSize   = lzwMinCodeSize + 1;
    int nextCode   = endCode + 1;
    int codeMask   = (1 << codeSize) - 1;
    int prevCode   = -1;
    size_t maxPixels = static_cast<size_t>(imgWidth) * imgHeight;

    // Initialize base table entries
    for (int i = 0; i < clearCode; i++) {
        table[i].prefix = 0xFFFF;
        table[i].suffix = static_cast<uint8_t>(i);
        table[i].firstChar = static_cast<uint8_t>(i);
        table[i].length = 1;
    }

    // Helper: output the string for a code iteratively
    auto outputCode = [&](int code) {
        if (code < 0 || code >= maxCode) return;
        int len = table[code].length;
        if (len <= 0 || len > 4096) return;
        // Walk the chain backwards, filling stringBuf from the end
        int idx = len - 1;
        int c = code;
        while (c != 0xFFFF && idx >= 0) {
            stringBuf[idx--] = table[c].suffix;
            c = table[c].prefix;
        }
        // Append to pixel output
        int toWrite = std::min(len, static_cast<int>(maxPixels - pixels.size()));
        if (toWrite > 0) {
            pixels.insert(pixels.end(), stringBuf, stringBuf + toWrite);
        }
    };

    bool done = false;
    while (!done && pixels.size() < maxPixels) {
        int code = readBits(codeSize);

        if (code == endCode || code < 0) {
            done = true;
        } else if (code == clearCode) {
            // Reset
            codeSize = lzwMinCodeSize + 1;
            codeMask = (1 << codeSize) - 1;
            nextCode = endCode + 1;
            prevCode = -1;
        } else {
            if (prevCode < 0) {
                // First code after clear
                outputCode(code);
                prevCode = code;
            } else {
                if (code < nextCode) {
                    // Code in table
                    outputCode(code);
                    // Add new entry: prevCode's string + first char of code's string
                    if (nextCode < maxCode) {
                        table[nextCode].prefix = static_cast<uint16_t>(prevCode);
                        table[nextCode].suffix = table[code].firstChar;
                        table[nextCode].firstChar = table[prevCode].firstChar;
                        table[nextCode].length = table[prevCode].length + 1;
                        nextCode++;
                    }
                } else {
                    // Code not yet in table (special KwKwK case)
                    // New string = prevCode's string + first char of prevCode's string
                    if (nextCode < maxCode) {
                        table[nextCode].prefix = static_cast<uint16_t>(prevCode);
                        table[nextCode].suffix = table[prevCode].firstChar;
                        table[nextCode].firstChar = table[prevCode].firstChar;
                        table[nextCode].length = table[prevCode].length + 1;
                        nextCode++;
                    }
                    outputCode(code);
                }
                prevCode = code;
                // Increase code size when needed
                if (nextCode > codeMask && codeSize < 12) {
                    codeSize++;
                    codeMask = (1 << codeSize) - 1;
                }
            }
        }
    }

    if (pixels.empty()) {
        brls::Logger::error("ImageLoader: GIF iterative LZW decode produced no pixels");
        return {};
    }

    // Convert indexed pixels to RGBA
    std::vector<uint8_t> rgba;
    try {
        rgba.resize(static_cast<size_t>(screenWidth) * screenHeight * 4, 0);
    } catch (const std::bad_alloc&) {
        signalOOM("decodeGIFToTGA RGBA");
        return {};
    }

    // Build interlaced row lookup table (O(n) once instead of O(n²) per-pixel)
    std::vector<int> rowMap;
    if (interlaced) {
        rowMap.resize(imgHeight);
        static const int startRow[] = {0, 4, 2, 1};
        static const int stepRow[]  = {8, 8, 4, 2};
        int row = 0;
        for (int pass = 0; pass < 4; pass++) {
            for (int r = startRow[pass]; r < imgHeight; r += stepRow[pass]) {
                if (row < imgHeight) rowMap[row++] = r;
            }
        }
    }

    int px = 0, rawY = 0;
    for (size_t i = 0; i < pixels.size(); i++) {
        int screenX = imgLeft + px;
        int screenY = imgTop + (interlaced ? rowMap[rawY] : rawY);

        if (++px >= imgWidth) { px = 0; rawY++; }

        if (screenX < 0 || screenX >= screenWidth || screenY < 0 || screenY >= screenHeight)
            continue;

        uint8_t colorIdx = pixels[i];
        if (hasTransparency && colorIdx == transparentIndex)
            continue;  // Leave as transparent (0,0,0,0)

        if (colorIdx >= colorTableSize)
            continue;  // Invalid color index

        size_t outIdx = (static_cast<size_t>(screenY) * screenWidth + screenX) * 4;
        rgba[outIdx + 0] = colorTable[colorIdx * 3 + 0];
        rgba[outIdx + 1] = colorTable[colorIdx * 3 + 1];
        rgba[outIdx + 2] = colorTable[colorIdx * 3 + 2];
        rgba[outIdx + 3] = 255;
    }

    // Downscale if needed
    int dstW = screenWidth;
    int dstH = screenHeight;
    if (maxSize > 0 && (screenWidth > maxSize || screenHeight > maxSize)) {
        float scale = static_cast<float>(maxSize) / std::max(screenWidth, screenHeight);
        dstW = std::max(1, static_cast<int>(screenWidth * scale));
        dstH = std::max(1, static_cast<int>(screenHeight * scale));
    }

    if (dstW != screenWidth || dstH != screenHeight) {
        brls::Logger::info("ImageLoader: GIF decoded {}x{} -> {}x{} (iterative LZW)",
                           screenWidth, screenHeight, dstW, dstH);
        // Fused downscale + RGBA→BGRA swizzle in one pass
        return downscaleRGBAtoTGA(rgba.data(), screenWidth, screenHeight, dstW, dstH);
    }

    brls::Logger::info("ImageLoader: GIF decoded {}x{} (iterative LZW)", screenWidth, screenHeight);
    return createTGAFromRGBA(rgba.data(), dstW, dstH);
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

    config.output.colorspace = MODE_BGRA;

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

    auto tgaData = createTGAFromBGRA(config.output.u.RGBA.rgba, targetW, targetH);
    WebPFreeDecBuffer(&config.output);
    return tgaData;
}

// Convert JPEG/PNG to TGA for a specific segment of a tall image (using stb_image)
static std::vector<uint8_t> convertImageToTGASegment(const uint8_t* data, size_t dataSize,
                                                      int segment, int totalSegments, int maxSize) {
    std::vector<uint8_t> tgaData;

    if (totalSegments < 1 || segment < 0 || segment >= totalSegments) return tgaData;

    // Pre-check dimensions to prevent OOM crash on PS Vita.
    // stb_image decodes at full resolution (width*height*4 bytes) before we can
    // downscale.  Large GIFs or other images can require 10-30+ MB for the
    // intermediate RGBA buffer, exhausting the Vita's limited memory.
    {
        int preW, preH, preC;
        if (stbi_info_from_memory(data, static_cast<int>(dataSize), &preW, &preH, &preC)) {
            long long rgbaBytes = (long long)preW * preH * 4;
            if (rgbaBytes > 64 * 1024 * 1024) {
                brls::Logger::warning("ImageLoader: Image too large to decode ({}x{} = {}MB RGBA), skipping",
                                      preW, preH, rgbaBytes / (1024 * 1024));
                return tgaData;
            }
        }
    }

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

    // Fused segment-extract + downscale + RGBA→BGRA swizzle in one pass.
    // Reads directly from the full RGBA buffer at startY offset, avoiding
    // the intermediate memcpy of segment rows into a separate buffer.
    try {
        if (targetW != width || targetH != actualHeight) {
            tgaData = downscaleSegmentRGBAtoTGA(rgba, width, startY, actualHeight, targetW, targetH);
        } else {
            // No scaling needed — extract segment rows and swizzle to TGA
            tgaData = downscaleSegmentRGBAtoTGA(rgba, width, startY, actualHeight, width, actualHeight);
        }
    } catch (const std::bad_alloc&) {
        stbi_image_free(rgba);
        signalOOM("convertImageToTGASegment");
        return tgaData;
    }

    stbi_image_free(rgba);

    brls::Logger::debug("ImageLoader: JPEG/PNG Segment {}/{} - {}x{} (from {}x{} startY={})",
                        segment + 1, totalSegments, targetW, targetH, width, height, startY);

    return tgaData;
}

// Convert JPEG/PNG to TGA for ALL segments in one pass (decode once, extract N segments).
// This avoids the O(N) full stbi_load_from_memory decodes that happen when
// convertImageToTGASegment is called in a loop for each segment.
// Returns true on success with segmentDatas filled; false on failure.
static bool convertImageToTGAAllSegments(const uint8_t* data, size_t dataSize,
                                          int totalSegments, int maxSize,
                                          std::vector<std::vector<uint8_t>>& segmentDatas,
                                          int& outWidth, int& outHeight) {
    segmentDatas.clear();
    outWidth = outHeight = 0;

    if (totalSegments < 1) return false;

    // Pre-check dimensions to prevent OOM crash on PS Vita.
    {
        int preW, preH, preC;
        if (stbi_info_from_memory(data, static_cast<int>(dataSize), &preW, &preH, &preC)) {
            long long rgbaBytes = (long long)preW * preH * 4;
            if (rgbaBytes > 64 * 1024 * 1024) {
                brls::Logger::warning("ImageLoader: Image too large to decode ({}x{} = {}MB RGBA), skipping",
                                      preW, preH, rgbaBytes / (1024 * 1024));
                return false;
            }
        }
    }

    int width, height, channels;
    uint8_t* rgba = stbi_load_from_memory(data, static_cast<int>(dataSize), &width, &height, &channels, 4);
    if (!rgba) {
        brls::Logger::error("ImageLoader: stb_image failed to decode image (all-segments)");
        return false;
    }

    if (width <= 0 || height <= 0) {
        stbi_image_free(rgba);
        return false;
    }

    outWidth = width;
    outHeight = height;

    // Extract all segments from the single decoded buffer
    for (int seg = 0; seg < totalSegments; seg++) {
        int segmentHeight = (height + totalSegments - 1) / totalSegments;
        int startY = seg * segmentHeight;
        int endY = std::min(startY + segmentHeight, height);
        int actualHeight = endY - startY;

        if (actualHeight <= 0 || startY >= height) {
            stbi_image_free(rgba);
            return false;
        }

        int targetW = width;
        int targetH = actualHeight;
        if (width > maxSize) {
            float scale = (float)maxSize / width;
            targetW = maxSize;
            targetH = std::max(1, (int)(actualHeight * scale));
        }

        std::vector<uint8_t> segData;
        try {
            segData = downscaleSegmentRGBAtoTGA(rgba, width, startY, actualHeight, targetW, targetH);
        } catch (const std::bad_alloc&) {
            stbi_image_free(rgba);
            signalOOM("convertImageToTGAAllSegments");
            return false;
        }

        if (segData.empty()) {
            stbi_image_free(rgba);
            return false;
        }

        segmentDatas.push_back(std::move(segData));
    }

    stbi_image_free(rgba);

    brls::Logger::info("ImageLoader: JPEG/PNG decoded once, extracted {} segments from {}x{}",
                       totalSegments, width, height);
    return true;
}

// Convert JPEG/PNG to TGA with optional downscaling (using stb_image)
static std::vector<uint8_t> convertImageToTGA(const uint8_t* data, size_t dataSize, int maxSize) {
    std::vector<uint8_t> tgaData;

    // Pre-check dimensions to prevent OOM crash on PS Vita.
    // stb_image decodes at full resolution (width*height*4 bytes) before we can
    // downscale.  Large GIFs can require 10-30+ MB for the intermediate RGBA
    // buffer, exhausting the Vita's limited memory.  WebP avoids this because
    // libwebp supports scaled decode natively.
    {
        int preW, preH, preC;
        if (stbi_info_from_memory(data, static_cast<int>(dataSize), &preW, &preH, &preC)) {
            long long rgbaBytes = (long long)preW * preH * 4;
            if (rgbaBytes > 64 * 1024 * 1024) {
                brls::Logger::warning("ImageLoader: Image too large to decode ({}x{} = {}MB RGBA), skipping",
                                      preW, preH, rgbaBytes / (1024 * 1024));
                // Don't signalOOM here — this is a pre-check rejection, not an
                // actual memory failure.  Triggering the global OOM cooldown
                // would block all other (smaller) image loads for 60 frames.
                return tgaData;
            }
        }
    }

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

    try {
        if (targetW != width || targetH != height) {
            // Fused downscale + RGBA→BGRA swizzle in one pass (avoids
            // touching every pixel twice with separate downscale+swizzle)
            tgaData = downscaleRGBAtoTGA(rgba, width, height, targetW, targetH);
        } else {
            tgaData = createTGAFromRGBA(rgba, targetW, targetH);
        }
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
        // --- Attempt 1: Scaled BGRA decode (matches TGA byte order — no swizzle) ---
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
        config.output.colorspace = MODE_BGRA;

        VP8StatusCode decStatus = WebPDecode(webpData, webpSize, &config);
        if (outStatus) *outStatus = decStatus;
        if (decStatus == VP8_STATUS_OK) {
            auto tgaData = createTGAFromBGRA(config.output.u.RGBA.rgba, targetW, targetH);
            WebPFreeDecBuffer(&config.output);
            trackDecodeSuccess();
            brls::Logger::info("ImageLoader: WebP BGRA decode OK {}x{}->{}x{}", srcW, srcH, targetW, targetH);
            return tgaData;
        }

        if (decStatus == VP8_STATUS_OUT_OF_MEMORY) {
            signalOOM("WebPDecode BGRA scaled");
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

        brls::Logger::warning("ImageLoader: WebP scaled BGRA failed (status={}) for {}x{}->{}x{}, trying BGR",
                              static_cast<int>(decStatus), srcW, srcH, targetW, targetH);

        // --- Attempt 2: Scaled BGR decode (25% less memory) ---
        if (!WebPInitDecoderConfig(&config)) return {};
        config.options.use_scaling = 1;
        config.options.scaled_width = targetW;
        config.options.scaled_height = targetH;
        config.options.bypass_filtering = 1;
        config.options.no_fancy_upsampling = 1;
        config.output.colorspace = MODE_BGR;

        decStatus = WebPDecode(webpData, webpSize, &config);
        if (outStatus) *outStatus = decStatus;
        if (decStatus == VP8_STATUS_OK) {
            // Convert BGR to BGRA for TGA
            uint8_t* bgr = config.output.u.RGBA.rgba;
            size_t pixelCount = static_cast<size_t>(targetW) * static_cast<size_t>(targetH);
            std::vector<uint8_t> bgraVec;
            try {
                bgraVec.resize(pixelCount * 4);
            } catch (const std::bad_alloc&) {
                WebPFreeDecBuffer(&config.output);
                signalOOM("WebP BGR->BGRA alloc");
                return {};
            }
            for (size_t i = 0; i < pixelCount; i++) {
                bgraVec[i * 4 + 0] = bgr[i * 3 + 0];
                bgraVec[i * 4 + 1] = bgr[i * 3 + 1];
                bgraVec[i * 4 + 2] = bgr[i * 3 + 2];
                bgraVec[i * 4 + 3] = 255;
            }
            WebPFreeDecBuffer(&config.output);
            trackDecodeSuccess();
            brls::Logger::info("ImageLoader: WebP BGR decode OK {}x{}->{}x{}", srcW, srcH, targetW, targetH);
            return createTGAFromBGRA(bgraVec.data(), targetW, targetH);
        }

        if (decStatus == VP8_STATUS_OUT_OF_MEMORY) {
            signalOOM("WebPDecode BGR scaled");
        }
        WebPFreeDecBuffer(&config.output);
        return {};
    }

    // No scaling needed — decode at full resolution directly to BGRA
    uint8_t* bgra = WebPDecodeBGRA(webpData, webpSize, &srcW, &srcH);
    if (!bgra) {
        if (outStatus) *outStatus = VP8_STATUS_BITSTREAM_ERROR;
        return {};
    }
    if (outStatus) *outStatus = VP8_STATUS_OK;
    trackDecodeSuccess();
    auto tgaData = createTGAFromBGRA(bgra, srcW, srcH);
    WebPFree(bgra);
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

    // Hint the input format when known — avformat_open_input's auto-detection
    // can fail on memory buffers, especially for extracted GIF first-frames.
    // av_find_input_format returns const AVInputFormat* in FFmpeg 5+ but
    // AVInputFormat* in older versions.  avformat_open_input accepts both.
#if LIBAVFORMAT_VERSION_MAJOR >= 59
    const AVInputFormat* inputFmt = nullptr;
#else
    AVInputFormat* inputFmt = nullptr;
#endif
    if (formatName) {
        // FFmpeg demuxer names: "gif", "avif" (via mov), "heif" (via mov), "webp" (via image2)
        // Try the format name in lowercase first
        std::string fmtLower;
        for (const char* p = formatName; *p; ++p)
            fmtLower += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
        inputFmt = av_find_input_format(fmtLower.c_str());
        // For GIF specifically, ensure we use the "gif" demuxer
        if (!inputFmt && fmtLower == "gif") {
            inputFmt = av_find_input_format("gif_pipe");
        }
    }

    // Open input from memory
    int ret = avformat_open_input(&fmtCtx, nullptr, inputFmt, nullptr);
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

    // Convert directly to BGRA using swscale (matches TGA byte order — no swizzle)
    SwsContext* swsCtx = sws_getContext(srcW, srcH, static_cast<AVPixelFormat>(frame->format),
                                         dstW, dstH, AV_PIX_FMT_BGRA,
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

    size_t imageSize = static_cast<size_t>(dstW) * dstH * 4;
    std::vector<uint8_t> tga;
    try {
        tga.resize(18 + imageSize);
    } catch (const std::bad_alloc&) {
        sws_freeContext(swsCtx);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        avio_context_free(&avioCtx);
        signalOOM("FFmpeg BGRA alloc");
        return result;
    }
    writeTGAHeader(tga.data(), dstW, dstH);
    uint8_t* dstSlice[1] = {tga.data() + 18};
    int dstStride[1] = {dstW * 4};

    sws_scale(swsCtx, frame->data, frame->linesize, 0, srcH, dstSlice, dstStride);

    sws_freeContext(swsCtx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    avio_context_free(&avioCtx);

    brls::Logger::info("ImageLoader: {} decoded {}x{} -> {}x{}", formatName, srcW, srcH, dstW, dstH);
    return tga;
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
            // Check alive TWICE: once before and once after validation to narrow
            // the TOCTOU window (both checks run on main thread, so the real risk
            // is a brls::sync callback destroying the cell between iterations).
            if (update.alive && !*update.alive) {
                continue;
            }
            // Skip empty data to avoid passing garbage to NVG
            if (update.data.empty()) {
                continue;
            }
            // Re-check alive right before the actual GPU upload - a brls::sync
            // callback processed earlier in this frame could have destroyed the cell.
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
                    // Re-check alive after disk I/O
                    if (alive && !*alive) return;
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
    bool isGIF = false;
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
            isGIF = true;
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

    // Move the HTTP response body into a local vector so we hold a compact copy.
    // The `resp` object (with its string, headers, etc.) is freed here, reducing
    // peak memory during the decode step that follows.
    std::vector<uint8_t> bodyVec;
    try {
        bodyVec.assign(bodyData, bodyData + bodySize);
    } catch (const std::bad_alloc&) {
        signalOOM("executeLoad bodyVec alloc");
        return;
    }
    { std::string().swap(resp.body); }  // release resp.body memory now
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
        // For animated WebP, extract just the first frame before decoding.
        // This avoids holding the entire multi-MB file in memory during decode
        // — the first frame is typically ~70KB vs the full file being 2+ MB.
        {
            WebPBitstreamFeatures webpFeat;
            if (WebPGetFeatures(decData, decSize, &webpFeat) == VP8_STATUS_OK && webpFeat.has_animation) {
                auto firstFrame = extractFirstFrameFromAnimatedWebP(decData, decSize);
                if (!firstFrame.empty()) {
                    // Replace bodyVec with the small first-frame data
                    bodyVec = std::move(firstFrame);
                    decData = bodyVec.data();
                    decSize = bodyVec.size();
                }
            }
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
    } else if (isGIF) {
        // For animated GIFs, extract just the first frame before decoding.
        // This avoids holding the entire multi-MB file in memory during LZW
        // decode — a 10MB animated GIF's first frame is typically only a few KB.
        const uint8_t* gifDecData = decData;
        size_t gifDecSize = decSize;
        std::vector<uint8_t> firstFrame;
        if (isAnimatedGIF(decData, decSize)) {
            firstFrame = extractFirstFrameFromGIF(decData, decSize);
            if (!firstFrame.empty()) {
                // Release the large original buffer now that we have the small first frame
                bodyVec.clear();
                bodyVec.shrink_to_fit();
                gifDecData = firstFrame.data();
                gifDecSize = firstFrame.size();
            }
        }
        // Decode GIF using our iterative LZW decoder.  This bypasses both FFmpeg
        // (which may lack a GIF decoder in some Vita builds) and stb_image (whose
        // recursive LZW decoder overflows Vita worker thread stacks).
        imageData = decodeGIFToTGA(gifDecData, gifDecSize, s_maxThumbnailSize);
        if (imageData.empty()) {
            brls::Logger::error("ImageLoader: GIF decode failed for {}", url);
            return;
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

    // Re-check alive flag after the (potentially long) decode.  The owning
    // view may have been destroyed while we were downloading / decoding.
    // Without this, a stale `target` pointer reaches processPendingTextures()
    // and crashes on setImageFromMem().
    if (alive && !*alive) return;

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

    // Check page disk cache before downloading (avoids network + expensive decode)
    int pageMangaId = -1, pageChapterIdx = -1, pagePageIdx = -1;
    bool hasPageIds = extractPageUrlIds(url, pageMangaId, pageChapterIdx, pagePageIdx);
    bool pageCacheOn = Application::getInstance().getSettings().pageCacheEnabled;
    if (pageCacheOn && hasPageIds && totalSegments <= 1) {
        std::vector<uint8_t> diskData;
        if (LibraryCache::getInstance().loadPageImage(pageMangaId, pageChapterIdx, pagePageIdx, diskData)) {
            auto ioEndTime = std::chrono::steady_clock::now();
            auto ioMs = std::chrono::duration_cast<std::chrono::milliseconds>(ioEndTime - loadStartTime).count();
            brls::Logger::info("ImageLoader: [TIMING] Disk cache hit {}ms for {} ({} bytes)",
                              ioMs, url, diskData.size());

            // Put in LRU memory cache too
            std::string cacheKey = url + "_full";
            cachePut(cacheKey, diskData);

            if (target || callback) {
                queueRotatableTextureUpdate(diskData, target, callback, alive);
            }
            return;
        }
    }

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
        bool isGIF = false;
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
            // GIF (decode via FFmpeg — stb_image's recursive LZW decoder can
            // overflow the stack on PS Vita worker threads, causing psp2core crashes)
            else if (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46) {
                isGIF = true;
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

        // Convert images to TGA with size limit optimized for Vita display.
        // The Vita renders at 1280x726 internally, so anything larger than 1280
        // on the longest dimension is wasted resolution. Using 1280 instead of 2048
        // reduces decode time and memory significantly:
        // e.g. 1125x1600 → 900x1280 (saves ~40% pixels, ~40% faster decode)
        const int MAX_TEXTURE_SIZE = 1280;

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
                    if (feat.has_animation) {
                        // Animated WebP: extract just the first frame and release
                        // the large original buffer.  This avoids holding the full
                        // multi-MB file in memory during decode.
                        auto firstFrame = extractFirstFrameFromAnimatedWebP(
                            reinterpret_cast<const uint8_t*>(imageBody.data()), imageBody.size());
                        if (!firstFrame.empty()) {
                            imageBody.assign(reinterpret_cast<const char*>(firstFrame.data()), firstFrame.size());
                            firstFrame.clear();
                            firstFrame.shrink_to_fit();
                        }
                        // Skip auto-split — segmented crop decode doesn't work
                        // on animated containers (now single-frame anyway).
                    } else {
                        origW = feat.width;
                        origH = feat.height;
                    }
                }
            } else {
                int c;
                stbi_info_from_memory(reinterpret_cast<const unsigned char*>(imageBody.data()),
                                      static_cast<int>(imageBody.size()), &origW, &origH, &c);
            }

            if (origW > 0 && origH > MAX_TEXTURE_SIZE * 2) {
                // Only auto-split if image is MUCH taller than max (webtoon-style).
                // For normal manga pages slightly taller than max, proportional
                // scaling into a single texture is faster than multi-segment decode.
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

                if (!isWebP) {
                    // JPEG/PNG optimization: decode the full image ONCE and extract
                    // all segments from the decoded buffer. This avoids N separate
                    // stbi_load_from_memory calls (each decoding the entire image)
                    // when splitting into N segments.
                    int decW = 0, decH = 0;
                    allOK = convertImageToTGAAllSegments(
                        reinterpret_cast<const uint8_t*>(imageBody.data()),
                        imageBody.size(), autoSegments, segMaxSize,
                        segmentDatas, decW, decH);

                    if (allOK) {
                        // Cache each segment and build height list
                        for (int seg = 0; seg < autoSegments; seg++) {
                            std::string segCK = url + "_full_autoseg" + std::to_string(seg);
                            cachePut(segCK, segmentDatas[seg]);

                            int segH = (decH + autoSegments - 1) / autoSegments;
                            int startY = seg * segH;
                            int endY = std::min(startY + segH, decH);
                            segSrcHeights.push_back(endY - startY);
                        }
                    }
                } else {
                    // WebP: use per-segment crop+scale decode (efficient, no full alloc)
                    for (int seg = 0; seg < autoSegments && allOK; seg++) {
                        if (alive && !*alive) return;  // Owner destroyed during processing
                        if (isUnderMemoryPressure()) {
                            brls::Logger::warning("ImageLoader: Aborting auto-split at seg {}/{} (OOM cooldown)", seg, autoSegments);
                            allOK = false;
                            break;
                        }

                        std::vector<uint8_t> segData;
                        segData = convertWebPtoTGASegment(
                            reinterpret_cast<const uint8_t*>(imageBody.data()),
                            imageBody.size(), seg, autoSegments, segMaxSize);
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
        } else if (isGIF) {
            // For animated GIFs, extract just the first frame before decoding.
            // This avoids holding the entire multi-MB file in memory during LZW
            // decode — a 10MB animated GIF's first frame is typically only a few KB.
            const uint8_t* gifData = reinterpret_cast<const uint8_t*>(imageBody.data());
            size_t gifSize = imageBody.size();
            std::vector<uint8_t> firstFrame;
            if (isAnimatedGIF(gifData, gifSize)) {
                firstFrame = extractFirstFrameFromGIF(gifData, gifSize);
                if (!firstFrame.empty()) {
                    // Release the large original buffer now that we have the small first frame
                    { std::string().swap(imageBody); }
                    gifData = firstFrame.data();
                    gifSize = firstFrame.size();
                }
            }
            // Decode GIF using our iterative LZW decoder.  This bypasses both FFmpeg
            // (which may lack a GIF decoder in some Vita builds) and stb_image (whose
            // recursive LZW decoder overflows Vita worker thread stacks).
            imageData = decodeGIFToTGA(gifData, gifSize, MAX_TEXTURE_SIZE);
            if (imageData.empty()) {
                brls::Logger::error("ImageLoader: GIF decode failed for {}", url);
                return;
            }
        } else {
            // For other formats, pass through directly
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

        // Save to page disk cache for instant loading next time
        if (pageCacheOn && hasPageIds && totalSegments <= 1 && !imageData.empty()) {
            LibraryCache::getInstance().savePageImage(pageMangaId, pageChapterIdx, pagePageIdx, imageData);
        }

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
