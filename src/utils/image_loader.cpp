/**
 * VitaSuwayomi - Asynchronous Image Loader implementation
 */

#include "utils/image_loader.hpp"
#include "utils/http_client.hpp"
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

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
                // WebP: RIFF....WEBP
                else if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46) {
                    brls::Logger::warning("ImageLoader: WebP format not supported: {}", url);
                }
            }

            if (!isValidImage) {
                brls::Logger::warning("ImageLoader: Response may not be a valid image: {} (first bytes: {:02X} {:02X} {:02X})",
                    url,
                    resp.body.size() > 0 ? (unsigned char)resp.body[0] : 0,
                    resp.body.size() > 1 ? (unsigned char)resp.body[1] : 0,
                    resp.body.size() > 2 ? (unsigned char)resp.body[2] : 0);
            }

            // Cache the image data
            std::vector<uint8_t> imageData(resp.body.begin(), resp.body.end());

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
            std::vector<uint8_t> imageData(resp.body.begin(), resp.body.end());

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
