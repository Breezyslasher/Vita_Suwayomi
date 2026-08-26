/**
 * VitaSuwayomi - HTTP Client
 * Using libcurl for network requests
 */

#pragma once

#include <string>
#include <map>
#include <functional>
#include <cstdint>

namespace vitasuwayomi {

// HTTP response
struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error;
    bool success = false;
};

// HTTP request configuration
struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::string body;
    std::map<std::string, std::string> headers;
    int timeout = 30;
    bool followRedirects = true;
};

/**
 * HTTP Client using libcurl
 */
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // Non-copyable (m_curl is a raw handle — copying would double-free)
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Movable
    HttpClient(HttpClient&& other) noexcept;
    HttpClient& operator=(HttpClient&& other) noexcept;

    // Initialize/cleanup (call once globally)
    static bool globalInit();
    static void globalCleanup();

    // Simple requests
    HttpResponse get(const std::string& url);
    HttpResponse post(const std::string& url, const std::string& body,
                      const std::string& contentType = "application/json");
    HttpResponse put(const std::string& url, const std::string& body);
    HttpResponse del(const std::string& url);

    // Full request
    HttpResponse request(const HttpRequest& req);

    // Headers
    void setDefaultHeader(const std::string& key, const std::string& value);
    void removeDefaultHeader(const std::string& key);
    void clearDefaultHeaders();

    // Configuration
    void setTimeout(int seconds) { m_timeout = seconds; }
    void setFollowRedirects(bool follow) { m_followRedirects = follow; }
    void setUserAgent(const std::string& ua) { m_userAgent = ua; }

    /// Enforce TLS certificate verification (peer + hostname) for this client.
    ///
    /// Off by default: a Suwayomi server is typically the user's own machine on
    /// the LAN, often over plain HTTP or with a self-signed certificate, so
    /// forcing verification there would break ordinary setups.
    ///
    /// It MUST be on for anything fetched from the public internet — above all
    /// the in-app updater, which downloads code that is then installed and run.
    /// Without verification a network attacker can substitute the release feed
    /// and the artifact. Uses the bundled CA bundle when present, else the
    /// platform's own store; if neither can verify, the request fails closed.
    void setVerifyTls(bool verify) { m_verifyTls = verify; }

    /// Offline mode: refuse to dial out at all, failing every request at once.
    ///
    /// This is global rather than per-client on purpose. Server traffic does
    /// not all go through SuwayomiClient — the image loader's worker pool and
    /// the download manager build their own clients — so gating one funnel
    /// leaves the rest waiting out the full connection timeout, per thread,
    /// against a server the user has told us not to talk to. Set from
    /// Application::setOfflineMode.
    static void setGlobalOffline(bool offline);
    static bool isGlobalOffline();

    /// Opt this client out of the global gate: it targets the public internet
    /// rather than the user's Suwayomi server, so "offline" (meaning: don't use
    /// my server) does not apply to it. The updater is the only such client.
    void setInternetClient(bool internet) { m_internetClient = internet; }

    /// Gag this one client regardless of the global flag.
    void setOffline(bool offline) { m_offline = offline; }
    bool isOffline() const;

    // Simple get that returns body directly
    bool get(const std::string& url, std::string& response);

    // Download file with progress callbacks
    // writeCallback: receives data chunks, return false to cancel
    // sizeCallback: called with total file size when known
    using WriteCallback = std::function<bool(const char* data, size_t size)>;
    using SizeCallback = std::function<void(int64_t totalSize)>;
    bool downloadFile(const std::string& url, WriteCallback writeCallback, SizeCallback sizeCallback = nullptr);

    // Download directly to file (streams to disk, no memory buffering)
    bool downloadToFile(const std::string& url, const std::string& filePath);

    // URL encoding
    static std::string urlEncode(const std::string& str);
    static std::string urlDecode(const std::string& str);

private:
    void* m_curl = nullptr;
    int m_timeout = 30;
    bool m_followRedirects = true;
    bool m_verifyTls = false;
    bool m_offline = false;
    bool m_internetClient = false;
    std::string m_userAgent;
    std::map<std::string, std::string> m_defaultHeaders;

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static size_t headerCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

} // namespace vitasuwayomi
