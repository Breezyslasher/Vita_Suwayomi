/**
 * VitaSuwayomi - Asynchronous Image Loader implementation
 */

#include "utils/image_loader.hpp"
#include "utils/http_client.hpp"
#include "app/suwayomi_client.hpp"

// WebP decoding support
#include <webp/decode.h>
#include <cstring>

namespace vitasuwayomi {

// Helper function to convert WebP to TGA format (which stb_image can load)
// TGA is simple: header + raw RGBA data
static std::vector<uint8_t> convertWebPtoTGA(const uint8_t* webpData, size_t webpSize) {
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

    // Create TGA file in memory
    // TGA header is 18 bytes
    size_t imageSize = width * height * 4; // RGBA
    tgaData.resize(18 + imageSize);

    // TGA header
    uint8_t* header = tgaData.data();
    memset(header, 0, 18);
    header[2] = 2;          // Uncompressed true-color image
    header[12] = width & 0xFF;
    header[13] = (width >> 8) & 0xFF;
    header[14] = height & 0xFF;
    header[15] = (height >> 8) & 0xFF;
    header[16] = 32;        // 32 bits per pixel (RGBA)
    header[17] = 0x28;      // Image descriptor: top-left origin + 8 alpha bits

    // TGA stores pixels in BGRA order, bottom-to-top by default
    // But with header[17] = 0x28, we have top-left origin
    // Convert RGBA to BGRA
    uint8_t* pixels = tgaData.data() + 18;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int srcIdx = (y * width + x) * 4;
            int dstIdx = (y * width + x) * 4;
            pixels[dstIdx + 0] = rgba[srcIdx + 2];  // B
            pixels[dstIdx + 1] = rgba[srcIdx + 1];  // G
            pixels[dstIdx + 2] = rgba[srcIdx + 0];  // R
            pixels[dstIdx + 3] = rgba[srcIdx + 3];  // A
        }
    }

    // Free WebP decoded data
    WebPFree(rgba);

    brls::Logger::info("ImageLoader: Converted WebP to TGA ({} bytes)", tgaData.size());
    return tgaData;
}

std::map<std::string, std::vector<uint8_t>> ImageLoader::s_cache;
std::mutex ImageLoader::s_cacheMutex;
std::string ImageLoader::s_authUsername;
std::string ImageLoader::s_authPassword;

void ImageLoader::setAuthCredentials(const std::string& username, const std::string& password) {
    s_authUsername = username;
    s_authPassword = password;
}

void ImageLoader::loadAsync(const std::string& url, LoadCallback callback, brls::Image* target) {
    if (url.empty() || !target) return;

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(url);
        if (it != s_cache.end()) {
            // Load from cache
            target->setImageFromMem(it->second.data(), it->second.size());
            if (callback) callback(target);
            return;
        }
    }

    // Capture auth credentials for async use
    std::string authUser = s_authUsername;
    std::string authPass = s_authPassword;

    // Load asynchronously
    brls::async([url, callback, target, authUser, authPass]() {
        HttpClient client;

        // Add authentication if credentials are set
        if (!authUser.empty() && !authPass.empty()) {
            // Base64 encode username:password for Basic Auth
            std::string credentials = authUser + ":" + authPass;
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
            brls::Logger::debug("ImageLoader: Loaded {} bytes from {} (status={})",
                resp.body.size(), url, resp.statusCode);

            // Check if response looks like an image (basic header check)
            bool isValidImage = false;
            if (resp.body.size() > 8) {
                // Check for common image headers
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
                // WebP: RIFF....WEBP (will be converted)
                else if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46) {
                    brls::Logger::debug("ImageLoader: WebP detected, will convert: {}", url);
                }
            }

            // Check if it's WebP and convert to TGA
            bool isWebP = false;
            if (resp.body.size() > 12) {
                unsigned char* data = reinterpret_cast<unsigned char*>(resp.body.data());
                // WebP: RIFF....WEBP
                if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46 &&
                    data[8] == 0x57 && data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50) {
                    isWebP = true;
                    isValidImage = true; // We'll convert it
                }
            }

            if (!isValidImage) {
                brls::Logger::warning("ImageLoader: Response may not be a valid image: {} (first bytes: {:02X} {:02X} {:02X})",
                    url,
                    resp.body.size() > 0 ? (unsigned char)resp.body[0] : 0,
                    resp.body.size() > 1 ? (unsigned char)resp.body[1] : 0,
                    resp.body.size() > 2 ? (unsigned char)resp.body[2] : 0);
            }

            // Convert WebP to TGA if needed
            std::vector<uint8_t> imageData;
            if (isWebP) {
                imageData = convertWebPtoTGA(
                    reinterpret_cast<const uint8_t*>(resp.body.data()),
                    resp.body.size()
                );
                if (imageData.empty()) {
                    brls::Logger::error("ImageLoader: WebP conversion failed for {}", url);
                    return;
                }
            } else {
                imageData.assign(resp.body.begin(), resp.body.end());
            }

            {
                std::lock_guard<std::mutex> lock(s_cacheMutex);
                // Limit cache size
                if (s_cache.size() > 100) {
                    s_cache.clear();
                }
                s_cache[url] = imageData;
            }

            // Update UI on main thread
            brls::sync([imageData, callback, target, url]() {
                target->setImageFromMem(imageData.data(), imageData.size());
                if (callback) callback(target);
            });
        } else {
            brls::Logger::error("ImageLoader: Failed to load {}: status={} bodySize={} error={}",
                url, resp.statusCode, resp.body.size(),
                resp.error.empty() ? "no error message" : resp.error);
        }
    });
}

void ImageLoader::preload(const std::string& url) {
    if (url.empty()) return;

    // Check if already cached
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_cache.find(url) != s_cache.end()) {
            return; // Already in cache
        }
    }

    // Capture auth credentials for async use
    std::string authUser = s_authUsername;
    std::string authPass = s_authPassword;

    // Preload asynchronously (no callback, just cache)
    brls::async([url, authUser, authPass]() {
        HttpClient client;

        // Add authentication if credentials are set
        if (!authUser.empty() && !authPass.empty()) {
            std::string credentials = authUser + ":" + authPass;
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
            std::vector<uint8_t> imageData;

            // Check if it's WebP and convert
            bool isWebP = false;
            if (resp.body.size() > 12) {
                unsigned char* data = reinterpret_cast<unsigned char*>(resp.body.data());
                if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46 &&
                    data[8] == 0x57 && data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50) {
                    isWebP = true;
                }
            }

            if (isWebP) {
                imageData = convertWebPtoTGA(
                    reinterpret_cast<const uint8_t*>(resp.body.data()),
                    resp.body.size()
                );
                if (imageData.empty()) {
                    brls::Logger::error("ImageLoader: WebP conversion failed for preload: {}", url);
                    return;
                }
            } else {
                imageData.assign(resp.body.begin(), resp.body.end());
            }

            std::lock_guard<std::mutex> lock(s_cacheMutex);
            if (s_cache.size() > 100) {
                s_cache.clear();
            }
            s_cache[url] = imageData;
            brls::Logger::debug("ImageLoader: Preloaded {} bytes from {}", imageData.size(), url);
        }
    });
}

void ImageLoader::clearCache() {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_cache.clear();
}

void ImageLoader::cancelAll() {
    // Borealis handles thread cancellation
}

} // namespace vitasuwayomi
