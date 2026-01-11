/**
 * VitaSuwayomi - Suwayomi Server API Client implementation
 */

#include "app/suwayomi_client.hpp"
#include "utils/http_client.hpp"

#include <borealis.hpp>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace vitasuwayomi {

SuwayomiClient& SuwayomiClient::getInstance() {
    static SuwayomiClient instance;
    return instance;
}

std::string SuwayomiClient::buildApiUrl(const std::string& endpoint) {
    std::string url = m_serverUrl;

    // Remove trailing slash
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    // All Suwayomi REST API endpoints are under /api/v1
    url += "/api/v1" + endpoint;

    return url;
}

// Helper to create an HTTP client with authentication headers
vitasuwayomi::HttpClient SuwayomiClient::createHttpClient() {
    vitasuwayomi::HttpClient http = createHttpClient();

    // Add basic auth if credentials are set
    if (!m_authUsername.empty() && !m_authPassword.empty()) {
        // Base64 encode username:password for Basic Auth
        std::string credentials = m_authUsername + ":" + m_authPassword;
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
        http.setDefaultHeader("Authorization", "Basic " + encoded);
    }

    return http;
}

// ============================================================================
// JSON parsing helpers
// ============================================================================

std::string SuwayomiClient::extractJsonValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = json.find(':', keyPos);
    if (colonPos == std::string::npos) return "";

    size_t valueStart = json.find_first_not_of(" \t\n\r", colonPos + 1);
    if (valueStart == std::string::npos) return "";

    if (json[valueStart] == '"') {
        size_t valueEnd = valueStart + 1;
        while (valueEnd < json.length()) {
            if (json[valueEnd] == '"' && json[valueEnd - 1] != '\\') {
                break;
            }
            valueEnd++;
        }
        if (valueEnd == std::string::npos) return "";
        return json.substr(valueStart + 1, valueEnd - valueStart - 1);
    } else if (json[valueStart] == 'n' && json.substr(valueStart, 4) == "null") {
        return "";
    } else {
        size_t valueEnd = json.find_first_of(",}]", valueStart);
        if (valueEnd == std::string::npos) return "";
        std::string value = json.substr(valueStart, valueEnd - valueStart);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\n' || value.back() == '\r')) {
            value.pop_back();
        }
        return value;
    }
}

int SuwayomiClient::extractJsonInt(const std::string& json, const std::string& key) {
    std::string value = extractJsonValue(json, key);
    if (value.empty()) return 0;
    return atoi(value.c_str());
}

float SuwayomiClient::extractJsonFloat(const std::string& json, const std::string& key) {
    std::string value = extractJsonValue(json, key);
    if (value.empty()) return 0.0f;
    return (float)atof(value.c_str());
}

bool SuwayomiClient::extractJsonBool(const std::string& json, const std::string& key) {
    std::string value = extractJsonValue(json, key);
    return (value == "true" || value == "1");
}

int64_t SuwayomiClient::extractJsonInt64(const std::string& json, const std::string& key) {
    std::string value = extractJsonValue(json, key);
    if (value.empty()) return 0;
    try {
        return std::stoll(value);
    } catch (...) {
        return 0;
    }
}

std::string SuwayomiClient::extractJsonArray(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = json.find(':', keyPos + searchKey.length());
    if (colonPos == std::string::npos) return "";

    size_t arrStart = json.find('[', colonPos);
    if (arrStart == std::string::npos) return "";

    // Make sure there's nothing but whitespace between colon and bracket
    for (size_t i = colonPos + 1; i < arrStart; i++) {
        char c = json[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            return "";
        }
    }

    int bracketCount = 1;
    size_t arrEnd = arrStart + 1;
    while (bracketCount > 0 && arrEnd < json.length()) {
        if (json[arrEnd] == '[') bracketCount++;
        else if (json[arrEnd] == ']') bracketCount--;
        arrEnd++;
    }

    return json.substr(arrStart, arrEnd - arrStart);
}

std::string SuwayomiClient::extractJsonObject(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = json.find(':', keyPos + searchKey.length());
    if (colonPos == std::string::npos) return "";

    size_t objStart = json.find('{', colonPos);
    if (objStart == std::string::npos) return "";

    // Make sure there's nothing but whitespace between colon and bracket
    for (size_t i = colonPos + 1; i < objStart; i++) {
        char c = json[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            return "";
        }
    }

    int braceCount = 1;
    size_t objEnd = objStart + 1;
    while (braceCount > 0 && objEnd < json.length()) {
        if (json[objEnd] == '{') braceCount++;
        else if (json[objEnd] == '}') braceCount--;
        objEnd++;
    }

    return json.substr(objStart, objEnd - objStart);
}

std::vector<std::string> SuwayomiClient::extractJsonStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string arrStr = extractJsonArray(json, key);
    if (arrStr.empty() || arrStr == "[]") return result;

    // Parse string array: ["item1", "item2", ...]
    size_t pos = 1; // Skip opening [
    while (pos < arrStr.length()) {
        size_t quoteStart = arrStr.find('"', pos);
        if (quoteStart == std::string::npos) break;

        size_t quoteEnd = quoteStart + 1;
        while (quoteEnd < arrStr.length()) {
            if (arrStr[quoteEnd] == '"' && arrStr[quoteEnd - 1] != '\\') break;
            quoteEnd++;
        }
        if (quoteEnd >= arrStr.length()) break;

        result.push_back(arrStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1));
        pos = quoteEnd + 1;
    }

    return result;
}

std::vector<std::string> SuwayomiClient::splitJsonArray(const std::string& arrayJson) {
    std::vector<std::string> items;
    if (arrayJson.empty() || arrayJson == "[]") return items;

    size_t pos = 1; // Skip opening [
    int braceCount = 0;
    int bracketCount = 0;
    size_t itemStart = pos;
    bool inString = false;

    while (pos < arrayJson.length() - 1) {
        char c = arrayJson[pos];

        if (c == '"' && (pos == 0 || arrayJson[pos - 1] != '\\')) {
            inString = !inString;
        } else if (!inString) {
            if (c == '{') braceCount++;
            else if (c == '}') braceCount--;
            else if (c == '[') bracketCount++;
            else if (c == ']') bracketCount--;
            else if (c == ',' && braceCount == 0 && bracketCount == 0) {
                std::string item = arrayJson.substr(itemStart, pos - itemStart);
                // Trim whitespace
                size_t start = item.find_first_not_of(" \t\n\r");
                size_t end = item.find_last_not_of(" \t\n\r");
                if (start != std::string::npos && end != std::string::npos) {
                    items.push_back(item.substr(start, end - start + 1));
                }
                itemStart = pos + 1;
            }
        }
        pos++;
    }

    // Add last item
    if (itemStart < arrayJson.length() - 1) {
        std::string item = arrayJson.substr(itemStart, arrayJson.length() - 1 - itemStart);
        size_t start = item.find_first_not_of(" \t\n\r");
        size_t end = item.find_last_not_of(" \t\n\r");
        if (start != std::string::npos && end != std::string::npos) {
            items.push_back(item.substr(start, end - start + 1));
        }
    }

    return items;
}

// ============================================================================
// Object parsing
// ============================================================================

Manga SuwayomiClient::parseManga(const std::string& json) {
    Manga manga;

    manga.id = extractJsonInt(json, "id");
    manga.sourceId = extractJsonInt64(json, "sourceId");
    manga.url = extractJsonValue(json, "url");
    manga.title = extractJsonValue(json, "title");
    manga.thumbnailUrl = extractJsonValue(json, "thumbnailUrl");
    manga.artist = extractJsonValue(json, "artist");
    manga.author = extractJsonValue(json, "author");
    manga.description = extractJsonValue(json, "description");
    manga.inLibrary = extractJsonBool(json, "inLibrary");
    manga.initialized = extractJsonBool(json, "initialized");
    manga.freshData = extractJsonBool(json, "freshData");
    manga.sourceName = extractJsonValue(json, "sourceName");

    // Parse status
    int statusInt = extractJsonInt(json, "status");
    manga.status = static_cast<MangaStatus>(statusInt);

    // Parse genre array
    manga.genre = extractJsonStringArray(json, "genre");

    // Parse counts
    manga.unreadCount = extractJsonInt(json, "unreadCount");
    manga.downloadedCount = extractJsonInt(json, "downloadedCount");
    manga.chapterCount = extractJsonInt(json, "chapterCount");
    manga.lastChapterRead = extractJsonInt(json, "lastChapterRead");

    return manga;
}

Chapter SuwayomiClient::parseChapter(const std::string& json) {
    Chapter ch;

    ch.id = extractJsonInt(json, "id");
    ch.url = extractJsonValue(json, "url");
    ch.name = extractJsonValue(json, "name");
    ch.scanlator = extractJsonValue(json, "scanlator");
    ch.chapterNumber = extractJsonFloat(json, "chapterNumber");
    ch.uploadDate = extractJsonInt64(json, "uploadDate");
    ch.read = extractJsonBool(json, "read");
    ch.bookmarked = extractJsonBool(json, "bookmarked");
    ch.lastPageRead = extractJsonInt(json, "lastPageRead");
    ch.pageCount = extractJsonInt(json, "pageCount");
    ch.index = extractJsonInt(json, "index");
    ch.fetchedAt = extractJsonInt64(json, "fetchedAt");
    ch.downloaded = extractJsonBool(json, "downloaded");
    ch.mangaId = extractJsonInt(json, "mangaId");

    return ch;
}

Source SuwayomiClient::parseSource(const std::string& json) {
    Source src;

    src.id = extractJsonInt64(json, "id");
    src.name = extractJsonValue(json, "name");
    src.lang = extractJsonValue(json, "lang");
    src.iconUrl = extractJsonValue(json, "iconUrl");
    src.supportsLatest = extractJsonBool(json, "supportsLatest");
    src.isConfigurable = extractJsonBool(json, "isConfigurable");
    src.isNsfw = extractJsonBool(json, "isNsfw");

    return src;
}

Extension SuwayomiClient::parseExtension(const std::string& json) {
    Extension ext;

    ext.pkgName = extractJsonValue(json, "pkgName");
    ext.name = extractJsonValue(json, "name");
    ext.lang = extractJsonValue(json, "lang");
    ext.versionName = extractJsonValue(json, "versionName");
    ext.versionCode = extractJsonInt(json, "versionCode");
    ext.iconUrl = extractJsonValue(json, "iconUrl");
    ext.installed = extractJsonBool(json, "installed");
    ext.hasUpdate = extractJsonBool(json, "hasUpdate");
    ext.obsolete = extractJsonBool(json, "obsolete");
    ext.isNsfw = extractJsonBool(json, "isNsfw");

    return ext;
}

Category SuwayomiClient::parseCategory(const std::string& json) {
    Category cat;

    cat.id = extractJsonInt(json, "id");
    cat.name = extractJsonValue(json, "name");
    cat.order = extractJsonInt(json, "order");
    cat.isDefault = extractJsonBool(json, "default");

    return cat;
}

Page SuwayomiClient::parsePage(const std::string& json) {
    Page page;

    page.index = extractJsonInt(json, "index");
    page.url = extractJsonValue(json, "url");
    page.imageUrl = extractJsonValue(json, "imageUrl");

    return page;
}

Tracker SuwayomiClient::parseTracker(const std::string& json) {
    Tracker tracker;

    tracker.id = extractJsonInt(json, "id");
    tracker.name = extractJsonValue(json, "name");
    tracker.iconUrl = extractJsonValue(json, "icon");
    tracker.isLoggedIn = extractJsonBool(json, "isLoggedIn");

    return tracker;
}

TrackRecord SuwayomiClient::parseTrackRecord(const std::string& json) {
    TrackRecord record;

    record.id = extractJsonInt(json, "id");
    record.mangaId = extractJsonInt(json, "mangaId");
    record.trackerId = extractJsonInt(json, "trackerId");
    record.remoteId = extractJsonValue(json, "remoteId");
    record.title = extractJsonValue(json, "title");
    record.lastChapterRead = extractJsonInt(json, "lastChapterRead");
    record.totalChapters = extractJsonInt(json, "totalChapters");
    record.score = extractJsonInt(json, "score");
    record.status = extractJsonInt(json, "status");
    record.displayScore = extractJsonValue(json, "displayScore");

    return record;
}

// ============================================================================
// Connection & Server Info
// ============================================================================

bool SuwayomiClient::connectToServer(const std::string& url) {
    m_serverUrl = url;

    // Remove trailing slash
    while (!m_serverUrl.empty() && m_serverUrl.back() == '/') {
        m_serverUrl.pop_back();
    }

    brls::Logger::info("Connecting to Suwayomi server: {}", m_serverUrl);

    // Test connection by fetching server info
    ServerInfo info;
    if (fetchServerInfo(info)) {
        m_isConnected = true;
        m_serverInfo = info;
        brls::Logger::info("Connected to Suwayomi {} ({})", info.version, info.buildType);
        return true;
    }

    m_isConnected = false;
    return false;
}

bool SuwayomiClient::fetchServerInfo(ServerInfo& info) {
    vitasuwayomi::HttpClient http = createHttpClient();

    // Try /api/v1/settings/about endpoint (standard Suwayomi endpoint)
    std::string url = m_serverUrl + "/api/v1/settings/about";
    brls::Logger::info("Fetching server info from: {}", url);
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::warning("Primary endpoint failed ({}), trying fallback...", response.statusCode);

        // Fallback: Try to fetch source list as connection test
        url = m_serverUrl + "/api/v1/source/list";
        response = http.get(url);

        if (!response.success || response.statusCode != 200) {
            brls::Logger::error("Failed to connect to server: {} ({})",
                               response.error, response.statusCode);
            return false;
        }

        // If source list works, server is reachable but about endpoint might not exist
        info.name = "Suwayomi";
        info.version = "Unknown";
        info.buildType = "Unknown";
        brls::Logger::info("Connected via fallback (source list endpoint)");
        return true;
    }

    brls::Logger::debug("Server response: {}", response.body.substr(0, 200));

    // Parse About response (name, version, revision, buildType, buildTime, github, discord)
    info.name = extractJsonValue(response.body, "name");
    info.version = extractJsonValue(response.body, "version");
    info.revision = extractJsonValue(response.body, "revision");
    info.buildType = extractJsonValue(response.body, "buildType");
    info.buildTime = extractJsonInt64(response.body, "buildTime");
    info.github = extractJsonValue(response.body, "github");
    info.discord = extractJsonValue(response.body, "discord");

    // Fallback name if not set
    if (info.name.empty()) info.name = "Suwayomi";

    brls::Logger::info("Server: {} v{} ({})", info.name, info.version, info.buildType);
    return true;
}

bool SuwayomiClient::testConnection() {
    ServerInfo info;
    return fetchServerInfo(info);
}

void SuwayomiClient::setAuthCredentials(const std::string& username, const std::string& password) {
    m_authUsername = username;
    m_authPassword = password;
}

void SuwayomiClient::clearAuth() {
    m_authUsername.clear();
    m_authPassword.clear();
}

// ============================================================================
// Extension Management
// ============================================================================

bool SuwayomiClient::fetchExtensionList(std::vector<Extension>& extensions) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/extension/list");
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to fetch extensions: {}", response.error);
        return false;
    }

    extensions.clear();
    std::vector<std::string> items = splitJsonArray(response.body);
    for (const auto& item : items) {
        extensions.push_back(parseExtension(item));
    }

    brls::Logger::debug("Fetched {} extensions", extensions.size());
    return true;
}

bool SuwayomiClient::installExtension(const std::string& pkgName) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/extension/install/" + pkgName);
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::updateExtension(const std::string& pkgName) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/extension/update/" + pkgName);
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::uninstallExtension(const std::string& pkgName) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/extension/uninstall/" + pkgName);
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

std::string SuwayomiClient::getExtensionIconUrl(const std::string& apkName) {
    return buildApiUrl("/extension/icon/" + apkName);
}

// ============================================================================
// Source Management
// ============================================================================

bool SuwayomiClient::fetchSourceList(std::vector<Source>& sources) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/source/list");
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to fetch sources: {}", response.error);
        return false;
    }

    sources.clear();
    std::vector<std::string> items = splitJsonArray(response.body);
    for (const auto& item : items) {
        sources.push_back(parseSource(item));
    }

    brls::Logger::debug("Fetched {} sources", sources.size());
    return true;
}

bool SuwayomiClient::fetchSource(int64_t sourceId, Source& source) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/source/" + std::to_string(sourceId));
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        return false;
    }

    source = parseSource(response.body);
    return true;
}

bool SuwayomiClient::fetchSourceFilters(int64_t sourceId, std::vector<SourceFilter>& filters) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/source/" + std::to_string(sourceId) + "/filters");
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        return false;
    }

    // TODO: Parse filters (complex structure)
    filters.clear();
    return true;
}

bool SuwayomiClient::setSourceFilters(int64_t sourceId, const std::vector<SourceFilter>& filters) {
    // TODO: Implement filter setting
    return false;
}

// ============================================================================
// Source Browsing
// ============================================================================

bool SuwayomiClient::fetchPopularManga(int64_t sourceId, int page,
                                        std::vector<Manga>& manga, bool& hasNextPage) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/source/" + std::to_string(sourceId) + "/popular/" + std::to_string(page));
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to fetch popular manga: {}", response.error);
        return false;
    }

    manga.clear();
    hasNextPage = extractJsonBool(response.body, "hasNextPage");

    std::string mangaListJson = extractJsonArray(response.body, "mangaList");
    std::vector<std::string> items = splitJsonArray(mangaListJson);
    for (const auto& item : items) {
        manga.push_back(parseManga(item));
    }

    brls::Logger::debug("Fetched {} popular manga (hasNext: {})", manga.size(), hasNextPage);
    return true;
}

bool SuwayomiClient::fetchLatestManga(int64_t sourceId, int page,
                                       std::vector<Manga>& manga, bool& hasNextPage) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/source/" + std::to_string(sourceId) + "/latest/" + std::to_string(page));
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to fetch latest manga: {}", response.error);
        return false;
    }

    manga.clear();
    hasNextPage = extractJsonBool(response.body, "hasNextPage");

    std::string mangaListJson = extractJsonArray(response.body, "mangaList");
    std::vector<std::string> items = splitJsonArray(mangaListJson);
    for (const auto& item : items) {
        manga.push_back(parseManga(item));
    }

    return true;
}

bool SuwayomiClient::searchManga(int64_t sourceId, const std::string& query, int page,
                                  std::vector<Manga>& manga, bool& hasNextPage) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string encodedQuery = vitasuwayomi::HttpClient::urlEncode(query);
    std::string url = buildApiUrl("/source/" + std::to_string(sourceId) + "/search?searchTerm=" +
                                   encodedQuery + "&pageNum=" + std::to_string(page));
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to search manga: {}", response.error);
        return false;
    }

    manga.clear();
    hasNextPage = extractJsonBool(response.body, "hasNextPage");

    std::string mangaListJson = extractJsonArray(response.body, "mangaList");
    std::vector<std::string> items = splitJsonArray(mangaListJson);
    for (const auto& item : items) {
        manga.push_back(parseManga(item));
    }

    return true;
}

bool SuwayomiClient::quickSearchManga(int64_t sourceId, const std::string& query, std::vector<Manga>& manga) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/source/" + std::to_string(sourceId) + "/quick-search");
    std::string body = "{\"searchTerm\":\"" + query + "\"}";
    vitasuwayomi::HttpResponse response = http.post(url, body);

    if (!response.success || response.statusCode != 200) {
        return false;
    }

    manga.clear();
    std::vector<std::string> items = splitJsonArray(response.body);
    for (const auto& item : items) {
        manga.push_back(parseManga(item));
    }

    return true;
}

// ============================================================================
// Manga Operations
// ============================================================================

bool SuwayomiClient::fetchManga(int mangaId, Manga& manga) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId));
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to fetch manga {}: {}", mangaId, response.error);
        return false;
    }

    manga = parseManga(response.body);
    return true;
}

bool SuwayomiClient::fetchMangaFull(int mangaId, Manga& manga) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) + "/full");
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        return false;
    }

    manga = parseManga(response.body);
    return true;
}

bool SuwayomiClient::refreshManga(int mangaId, Manga& manga) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) + "?onlineFetch=true");
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        return false;
    }

    manga = parseManga(response.body);
    return true;
}

bool SuwayomiClient::addMangaToLibrary(int mangaId) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) + "/library");
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::removeMangaFromLibrary(int mangaId) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) + "/library");
    vitasuwayomi::HttpResponse response = http.del(url);

    return response.success && response.statusCode == 200;
}

std::string SuwayomiClient::getMangaThumbnailUrl(int mangaId) {
    return buildApiUrl("/manga/" + std::to_string(mangaId) + "/thumbnail");
}

// ============================================================================
// Chapter Operations
// ============================================================================

bool SuwayomiClient::fetchChapters(int mangaId, std::vector<Chapter>& chapters) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) + "/chapters");
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to fetch chapters: {}", response.error);
        return false;
    }

    chapters.clear();
    std::vector<std::string> items = splitJsonArray(response.body);
    for (const auto& item : items) {
        chapters.push_back(parseChapter(item));
    }

    brls::Logger::debug("Fetched {} chapters for manga {}", chapters.size(), mangaId);
    return true;
}

bool SuwayomiClient::fetchChapter(int mangaId, int chapterIndex, Chapter& chapter) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) +
                                   "/chapter/" + std::to_string(chapterIndex));
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        return false;
    }

    chapter = parseChapter(response.body);
    return true;
}

bool SuwayomiClient::updateChapter(int mangaId, int chapterIndex, bool read, bool bookmarked) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) +
                                   "/chapter/" + std::to_string(chapterIndex));

    std::string body = "{\"read\":" + std::string(read ? "true" : "false") +
                       ",\"bookmarked\":" + std::string(bookmarked ? "true" : "false") + "}";

    vitasuwayomi::HttpRequest req;
    req.url = url;
    req.method = "PATCH";
    req.body = body;
    req.headers["Content-Type"] = "application/json";

    vitasuwayomi::HttpResponse response = http.request(req);
    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::markChapterRead(int mangaId, int chapterIndex) {
    return updateChapter(mangaId, chapterIndex, true, false);
}

bool SuwayomiClient::markChapterUnread(int mangaId, int chapterIndex) {
    return updateChapter(mangaId, chapterIndex, false, false);
}

bool SuwayomiClient::markChaptersRead(int mangaId, const std::vector<int>& chapterIndexes) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) + "/chapter/batch");

    std::string indexList;
    for (size_t i = 0; i < chapterIndexes.size(); i++) {
        if (i > 0) indexList += ",";
        indexList += std::to_string(chapterIndexes[i]);
    }

    std::string body = "{\"chapterIndexes\":[" + indexList + "],\"change\":{\"read\":true}}";

    vitasuwayomi::HttpResponse response = http.post(url, body);
    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::markChaptersUnread(int mangaId, const std::vector<int>& chapterIndexes) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) + "/chapter/batch");

    std::string indexList;
    for (size_t i = 0; i < chapterIndexes.size(); i++) {
        if (i > 0) indexList += ",";
        indexList += std::to_string(chapterIndexes[i]);
    }

    std::string body = "{\"chapterIndexes\":[" + indexList + "],\"change\":{\"read\":false}}";

    vitasuwayomi::HttpResponse response = http.post(url, body);
    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::updateChapterProgress(int mangaId, int chapterIndex, int lastPageRead) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) +
                                   "/chapter/" + std::to_string(chapterIndex));

    std::string body = "{\"lastPageRead\":" + std::to_string(lastPageRead) + "}";

    vitasuwayomi::HttpRequest req;
    req.url = url;
    req.method = "PATCH";
    req.body = body;
    req.headers["Content-Type"] = "application/json";

    vitasuwayomi::HttpResponse response = http.request(req);
    return response.success && response.statusCode == 200;
}

// ============================================================================
// Page Operations
// ============================================================================

bool SuwayomiClient::fetchChapterPages(int mangaId, int chapterIndex, std::vector<Page>& pages) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) +
                                   "/chapter/" + std::to_string(chapterIndex));
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to fetch chapter pages: {}", response.error);
        return false;
    }

    pages.clear();
    int pageCount = extractJsonInt(response.body, "pageCount");

    for (int i = 0; i < pageCount; i++) {
        Page page;
        page.index = i;
        page.imageUrl = getPageImageUrl(mangaId, chapterIndex, i);
        pages.push_back(page);
    }

    brls::Logger::debug("Chapter has {} pages", pageCount);
    return true;
}

std::string SuwayomiClient::getPageImageUrl(int mangaId, int chapterIndex, int pageIndex) {
    return buildApiUrl("/manga/" + std::to_string(mangaId) +
                       "/chapter/" + std::to_string(chapterIndex) +
                       "/page/" + std::to_string(pageIndex));
}

// ============================================================================
// Category Management
// ============================================================================

bool SuwayomiClient::fetchCategories(std::vector<Category>& categories) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/category");
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to fetch categories: {}", response.error);
        return false;
    }

    categories.clear();
    std::vector<std::string> items = splitJsonArray(response.body);
    for (const auto& item : items) {
        categories.push_back(parseCategory(item));
    }

    return true;
}

bool SuwayomiClient::createCategory(const std::string& name) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/category");
    std::string body = "{\"name\":\"" + name + "\"}";

    vitasuwayomi::HttpResponse response = http.post(url, body);
    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::deleteCategory(int categoryId) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/category/" + std::to_string(categoryId));
    vitasuwayomi::HttpResponse response = http.del(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::updateCategory(int categoryId, const std::string& name, bool isDefault) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/category/" + std::to_string(categoryId));
    std::string body = "{\"name\":\"" + name + "\",\"default\":" +
                       std::string(isDefault ? "true" : "false") + "}";

    vitasuwayomi::HttpRequest req;
    req.url = url;
    req.method = "PATCH";
    req.body = body;
    req.headers["Content-Type"] = "application/json";

    vitasuwayomi::HttpResponse response = http.request(req);
    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::reorderCategories(const std::vector<int>& categoryIds) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/category/reorder");

    std::string idList;
    for (size_t i = 0; i < categoryIds.size(); i++) {
        if (i > 0) idList += ",";
        idList += std::to_string(categoryIds[i]);
    }

    std::string body = "{\"categoryIds\":[" + idList + "]}";

    vitasuwayomi::HttpRequest req;
    req.url = url;
    req.method = "PATCH";
    req.body = body;
    req.headers["Content-Type"] = "application/json";

    vitasuwayomi::HttpResponse response = http.request(req);
    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::addMangaToCategory(int mangaId, int categoryId) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) +
                                   "/category/" + std::to_string(categoryId));
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::removeMangaFromCategory(int mangaId, int categoryId) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) +
                                   "/category/" + std::to_string(categoryId));
    vitasuwayomi::HttpResponse response = http.del(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::fetchCategoryManga(int categoryId, std::vector<Manga>& manga) {
    vitasuwayomi::HttpClient http = createHttpClient();

    // Correct endpoint: /category/{categoryId}/manga returns Array<MangaDataClass>
    std::string url = buildApiUrl("/category/" + std::to_string(categoryId) + "/manga");
    brls::Logger::debug("Fetching category manga from: {}", url);
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to fetch category {} manga: {} ({})",
                          categoryId, response.error, response.statusCode);
        return false;
    }

    manga.clear();
    std::vector<std::string> items = splitJsonArray(response.body);
    for (const auto& item : items) {
        manga.push_back(parseManga(item));
    }

    brls::Logger::debug("Fetched {} manga from category {}", manga.size(), categoryId);
    return true;
}

// ============================================================================
// Library Operations
// ============================================================================

bool SuwayomiClient::fetchLibraryManga(std::vector<Manga>& manga) {
    // Default category (0) contains all library manga
    return fetchCategoryManga(0, manga);
}

bool SuwayomiClient::fetchLibraryMangaByCategory(int categoryId, std::vector<Manga>& manga) {
    return fetchCategoryManga(categoryId, manga);
}

bool SuwayomiClient::triggerLibraryUpdate() {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/update/fetch");
    vitasuwayomi::HttpResponse response = http.post(url, "{}");

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::triggerLibraryUpdate(int categoryId) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/update/fetch");
    std::string body = "{\"categoryId\":" + std::to_string(categoryId) + "}";

    vitasuwayomi::HttpResponse response = http.post(url, body);
    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::fetchRecentUpdates(int page, std::vector<RecentUpdate>& updates) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/update/recentChapters/" + std::to_string(page));
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to fetch recent updates: {}", response.error);
        return false;
    }

    updates.clear();
    std::string pageListJson = extractJsonArray(response.body, "page");
    std::vector<std::string> items = splitJsonArray(pageListJson);

    for (const auto& item : items) {
        RecentUpdate update;

        // Parse manga and chapter from update item
        std::string mangaJson = extractJsonObject(item, "manga");
        std::string chapterJson = extractJsonObject(item, "chapter");

        if (!mangaJson.empty()) {
            update.manga = parseManga(mangaJson);
        }
        if (!chapterJson.empty()) {
            update.chapter = parseChapter(chapterJson);
        }

        updates.push_back(update);
    }

    brls::Logger::debug("Fetched {} recent updates", updates.size());
    return true;
}

// ============================================================================
// Download Management
// ============================================================================

bool SuwayomiClient::queueChapterDownload(int mangaId, int chapterIndex) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/download/" + std::to_string(mangaId) +
                                   "/chapter/" + std::to_string(chapterIndex));
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::deleteChapterDownload(int mangaId, int chapterIndex) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/download/" + std::to_string(mangaId) +
                                   "/chapter/" + std::to_string(chapterIndex));
    vitasuwayomi::HttpResponse response = http.del(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::queueChapterDownloads(int mangaId, const std::vector<int>& chapterIndexes) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/download/batch");

    std::string chapterList;
    for (size_t i = 0; i < chapterIndexes.size(); i++) {
        if (i > 0) chapterList += ",";
        chapterList += "{\"mangaId\":" + std::to_string(mangaId) +
                       ",\"chapterIndex\":" + std::to_string(chapterIndexes[i]) + "}";
    }

    std::string body = "{\"chapterIds\":[" + chapterList + "]}";
    vitasuwayomi::HttpResponse response = http.post(url, body);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::deleteChapterDownloads(int mangaId, const std::vector<int>& chapterIndexes) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/download/batch");

    std::string chapterList;
    for (size_t i = 0; i < chapterIndexes.size(); i++) {
        if (i > 0) chapterList += ",";
        chapterList += "{\"mangaId\":" + std::to_string(mangaId) +
                       ",\"chapterIndex\":" + std::to_string(chapterIndexes[i]) + "}";
    }

    std::string body = "{\"chapterIds\":[" + chapterList + "]}";
    vitasuwayomi::HttpResponse response = http.del(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::fetchDownloadQueue(std::vector<DownloadQueueItem>& queue) {
    // Note: Download queue uses WebSocket in Suwayomi
    // For REST, we can check download status per chapter
    queue.clear();
    return true;
}

bool SuwayomiClient::startDownloads() {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/downloads/start");
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::stopDownloads() {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/downloads/stop");
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::clearDownloadQueue() {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/downloads/clear");
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::reorderDownload(int mangaId, int chapterIndex, int newPosition) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/download/" + std::to_string(mangaId) +
                                   "/chapter/" + std::to_string(chapterIndex) +
                                   "/reorder/" + std::to_string(newPosition));

    vitasuwayomi::HttpRequest req;
    req.url = url;
    req.method = "PATCH";
    req.headers["Content-Type"] = "application/json";

    vitasuwayomi::HttpResponse response = http.request(req);
    return response.success && response.statusCode == 200;
}

// ============================================================================
// Backup/Restore
// ============================================================================

bool SuwayomiClient::exportBackup(const std::string& savePath) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/backup/export/file");
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        return false;
    }

    // Save response body to file
    // TODO: Implement file save
    return true;
}

bool SuwayomiClient::importBackup(const std::string& filePath) {
    // TODO: Implement file upload
    return false;
}

bool SuwayomiClient::validateBackup(const std::string& filePath) {
    // TODO: Implement backup validation
    return false;
}

// ============================================================================
// Tracking
// ============================================================================

bool SuwayomiClient::fetchTrackers(std::vector<Tracker>& trackers) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/track/list");
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        return false;
    }

    trackers.clear();
    std::vector<std::string> items = splitJsonArray(response.body);
    for (const auto& item : items) {
        trackers.push_back(parseTracker(item));
    }

    return true;
}

bool SuwayomiClient::loginTracker(int trackerId, const std::string& username, const std::string& password) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/track/login");
    std::string body = "{\"trackerId\":" + std::to_string(trackerId) +
                       ",\"username\":\"" + username + "\"" +
                       ",\"password\":\"" + password + "\"}";

    vitasuwayomi::HttpResponse response = http.post(url, body);
    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::logoutTracker(int trackerId) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/track/logout");
    std::string body = "{\"trackerId\":" + std::to_string(trackerId) + "}";

    vitasuwayomi::HttpResponse response = http.post(url, body);
    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::searchTracker(int trackerId, const std::string& query, std::vector<TrackRecord>& results) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/track/search");
    std::string body = "{\"trackerId\":" + std::to_string(trackerId) +
                       ",\"query\":\"" + query + "\"}";

    vitasuwayomi::HttpResponse response = http.post(url, body);

    if (!response.success || response.statusCode != 200) {
        return false;
    }

    results.clear();
    std::vector<std::string> items = splitJsonArray(response.body);
    for (const auto& item : items) {
        results.push_back(parseTrackRecord(item));
    }

    return true;
}

bool SuwayomiClient::bindTracker(int mangaId, int trackerId, int remoteId) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/track/bind");
    std::string body = "{\"mangaId\":" + std::to_string(mangaId) +
                       ",\"trackerId\":" + std::to_string(trackerId) +
                       ",\"remoteId\":" + std::to_string(remoteId) + "}";

    vitasuwayomi::HttpResponse response = http.post(url, body);
    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::updateTracking(int mangaId, int trackerId, const TrackRecord& record) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/track/update");
    std::string body = "{\"mangaId\":" + std::to_string(mangaId) +
                       ",\"trackerId\":" + std::to_string(trackerId) +
                       ",\"lastChapterRead\":" + std::to_string(record.lastChapterRead) +
                       ",\"score\":" + std::to_string(record.score) +
                       ",\"status\":" + std::to_string(record.status) + "}";

    vitasuwayomi::HttpResponse response = http.post(url, body);
    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::fetchMangaTracking(int mangaId, std::vector<TrackRecord>& records) {
    // Note: Tracking info is typically included in manga details
    records.clear();
    return true;
}

// ============================================================================
// Update Summary
// ============================================================================

bool SuwayomiClient::fetchUpdateSummary(int& pendingUpdates, int& runningJobs, bool& isUpdating) {
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/update/summary");
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        return false;
    }

    pendingUpdates = extractJsonInt(response.body, "pendingJobs");
    runningJobs = extractJsonInt(response.body, "runningJobs");
    isUpdating = extractJsonBool(response.body, "isRunning");

    return true;
}

} // namespace vitasuwayomi
