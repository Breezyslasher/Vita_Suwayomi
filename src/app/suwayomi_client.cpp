/**
 * VitaSuwayomi - Suwayomi Server API Client implementation
 */

#include "app/suwayomi_client.hpp"
#include "app/application.hpp"
#include "utils/http_client.hpp"
#include "utils/image_loader.hpp"

#include <borealis.hpp>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <set>
#include <fstream>
#include <sstream>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

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

std::string SuwayomiClient::buildGraphQLUrl() {
    std::string url = m_serverUrl;

    // Remove trailing slash
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    // GraphQL endpoint
    url += "/api/graphql";

    return url;
}

std::string SuwayomiClient::executeGraphQL(const std::string& query, const std::string& variables) {
    return executeGraphQLInternal(query, variables, true);
}

std::string SuwayomiClient::executeGraphQLInternal(const std::string& query, const std::string& variables, bool allowRetry) {
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildGraphQLUrl();

    // Build GraphQL request body
    std::string body = "{\"query\":\"";

    // Escape query string (newlines, quotes, etc.)
    for (char c : query) {
        switch (c) {
            case '"': body += "\\\""; break;
            case '\\': body += "\\\\"; break;
            case '\n': body += "\\n"; break;
            case '\r': body += "\\r"; break;
            case '\t': body += "\\t"; break;
            default: body += c; break;
        }
    }
    body += "\"";

    // Add variables if provided
    if (!variables.empty()) {
        body += ",\"variables\":" + variables;
    }

    body += "}";

    brls::Logger::debug("GraphQL request to {}: {}", url, body.substr(0, 200));

    vitasuwayomi::HttpResponse response = http.post(url, body);

    // Handle 401 Unauthorized - try to refresh token and retry
    if (response.statusCode == 401 && allowRetry) {
        brls::Logger::info("Got 401 Unauthorized, attempting token refresh...");
        if (refreshToken()) {
            brls::Logger::info("Token refreshed successfully, retrying request");
            return executeGraphQLInternal(query, variables, false);  // Retry once
        } else {
            brls::Logger::warning("Token refresh failed");
            return "";
        }
    }

    if (!response.success || response.statusCode != 200) {
        brls::Logger::warning("GraphQL request failed: {} ({})", response.error, response.statusCode);
        return "";
    }

    // Check for GraphQL errors
    if (response.body.find("\"errors\"") != std::string::npos) {
        std::string errors = extractJsonArray(response.body, "errors");
        brls::Logger::warning("GraphQL errors: {}", errors.substr(0, 200));

        // Check for Unauthorized errors in GraphQL response - attempt token refresh
        if (allowRetry && (errors.find("Unauthorized") != std::string::npos ||
                          errors.find("UnauthorizedException") != std::string::npos)) {
            brls::Logger::info("GraphQL returned Unauthorized, attempting token refresh...");
            if (refreshToken()) {
                brls::Logger::info("Token refreshed successfully, retrying GraphQL request");
                return executeGraphQLInternal(query, variables, false);  // Retry once
            } else {
                brls::Logger::warning("Token refresh failed for GraphQL Unauthorized error");
            }
        }
        return "";
    }

    brls::Logger::debug("GraphQL response: {}", response.body.substr(0, 300));
    return response.body;
}

// Helper for Base64 encoding
static std::string base64Encode(const std::string& input) {
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

// Helper to create an HTTP client with authentication headers
vitasuwayomi::HttpClient SuwayomiClient::createHttpClient() {
    vitasuwayomi::HttpClient http;
    http.setDefaultHeader("Accept", "application/json");

    // Apply connection timeout from settings
    int timeout = Application::getInstance().getSettings().connectionTimeout;
    if (timeout > 0) {
        http.setTimeout(timeout);
    }

    // Apply authentication based on auth mode
    switch (m_authMode) {
        case AuthMode::NONE:
            // No authentication
            break;

        case AuthMode::BASIC_AUTH:
            // HTTP Basic Access Authentication
            if (!m_authUsername.empty() && !m_authPassword.empty()) {
                std::string credentials = m_authUsername + ":" + m_authPassword;
                http.setDefaultHeader("Authorization", "Basic " + base64Encode(credentials));
            }
            break;

        case AuthMode::SIMPLE_LOGIN:
            // Cookie-based session - use JWT token if available, otherwise try basic auth
            if (!m_accessToken.empty()) {
                http.setDefaultHeader("Authorization", "Bearer " + m_accessToken);
            } else if (!m_sessionCookie.empty()) {
                http.setDefaultHeader("Cookie", m_sessionCookie);
            } else if (!m_authUsername.empty() && !m_authPassword.empty()) {
                // Fallback to basic auth for initial requests
                std::string credentials = m_authUsername + ":" + m_authPassword;
                http.setDefaultHeader("Authorization", "Basic " + base64Encode(credentials));
            }
            break;

        case AuthMode::UI_LOGIN:
            // JWT-based authentication
            if (!m_accessToken.empty()) {
                http.setDefaultHeader("Authorization", "Bearer " + m_accessToken);
            } else if (!m_authUsername.empty() && !m_authPassword.empty()) {
                // Fallback to basic auth if no token yet
                std::string credentials = m_authUsername + ":" + m_authPassword;
                http.setDefaultHeader("Authorization", "Basic " + base64Encode(credentials));
            }
            break;
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
    record.remoteId = extractJsonInt64(json, "remoteId");
    record.remoteUrl = extractJsonValue(json, "remoteUrl");
    record.title = extractJsonValue(json, "title");
    record.lastChapterRead = extractJsonFloat(json, "lastChapterRead");
    record.totalChapters = extractJsonInt(json, "totalChapters");
    record.score = extractJsonFloat(json, "score");
    record.status = extractJsonInt(json, "status");
    record.displayScore = extractJsonValue(json, "displayScore");
    record.startDate = extractJsonInt64(json, "startDate");
    record.finishDate = extractJsonInt64(json, "finishDate");

    return record;
}

// ============================================================================
// GraphQL-specific parsers (field names differ from REST API)
// ============================================================================

Manga SuwayomiClient::parseMangaFromGraphQL(const std::string& json) {
    Manga manga;

    manga.id = extractJsonInt(json, "id");
    manga.title = extractJsonValue(json, "title");
    manga.thumbnailUrl = extractJsonValue(json, "thumbnailUrl");
    manga.artist = extractJsonValue(json, "artist");
    manga.author = extractJsonValue(json, "author");
    manga.description = extractJsonValue(json, "description");
    manga.inLibrary = extractJsonBool(json, "inLibrary");
    manga.inLibraryAt = extractJsonInt64(json, "inLibraryAt");
    manga.initialized = extractJsonBool(json, "initialized");
    manga.url = extractJsonValue(json, "url");

    // Parse status (GraphQL uses enum string)
    std::string statusStr = extractJsonValue(json, "status");
    if (statusStr == "ONGOING") manga.status = MangaStatus::ONGOING;
    else if (statusStr == "COMPLETED") manga.status = MangaStatus::COMPLETED;
    else if (statusStr == "LICENSED") manga.status = MangaStatus::LICENSED;
    else if (statusStr == "PUBLISHING_FINISHED") manga.status = MangaStatus::PUBLISHING_FINISHED;
    else if (statusStr == "CANCELLED") manga.status = MangaStatus::CANCELLED;
    else if (statusStr == "ON_HIATUS") manga.status = MangaStatus::ON_HIATUS;
    else manga.status = MangaStatus::UNKNOWN;

    // Parse genre array
    manga.genre = extractJsonStringArray(json, "genre");

    // Parse source name from nested source object
    std::string sourceObj = extractJsonObject(json, "source");
    if (!sourceObj.empty()) {
        manga.sourceName = extractJsonValue(sourceObj, "displayName");
    }

    // GraphQL might have unreadCount directly
    manga.unreadCount = extractJsonInt(json, "unreadCount");

    // Parse downloadCount and chapterCount
    manga.downloadedCount = extractJsonInt(json, "downloadCount");
    // chapterCount comes from chapters.totalCount in GraphQL
    std::string chaptersObj = extractJsonObject(json, "chapters");
    if (!chaptersObj.empty()) {
        manga.chapterCount = extractJsonInt(chaptersObj, "totalCount");
    } else {
        // Fallback for REST API which may have chapterCount directly
        manga.chapterCount = extractJsonInt(json, "chapterCount");
    }

    // Parse lastReadChapter for lastReadAt timestamp
    std::string lastReadChapter = extractJsonObject(json, "lastReadChapter");
    if (!lastReadChapter.empty()) {
        manga.lastReadAt = extractJsonInt64(lastReadChapter, "lastReadAt");
    }

    // Parse latestUploadedChapter for uploadDate
    std::string latestChapter = extractJsonObject(json, "latestUploadedChapter");
    if (!latestChapter.empty()) {
        manga.latestChapterUploadDate = extractJsonInt64(latestChapter, "uploadDate");
    }

    return manga;
}

Chapter SuwayomiClient::parseChapterFromGraphQL(const std::string& json) {
    Chapter ch;

    ch.id = extractJsonInt(json, "id");
    ch.name = extractJsonValue(json, "name");
    ch.scanlator = extractJsonValue(json, "scanlator");
    ch.chapterNumber = extractJsonFloat(json, "chapterNumber");
    ch.uploadDate = extractJsonInt64(json, "uploadDate");
    ch.pageCount = extractJsonInt(json, "pageCount");
    ch.lastPageRead = extractJsonInt(json, "lastPageRead");
    ch.lastReadAt = extractJsonInt64(json, "lastReadAt");
    ch.index = extractJsonInt(json, "sourceOrder");

    // GraphQL uses isRead, isDownloaded, isBookmarked
    ch.read = extractJsonBool(json, "isRead");
    ch.downloaded = extractJsonBool(json, "isDownloaded");
    ch.bookmarked = extractJsonBool(json, "isBookmarked");

    return ch;
}

Source SuwayomiClient::parseSourceFromGraphQL(const std::string& json) {
    Source src;

    // GraphQL returns id as string (LongString)
    std::string idStr = extractJsonValue(json, "id");
    if (!idStr.empty()) {
        try {
            src.id = std::stoll(idStr);
        } catch (...) {
            src.id = 0;
        }
    }

    // GraphQL has displayName as well as name
    src.name = extractJsonValue(json, "displayName");
    if (src.name.empty()) {
        src.name = extractJsonValue(json, "name");
    }
    src.lang = extractJsonValue(json, "lang");
    src.iconUrl = extractJsonValue(json, "iconUrl");
    src.supportsLatest = extractJsonBool(json, "supportsLatest");
    src.isConfigurable = extractJsonBool(json, "isConfigurable");
    src.isNsfw = extractJsonBool(json, "isNsfw");

    return src;
}

Category SuwayomiClient::parseCategoryFromGraphQL(const std::string& json) {
    Category cat;

    cat.id = extractJsonInt(json, "id");
    cat.name = extractJsonValue(json, "name");
    cat.order = extractJsonInt(json, "order");

    // Parse manga count from nested mangas object
    std::string mangasJson = extractJsonObject(json, "mangas");
    if (!mangasJson.empty()) {
        cat.mangaCount = extractJsonInt(mangasJson, "totalCount");
    }

    return cat;
}

// ============================================================================
// GraphQL implementations (primary API)
// ============================================================================

bool SuwayomiClient::fetchServerInfoGraphQL(ServerInfo& info) {
    const char* query = R"(
        query {
            aboutServer {
                name
                version
                revision
            }
        }
    )";

    std::string response = executeGraphQL(query);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string aboutServer = extractJsonObject(data, "aboutServer");
    if (aboutServer.empty()) return false;

    info.name = extractJsonValue(aboutServer, "name");
    info.version = extractJsonValue(aboutServer, "version");
    info.revision = extractJsonValue(aboutServer, "revision");
    if (info.name.empty()) info.name = "Suwayomi";

    return true;
}

bool SuwayomiClient::fetchSourceListGraphQL(std::vector<Source>& sources) {
    const char* query = R"(
        query {
            sources {
                nodes {
                    id
                    name
                    displayName
                    lang
                    iconUrl
                    isNsfw
                    supportsLatest
                    isConfigurable
                }
            }
        }
    )";

    std::string response = executeGraphQL(query);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string sourcesObj = extractJsonObject(data, "sources");
    if (sourcesObj.empty()) return false;

    std::string nodesJson = extractJsonArray(sourcesObj, "nodes");
    if (nodesJson.empty()) return false;

    sources.clear();
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        sources.push_back(parseSourceFromGraphQL(item));
    }

    brls::Logger::debug("GraphQL: Fetched {} sources", sources.size());
    return true;
}

bool SuwayomiClient::fetchPopularMangaGraphQL(int64_t sourceId, int page,
                                               std::vector<Manga>& manga, bool& hasNextPage) {
    const char* query = R"(
        mutation GetPopular($sourceId: LongString!, $page: Int!) {
            fetchSourceManga(input: { source: $sourceId, type: POPULAR, page: $page }) {
                mangas {
                    id
                    title
                    thumbnailUrl
                    author
                    artist
                    description
                    inLibrary
                }
                hasNextPage
            }
        }
    )";

    std::string variables = "{\"sourceId\":\"" + std::to_string(sourceId) + "\",\"page\":" + std::to_string(page) + "}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string fetchResult = extractJsonObject(data, "fetchSourceManga");
    if (fetchResult.empty()) return false;

    hasNextPage = extractJsonBool(fetchResult, "hasNextPage");

    std::string mangasJson = extractJsonArray(fetchResult, "mangas");
    manga.clear();
    std::vector<std::string> items = splitJsonArray(mangasJson);
    for (const auto& item : items) {
        manga.push_back(parseMangaFromGraphQL(item));
    }

    brls::Logger::debug("GraphQL: Fetched {} popular manga (hasNext: {})", manga.size(), hasNextPage);
    return true;
}

bool SuwayomiClient::fetchLatestMangaGraphQL(int64_t sourceId, int page,
                                              std::vector<Manga>& manga, bool& hasNextPage) {
    const char* query = R"(
        mutation GetLatest($sourceId: LongString!, $page: Int!) {
            fetchSourceManga(input: { source: $sourceId, type: LATEST, page: $page }) {
                mangas {
                    id
                    title
                    thumbnailUrl
                    author
                    artist
                    description
                    inLibrary
                }
                hasNextPage
            }
        }
    )";

    std::string variables = "{\"sourceId\":\"" + std::to_string(sourceId) + "\",\"page\":" + std::to_string(page) + "}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string fetchResult = extractJsonObject(data, "fetchSourceManga");
    if (fetchResult.empty()) return false;

    hasNextPage = extractJsonBool(fetchResult, "hasNextPage");

    std::string mangasJson = extractJsonArray(fetchResult, "mangas");
    manga.clear();
    std::vector<std::string> items = splitJsonArray(mangasJson);
    for (const auto& item : items) {
        manga.push_back(parseMangaFromGraphQL(item));
    }

    brls::Logger::debug("GraphQL: Fetched {} latest manga (hasNext: {})", manga.size(), hasNextPage);
    return true;
}

bool SuwayomiClient::searchMangaGraphQL(int64_t sourceId, const std::string& searchQuery, int page,
                                         std::vector<Manga>& manga, bool& hasNextPage) {
    const char* query = R"(
        mutation SearchSource($sourceId: LongString!, $searchTerm: String!, $page: Int!) {
            fetchSourceManga(input: { source: $sourceId, type: SEARCH, query: $searchTerm, page: $page }) {
                mangas {
                    id
                    title
                    thumbnailUrl
                    author
                    artist
                    description
                    inLibrary
                }
                hasNextPage
            }
        }
    )";

    // Escape the search query for JSON
    std::string escapedQuery;
    for (char c : searchQuery) {
        switch (c) {
            case '"': escapedQuery += "\\\""; break;
            case '\\': escapedQuery += "\\\\"; break;
            default: escapedQuery += c; break;
        }
    }

    std::string variables = "{\"sourceId\":\"" + std::to_string(sourceId) +
                            "\",\"searchTerm\":\"" + escapedQuery +
                            "\",\"page\":" + std::to_string(page) + "}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string fetchResult = extractJsonObject(data, "fetchSourceManga");
    if (fetchResult.empty()) return false;

    hasNextPage = extractJsonBool(fetchResult, "hasNextPage");

    std::string mangasJson = extractJsonArray(fetchResult, "mangas");
    manga.clear();
    std::vector<std::string> items = splitJsonArray(mangasJson);
    for (const auto& item : items) {
        manga.push_back(parseMangaFromGraphQL(item));
    }

    brls::Logger::debug("GraphQL: Searched {} manga (hasNext: {})", manga.size(), hasNextPage);
    return true;
}

bool SuwayomiClient::fetchLibraryMangaGraphQL(std::vector<Manga>& manga) {
    const char* query = R"(
        query GetLibraryManga {
            mangas(
                condition: { inLibrary: true }
                first: 500
                orderBy: TITLE
            ) {
                nodes {
                    id
                    title
                    thumbnailUrl
                    author
                    artist
                    description
                    genre
                    status
                    inLibrary
                    unreadCount
                    source {
                        displayName
                    }
                }
                totalCount
            }
        }
    )";

    std::string response = executeGraphQL(query);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string mangasObj = extractJsonObject(data, "mangas");
    if (mangasObj.empty()) return false;

    std::string nodesJson = extractJsonArray(mangasObj, "nodes");
    manga.clear();
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        manga.push_back(parseMangaFromGraphQL(item));
    }

    brls::Logger::debug("GraphQL: Fetched {} library manga", manga.size());
    return true;
}

bool SuwayomiClient::fetchCategoriesGraphQL(std::vector<Category>& categories) {
    const char* query = R"(
        query {
            categories {
                nodes {
                    id
                    name
                    order
                    mangas {
                        totalCount
                    }
                }
            }
        }
    )";

    std::string response = executeGraphQL(query);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string categoriesObj = extractJsonObject(data, "categories");
    if (categoriesObj.empty()) return false;

    std::string nodesJson = extractJsonArray(categoriesObj, "nodes");
    categories.clear();
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        categories.push_back(parseCategoryFromGraphQL(item));
    }

    brls::Logger::debug("GraphQL: Fetched {} categories", categories.size());
    return true;
}

bool SuwayomiClient::fetchChaptersGraphQL(int mangaId, std::vector<Chapter>& chapters) {
    const char* query = R"(
        query GetChapters($mangaId: Int!) {
            chapters(
                condition: { mangaId: $mangaId }
                first: 1000
                orderBy: SOURCE_ORDER
                orderByType: DESC
            ) {
                nodes {
                    id
                    name
                    chapterNumber
                    scanlator
                    uploadDate
                    isRead
                    isDownloaded
                    isBookmarked
                    pageCount
                    lastPageRead
                    lastReadAt
                    sourceOrder
                }
                totalCount
            }
        }
    )";

    std::string variables = "{\"mangaId\":" + std::to_string(mangaId) + "}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string chaptersObj = extractJsonObject(data, "chapters");
    if (chaptersObj.empty()) return false;

    std::string nodesJson = extractJsonArray(chaptersObj, "nodes");
    chapters.clear();
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        Chapter ch = parseChapterFromGraphQL(item);
        ch.mangaId = mangaId;
        chapters.push_back(ch);
    }

    brls::Logger::debug("GraphQL: Fetched {} chapters for manga {}", chapters.size(), mangaId);
    return true;
}

bool SuwayomiClient::fetchMangaGraphQL(int mangaId, Manga& manga) {
    const char* query = R"(
        query GetManga($id: Int!) {
            manga(id: $id) {
                id
                title
                thumbnailUrl
                author
                artist
                description
                genre
                status
                url
                inLibrary
                initialized
                source {
                    displayName
                }
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(mangaId) + "}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string mangaJson = extractJsonObject(data, "manga");
    if (mangaJson.empty()) return false;

    manga = parseMangaFromGraphQL(mangaJson);
    return true;
}

bool SuwayomiClient::addMangaToLibraryGraphQL(int mangaId) {
    const char* query = R"(
        mutation AddToLibrary($id: Int!) {
            updateManga(input: { id: $id, patch: { inLibrary: true } }) {
                manga {
                    id
                    inLibrary
                }
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(mangaId) + "}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::removeMangaFromLibraryGraphQL(int mangaId) {
    const char* query = R"(
        mutation RemoveFromLibrary($id: Int!) {
            updateManga(input: { id: $id, patch: { inLibrary: false } }) {
                manga {
                    id
                    inLibrary
                }
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(mangaId) + "}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::markChapterReadGraphQL(int chapterId, bool read) {
    const char* query = R"(
        mutation UpdateChapter($id: Int!, $isRead: Boolean!) {
            updateChapter(input: { id: $id, patch: { isRead: $isRead } }) {
                chapter {
                    id
                    isRead
                }
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(chapterId) +
                            ",\"isRead\":" + (read ? "true" : "false") + "}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::updateChapterProgressGraphQL(int chapterId, int lastPageRead) {
    const char* query = R"(
        mutation UpdateProgress($id: Int!, $lastPageRead: Int!) {
            updateChapter(input: { id: $id, patch: { lastPageRead: $lastPageRead } }) {
                chapter {
                    id
                    lastPageRead
                }
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(chapterId) +
                            ",\"lastPageRead\":" + std::to_string(lastPageRead) + "}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::fetchChapterPagesGraphQL(int chapterId, std::vector<Page>& pages) {
    brls::Logger::info("GraphQL: Fetching pages for chapter id={}", chapterId);

    const char* query = R"(
        mutation FetchChapterPages($id: Int!) {
            fetchChapterPages(input: { chapterId: $id }) {
                pages
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(chapterId) + "}";
    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("GraphQL: Empty response for fetchChapterPages");
        return false;
    }

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) {
        brls::Logger::error("GraphQL: No data in response for fetchChapterPages");
        return false;
    }

    std::string fetchResult = extractJsonObject(data, "fetchChapterPages");
    if (fetchResult.empty()) {
        brls::Logger::error("GraphQL: No fetchChapterPages result in response");
        return false;
    }

    // The pages field contains an array of page URLs
    std::string pagesJson = extractJsonArray(fetchResult, "pages");
    pages.clear();

    // Parse pages array (array of strings)
    std::vector<std::string> pageUrls = extractJsonStringArray(fetchResult, "pages");
    for (size_t i = 0; i < pageUrls.size(); i++) {
        Page page;
        page.index = static_cast<int>(i);
        page.imageUrl = pageUrls[i];
        pages.push_back(page);
    }

    brls::Logger::info("GraphQL: Fetched {} pages for chapter {}", pages.size(), chapterId);

    if (pages.empty()) {
        brls::Logger::warning("GraphQL: No pages returned for chapter {}", chapterId);
        return false;
    }

    return true;
}

bool SuwayomiClient::fetchReadingHistoryGraphQL(int offset, int limit, std::vector<ReadingHistoryItem>& history) {
    // Use newer order syntax for better compatibility with latest Suwayomi-Server
    const char* query = R"(
        query GetHistory($offset: Int!, $limit: Int!) {
            chapters(
                offset: $offset
                first: $limit
                filter: { lastReadAt: { greaterThan: "0" } }
                order: [{ by: LAST_READ_AT, byType: DESC }]
            ) {
                nodes {
                    id
                    name
                    chapterNumber
                    lastPageRead
                    lastReadAt
                    pageCount
                    isRead
                    manga {
                        id
                        title
                        thumbnailUrl
                        source {
                            displayName
                        }
                    }
                }
                totalCount
            }
        }
    )";

    std::string variables = "{\"offset\":" + std::to_string(offset) +
                            ",\"limit\":" + std::to_string(limit) + "}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string chaptersObj = extractJsonObject(data, "chapters");
    if (chaptersObj.empty()) return false;

    std::string nodesJson = extractJsonArray(chaptersObj, "nodes");
    history.clear();
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        ReadingHistoryItem histItem;

        histItem.chapterId = extractJsonInt(item, "id");
        histItem.chapterName = extractJsonValue(item, "name");
        histItem.chapterNumber = extractJsonFloat(item, "chapterNumber");
        histItem.lastPageRead = extractJsonInt(item, "lastPageRead");
        histItem.lastReadAt = extractJsonInt64(item, "lastReadAt");
        histItem.pageCount = extractJsonInt(item, "pageCount");

        std::string mangaJson = extractJsonObject(item, "manga");
        if (!mangaJson.empty()) {
            histItem.mangaId = extractJsonInt(mangaJson, "id");
            histItem.mangaTitle = extractJsonValue(mangaJson, "title");
            histItem.mangaThumbnail = extractJsonValue(mangaJson, "thumbnailUrl");

            std::string sourceJson = extractJsonObject(mangaJson, "source");
            if (!sourceJson.empty()) {
                histItem.sourceName = extractJsonValue(sourceJson, "displayName");
            }
        }

        history.push_back(histItem);
    }

    brls::Logger::debug("GraphQL: Fetched {} reading history items", history.size());
    return true;
}

bool SuwayomiClient::globalSearchGraphQL(const std::string& query, std::vector<GlobalSearchResult>& results) {
    const char* gqlQuery = R"(
        mutation GlobalSearch($searchTerm: String!) {
            fetchSourceManga(input: { type: SEARCH, query: $searchTerm }) {
                mangas {
                    id
                    title
                    thumbnailUrl
                    author
                    inLibrary
                }
                hasNextPage
            }
        }
    )";

    // Escape the search query for JSON
    std::string escapedQuery;
    for (char c : query) {
        switch (c) {
            case '"': escapedQuery += "\\\""; break;
            case '\\': escapedQuery += "\\\\"; break;
            default: escapedQuery += c; break;
        }
    }

    std::string variables = "{\"searchTerm\":\"" + escapedQuery + "\"}";

    std::string response = executeGraphQL(gqlQuery, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string fetchResult = extractJsonObject(data, "fetchSourceManga");
    if (fetchResult.empty()) return false;

    // For global search, we get results from the default/all sources
    GlobalSearchResult result;
    result.hasNextPage = extractJsonBool(fetchResult, "hasNextPage");

    std::string mangasJson = extractJsonArray(fetchResult, "mangas");
    std::vector<std::string> items = splitJsonArray(mangasJson);
    for (const auto& item : items) {
        result.manga.push_back(parseMangaFromGraphQL(item));
    }

    if (!result.manga.empty()) {
        results.push_back(result);
    }

    brls::Logger::debug("GraphQL: Global search found {} results", result.manga.size());
    return true;
}

bool SuwayomiClient::setMangaCategoriesGraphQL(int mangaId, const std::vector<int>& categoryIds) {
    const char* query = R"(
        mutation UpdateMangaCategories($id: Int!, $categories: [Int!]!) {
            updateMangaCategories(input: { id: $id, patch: { addToCategories: $categories, clearCategories: true } }) {
                manga {
                    id
                    categories {
                        nodes {
                            id
                            name
                        }
                    }
                }
            }
        }
    )";

    std::string catList = "[";
    for (size_t i = 0; i < categoryIds.size(); i++) {
        if (i > 0) catList += ",";
        catList += std::to_string(categoryIds[i]);
    }
    catList += "]";

    std::string variables = "{\"id\":" + std::to_string(mangaId) + ",\"categories\":" + catList + "}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::fetchMangaMetaGraphQL(int mangaId, std::map<std::string, std::string>& meta) {
    const char* query = R"(
        query GetMangaMeta($id: Int!) {
            manga(id: $id) {
                id
                meta {
                    key
                    value
                }
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(mangaId) + "}";
    std::string response = executeGraphQL(query, variables);

    if (response.empty()) {
        brls::Logger::warning("Failed to fetch manga meta for manga {}", mangaId);
        return false;
    }

    meta.clear();

    // Parse the meta array from response
    // Response format: {"data":{"manga":{"id":123,"meta":[{"key":"k1","value":"v1"},...]}}
    size_t metaPos = response.find("\"meta\"");
    if (metaPos == std::string::npos) {
        brls::Logger::debug("No meta field found for manga {}", mangaId);
        return true;  // No meta is valid
    }

    size_t arrayStart = response.find('[', metaPos);
    size_t arrayEnd = response.find(']', arrayStart);
    if (arrayStart == std::string::npos || arrayEnd == std::string::npos) {
        return true;  // Empty or invalid meta
    }

    std::string metaArray = response.substr(arrayStart, arrayEnd - arrayStart + 1);

    // Parse each meta entry
    size_t pos = 0;
    while ((pos = metaArray.find("{\"key\"", pos)) != std::string::npos) {
        // Extract key
        size_t keyStart = metaArray.find("\"key\"", pos);
        if (keyStart == std::string::npos) break;
        keyStart = metaArray.find("\"", keyStart + 5);  // Skip to opening quote of value
        if (keyStart == std::string::npos) break;
        keyStart++;
        size_t keyEnd = metaArray.find("\"", keyStart);
        if (keyEnd == std::string::npos) break;
        std::string key = metaArray.substr(keyStart, keyEnd - keyStart);

        // Extract value
        size_t valueStart = metaArray.find("\"value\"", keyEnd);
        if (valueStart == std::string::npos) break;
        valueStart = metaArray.find("\"", valueStart + 7);  // Skip to opening quote of value
        if (valueStart == std::string::npos) break;
        valueStart++;
        size_t valueEnd = metaArray.find("\"", valueStart);
        if (valueEnd == std::string::npos) break;
        std::string value = metaArray.substr(valueStart, valueEnd - valueStart);

        meta[key] = value;
        pos = valueEnd + 1;
    }

    brls::Logger::debug("Fetched {} meta entries for manga {}", meta.size(), mangaId);
    return true;
}

bool SuwayomiClient::setMangaMetaGraphQL(int mangaId, const std::string& key, const std::string& value) {
    const char* query = R"(
        mutation SetMangaMeta($id: Int!, $key: String!, $value: String!) {
            setMangaMeta(input: { meta: { mangaId: $id, key: $key, value: $value } }) {
                meta {
                    key
                    value
                }
            }
        }
    )";

    // Escape special characters in key and value for JSON
    std::string escapedKey = key;
    std::string escapedValue = value;
    // Basic JSON escaping for quotes
    size_t pos = 0;
    while ((pos = escapedKey.find("\"", pos)) != std::string::npos) {
        escapedKey.replace(pos, 1, "\\\"");
        pos += 2;
    }
    pos = 0;
    while ((pos = escapedValue.find("\"", pos)) != std::string::npos) {
        escapedValue.replace(pos, 1, "\\\"");
        pos += 2;
    }

    std::string variables = "{\"id\":" + std::to_string(mangaId) +
                            ",\"key\":\"" + escapedKey +
                            "\",\"value\":\"" + escapedValue + "\"}";
    std::string response = executeGraphQL(query, variables);

    if (!response.empty()) {
        brls::Logger::debug("Set manga meta: {}={} for manga {}", key, value, mangaId);
        return true;
    }

    brls::Logger::warning("Failed to set manga meta for manga {}", mangaId);
    return false;
}

bool SuwayomiClient::deleteMangaMetaGraphQL(int mangaId, const std::string& key) {
    const char* query = R"(
        mutation DeleteMangaMeta($id: Int!, $key: String!) {
            deleteMangaMeta(input: { mangaId: $id, key: $key }) {
                meta {
                    key
                }
                manga {
                    id
                }
            }
        }
    )";

    std::string escapedKey = key;
    size_t pos = 0;
    while ((pos = escapedKey.find("\"", pos)) != std::string::npos) {
        escapedKey.replace(pos, 1, "\\\"");
        pos += 2;
    }

    std::string variables = "{\"id\":" + std::to_string(mangaId) +
                            ",\"key\":\"" + escapedKey + "\"}";
    std::string response = executeGraphQL(query, variables);

    if (!response.empty()) {
        brls::Logger::debug("Deleted manga meta key '{}' for manga {}", key, mangaId);
        return true;
    }

    brls::Logger::warning("Failed to delete manga meta for manga {}", mangaId);
    return false;
}

// Public wrapper methods with REST fallback
bool SuwayomiClient::fetchMangaMeta(int mangaId, std::map<std::string, std::string>& meta) {
    // Try GraphQL first
    if (fetchMangaMetaGraphQL(mangaId, meta)) {
        return true;
    }

    // REST fallback: GET /api/v1/manga/{id}/meta
    brls::Logger::info("GraphQL failed for fetchMangaMeta, falling back to REST...");
    HttpClient client = createHttpClient();
    HttpResponse httpResp = client.get(buildApiUrl("manga/" + std::to_string(mangaId) + "/meta"));

    if (!httpResp.success || httpResp.body.empty()) {
        brls::Logger::warning("REST fallback also failed for manga meta");
        return false;
    }

    // Parse REST response (array of {key, value} objects)
    std::string response = httpResp.body;
    meta.clear();
    size_t pos = 0;
    while ((pos = response.find("\"key\"", pos)) != std::string::npos) {
        size_t keyStart = response.find("\"", pos + 5) + 1;
        size_t keyEnd = response.find("\"", keyStart);
        std::string key = response.substr(keyStart, keyEnd - keyStart);

        size_t valueStart = response.find("\"value\"", keyEnd);
        if (valueStart != std::string::npos) {
            valueStart = response.find("\"", valueStart + 7) + 1;
            size_t valueEnd = response.find("\"", valueStart);
            std::string value = response.substr(valueStart, valueEnd - valueStart);
            meta[key] = value;
            pos = valueEnd + 1;
        } else {
            break;
        }
    }

    return true;
}

bool SuwayomiClient::setMangaMeta(int mangaId, const std::string& key, const std::string& value) {
    // Try GraphQL first
    if (setMangaMetaGraphQL(mangaId, key, value)) {
        return true;
    }

    // REST fallback: PATCH /api/v1/manga/{id}/meta
    brls::Logger::info("GraphQL failed for setMangaMeta, falling back to REST...");
    HttpClient client = createHttpClient();
    client.setDefaultHeader("Content-Type", "application/json");

    std::string body = "{\"key\":\"" + key + "\",\"value\":\"" + value + "\"}";

    // Use request() with PATCH method since HttpClient doesn't have patch()
    HttpRequest req;
    req.url = buildApiUrl("manga/" + std::to_string(mangaId) + "/meta");
    req.method = "PATCH";
    req.body = body;
    req.headers["Content-Type"] = "application/json";

    HttpResponse httpResp = client.request(req);
    return httpResp.success;
}

bool SuwayomiClient::deleteMangaMeta(int mangaId, const std::string& key) {
    // Try GraphQL first
    if (deleteMangaMetaGraphQL(mangaId, key)) {
        return true;
    }

    // REST fallback: DELETE /api/v1/manga/{id}/meta/{key}
    brls::Logger::info("GraphQL failed for deleteMangaMeta, falling back to REST...");
    HttpClient client = createHttpClient();
    HttpResponse httpResp = client.del(buildApiUrl("manga/" + std::to_string(mangaId) + "/meta/" + key));

    return httpResp.success;
}

bool SuwayomiClient::fetchCategoryMangaGraphQL(int categoryId, std::vector<Manga>& manga) {
    // Use mangas query with categoryId filter - this correctly filters manga by category
    // The category(id).mangas approach returns ALL library manga unfiltered
    const char* query = R"(
        query GetMangasByCategory($categoryId: Int!) {
            mangas(
                filter: {
                    inLibrary: { equalTo: true }
                    categoryId: { equalTo: $categoryId }
                }
            ) {
                nodes {
                    id
                    title
                    thumbnailUrl
                    author
                    artist
                    description
                    genre
                    status
                    inLibrary
                    inLibraryAt
                    unreadCount
                    downloadCount
                    source {
                        displayName
                    }
                    chapters {
                        totalCount
                    }
                    lastReadChapter {
                        lastReadAt
                    }
                    latestUploadedChapter {
                        uploadDate
                    }
                }
            }
        }
    )";

    std::string variables = "{\"categoryId\":" + std::to_string(categoryId) + "}";

    brls::Logger::info("GraphQL: Fetching manga for category {} with filter", categoryId);

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::warning("GraphQL filter query failed, trying fallback...");
        return fetchCategoryMangaGraphQLFallback(categoryId, manga);
    }

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) {
        brls::Logger::warning("GraphQL: No data in response, trying fallback...");
        return fetchCategoryMangaGraphQLFallback(categoryId, manga);
    }

    std::string mangasObj = extractJsonObject(data, "mangas");
    if (mangasObj.empty()) {
        brls::Logger::warning("GraphQL: No mangas object, trying fallback...");
        return fetchCategoryMangaGraphQLFallback(categoryId, manga);
    }

    std::string nodesJson = extractJsonArray(mangasObj, "nodes");
    manga.clear();
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        manga.push_back(parseMangaFromGraphQL(item));
    }

    // If filter returned 0 results for the default category (id=0),
    // the server may not support categoryId filter for the default category.
    // Fall back to category(id) query which works for the default category.
    if (manga.empty() && categoryId == 0) {
        brls::Logger::info("GraphQL: Filter returned 0 for default category, trying fallback...");
        return fetchCategoryMangaGraphQLFallback(categoryId, manga);
    }

    brls::Logger::info("GraphQL: Fetched {} manga for category {} (filter method)", manga.size(), categoryId);
    return true;
}

// Fallback method using category(id).mangas query
// Only reliable for the default category (id=0); for other categories this may return all library manga
bool SuwayomiClient::fetchCategoryMangaGraphQLFallback(int categoryId, std::vector<Manga>& manga) {
    const char* query = R"(
        query GetCategoryManga($categoryId: Int!) {
            category(id: $categoryId) {
                id
                name
                mangas {
                    nodes {
                        id
                        title
                        thumbnailUrl
                        author
                        artist
                        description
                        genre
                        status
                        inLibrary
                        inLibraryAt
                        unreadCount
                        downloadCount
                        source {
                            displayName
                        }
                        chapters {
                            totalCount
                        }
                        lastReadChapter {
                            lastReadAt
                        }
                        latestUploadedChapter {
                            uploadDate
                        }
                    }
                }
            }
        }
    )";

    std::string variables = "{\"categoryId\":" + std::to_string(categoryId) + "}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string categoryObj = extractJsonObject(data, "category");
    if (categoryObj.empty()) return false;

    std::string mangasObj = extractJsonObject(categoryObj, "mangas");
    if (mangasObj.empty()) return false;

    std::string nodesJson = extractJsonArray(mangasObj, "nodes");
    manga.clear();
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        manga.push_back(parseMangaFromGraphQL(item));
    }

    brls::Logger::info("GraphQL fallback: Fetched {} manga for category {}", manga.size(), categoryId);
    return true;
}

// ============================================================================
// Combined/Parallel GraphQL queries
// ============================================================================

bool SuwayomiClient::fetchCategoriesWithMangaGraphQL(std::vector<Category>& categories, int categoryId, std::vector<Manga>& manga) {
    const char* query = R"(
        query GetCategoriesAndManga($categoryId: Int!) {
            categories {
                nodes {
                    id
                    name
                    order
                    mangas {
                        totalCount
                    }
                }
            }
            mangas(
                filter: {
                    inLibrary: { equalTo: true }
                    categoryId: { equalTo: $categoryId }
                }
            ) {
                nodes {
                    id
                    title
                    thumbnailUrl
                    author
                    artist
                    description
                    genre
                    status
                    inLibrary
                    inLibraryAt
                    unreadCount
                    downloadCount
                    source {
                        displayName
                    }
                    chapters {
                        totalCount
                    }
                    lastReadChapter {
                        lastReadAt
                    }
                    latestUploadedChapter {
                        uploadDate
                    }
                }
            }
        }
    )";

    std::string variables = "{\"categoryId\":" + std::to_string(categoryId) + "}";
    brls::Logger::info("GraphQL: Fetching categories + manga for category {} in single request", categoryId);

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    // Parse categories
    std::string categoriesObj = extractJsonObject(data, "categories");
    if (!categoriesObj.empty()) {
        std::string nodesJson = extractJsonArray(categoriesObj, "nodes");
        categories.clear();
        std::vector<std::string> items = splitJsonArray(nodesJson);
        for (const auto& item : items) {
            categories.push_back(parseCategoryFromGraphQL(item));
        }
        brls::Logger::debug("GraphQL combined: Fetched {} categories", categories.size());
    }

    // Parse manga
    std::string mangasObj = extractJsonObject(data, "mangas");
    if (!mangasObj.empty()) {
        std::string nodesJson = extractJsonArray(mangasObj, "nodes");
        manga.clear();
        std::vector<std::string> items = splitJsonArray(nodesJson);
        for (const auto& item : items) {
            manga.push_back(parseMangaFromGraphQL(item));
        }
        brls::Logger::debug("GraphQL combined: Fetched {} manga for category {}", manga.size(), categoryId);
    }

    return !categories.empty();
}

bool SuwayomiClient::fetchCategoriesWithManga(std::vector<Category>& categories, int categoryId, std::vector<Manga>& manga) {
    if (fetchCategoriesWithMangaGraphQL(categories, categoryId, manga)) {
        return true;
    }

    // Fallback: fetch separately
    brls::Logger::info("Combined query failed, falling back to separate fetches");
    if (!fetchCategories(categories)) {
        return false;
    }
    fetchCategoryManga(categoryId, manga);
    return true;
}

bool SuwayomiClient::fetchMangaWithChaptersGraphQL(int mangaId, Manga& manga, std::vector<Chapter>& chapters) {
    const char* query = R"(
        query GetMangaWithChapters($mangaId: Int!) {
            manga(id: $mangaId) {
                id
                title
                thumbnailUrl
                author
                artist
                description
                genre
                status
                url
                inLibrary
                initialized
                source {
                    displayName
                }
            }
            chapters(
                condition: { mangaId: $mangaId }
                first: 1000
                orderBy: SOURCE_ORDER
                orderByType: DESC
            ) {
                nodes {
                    id
                    name
                    chapterNumber
                    scanlator
                    uploadDate
                    isRead
                    isDownloaded
                    isBookmarked
                    pageCount
                    lastPageRead
                    lastReadAt
                    sourceOrder
                }
                totalCount
            }
        }
    )";

    std::string variables = "{\"mangaId\":" + std::to_string(mangaId) + "}";
    brls::Logger::info("GraphQL: Fetching manga details + chapters for manga {} in single request", mangaId);

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    // Parse manga details
    std::string mangaJson = extractJsonObject(data, "manga");
    if (!mangaJson.empty()) {
        manga = parseMangaFromGraphQL(mangaJson);
    }

    // Parse chapters
    std::string chaptersObj = extractJsonObject(data, "chapters");
    if (!chaptersObj.empty()) {
        std::string nodesJson = extractJsonArray(chaptersObj, "nodes");
        chapters.clear();
        std::vector<std::string> items = splitJsonArray(nodesJson);
        for (const auto& item : items) {
            Chapter ch = parseChapterFromGraphQL(item);
            ch.mangaId = mangaId;
            chapters.push_back(ch);
        }
        brls::Logger::debug("GraphQL combined: Fetched {} chapters for manga {}", chapters.size(), mangaId);
    }

    return manga.id > 0;
}

bool SuwayomiClient::fetchMangaWithChapters(int mangaId, Manga& manga, std::vector<Chapter>& chapters) {
    if (fetchMangaWithChaptersGraphQL(mangaId, manga, chapters)) {
        return true;
    }

    // Fallback: fetch separately
    brls::Logger::info("Combined manga+chapters query failed, falling back to separate fetches");
    bool gotManga = fetchManga(mangaId, manga);
    bool gotChapters = fetchChapters(mangaId, chapters);
    return gotManga || gotChapters;
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

    // Connection failed - try auto-switch if enabled
    Application& app = Application::getInstance();
    if (app.tryAlternateUrl()) {
        // Successfully switched to alternate URL, update our server URL
        m_serverUrl = app.getActiveServerUrl();
        brls::Logger::info("Auto-switched to: {}", m_serverUrl);

        // Try connection with new URL
        if (fetchServerInfo(info)) {
            m_isConnected = true;
            m_serverInfo = info;
            brls::Logger::info("Connected to Suwayomi {} ({}) via auto-switch", info.version, info.buildType);
            brls::Application::notify("Auto-switched to " + std::string(app.getSettings().useRemoteUrl ? "remote" : "local") + " URL");
            return true;
        }
    }

    m_isConnected = false;
    return false;
}

bool SuwayomiClient::checkServerRequiresAuth(const std::string& url) {
    // Make a simple request WITHOUT authentication to check if server requires auth
    // Returns true if server responds with 401 Unauthorized
    brls::Logger::info("Checking if server requires authentication...");

    // Create a basic HTTP client without any auth headers
    vitasuwayomi::HttpClient http;
    http.setTimeout(10);

    // Try the GraphQL endpoint first (most common)
    std::string testUrl = url + "/api/graphql";
    std::string body = R"({"query":"{ aboutServer { name } }"})";

    vitasuwayomi::HttpResponse response = http.post(testUrl, body, "application/json");

    if (response.statusCode == 401) {
        brls::Logger::info("Server requires authentication (got 401 from GraphQL)");
        return true;
    }

    // Also try REST endpoint as fallback check
    testUrl = url + "/api/v1/settings/about";
    response = http.get(testUrl);

    if (response.statusCode == 401) {
        brls::Logger::info("Server requires authentication (got 401 from REST)");
        return true;
    }

    brls::Logger::info("Server does not require authentication");
    return false;
}

bool SuwayomiClient::checkServerSupportsJWTLogin(const std::string& url) {
    // Try the login mutation with dummy credentials to see if it exists
    // If the server supports JWT login, it will return an error about invalid credentials
    // If not, it will return an error about unknown field 'login'
    brls::Logger::info("Checking if server supports JWT login...");

    vitasuwayomi::HttpClient http;
    http.setTimeout(10);
    http.setDefaultHeader("Accept", "application/json");
    http.setDefaultHeader("Content-Type", "application/json");

    std::string testUrl = url + "/api/graphql";

    // Try the login mutation - we don't care about credentials, just if the mutation exists
    std::string body = R"({"query":"mutation { login(input: { username: \"test\", password: \"test\" }) { accessToken } }"})";

    vitasuwayomi::HttpResponse response = http.post(testUrl, body);

    if (!response.success) {
        brls::Logger::warning("Failed to check JWT support: {}", response.error);
        return false;
    }

    // Check for GraphQL errors
    if (response.body.find("\"errors\"") != std::string::npos) {
        // If error contains "Cannot query field" or "Unknown field" or similar,
        // the login mutation doesn't exist - server uses Basic Auth only
        if (response.body.find("Cannot query field") != std::string::npos ||
            response.body.find("Unknown field") != std::string::npos ||
            response.body.find("unknown field") != std::string::npos ||
            response.body.find("Field \"login\" not found") != std::string::npos ||
            response.body.find("not found") != std::string::npos) {
            brls::Logger::info("Server does NOT support JWT login (login mutation not found)");
            return false;
        }

        // Other errors (like invalid credentials) mean the mutation exists
        brls::Logger::info("Server supports JWT login (mutation exists but credentials failed)");
        return true;
    }

    // If no errors, the mutation succeeded (unlikely with test credentials, but possible)
    brls::Logger::info("Server supports JWT login");
    return true;
}

AuthMode SuwayomiClient::detectServerAuthMode(const std::string& url, std::string& errorMessage) {
    brls::Logger::info("Detecting server auth mode for: {}", url);

    // Step 1: Check if server requires authentication at all
    bool requiresAuth = checkServerRequiresAuth(url);

    if (!requiresAuth) {
        brls::Logger::info("Server detected as: No authentication required");
        errorMessage = "";
        return AuthMode::NONE;
    }

    // Step 2: Server requires auth - check if it supports JWT login
    bool supportsJWT = checkServerSupportsJWTLogin(url);

    if (supportsJWT) {
        // Server supports JWT - could be SIMPLE_LOGIN or UI_LOGIN
        // We default to UI_LOGIN as it's the more recent/common one
        brls::Logger::info("Server detected as: JWT-based authentication (UI Login)");
        errorMessage = "";
        return AuthMode::UI_LOGIN;
    } else {
        // Server doesn't support JWT login mutation - likely Basic Auth
        brls::Logger::info("Server detected as: Basic authentication");
        errorMessage = "";
        return AuthMode::BASIC_AUTH;
    }
}

bool SuwayomiClient::fetchServerInfo(ServerInfo& info) {
    // Try GraphQL first (primary API)
    brls::Logger::info("Fetching server info via GraphQL...");
    if (fetchServerInfoGraphQL(info)) {
        brls::Logger::info("Server (GraphQL): {} v{}", info.name, info.version);
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed, falling back to REST API...");
    vitasuwayomi::HttpClient http = createHttpClient();

    // Try /api/v1/settings/about endpoint (standard Suwayomi endpoint)
    std::string url = m_serverUrl + "/api/v1/settings/about";
    brls::Logger::info("Fetching server info from: {}", url);
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::warning("REST about endpoint failed ({}), trying source list...", response.statusCode);

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

    brls::Logger::info("Server (REST): {} v{} ({})", info.name, info.version, info.buildType);
    return true;
}

bool SuwayomiClient::testConnection() {
    ServerInfo info;
    return fetchServerInfo(info);
}

void SuwayomiClient::setAuthCredentials(const std::string& username, const std::string& password) {
    m_authUsername = username;
    m_authPassword = password;

    // Also set credentials for image loading
    ImageLoader::setAuthCredentials(username, password);
}

void SuwayomiClient::clearAuth() {
    m_authUsername.clear();
    m_authPassword.clear();
    m_accessToken.clear();
    m_refreshToken.clear();
    m_sessionCookie.clear();

    // Also clear image loader credentials
    ImageLoader::setAuthCredentials("", "");
}

void SuwayomiClient::setTokens(const std::string& accessToken, const std::string& refreshToken) {
    m_accessToken = accessToken;
    m_refreshToken = refreshToken;
    // Also update ImageLoader with access token
    if (!accessToken.empty()) {
        ImageLoader::setAccessToken(accessToken);
    }
}

void SuwayomiClient::setSessionCookie(const std::string& cookie) {
    m_sessionCookie = cookie;
}

bool SuwayomiClient::isAuthenticated() const {
    switch (m_authMode) {
        case AuthMode::NONE:
            return true;  // No auth required
        case AuthMode::BASIC_AUTH:
            return !m_authUsername.empty() && !m_authPassword.empty();
        case AuthMode::SIMPLE_LOGIN:
            return !m_sessionCookie.empty();
        case AuthMode::UI_LOGIN:
            return !m_accessToken.empty();
        default:
            return false;
    }
}

void SuwayomiClient::logout() {
    m_accessToken.clear();
    m_refreshToken.clear();
    m_sessionCookie.clear();
    // Keep username/password for re-login if needed
}

bool SuwayomiClient::login(const std::string& username, const std::string& password) {
    m_authUsername = username;
    m_authPassword = password;

    switch (m_authMode) {
        case AuthMode::NONE:
            // No login needed
            return true;

        case AuthMode::BASIC_AUTH:
            // Basic auth just stores credentials, no login request needed
            // Test connection to verify credentials work
            ImageLoader::setAuthCredentials(username, password);
            return testConnection();

        case AuthMode::SIMPLE_LOGIN:
        case AuthMode::UI_LOGIN:
            // Both use the same GraphQL login mutation
            // The difference is in how the tokens/cookies are used
            ImageLoader::setAuthCredentials(username, password);
            return loginGraphQL(username, password);

        default:
            return false;
    }
}

bool SuwayomiClient::loginGraphQL(const std::string& username, const std::string& password) {
    // GraphQL login mutation
    const char* query = R"(
        mutation Login($username: String!, $password: String!) {
            login(input: { username: $username, password: $password }) {
                accessToken
                refreshToken
            }
        }
    )";

    // Build variables
    std::string variables = "{\"username\":\"" + username + "\",\"password\":\"" + password + "\"}";

    // Execute GraphQL - need to use a fresh client without auth for login
    vitasuwayomi::HttpClient http;
    http.setDefaultHeader("Accept", "application/json");
    http.setDefaultHeader("Content-Type", "application/json");

    int timeout = Application::getInstance().getSettings().connectionTimeout;
    if (timeout > 0) {
        http.setTimeout(timeout);
    }

    std::string url = buildGraphQLUrl();

    // Escape special characters in variables for JSON
    std::string escapedQuery = query;
    // Remove newlines for JSON
    size_t pos = 0;
    while ((pos = escapedQuery.find('\n', pos)) != std::string::npos) {
        escapedQuery.replace(pos, 1, "\\n");
        pos += 2;
    }

    std::string body = "{\"query\":\"" + escapedQuery + "\",\"variables\":" + variables + "}";

    vitasuwayomi::HttpResponse response = http.post(url, body);

    if (!response.success) {
        brls::Logger::error("Login request failed: {}", response.error);
        return false;
    }

    // Check for errors in response
    if (response.body.find("\"errors\"") != std::string::npos) {
        std::string errorMsg = extractJsonValue(response.body, "message");
        brls::Logger::error("Login failed: {}", errorMsg.empty() ? "Unknown error" : errorMsg);
        return false;
    }

    // Extract tokens from response
    std::string data = extractJsonObject(response.body, "data");
    std::string loginData = extractJsonObject(data, "login");

    m_accessToken = extractJsonValue(loginData, "accessToken");
    m_refreshToken = extractJsonValue(loginData, "refreshToken");

    if (m_accessToken.empty()) {
        brls::Logger::error("Login failed: No access token received");
        return false;
    }

    brls::Logger::info("Login successful, received tokens");

    // Update image loader with auth info and access token
    ImageLoader::setAuthCredentials(username, password);
    ImageLoader::setAccessToken(m_accessToken);

    return true;
}

bool SuwayomiClient::refreshToken() {
    if (m_authMode != AuthMode::UI_LOGIN && m_authMode != AuthMode::SIMPLE_LOGIN) {
        return true;  // No token refresh needed for basic auth or no auth
    }

    if (m_refreshToken.empty()) {
        brls::Logger::error("Cannot refresh token: No refresh token available");
        return false;
    }

    return refreshTokenGraphQL();
}

bool SuwayomiClient::refreshTokenGraphQL() {
    const char* query = R"(
        mutation RefreshToken($refreshToken: String!) {
            refreshToken(input: { refreshToken: $refreshToken }) {
                accessToken
            }
        }
    )";

    std::string variables = "{\"refreshToken\":\"" + m_refreshToken + "\"}";

    // Use fresh HTTP client without existing auth
    vitasuwayomi::HttpClient http;
    http.setDefaultHeader("Accept", "application/json");
    http.setDefaultHeader("Content-Type", "application/json");

    int timeout = Application::getInstance().getSettings().connectionTimeout;
    if (timeout > 0) {
        http.setTimeout(timeout);
    }

    std::string url = buildGraphQLUrl();

    std::string escapedQuery = query;
    size_t pos = 0;
    while ((pos = escapedQuery.find('\n', pos)) != std::string::npos) {
        escapedQuery.replace(pos, 1, "\\n");
        pos += 2;
    }

    std::string body = "{\"query\":\"" + escapedQuery + "\",\"variables\":" + variables + "}";

    vitasuwayomi::HttpResponse response = http.post(url, body);

    if (!response.success) {
        brls::Logger::error("Token refresh request failed: {}", response.error);
        return false;
    }

    // Check for errors
    if (response.body.find("\"errors\"") != std::string::npos) {
        std::string errorMsg = extractJsonValue(response.body, "message");
        brls::Logger::error("Token refresh failed: {}", errorMsg.empty() ? "Unknown error" : errorMsg);
        return false;
    }

    // Extract new access token
    std::string data = extractJsonObject(response.body, "data");
    std::string refreshData = extractJsonObject(data, "refreshToken");

    std::string newAccessToken = extractJsonValue(refreshData, "accessToken");

    if (newAccessToken.empty()) {
        brls::Logger::error("Token refresh failed: No access token received");
        return false;
    }

    m_accessToken = newAccessToken;
    ImageLoader::setAccessToken(m_accessToken);
    brls::Logger::info("Token refreshed successfully");

    return true;
}

// ============================================================================
// Extension Management
// ============================================================================

bool SuwayomiClient::fetchExtensionList(std::vector<Extension>& extensions) {
    // Try GraphQL first (primary API)
    if (fetchExtensionListGraphQL(extensions)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for extensions, falling back to REST...");
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

    brls::Logger::debug("REST: Fetched {} extensions", extensions.size());
    return true;
}

bool SuwayomiClient::installExtension(const std::string& pkgName) {
    const int maxRetries = 3;

    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        brls::Logger::info("Installing extension {} (attempt {}/{})", pkgName, attempt, maxRetries);

        // Try GraphQL first (primary API)
        if (installExtensionGraphQL(pkgName)) {
            return true;
        }

        // REST fallback
        brls::Logger::info("GraphQL failed for install extension, falling back to REST...");
        vitasuwayomi::HttpClient http = createHttpClient();
        http.setTimeout(60);  // Extended timeout for extensions

        std::string url = buildApiUrl("/extension/install/" + pkgName);
        vitasuwayomi::HttpResponse response = http.get(url);

        // REST API returns 201 (Created), 302 (Redirect), or 200 on success
        if (response.success && (response.statusCode == 200 || response.statusCode == 201 || response.statusCode == 302)) {
            return true;
        }

        if (attempt < maxRetries) {
            brls::Logger::warning("Extension install failed (attempt {}/{}), retrying...", attempt, maxRetries);
        }
    }

    brls::Logger::error("Extension install failed after {} attempts", maxRetries);
    return false;
}

bool SuwayomiClient::updateExtension(const std::string& pkgName) {
    const int maxRetries = 3;

    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        brls::Logger::info("Updating extension {} (attempt {}/{})", pkgName, attempt, maxRetries);

        // Try GraphQL first (primary API)
        if (updateExtensionGraphQL(pkgName)) {
            return true;
        }

        // REST fallback
        brls::Logger::info("GraphQL failed for update extension, falling back to REST...");
        vitasuwayomi::HttpClient http = createHttpClient();
        http.setTimeout(60);  // Extended timeout for extensions

        std::string url = buildApiUrl("/extension/update/" + pkgName);
        vitasuwayomi::HttpResponse response = http.get(url);

        // REST API returns 201 (Created), 302 (Redirect), or 200 on success
        if (response.success && (response.statusCode == 200 || response.statusCode == 201 || response.statusCode == 302)) {
            return true;
        }

        if (attempt < maxRetries) {
            brls::Logger::warning("Extension update failed (attempt {}/{}), retrying...", attempt, maxRetries);
        }
    }

    brls::Logger::error("Extension update failed after {} attempts", maxRetries);
    return false;
}

bool SuwayomiClient::uninstallExtension(const std::string& pkgName) {
    const int maxRetries = 3;

    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        brls::Logger::info("Uninstalling extension {} (attempt {}/{})", pkgName, attempt, maxRetries);

        // Try GraphQL first (primary API)
        if (uninstallExtensionGraphQL(pkgName)) {
            return true;
        }

        // REST fallback
        brls::Logger::info("GraphQL failed for uninstall extension, falling back to REST...");
        vitasuwayomi::HttpClient http = createHttpClient();
        http.setTimeout(60);  // Extended timeout for extensions

        std::string url = buildApiUrl("/extension/uninstall/" + pkgName);
        vitasuwayomi::HttpResponse response = http.get(url);

        // REST API returns 201 (Created), 302 (Redirect), or 200 on success
        if (response.success && (response.statusCode == 200 || response.statusCode == 201 || response.statusCode == 302)) {
            return true;
        }

        if (attempt < maxRetries) {
            brls::Logger::warning("Extension uninstall failed (attempt {}/{}), retrying...", attempt, maxRetries);
        }
    }

    brls::Logger::error("Extension uninstall failed after {} attempts", maxRetries);
    return false;
}

std::string SuwayomiClient::getExtensionIconUrl(const std::string& apkName) {
    return buildApiUrl("/extension/icon/" + apkName);
}

// ============================================================================
// Extension Repository Management
// ============================================================================

bool SuwayomiClient::fetchExtensionRepos(std::vector<std::string>& repos) {
    const char* query = R"(
        query GetSettings {
            settings {
                extensionRepos
            }
        }
    )";

    std::string response = executeGraphQL(query, "");
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string settings = extractJsonObject(data, "settings");
    if (settings.empty()) return false;

    repos = extractJsonStringArray(settings, "extensionRepos");
    brls::Logger::debug("Fetched {} extension repositories", repos.size());
    return true;
}

bool SuwayomiClient::addExtensionRepo(const std::string& repoUrl) {
    // First fetch current repos
    std::vector<std::string> currentRepos;
    if (!fetchExtensionRepos(currentRepos)) {
        brls::Logger::error("Failed to fetch current extension repos");
        return false;
    }

    // Check if repo already exists
    for (const auto& repo : currentRepos) {
        if (repo == repoUrl) {
            brls::Logger::info("Extension repo already exists: {}", repoUrl);
            return true;  // Already exists, consider it a success
        }
    }

    // Add new repo to the list
    currentRepos.push_back(repoUrl);

    // Build the repos array string for GraphQL
    std::string reposArray = "[";
    for (size_t i = 0; i < currentRepos.size(); i++) {
        if (i > 0) reposArray += ",";
        // Escape the URL string
        std::string escapedUrl;
        for (char c : currentRepos[i]) {
            switch (c) {
                case '"': escapedUrl += "\\\""; break;
                case '\\': escapedUrl += "\\\\"; break;
                default: escapedUrl += c; break;
            }
        }
        reposArray += "\"" + escapedUrl + "\"";
    }
    reposArray += "]";

    const char* query = R"(
        mutation SetSettings($input: SetSettingsInput!) {
            setSettings(input: $input) {
                settings {
                    extensionRepos
                }
            }
        }
    )";

    std::string variables = "{\"input\":{\"settings\":{\"extensionRepos\":" + reposArray + "}}}";
    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("Failed to update extension repos");
        return false;
    }

    brls::Logger::info("Successfully added extension repo: {}", repoUrl);
    return true;
}

bool SuwayomiClient::removeExtensionRepo(const std::string& repoUrl) {
    // First fetch current repos
    std::vector<std::string> currentRepos;
    if (!fetchExtensionRepos(currentRepos)) {
        brls::Logger::error("Failed to fetch current extension repos");
        return false;
    }

    // Remove the repo from the list
    auto it = std::find(currentRepos.begin(), currentRepos.end(), repoUrl);
    if (it == currentRepos.end()) {
        brls::Logger::info("Extension repo not found: {}", repoUrl);
        return true;  // Not found, consider it a success
    }
    currentRepos.erase(it);

    // Build the repos array string for GraphQL
    std::string reposArray = "[";
    for (size_t i = 0; i < currentRepos.size(); i++) {
        if (i > 0) reposArray += ",";
        // Escape the URL string
        std::string escapedUrl;
        for (char c : currentRepos[i]) {
            switch (c) {
                case '"': escapedUrl += "\\\""; break;
                case '\\': escapedUrl += "\\\\"; break;
                default: escapedUrl += c; break;
            }
        }
        reposArray += "\"" + escapedUrl + "\"";
    }
    reposArray += "]";

    const char* query = R"(
        mutation SetSettings($input: SetSettingsInput!) {
            setSettings(input: $input) {
                settings {
                    extensionRepos
                }
            }
        }
    )";

    std::string variables = "{\"input\":{\"settings\":{\"extensionRepos\":" + reposArray + "}}}";
    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("Failed to update extension repos");
        return false;
    }

    brls::Logger::info("Successfully removed extension repo: {}", repoUrl);
    return true;
}

// ============================================================================
// Source Management
// ============================================================================

bool SuwayomiClient::fetchSourceList(std::vector<Source>& sources) {
    // Try GraphQL first (primary API)
    if (fetchSourceListGraphQL(sources)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for sources, falling back to REST...");
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

    brls::Logger::debug("REST: Fetched {} sources", sources.size());
    return true;
}

bool SuwayomiClient::fetchSource(int64_t sourceId, Source& source) {
    // Try GraphQL first (primary API)
    if (fetchSourceGraphQL(sourceId, source)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for source, falling back to REST...");
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
    // Try GraphQL first (primary API)
    if (fetchPopularMangaGraphQL(sourceId, page, manga, hasNextPage)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for popular manga, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    // Use query parameter format like Kodi addon: /source/{id}/popular?pageNum={page}
    std::string url = buildApiUrl("/source/" + std::to_string(sourceId) + "/popular?pageNum=" + std::to_string(page));
    brls::Logger::debug("Fetching popular manga from: {}", url);
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to fetch popular manga: {} ({})", response.error, response.statusCode);
        return false;
    }

    manga.clear();
    hasNextPage = extractJsonBool(response.body, "hasNextPage");

    // Try "mangaList" first (older API), then "mangas" (newer API)
    std::string mangaListJson = extractJsonArray(response.body, "mangaList");
    if (mangaListJson.empty()) {
        mangaListJson = extractJsonArray(response.body, "mangas");
    }

    std::vector<std::string> items = splitJsonArray(mangaListJson);
    for (const auto& item : items) {
        manga.push_back(parseManga(item));
    }

    brls::Logger::debug("REST: Fetched {} popular manga (hasNext: {})", manga.size(), hasNextPage);
    return true;
}

bool SuwayomiClient::fetchLatestManga(int64_t sourceId, int page,
                                       std::vector<Manga>& manga, bool& hasNextPage) {
    // Try GraphQL first (primary API)
    if (fetchLatestMangaGraphQL(sourceId, page, manga, hasNextPage)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for latest manga, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    // Use query parameter format like Kodi addon: /source/{id}/latest?pageNum={page}
    std::string url = buildApiUrl("/source/" + std::to_string(sourceId) + "/latest?pageNum=" + std::to_string(page));
    brls::Logger::debug("Fetching latest manga from: {}", url);
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to fetch latest manga: {} ({})", response.error, response.statusCode);
        return false;
    }

    manga.clear();
    hasNextPage = extractJsonBool(response.body, "hasNextPage");

    // Try "mangaList" first (older API), then "mangas" (newer API)
    std::string mangaListJson = extractJsonArray(response.body, "mangaList");
    if (mangaListJson.empty()) {
        mangaListJson = extractJsonArray(response.body, "mangas");
    }

    std::vector<std::string> items = splitJsonArray(mangaListJson);
    for (const auto& item : items) {
        manga.push_back(parseManga(item));
    }

    brls::Logger::debug("REST: Fetched {} latest manga (hasNext: {})", manga.size(), hasNextPage);
    return true;
}

bool SuwayomiClient::searchManga(int64_t sourceId, const std::string& query, int page,
                                  std::vector<Manga>& manga, bool& hasNextPage) {
    // Try GraphQL first (primary API)
    if (searchMangaGraphQL(sourceId, query, page, manga, hasNextPage)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for search, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string encodedQuery = vitasuwayomi::HttpClient::urlEncode(query);
    std::string url = buildApiUrl("/source/" + std::to_string(sourceId) + "/search?searchTerm=" +
                                   encodedQuery + "&pageNum=" + std::to_string(page));
    brls::Logger::debug("Searching manga from: {}", url);
    vitasuwayomi::HttpResponse response = http.get(url);

    if (!response.success || response.statusCode != 200) {
        brls::Logger::error("Failed to search manga: {} ({})", response.error, response.statusCode);
        return false;
    }

    manga.clear();
    hasNextPage = extractJsonBool(response.body, "hasNextPage");

    // Try "mangaList" first (older API), then "mangas" (newer API)
    std::string mangaListJson = extractJsonArray(response.body, "mangaList");
    if (mangaListJson.empty()) {
        mangaListJson = extractJsonArray(response.body, "mangas");
    }

    std::vector<std::string> items = splitJsonArray(mangaListJson);
    for (const auto& item : items) {
        manga.push_back(parseManga(item));
    }

    brls::Logger::debug("REST: Searched {} manga (hasNext: {})", manga.size(), hasNextPage);
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
    // Try GraphQL first (primary API)
    if (fetchMangaGraphQL(mangaId, manga)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for manga, falling back to REST...");
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
    // Try GraphQL first (primary API)
    if (addMangaToLibraryGraphQL(mangaId)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for add to library, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/manga/" + std::to_string(mangaId) + "/library");
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::removeMangaFromLibrary(int mangaId) {
    // Try GraphQL first (primary API)
    if (removeMangaFromLibraryGraphQL(mangaId)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for remove from library, falling back to REST...");
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
    // Try GraphQL first (primary API)
    if (fetchChaptersGraphQL(mangaId, chapters)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for chapters, falling back to REST...");
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

    brls::Logger::debug("REST: Fetched {} chapters for manga {}", chapters.size(), mangaId);
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

bool SuwayomiClient::markChapterRead(int mangaId, int chapterId) {
    // Try GraphQL first (uses chapter ID correctly)
    if (markChapterReadGraphQL(chapterId, true)) {
        brls::Logger::debug("Marked chapter {} as read via GraphQL", chapterId);
        return true;
    }

    // REST fallback (less reliable with ID vs index confusion)
    brls::Logger::info("GraphQL failed for mark read, trying REST...");
    return updateChapter(mangaId, chapterId, true, false);
}

bool SuwayomiClient::markChapterUnread(int mangaId, int chapterId) {
    // Try GraphQL first (uses chapter ID correctly)
    if (markChapterReadGraphQL(chapterId, false)) {
        brls::Logger::debug("Marked chapter {} as unread via GraphQL", chapterId);
        return true;
    }

    // REST fallback (less reliable with ID vs index confusion)
    brls::Logger::info("GraphQL failed for mark unread, trying REST...");
    return updateChapter(mangaId, chapterId, false, false);
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

bool SuwayomiClient::markAllChaptersRead(int mangaId) {
    // Fetch all chapters, then mark them all read via GraphQL
    std::vector<Chapter> chapters;
    if (!fetchChapters(mangaId, chapters)) return false;

    std::vector<int> chapterIds;
    for (const auto& ch : chapters) {
        if (!ch.read) {
            chapterIds.push_back(ch.id);
        }
    }
    if (chapterIds.empty()) return true;

    // Use GraphQL batch update
    const char* query = R"(
        mutation UpdateChapters($ids: [Int!]!, $isRead: Boolean!) {
            updateChapters(input: { ids: $ids, patch: { isRead: $isRead } }) {
                chapters {
                    id
                    isRead
                }
            }
        }
    )";

    std::string idList = "[";
    for (size_t i = 0; i < chapterIds.size(); i++) {
        if (i > 0) idList += ",";
        idList += std::to_string(chapterIds[i]);
    }
    idList += "]";

    std::string variables = "{\"ids\":" + idList + ",\"isRead\":true}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::markAllChaptersUnread(int mangaId) {
    std::vector<Chapter> chapters;
    if (!fetchChapters(mangaId, chapters)) return false;

    std::vector<int> chapterIds;
    for (const auto& ch : chapters) {
        if (ch.read) {
            chapterIds.push_back(ch.id);
        }
    }
    if (chapterIds.empty()) return true;

    const char* query = R"(
        mutation UpdateChapters($ids: [Int!]!, $isRead: Boolean!) {
            updateChapters(input: { ids: $ids, patch: { isRead: $isRead } }) {
                chapters {
                    id
                    isRead
                }
            }
        }
    )";

    std::string idList = "[";
    for (size_t i = 0; i < chapterIds.size(); i++) {
        if (i > 0) idList += ",";
        idList += std::to_string(chapterIds[i]);
    }
    idList += "]";

    std::string variables = "{\"ids\":" + idList + ",\"isRead\":false}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::updateChapterProgress(int mangaId, int chapterId, int lastPageRead) {
    // Try GraphQL first (uses chapter ID correctly)
    if (updateChapterProgressGraphQL(chapterId, lastPageRead)) {
        brls::Logger::debug("Updated chapter progress via GraphQL: chapter={}, page={}", chapterId, lastPageRead);
        return true;
    }

    // REST fallback - uses /chapter/{id} endpoint with chapter ID
    brls::Logger::info("GraphQL failed for chapter progress, trying REST...");
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    // Use the chapter ID endpoint directly
    std::string url = buildApiUrl("/chapter/" + std::to_string(chapterId));

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

bool SuwayomiClient::fetchChapterPages(int mangaId, int chapterId, std::vector<Page>& pages) {
    // Try GraphQL first (uses chapter ID)
    if (fetchChapterPagesGraphQL(chapterId, pages)) {
        // GraphQL returns relative URLs, convert to full URLs
        for (auto& page : pages) {
            if (!page.imageUrl.empty() && page.imageUrl[0] == '/') {
                page.imageUrl = m_serverUrl + page.imageUrl;
            }
        }
        return true;
    }

    // REST fallback - use chapter ID in URL
    brls::Logger::info("GraphQL failed for chapter pages, trying REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/chapter/" + std::to_string(chapterId));
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
        page.imageUrl = getPageImageUrl(chapterId, i);
        pages.push_back(page);
    }

    brls::Logger::debug("Chapter {} has {} pages", chapterId, pageCount);
    return true;
}

std::string SuwayomiClient::getPageImageUrl(int chapterId, int pageIndex) {
    return buildApiUrl("/chapter/" + std::to_string(chapterId) +
                       "/page/" + std::to_string(pageIndex));
}

// ============================================================================
// Category Management
// ============================================================================

bool SuwayomiClient::fetchCategories(std::vector<Category>& categories) {
    // Try GraphQL first (primary API)
    if (fetchCategoriesGraphQL(categories)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for categories, falling back to REST...");
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

    brls::Logger::debug("REST: Fetched {} categories", categories.size());
    return true;
}

bool SuwayomiClient::createCategory(const std::string& name) {
    // Try GraphQL first (primary API)
    if (createCategoryGraphQL(name)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for createCategory, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/category");
    std::string body = "{\"name\":\"" + name + "\"}";

    vitasuwayomi::HttpResponse response = http.post(url, body);
    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::deleteCategory(int categoryId) {
    // Try GraphQL first (primary API)
    if (deleteCategoryGraphQL(categoryId)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for deleteCategory, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/category/" + std::to_string(categoryId));
    vitasuwayomi::HttpResponse response = http.del(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::updateCategory(int categoryId, const std::string& name, bool isDefault) {
    // Try GraphQL first (primary API)
    if (updateCategoryGraphQL(categoryId, name, isDefault)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for updateCategory, falling back to REST...");
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

bool SuwayomiClient::moveCategoryOrder(int categoryId, int newPosition) {
    // Try GraphQL first (primary API)
    if (updateCategoryOrderGraphQL(categoryId, newPosition)) {
        return true;
    }

    // REST fallback - reorder by fetching all categories and rebuilding the order
    brls::Logger::info("GraphQL failed for moveCategoryOrder, falling back to REST...");

    // Fetch current categories
    std::vector<Category> categories;
    if (!fetchCategories(categories)) {
        return false;
    }

    // Sort by current order
    std::sort(categories.begin(), categories.end(),
              [](const Category& a, const Category& b) { return a.order < b.order; });

    // Find the category to move
    int currentIndex = -1;
    for (size_t i = 0; i < categories.size(); i++) {
        if (categories[i].id == categoryId) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    if (currentIndex < 0) {
        brls::Logger::error("moveCategoryOrder: Category {} not found", categoryId);
        return false;
    }

    // Clamp newPosition
    newPosition = std::max(0, std::min(newPosition, static_cast<int>(categories.size()) - 1));

    if (currentIndex == newPosition) {
        return true;  // Already in position
    }

    // Move the category
    Category cat = categories[currentIndex];
    categories.erase(categories.begin() + currentIndex);
    categories.insert(categories.begin() + newPosition, cat);

    // Build new order
    std::vector<int> newOrder;
    for (const auto& c : categories) {
        newOrder.push_back(c.id);
    }

    return reorderCategories(newOrder);
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
    // Try GraphQL first (primary API)
    if (fetchCategoryMangaGraphQL(categoryId, manga)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for category manga, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

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
    // Try GraphQL first (primary API)
    if (fetchLibraryMangaGraphQL(manga)) {
        return true;
    }

    // REST fallback - Default category (0) contains all library manga
    brls::Logger::info("GraphQL failed for library, falling back to REST...");
    return fetchCategoryManga(0, manga);
}

bool SuwayomiClient::fetchLibraryMangaByCategory(int categoryId, std::vector<Manga>& manga) {
    return fetchCategoryManga(categoryId, manga);
}

bool SuwayomiClient::triggerLibraryUpdate() {
    // Try GraphQL first (primary API)
    if (triggerLibraryUpdateGraphQL()) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for triggerLibraryUpdate, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();
    http.setDefaultHeader("Content-Type", "application/json");

    std::string url = buildApiUrl("/update/fetch");
    vitasuwayomi::HttpResponse response = http.post(url, "{}");

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::triggerLibraryUpdate(int categoryId) {
    // Try GraphQL first (primary API)
    if (triggerCategoryUpdateGraphQL(categoryId)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for triggerCategoryUpdate, falling back to REST...");
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
// Reading History (Continue Reading)
// ============================================================================

bool SuwayomiClient::fetchReadingHistory(int offset, int limit, std::vector<ReadingHistoryItem>& history) {
    // Try GraphQL first (primary API)
    if (fetchReadingHistoryGraphQL(offset, limit, history)) {
        return true;
    }

    // REST fallback - not implemented in REST API, return empty
    brls::Logger::warning("Reading history not available via REST API");
    history.clear();
    return false;
}

bool SuwayomiClient::fetchReadingHistory(std::vector<ReadingHistoryItem>& history) {
    return fetchReadingHistory(0, 50, history);
}

// ============================================================================
// Global Search
// ============================================================================

bool SuwayomiClient::globalSearch(const std::string& query, std::vector<GlobalSearchResult>& results) {
    // Try GraphQL first (primary API)
    if (globalSearchGraphQL(query, results)) {
        return true;
    }

    // REST fallback - not available, return empty
    brls::Logger::warning("Global search not available via REST API");
    results.clear();
    return false;
}

bool SuwayomiClient::globalSearch(const std::string& query, const std::vector<int64_t>& sourceIds,
                                  std::vector<GlobalSearchResult>& results) {
    // For now, just use the basic global search
    // TODO: Implement source-filtered search if needed
    return globalSearch(query, results);
}

// ============================================================================
// Set Manga Categories
// ============================================================================

bool SuwayomiClient::setMangaCategories(int mangaId, const std::vector<int>& categoryIds) {
    // Try GraphQL first (primary API)
    if (setMangaCategoriesGraphQL(mangaId, categoryIds)) {
        return true;
    }

    // REST fallback - use individual add/remove operations
    brls::Logger::info("GraphQL failed for setMangaCategories, falling back to REST...");

    // First, fetch current categories
    std::vector<Category> allCategories;
    if (!fetchCategories(allCategories)) {
        return false;
    }

    // Remove from all categories first, then add to specified ones
    for (const auto& cat : allCategories) {
        removeMangaFromCategory(mangaId, cat.id);
    }

    // Add to specified categories
    for (int catId : categoryIds) {
        addMangaToCategory(mangaId, catId);
    }

    return true;
}

// ============================================================================
// Download Management
// ============================================================================

bool SuwayomiClient::queueChapterDownload(int chapterId, int mangaId, int chapterIndex) {
    // Try GraphQL first (primary API)
    if (enqueueChapterDownloadGraphQL(chapterId)) {
        brls::Logger::info("SuwayomiClient: Queued chapter {} via GraphQL", chapterId);
        return true;
    }

    // REST fallback
    brls::Logger::info("SuwayomiClient: GraphQL failed for queue download, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/download/" + std::to_string(mangaId) +
                                   "/chapter/" + std::to_string(chapterIndex));
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::deleteChapterDownload(int chapterId, int mangaId, int chapterIndex) {
    // Try GraphQL first (primary API)
    if (dequeueChapterDownloadGraphQL(chapterId)) {
        brls::Logger::info("SuwayomiClient: Dequeued chapter {} via GraphQL", chapterId);
        return true;
    }

    // REST fallback
    brls::Logger::info("SuwayomiClient: GraphQL failed for dequeue download, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/download/" + std::to_string(mangaId) +
                                   "/chapter/" + std::to_string(chapterIndex));
    vitasuwayomi::HttpResponse response = http.del(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::queueChapterDownloads(const std::vector<int>& chapterIds) {
    // Try GraphQL first (primary API)
    brls::Logger::info("SuwayomiClient: Queueing {} chapters for download via GraphQL", chapterIds.size());
    if (enqueueChapterDownloadsGraphQL(chapterIds)) {
        return true;
    }

    // REST fallback - need to queue each chapter individually
    // Note: REST API requires mangaId/chapterIndex which we don't have here,
    // but we can try to fetch chapter info and queue individually
    brls::Logger::info("SuwayomiClient: GraphQL batch failed, trying individual REST fallback...");

    vitasuwayomi::HttpClient http = createHttpClient();
    int successCount = 0;

    for (int chapterId : chapterIds) {
        // Try to get chapter info to build REST URL
        // For REST we need: GET /download/{mangaId}/chapter/{chapterIndex}
        // Since we only have chapterId, we need to fetch chapter details first
        std::string chapterQuery = R"(
            query GetChapter($id: Int!) {
                chapter(id: $id) {
                    sourceOrder
                    manga { id }
                }
            }
        )";
        std::string vars = "{\"id\":" + std::to_string(chapterId) + "}";
        std::string response = executeGraphQL(chapterQuery, vars);

        if (!response.empty()) {
            std::string data = extractJsonObject(response, "data");
            std::string chapterJson = extractJsonObject(data, "chapter");
            int chapterIndex = extractJsonInt(chapterJson, "sourceOrder");
            std::string mangaJson = extractJsonObject(chapterJson, "manga");
            int mangaId = extractJsonInt(mangaJson, "id");

            if (mangaId > 0) {
                std::string url = buildApiUrl("/download/" + std::to_string(mangaId) +
                                               "/chapter/" + std::to_string(chapterIndex));
                vitasuwayomi::HttpResponse resp = http.get(url);
                if (resp.success && resp.statusCode == 200) {
                    successCount++;
                }
            }
        }
    }

    brls::Logger::info("SuwayomiClient: REST fallback queued {}/{} chapters", successCount, chapterIds.size());
    return successCount == static_cast<int>(chapterIds.size());
}

bool SuwayomiClient::deleteChapterDownloads(const std::vector<int>& chapterIds, int mangaId, const std::vector<int>& chapterIndexes) {
    // Try GraphQL first (primary API)
    brls::Logger::info("SuwayomiClient: Dequeuing {} chapters via GraphQL", chapterIds.size());
    if (dequeueChapterDownloadsGraphQL(chapterIds)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("SuwayomiClient: GraphQL batch dequeue failed, falling back to REST...");

    // If we have mangaId and chapterIndexes for REST fallback
    if (mangaId > 0 && !chapterIndexes.empty()) {
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

    // If no REST fallback info provided, try to dequeue individually using single dequeue
    brls::Logger::info("SuwayomiClient: Trying individual dequeue fallback...");
    int successCount = 0;
    for (int chapterId : chapterIds) {
        if (dequeueChapterDownloadGraphQL(chapterId)) {
            successCount++;
        }
    }

    brls::Logger::info("SuwayomiClient: Individual dequeue: {}/{} succeeded", successCount, chapterIds.size());
    return successCount == static_cast<int>(chapterIds.size());
}

bool SuwayomiClient::fetchDownloadQueue(std::vector<DownloadQueueItem>& queue) {
    queue.clear();

    // Use GraphQL to fetch download queue
    const char* graphqlQuery = R"(
        query {
            downloadStatus {
                state
                queue {
                    chapter {
                        id
                        name
                        chapterNumber
                        pageCount
                        manga {
                            id
                            title
                        }
                    }
                    progress
                    state
                    tries
                }
            }
        }
    )";

    std::string response = executeGraphQL(graphqlQuery);
    if (response.empty()) {
        brls::Logger::error("SuwayomiClient: Failed to fetch download queue via GraphQL");
        return false;
    }

    // Parse the queue array from response
    std::string queueArray = extractJsonArray(response, "queue");
    if (queueArray.empty() || queueArray == "[]") {
        brls::Logger::debug("SuwayomiClient: Download queue is empty");
        return true;  // Empty queue is valid
    }

    // Parse each queue item
    size_t pos = 0;
    int braceCount = 0;
    size_t itemStart = 0;
    bool inItem = false;

    for (size_t i = 0; i < queueArray.length(); i++) {
        char c = queueArray[i];
        if (c == '{') {
            if (!inItem) {
                itemStart = i;
                inItem = true;
            }
            braceCount++;
        } else if (c == '}') {
            braceCount--;
            if (braceCount == 0 && inItem) {
                std::string itemJson = queueArray.substr(itemStart, i - itemStart + 1);

                DownloadQueueItem item;

                // Extract chapter info
                std::string chapterJson = extractJsonObject(itemJson, "chapter");
                if (!chapterJson.empty()) {
                    item.chapterId = extractJsonInt(chapterJson, "id");
                    item.chapterName = extractJsonValue(chapterJson, "name");
                    item.chapterNumber = extractJsonFloat(chapterJson, "chapterNumber");
                    item.pageCount = extractJsonInt(chapterJson, "pageCount");

                    // Extract manga info
                    std::string mangaJson = extractJsonObject(chapterJson, "manga");
                    if (!mangaJson.empty()) {
                        item.mangaId = extractJsonInt(mangaJson, "id");
                        item.mangaTitle = extractJsonValue(mangaJson, "title");
                    }
                }

                // Extract progress and state
                item.progress = extractJsonFloat(itemJson, "progress");
                item.downloadedPages = static_cast<int>(item.progress * item.pageCount);

                std::string stateStr = extractJsonValue(itemJson, "state");
                if (stateStr == "DOWNLOADING") {
                    item.state = DownloadState::DOWNLOADING;
                } else if (stateStr == "QUEUED") {
                    item.state = DownloadState::QUEUED;
                } else if (stateStr == "FINISHED") {
                    item.state = DownloadState::DOWNLOADED;
                } else if (stateStr == "ERROR") {
                    item.state = DownloadState::ERROR;
                }

                queue.push_back(item);
                inItem = false;
            }
        }
    }

    brls::Logger::info("SuwayomiClient: Fetched {} items in download queue", queue.size());
    return true;
}

bool SuwayomiClient::startDownloads() {
    // Try GraphQL first (primary API)
    if (startDownloadsGraphQL()) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for start downloads, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/downloads/start");
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::stopDownloads() {
    // Try GraphQL first (primary API)
    if (stopDownloadsGraphQL()) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for stop downloads, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/downloads/stop");
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::clearDownloadQueue() {
    // Try GraphQL first (primary API)
    if (clearDownloadQueueGraphQL()) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for clear downloads, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/downloads/clear");
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::reorderDownload(int chapterId, int mangaId, int chapterIndex, int newPosition) {
    // Try GraphQL first (primary API)
    if (reorderChapterDownloadGraphQL(chapterId, newPosition)) {
        brls::Logger::info("SuwayomiClient: Reordered chapter {} via GraphQL", chapterId);
        return true;
    }

    // REST fallback
    brls::Logger::info("SuwayomiClient: GraphQL failed for reorder, falling back to REST...");
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
        brls::Logger::error("SuwayomiClient: backup export failed - status {}", response.statusCode);
        return false;
    }

    if (response.body.empty()) {
        brls::Logger::error("SuwayomiClient: backup response is empty");
        return false;
    }

    // Save response body to file
#ifdef __vita__
    SceUID fd = sceIoOpen(savePath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        brls::Logger::error("SuwayomiClient: failed to open backup file for writing: {}", savePath);
        return false;
    }

    sceIoWrite(fd, response.body.c_str(), response.body.size());
    sceIoClose(fd);
#else
    // Non-Vita platforms: use standard C++ file operations
    std::ofstream outFile(savePath, std::ios::binary);
    if (!outFile.is_open()) {
        brls::Logger::error("SuwayomiClient: failed to open backup file for writing: {}", savePath);
        return false;
    }
    outFile.write(response.body.c_str(), response.body.size());
    outFile.close();
#endif

    brls::Logger::info("SuwayomiClient: backup saved to {}", savePath);
    return true;
}

bool SuwayomiClient::importBackup(const std::string& filePath) {
    // Read the backup file
    std::string fileContent;

#ifdef __vita__
    SceUID fd = sceIoOpen(filePath.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        brls::Logger::error("SuwayomiClient: failed to open backup file for reading: {}", filePath);
        return false;
    }

    // Get file size
    SceOff fileSize = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    if (fileSize <= 0 || fileSize > 50 * 1024 * 1024) { // Max 50MB
        sceIoClose(fd);
        brls::Logger::error("SuwayomiClient: backup file size invalid: {}", fileSize);
        return false;
    }

    fileContent.resize(static_cast<size_t>(fileSize));
    sceIoRead(fd, &fileContent[0], static_cast<SceSize>(fileSize));
    sceIoClose(fd);
#else
    // Non-Vita platforms: use standard C++ file operations
    std::ifstream inFile(filePath, std::ios::binary);
    if (!inFile.is_open()) {
        brls::Logger::error("SuwayomiClient: failed to open backup file for reading: {}", filePath);
        return false;
    }
    std::stringstream buffer;
    buffer << inFile.rdbuf();
    fileContent = buffer.str();
    inFile.close();
#endif

    if (fileContent.empty()) {
        brls::Logger::error("SuwayomiClient: backup file is empty");
        return false;
    }

    // Upload backup to server via multipart form
    vitasuwayomi::HttpClient http = createHttpClient();
    std::string url = buildApiUrl("/backup/import/file");

    // Build multipart form data
    std::string boundary = "----VitaSuwayomiBackup" + std::to_string(time(nullptr));
    std::string body;
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"backup.proto.gz\"; filename=\"backup.proto.gz\"\r\n";
    body += "Content-Type: application/octet-stream\r\n\r\n";
    body += fileContent;
    body += "\r\n--" + boundary + "--\r\n";

    vitasuwayomi::HttpRequest request;
    request.url = url;
    request.method = "POST";
    request.body = body;
    request.headers["Content-Type"] = "multipart/form-data; boundary=" + boundary;

    vitasuwayomi::HttpResponse response = http.request(request);

    if (!response.success || (response.statusCode != 200 && response.statusCode != 201)) {
        brls::Logger::error("SuwayomiClient: backup import failed - status {}", response.statusCode);
        return false;
    }

    brls::Logger::info("SuwayomiClient: backup imported successfully");
    return true;
}

bool SuwayomiClient::validateBackup(const std::string& filePath) {
    // Check if file exists and is readable
#ifdef __vita__
    SceUID fd = sceIoOpen(filePath.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }

    // Get file size - backup should be at least a few bytes
    SceOff fileSize = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoClose(fd);

    return fileSize > 10; // Basic validation - file exists and has content
#else
    std::ifstream inFile(filePath, std::ios::binary);
    if (!inFile.is_open()) {
        return false;
    }
    inFile.seekg(0, std::ios::end);
    auto size = inFile.tellg();
    inFile.close();
    return size > 10;
#endif
}

// ============================================================================
// Tracking
// ============================================================================

Tracker SuwayomiClient::parseTrackerFromGraphQL(const std::string& json) {
    Tracker tracker;

    tracker.id = extractJsonInt(json, "id");
    tracker.name = extractJsonValue(json, "name");
    tracker.iconUrl = extractJsonValue(json, "icon");
    tracker.isLoggedIn = extractJsonBool(json, "isLoggedIn");
    tracker.isTokenExpired = extractJsonBool(json, "isTokenExpired");
    tracker.supportsTrackDeletion = extractJsonBool(json, "supportsTrackDeletion");

    // Parse statuses array - use 'name' for display text (not 'value' which is numeric)
    std::string statusesJson = extractJsonArray(json, "statuses");
    if (!statusesJson.empty()) {
        std::vector<std::string> statusItems = splitJsonArray(statusesJson);
        for (const auto& item : statusItems) {
            // Prefer 'name' (display text) over 'value' (numeric status code)
            std::string name = extractJsonValue(item, "name");
            if (!name.empty()) {
                tracker.statuses.push_back(name);
            } else {
                // Fallback to value if name is not present
                std::string value = extractJsonValue(item, "value");
                if (!value.empty()) {
                    tracker.statuses.push_back(value);
                }
            }
        }
    }

    // Parse scores array
    std::string scoresJson = extractJsonArray(json, "scores");
    if (!scoresJson.empty()) {
        std::vector<std::string> scoreItems = splitJsonArray(scoresJson);
        for (const auto& item : scoreItems) {
            // Scores are usually just strings in the array
            std::string trimmed = item;
            // Remove quotes if present
            if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
                trimmed = trimmed.substr(1, trimmed.size() - 2);
            }
            if (!trimmed.empty()) {
                tracker.scores.push_back(trimmed);
            }
        }
    }

    return tracker;
}

TrackRecord SuwayomiClient::parseTrackRecordFromGraphQL(const std::string& json) {
    TrackRecord record;

    record.id = extractJsonInt(json, "id");
    record.mangaId = extractJsonInt(json, "mangaId");
    record.trackerId = extractJsonInt(json, "trackerId");
    record.remoteId = extractJsonInt64(json, "remoteId");
    record.remoteUrl = extractJsonValue(json, "remoteUrl");
    record.title = extractJsonValue(json, "title");
    record.lastChapterRead = extractJsonFloat(json, "lastChapterRead");
    record.totalChapters = extractJsonInt(json, "totalChapters");
    record.score = extractJsonFloat(json, "score");
    record.status = extractJsonInt(json, "status");
    record.displayScore = extractJsonValue(json, "displayScore");
    record.startDate = extractJsonInt64(json, "startDate");
    record.finishDate = extractJsonInt64(json, "finishDate");

    // Get tracker name from nested tracker object if available
    std::string trackerObj = extractJsonObject(json, "tracker");
    if (!trackerObj.empty()) {
        record.trackerName = extractJsonValue(trackerObj, "name");
    }

    return record;
}

TrackSearchResult SuwayomiClient::parseTrackSearchResultFromGraphQL(const std::string& json) {
    TrackSearchResult result;

    result.remoteId = extractJsonInt64(json, "remoteId");
    result.title = extractJsonValue(json, "title");
    result.coverUrl = extractJsonValue(json, "coverUrl");
    result.summary = extractJsonValue(json, "summary");
    result.publishingStatus = extractJsonValue(json, "publishingStatus");
    result.publishingType = extractJsonValue(json, "publishingType");
    result.startDate = extractJsonValue(json, "startDate");  // String from API
    result.totalChapters = extractJsonInt(json, "totalChapters");

    return result;
}

bool SuwayomiClient::fetchTrackersGraphQL(std::vector<Tracker>& trackers) {
    const char* query = R"(
        query GetTrackers {
            trackers {
                nodes {
                    id
                    name
                    icon
                    isLoggedIn
                    isTokenExpired
                    supportsTrackDeletion
                    statuses {
                        name
                        value
                    }
                    scores
                }
            }
        }
    )";

    std::string response = executeGraphQL(query);
    if (response.empty()) {
        brls::Logger::error("GraphQL: fetchTrackers - empty response");
        return false;
    }

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) {
        brls::Logger::error("GraphQL: fetchTrackers - no data in response");
        return false;
    }

    std::string trackersObj = extractJsonObject(data, "trackers");
    if (trackersObj.empty()) {
        brls::Logger::error("GraphQL: fetchTrackers - no trackers object");
        return false;
    }

    std::string nodesJson = extractJsonArray(trackersObj, "nodes");
    if (nodesJson.empty()) {
        brls::Logger::debug("GraphQL: fetchTrackers - no tracker nodes (may be empty)");
        trackers.clear();
        return true;
    }

    trackers.clear();
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        trackers.push_back(parseTrackerFromGraphQL(item));
    }

    brls::Logger::info("GraphQL: Fetched {} trackers", trackers.size());
    return true;
}

bool SuwayomiClient::fetchTrackers(std::vector<Tracker>& trackers) {
    return fetchTrackersGraphQL(trackers);
}

bool SuwayomiClient::fetchTrackerGraphQL(int trackerId, Tracker& tracker) {
    const char* query = R"(
        query GetTracker($id: Int!) {
            tracker(id: $id) {
                id
                name
                icon
                isLoggedIn
                isTokenExpired
                supportsTrackDeletion
                statuses {
                    name
                    value
                }
                scores
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(trackerId) + "}";
    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string trackerObj = extractJsonObject(data, "tracker");
    if (trackerObj.empty()) return false;

    tracker = parseTrackerFromGraphQL(trackerObj);
    return true;
}

bool SuwayomiClient::fetchTracker(int trackerId, Tracker& tracker) {
    return fetchTrackerGraphQL(trackerId, tracker);
}

bool SuwayomiClient::fetchMangaTrackingGraphQL(int mangaId, std::vector<TrackRecord>& records) {
    const char* query = R"(
        query GetMangaTracking($mangaId: Int!) {
            trackRecords(condition: { mangaId: $mangaId }) {
                nodes {
                    id
                    mangaId
                    trackerId
                    remoteId
                    remoteUrl
                    title
                    lastChapterRead
                    totalChapters
                    score
                    status
                    displayScore
                    startDate
                    finishDate
                    tracker {
                        id
                        name
                        icon
                        isLoggedIn
                        statuses {
                            name
                            value
                        }
                        scores
                    }
                }
            }
        }
    )";

    std::string variables = "{\"mangaId\":" + std::to_string(mangaId) + "}";
    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("GraphQL: fetchMangaTracking - empty response");
        return false;
    }

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) {
        brls::Logger::error("GraphQL: fetchMangaTracking - no data");
        return false;
    }

    std::string trackRecordsObj = extractJsonObject(data, "trackRecords");
    if (trackRecordsObj.empty()) {
        records.clear();
        return true;
    }

    std::string nodesJson = extractJsonArray(trackRecordsObj, "nodes");
    if (nodesJson.empty()) {
        records.clear();
        return true;
    }

    records.clear();
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        records.push_back(parseTrackRecordFromGraphQL(item));
    }

    brls::Logger::info("GraphQL: Fetched {} track records for manga {}", records.size(), mangaId);
    return true;
}

bool SuwayomiClient::fetchMangaTracking(int mangaId, std::vector<TrackRecord>& records) {
    return fetchMangaTrackingGraphQL(mangaId, records);
}

bool SuwayomiClient::searchTrackerGraphQL(int trackerId, const std::string& query, std::vector<TrackSearchResult>& results) {
    const char* gqlQuery = R"(
        query SearchTracker($trackerId: Int!, $query: String!) {
            searchTracker(input: { trackerId: $trackerId, query: $query }) {
                trackSearches {
                    remoteId
                    title
                    coverUrl
                    summary
                    publishingStatus
                    publishingType
                    startDate
                    totalChapters
                }
            }
        }
    )";

    // Escape query string
    std::string escapedQuery;
    for (char c : query) {
        switch (c) {
            case '"': escapedQuery += "\\\""; break;
            case '\\': escapedQuery += "\\\\"; break;
            case '\n': escapedQuery += "\\n"; break;
            case '\r': escapedQuery += "\\r"; break;
            case '\t': escapedQuery += "\\t"; break;
            default: escapedQuery += c; break;
        }
    }

    std::string variables = "{\"trackerId\":" + std::to_string(trackerId) +
                           ",\"query\":\"" + escapedQuery + "\"}";

    std::string response = executeGraphQL(gqlQuery, variables);
    if (response.empty()) {
        brls::Logger::error("GraphQL: searchTracker - empty response");
        return false;
    }

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) {
        brls::Logger::error("GraphQL: searchTracker - no data");
        return false;
    }

    std::string searchResult = extractJsonObject(data, "searchTracker");
    if (searchResult.empty()) {
        results.clear();
        return true;
    }

    std::string searchesJson = extractJsonArray(searchResult, "trackSearches");
    if (searchesJson.empty()) {
        results.clear();
        return true;
    }

    results.clear();
    std::vector<std::string> items = splitJsonArray(searchesJson);
    for (const auto& item : items) {
        results.push_back(parseTrackSearchResultFromGraphQL(item));
    }

    brls::Logger::info("GraphQL: Found {} tracker search results", results.size());
    return true;
}

bool SuwayomiClient::searchTracker(int trackerId, const std::string& query, std::vector<TrackSearchResult>& results) {
    return searchTrackerGraphQL(trackerId, query, results);
}

bool SuwayomiClient::bindTrackerGraphQL(int mangaId, int trackerId, int64_t remoteId) {
    const char* query = R"(
        mutation BindTracker($mangaId: Int!, $trackerId: Int!, $remoteId: LongString!) {
            bindTrack(input: { mangaId: $mangaId, trackerId: $trackerId, remoteId: $remoteId }) {
                trackRecord {
                    id
                    mangaId
                    trackerId
                    remoteId
                    title
                    lastChapterRead
                    status
                }
            }
        }
    )";

    std::string variables = "{\"mangaId\":" + std::to_string(mangaId) +
                           ",\"trackerId\":" + std::to_string(trackerId) +
                           ",\"remoteId\":\"" + std::to_string(remoteId) + "\"}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("GraphQL: bindTracker - empty response");
        return false;
    }

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) {
        // Check for errors
        std::string errors = extractJsonArray(response, "errors");
        if (!errors.empty()) {
            brls::Logger::error("GraphQL: bindTracker - error: {}", errors);
        }
        return false;
    }

    std::string bindResult = extractJsonObject(data, "bindTrack");
    if (bindResult.empty()) return false;

    brls::Logger::info("GraphQL: Successfully bound tracker {} to manga {}", trackerId, mangaId);
    return true;
}

bool SuwayomiClient::bindTracker(int mangaId, int trackerId, int64_t remoteId) {
    return bindTrackerGraphQL(mangaId, trackerId, remoteId);
}

// Legacy overload
bool SuwayomiClient::bindTracker(int mangaId, int trackerId, int remoteId) {
    return bindTrackerGraphQL(mangaId, trackerId, static_cast<int64_t>(remoteId));
}

bool SuwayomiClient::unbindTrackerGraphQL(int recordId, bool deleteRemoteTrack) {
    const char* query = R"(
        mutation UnbindTracker($recordId: Int!, $deleteRemote: Boolean) {
            unbindTrack(input: { recordId: $recordId, deleteRemoteTrack: $deleteRemote }) {
                trackRecord {
                    id
                }
            }
        }
    )";

    std::string variables = "{\"recordId\":" + std::to_string(recordId) +
                           ",\"deleteRemote\":" + (deleteRemoteTrack ? "true" : "false") + "}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("GraphQL: unbindTracker - empty response");
        return false;
    }

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    brls::Logger::info("GraphQL: Successfully unbound track record {}", recordId);
    return true;
}

bool SuwayomiClient::unbindTracker(int recordId, bool deleteRemoteTrack) {
    return unbindTrackerGraphQL(recordId, deleteRemoteTrack);
}

bool SuwayomiClient::updateTrackRecordGraphQL(int recordId, int status, double lastChapterRead,
                                              const std::string& scoreString, int64_t startDate, int64_t finishDate) {
    // Build optional parameters
    std::string optionalParams;

    if (status >= 0) {
        optionalParams += ", status: " + std::to_string(status);
    }
    if (lastChapterRead >= 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", lastChapterRead);
        optionalParams += ", lastChapterRead: " + std::string(buf);
    }
    if (!scoreString.empty()) {
        optionalParams += ", scoreString: \"" + scoreString + "\"";
    }
    if (startDate >= 0) {
        optionalParams += ", startDate: " + std::to_string(startDate);
    }
    if (finishDate >= 0) {
        optionalParams += ", finishDate: " + std::to_string(finishDate);
    }

    std::string query = R"(
        mutation UpdateTrack($recordId: Int!) {
            updateTrack(input: { recordId: $recordId)" + optionalParams + R"( }) {
                trackRecord {
                    id
                    status
                    lastChapterRead
                    score
                    displayScore
                    startDate
                    finishDate
                }
            }
        }
    )";

    std::string variables = "{\"recordId\":" + std::to_string(recordId) + "}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("GraphQL: updateTrackRecord - empty response");
        return false;
    }

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) {
        std::string errors = extractJsonArray(response, "errors");
        if (!errors.empty()) {
            brls::Logger::error("GraphQL: updateTrackRecord - error: {}", errors);
        }
        return false;
    }

    brls::Logger::info("GraphQL: Successfully updated track record {}", recordId);
    return true;
}

bool SuwayomiClient::updateTrackRecord(int recordId, int status, double lastChapterRead,
                                       const std::string& scoreString, int64_t startDate, int64_t finishDate) {
    return updateTrackRecordGraphQL(recordId, status, lastChapterRead, scoreString, startDate, finishDate);
}

// Legacy compatibility method
bool SuwayomiClient::updateTracking(int mangaId, int trackerId, const TrackRecord& record) {
    // First fetch the actual record ID for this manga/tracker combination
    std::vector<TrackRecord> records;
    if (!fetchMangaTracking(mangaId, records)) {
        return false;
    }

    for (const auto& r : records) {
        if (r.trackerId == trackerId) {
            return updateTrackRecord(r.id, record.status, record.lastChapterRead,
                                    record.displayScore, record.startDate, record.finishDate);
        }
    }
    return false;
}

bool SuwayomiClient::loginTrackerCredentialsGraphQL(int trackerId, const std::string& username, const std::string& password) {
    const char* query = R"(
        mutation LoginTracker($trackerId: Int!, $username: String!, $password: String!) {
            loginTrackerCredentials(input: { trackerId: $trackerId, username: $username, password: $password }) {
                isLoggedIn
                tracker {
                    id
                    name
                    isLoggedIn
                }
            }
        }
    )";

    // Escape credentials
    std::string escapedUser, escapedPass;
    for (char c : username) {
        switch (c) {
            case '"': escapedUser += "\\\""; break;
            case '\\': escapedUser += "\\\\"; break;
            default: escapedUser += c; break;
        }
    }
    for (char c : password) {
        switch (c) {
            case '"': escapedPass += "\\\""; break;
            case '\\': escapedPass += "\\\\"; break;
            default: escapedPass += c; break;
        }
    }

    std::string variables = "{\"trackerId\":" + std::to_string(trackerId) +
                           ",\"username\":\"" + escapedUser + "\"" +
                           ",\"password\":\"" + escapedPass + "\"}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("GraphQL: loginTrackerCredentials - empty response");
        return false;
    }

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) {
        std::string errors = extractJsonArray(response, "errors");
        if (!errors.empty()) {
            brls::Logger::error("GraphQL: loginTrackerCredentials - error: {}", errors);
        }
        return false;
    }

    std::string loginResult = extractJsonObject(data, "loginTrackerCredentials");
    if (loginResult.empty()) return false;

    bool isLoggedIn = extractJsonBool(loginResult, "isLoggedIn");
    brls::Logger::info("GraphQL: Tracker login result: {}", isLoggedIn ? "success" : "failed");
    return isLoggedIn;
}

bool SuwayomiClient::loginTrackerCredentials(int trackerId, const std::string& username, const std::string& password) {
    return loginTrackerCredentialsGraphQL(trackerId, username, password);
}

// Legacy compatibility
bool SuwayomiClient::loginTracker(int trackerId, const std::string& username, const std::string& password) {
    return loginTrackerCredentials(trackerId, username, password);
}

bool SuwayomiClient::loginTrackerOAuth(int trackerId, const std::string& callbackUrl, std::string& oauthUrl) {
    const char* query = R"(
        mutation LoginTrackerOAuth($trackerId: Int!, $callbackUrl: String!) {
            loginTrackerOAuth(input: { trackerId: $trackerId, callbackUrl: $callbackUrl }) {
                isLoggedIn
                tracker {
                    id
                    name
                    isLoggedIn
                }
            }
        }
    )";

    std::string variables = "{\"trackerId\":" + std::to_string(trackerId) +
                           ",\"callbackUrl\":\"" + callbackUrl + "\"}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    // OAuth typically requires opening a browser, which we can't do on Vita
    // This method is here for API completeness but may not be usable
    brls::Logger::warning("OAuth login not supported on this platform");
    return false;
}

bool SuwayomiClient::logoutTrackerGraphQL(int trackerId) {
    const char* query = R"(
        mutation LogoutTracker($trackerId: Int!) {
            logoutTracker(input: { trackerId: $trackerId }) {
                isLoggedIn
                tracker {
                    id
                    name
                    isLoggedIn
                }
            }
        }
    )";

    std::string variables = "{\"trackerId\":" + std::to_string(trackerId) + "}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("GraphQL: logoutTracker - empty response");
        return false;
    }

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    brls::Logger::info("GraphQL: Successfully logged out from tracker {}", trackerId);
    return true;
}

bool SuwayomiClient::logoutTracker(int trackerId) {
    return logoutTrackerGraphQL(trackerId);
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

// ============================================================================
// GraphQL Extension Operations
// ============================================================================

Extension SuwayomiClient::parseExtensionFromGraphQL(const std::string& json) {
    Extension ext;

    ext.pkgName = extractJsonValue(json, "pkgName");
    ext.name = extractJsonValue(json, "name");
    ext.lang = extractJsonValue(json, "lang");
    ext.versionName = extractJsonValue(json, "versionName");
    ext.versionCode = extractJsonInt(json, "versionCode");
    ext.iconUrl = extractJsonValue(json, "iconUrl");
    ext.installed = extractJsonBool(json, "isInstalled");
    ext.hasUpdate = extractJsonBool(json, "hasUpdate");
    ext.obsolete = extractJsonBool(json, "isObsolete");
    ext.isNsfw = extractJsonBool(json, "isNsfw");

    return ext;
}

bool SuwayomiClient::fetchExtensionListGraphQL(std::vector<Extension>& extensions) {
    // Include source.nodes with isConfigurable to determine if settings icon should be shown
    const char* query = R"(
        query {
            extensions {
                nodes {
                    pkgName
                    name
                    lang
                    versionName
                    versionCode
                    iconUrl
                    isInstalled
                    hasUpdate
                    isObsolete
                    isNsfw
                    repo
                    source {
                        nodes {
                            isConfigurable
                        }
                    }
                }
            }
        }
    )";

    std::string response = executeGraphQL(query);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string extensionsObj = extractJsonObject(data, "extensions");
    if (extensionsObj.empty()) return false;

    std::string nodesJson = extractJsonArray(extensionsObj, "nodes");
    if (nodesJson.empty()) return false;

    extensions.clear();
    std::set<std::string> seenPkgNames;
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        Extension ext = parseExtensionFromGraphQL(item);

        // Check if any source is configurable (for settings icon)
        std::string sourceObj = extractJsonObject(item, "source");
        if (!sourceObj.empty()) {
            std::string sourceNodes = extractJsonArray(sourceObj, "nodes");
            if (!sourceNodes.empty()) {
                std::vector<std::string> sources = splitJsonArray(sourceNodes);
                for (const auto& srcJson : sources) {
                    if (extractJsonBool(srcJson, "isConfigurable")) {
                        ext.hasConfigurableSources = true;
                        break;
                    }
                }
            }
        }

        // Skip duplicates based on pkgName
        if (seenPkgNames.find(ext.pkgName) == seenPkgNames.end()) {
            seenPkgNames.insert(ext.pkgName);
            extensions.push_back(ext);
        }
    }

    brls::Logger::debug("GraphQL: Fetched {} extensions (deduplicated)", extensions.size());
    return true;
}

bool SuwayomiClient::fetchInstalledExtensionsGraphQL(std::vector<Extension>& extensions) {
    // Server-side filtered query for installed extensions only
    // Include source.nodes with isConfigurable to determine if settings icon should be shown
    const char* query = R"(
        query {
            extensions(condition: { isInstalled: true }) {
                nodes {
                    pkgName
                    name
                    lang
                    versionName
                    versionCode
                    iconUrl
                    isInstalled
                    hasUpdate
                    isObsolete
                    isNsfw
                    repo
                    source {
                        nodes {
                            isConfigurable
                        }
                    }
                }
            }
        }
    )";

    std::string response = executeGraphQL(query);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string extensionsObj = extractJsonObject(data, "extensions");
    if (extensionsObj.empty()) return false;

    std::string nodesJson = extractJsonArray(extensionsObj, "nodes");
    if (nodesJson.empty()) return false;

    extensions.clear();
    std::set<std::string> seenPkgNames;
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        Extension ext = parseExtensionFromGraphQL(item);

        // Check if any source is configurable
        std::string sourceObj = extractJsonObject(item, "source");
        if (!sourceObj.empty()) {
            std::string sourceNodes = extractJsonArray(sourceObj, "nodes");
            if (!sourceNodes.empty()) {
                std::vector<std::string> sources = splitJsonArray(sourceNodes);
                for (const auto& srcJson : sources) {
                    if (extractJsonBool(srcJson, "isConfigurable")) {
                        ext.hasConfigurableSources = true;
                        break;
                    }
                }
            }
        }

        if (seenPkgNames.find(ext.pkgName) == seenPkgNames.end()) {
            seenPkgNames.insert(ext.pkgName);
            extensions.push_back(ext);
        }
    }

    brls::Logger::debug("GraphQL: Fetched {} installed extensions", extensions.size());
    return true;
}

bool SuwayomiClient::fetchUninstalledExtensionsGraphQL(std::vector<Extension>& extensions, const std::set<std::string>& languages) {
    // Build filter for uninstalled extensions with language filter
    // Using GraphQL filter with OR conditions for multiple languages

    std::string filterConditions;
    if (!languages.empty()) {
        // Build OR conditions for each language
        std::vector<std::string> langConditions;
        for (const auto& lang : languages) {
            // Escape the language string for JSON
            std::string escapedLang;
            for (char c : lang) {
                switch (c) {
                    case '"': escapedLang += "\\\""; break;
                    case '\\': escapedLang += "\\\\"; break;
                    default: escapedLang += c; break;
                }
            }
            langConditions.push_back("{ lang: { equalTo: \"" + escapedLang + "\" } }");
        }

        // Always include "multi" and "all" languages
        langConditions.push_back("{ lang: { equalTo: \"multi\" } }");
        langConditions.push_back("{ lang: { equalTo: \"all\" } }");

        // Join conditions with commas
        std::string orConditions;
        for (size_t i = 0; i < langConditions.size(); i++) {
            if (i > 0) orConditions += ", ";
            orConditions += langConditions[i];
        }

        filterConditions = "filter: { isInstalled: { equalTo: false }, or: [" + orConditions + "] }";
    } else {
        // No language filter, just filter by isInstalled: false
        filterConditions = "condition: { isInstalled: false }";
    }

    // Build query string with filter conditions
    std::string query = "query { extensions(" + filterConditions + ") { nodes { "
        "pkgName name lang versionName versionCode iconUrl isInstalled hasUpdate isObsolete isNsfw repo "
        "} } }";

    brls::Logger::debug("GraphQL: Fetching uninstalled extensions with filter: {}", filterConditions);

    std::string response = executeGraphQL(query);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string extensionsObj = extractJsonObject(data, "extensions");
    if (extensionsObj.empty()) return false;

    std::string nodesJson = extractJsonArray(extensionsObj, "nodes");
    if (nodesJson.empty()) {
        // Empty result is valid - no extensions match filter
        extensions.clear();
        brls::Logger::debug("GraphQL: No uninstalled extensions found matching filter");
        return true;
    }

    extensions.clear();
    std::set<std::string> seenPkgNames;
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        Extension ext = parseExtensionFromGraphQL(item);
        if (seenPkgNames.find(ext.pkgName) == seenPkgNames.end()) {
            seenPkgNames.insert(ext.pkgName);
            extensions.push_back(ext);
        }
    }

    brls::Logger::debug("GraphQL: Fetched {} uninstalled extensions (filtered by {} languages)",
                        extensions.size(), languages.size());
    return true;
}

bool SuwayomiClient::fetchInstalledExtensions(std::vector<Extension>& extensions) {
    // Try GraphQL first (primary API with server-side filtering)
    if (fetchInstalledExtensionsGraphQL(extensions)) {
        return true;
    }

    // Fallback: fetch all and filter client-side
    brls::Logger::info("GraphQL failed for installed extensions, falling back to full fetch...");
    std::vector<Extension> allExtensions;
    if (!fetchExtensionList(allExtensions)) {
        return false;
    }

    extensions.clear();
    for (const auto& ext : allExtensions) {
        if (ext.installed) {
            extensions.push_back(ext);
        }
    }
    return true;
}

bool SuwayomiClient::fetchUninstalledExtensions(std::vector<Extension>& extensions, const std::set<std::string>& languages) {
    // Try GraphQL first (primary API with server-side filtering)
    if (fetchUninstalledExtensionsGraphQL(extensions, languages)) {
        return true;
    }

    // Fallback: fetch all and filter client-side
    brls::Logger::info("GraphQL failed for uninstalled extensions, falling back to client-side filter...");
    std::vector<Extension> allExtensions;
    if (!fetchExtensionList(allExtensions)) {
        return false;
    }

    extensions.clear();
    for (const auto& ext : allExtensions) {
        if (!ext.installed) {
            // Apply language filter
            if (languages.empty()) {
                extensions.push_back(ext);
            } else {
                bool langMatch = languages.count(ext.lang) > 0 ||
                                ext.lang == "multi" ||
                                ext.lang == "all";
                // Also check base language (e.g., "zh" matches "zh-Hans")
                if (!langMatch) {
                    size_t dashPos = ext.lang.find('-');
                    if (dashPos != std::string::npos) {
                        std::string baseLang = ext.lang.substr(0, dashPos);
                        langMatch = languages.count(baseLang) > 0;
                    }
                }
                if (langMatch) {
                    extensions.push_back(ext);
                }
            }
        }
    }
    return true;
}

bool SuwayomiClient::installExtensionGraphQL(const std::string& pkgName) {
    const char* query = R"(
        mutation InstallExtension($id: String!, $install: Boolean) {
            updateExtension(input: { id: $id, patch: { install: $install } }) {
                extension {
                    pkgName
                    isInstalled
                }
            }
        }
    )";

    // Escape pkgName for JSON
    std::string escapedPkg;
    for (char c : pkgName) {
        switch (c) {
            case '"': escapedPkg += "\\\""; break;
            case '\\': escapedPkg += "\\\\"; break;
            default: escapedPkg += c; break;
        }
    }

    std::string variables = "{\"id\":\"" + escapedPkg + "\",\"install\":true}";
    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    // Verify the installation was successful by checking isInstalled
    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string updateResult = extractJsonObject(data, "updateExtension");
    if (updateResult.empty()) return false;

    std::string extension = extractJsonObject(updateResult, "extension");
    if (extension.empty()) return false;

    bool isInstalled = extractJsonBool(extension, "isInstalled");
    if (!isInstalled) {
        brls::Logger::warning("GraphQL: Extension install returned but isInstalled is false");
    }

    return isInstalled;
}

bool SuwayomiClient::updateExtensionGraphQL(const std::string& pkgName) {
    const char* query = R"(
        mutation UpdateExtension($id: String!, $update: Boolean) {
            updateExtension(input: { id: $id, patch: { update: $update } }) {
                extension {
                    pkgName
                    isInstalled
                    hasUpdate
                }
            }
        }
    )";

    std::string escapedPkg;
    for (char c : pkgName) {
        switch (c) {
            case '"': escapedPkg += "\\\""; break;
            case '\\': escapedPkg += "\\\\"; break;
            default: escapedPkg += c; break;
        }
    }

    std::string variables = "{\"id\":\"" + escapedPkg + "\",\"update\":true}";
    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    // Verify the update was successful
    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string updateResult = extractJsonObject(data, "updateExtension");
    if (updateResult.empty()) return false;

    std::string extension = extractJsonObject(updateResult, "extension");
    if (extension.empty()) return false;

    // After successful update, hasUpdate should be false and isInstalled should be true
    bool isInstalled = extractJsonBool(extension, "isInstalled");
    bool hasUpdate = extractJsonBool(extension, "hasUpdate");

    if (!isInstalled) {
        brls::Logger::warning("GraphQL: Extension update returned but isInstalled is false");
        return false;
    }

    brls::Logger::debug("GraphQL: Extension updated successfully (hasUpdate: {})", hasUpdate);
    return true;
}

bool SuwayomiClient::uninstallExtensionGraphQL(const std::string& pkgName) {
    const char* query = R"(
        mutation UninstallExtension($id: String!, $uninstall: Boolean) {
            updateExtension(input: { id: $id, patch: { uninstall: $uninstall } }) {
                extension {
                    pkgName
                    isInstalled
                }
            }
        }
    )";

    std::string escapedPkg;
    for (char c : pkgName) {
        switch (c) {
            case '"': escapedPkg += "\\\""; break;
            case '\\': escapedPkg += "\\\\"; break;
            default: escapedPkg += c; break;
        }
    }

    std::string variables = "{\"id\":\"" + escapedPkg + "\",\"uninstall\":true}";
    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    // Verify the uninstallation was successful
    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string updateResult = extractJsonObject(data, "updateExtension");
    if (updateResult.empty()) return false;

    std::string extension = extractJsonObject(updateResult, "extension");
    if (extension.empty()) return false;

    // After successful uninstall, isInstalled should be false
    bool isInstalled = extractJsonBool(extension, "isInstalled");
    if (isInstalled) {
        brls::Logger::warning("GraphQL: Extension uninstall returned but isInstalled is still true");
        return false;
    }

    brls::Logger::debug("GraphQL: Extension uninstalled successfully");
    return true;
}

// ============================================================================
// GraphQL Download Operations
// ============================================================================

bool SuwayomiClient::enqueueChapterDownloadGraphQL(int chapterId) {
    const char* query = R"(
        mutation EnqueueDownload($id: Int!) {
            enqueueChapterDownload(input: { id: $id }) {
                downloadStatus {
                    state
                }
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(chapterId) + "}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::dequeueChapterDownloadGraphQL(int chapterId) {
    const char* query = R"(
        mutation DequeueDownload($id: Int!) {
            dequeueChapterDownload(input: { id: $id }) {
                downloadStatus {
                    state
                }
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(chapterId) + "}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::enqueueChapterDownloadsGraphQL(const std::vector<int>& chapterIds) {
    const char* query = R"(
        mutation EnqueueDownloads($ids: [Int!]!) {
            enqueueChapterDownloads(input: { ids: $ids }) {
                downloadStatus {
                    state
                }
            }
        }
    )";

    std::string idList = "[";
    for (size_t i = 0; i < chapterIds.size(); i++) {
        if (i > 0) idList += ",";
        idList += std::to_string(chapterIds[i]);
    }
    idList += "]";

    std::string variables = "{\"ids\":" + idList + "}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::dequeueChapterDownloadsGraphQL(const std::vector<int>& chapterIds) {
    const char* query = R"(
        mutation DequeueDownloads($ids: [Int!]!) {
            dequeueChapterDownloads(input: { ids: $ids }) {
                downloadStatus {
                    state
                }
            }
        }
    )";

    std::string idList = "[";
    for (size_t i = 0; i < chapterIds.size(); i++) {
        if (i > 0) idList += ",";
        idList += std::to_string(chapterIds[i]);
    }
    idList += "]";

    std::string variables = "{\"ids\":" + idList + "}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::reorderChapterDownloadGraphQL(int chapterId, int newPosition) {
    const char* query = R"(
        mutation ReorderDownload($id: Int!, $position: Int!) {
            reorderChapterDownload(input: { chapterId: $id, to: $position }) {
                downloadStatus {
                    state
                }
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(chapterId) +
                            ",\"position\":" + std::to_string(newPosition) + "}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::startDownloadsGraphQL() {
    const char* query = R"(
        mutation {
            startDownloader(input: {}) {
                downloadStatus {
                    state
                }
            }
        }
    )";

    std::string response = executeGraphQL(query);
    return !response.empty();
}

bool SuwayomiClient::stopDownloadsGraphQL() {
    const char* query = R"(
        mutation {
            stopDownloader(input: {}) {
                downloadStatus {
                    state
                }
            }
        }
    )";

    std::string response = executeGraphQL(query);
    return !response.empty();
}

bool SuwayomiClient::clearDownloadQueueGraphQL() {
    const char* query = R"(
        mutation {
            clearDownloader(input: {}) {
                downloadStatus {
                    state
                }
            }
        }
    )";

    std::string response = executeGraphQL(query);
    return !response.empty();
}

// ============================================================================
// GraphQL Single Source
// ============================================================================

bool SuwayomiClient::fetchSourceGraphQL(int64_t sourceId, Source& source) {
    const char* query = R"(
        query GetSource($id: LongString!) {
            source(id: $id) {
                id
                name
                displayName
                lang
                iconUrl
                isNsfw
                supportsLatest
                isConfigurable
            }
        }
    )";

    std::string variables = "{\"id\":\"" + std::to_string(sourceId) + "\"}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string sourceJson = extractJsonObject(data, "source");
    if (sourceJson.empty()) return false;

    source = parseSourceFromGraphQL(sourceJson);
    return true;
}

// ============================================================================
// GraphQL Source Preferences
// ============================================================================

SourcePreference SuwayomiClient::parseSourcePreferenceFromGraphQL(const std::string& json) {
    SourcePreference pref;

    // Determine type from __typename
    std::string typeName = extractJsonValue(json, "__typename");
    brls::Logger::info("Parsing preference with __typename: '{}'", typeName);

    bool knownType = true;
    if (typeName == "SwitchPreference") {
        pref.type = SourcePreferenceType::SWITCH;
    } else if (typeName == "CheckBoxPreference") {
        pref.type = SourcePreferenceType::CHECKBOX;
    } else if (typeName == "EditTextPreference") {
        pref.type = SourcePreferenceType::EDIT_TEXT;
    } else if (typeName == "ListPreference") {
        pref.type = SourcePreferenceType::LIST;
    } else if (typeName == "MultiSelectListPreference") {
        pref.type = SourcePreferenceType::MULTI_SELECT_LIST;
    } else {
        // Unknown preference type - mark as not visible so it's skipped
        knownType = false;
        brls::Logger::warning("Unknown preference type: '{}', JSON snippet: {}", typeName, json.substr(0, std::min(json.size(), (size_t)200)));
    }

    // Common fields
    pref.key = extractJsonValue(json, "key");
    pref.title = extractJsonValue(json, "title");
    pref.summary = extractJsonValue(json, "summary");

    brls::Logger::debug("  key='{}', title='{}', knownType={}", pref.key, pref.title, knownType);

    // Only override visible/enabled if the field is present in JSON
    // (unknown types won't have these fields in their fragment)
    std::string visibleStr = extractJsonValue(json, "visible");
    brls::Logger::debug("  visibleStr='{}' (empty={})", visibleStr, visibleStr.empty());

    if (!visibleStr.empty()) {
        pref.visible = (visibleStr == "true" || visibleStr == "1");
    } else if (!knownType) {
        // Unknown types without visible field should be hidden
        pref.visible = false;
    }
    // else: keep default visible = true

    brls::Logger::debug("  Final visible={}", pref.visible);

    std::string enabledStr = extractJsonValue(json, "enabled");
    if (!enabledStr.empty()) {
        pref.enabled = (enabledStr == "true" || enabledStr == "1");
    }
    // else: keep default enabled = true

    // Type-specific fields (using aliased field names from GraphQL query)
    if (pref.type == SourcePreferenceType::SWITCH || pref.type == SourcePreferenceType::CHECKBOX) {
        pref.currentValue = extractJsonBool(json, "boolValue");
        pref.defaultValue = extractJsonBool(json, "boolDefault");
    } else if (pref.type == SourcePreferenceType::EDIT_TEXT) {
        pref.currentText = extractJsonValue(json, "stringValue");
        pref.defaultText = extractJsonValue(json, "stringDefault");
        pref.dialogTitle = extractJsonValue(json, "dialogTitle");
        pref.dialogMessage = extractJsonValue(json, "dialogMessage");
    } else if (pref.type == SourcePreferenceType::LIST) {
        pref.entries = extractJsonStringArray(json, "entries");
        pref.entryValues = extractJsonStringArray(json, "entryValues");
        pref.selectedValue = extractJsonValue(json, "stringValue");
        pref.defaultListValue = extractJsonValue(json, "stringDefault");
    } else if (pref.type == SourcePreferenceType::MULTI_SELECT_LIST) {
        pref.entries = extractJsonStringArray(json, "entries");
        pref.entryValues = extractJsonStringArray(json, "entryValues");
        pref.selectedValues = extractJsonStringArray(json, "stringListValue");
        pref.defaultMultiValues = extractJsonStringArray(json, "stringListDefault");
    }

    return pref;
}

bool SuwayomiClient::fetchSourcePreferencesGraphQL(int64_t sourceId, std::vector<SourcePreference>& preferences) {
    // Use field aliases to avoid GraphQL type conflict error:
    // currentValue returns Boolean for Switch/CheckBox, String for List/EditText, [String] for MultiSelect
    const char* query = R"(
        query GetSourcePreferences($id: LongString!) {
            source(id: $id) {
                preferences {
                    __typename
                    ... on SwitchPreference {
                        key
                        title
                        summary
                        visible
                        enabled
                        boolValue: currentValue
                        boolDefault: default
                    }
                    ... on CheckBoxPreference {
                        key
                        title
                        summary
                        visible
                        enabled
                        boolValue: currentValue
                        boolDefault: default
                    }
                    ... on EditTextPreference {
                        key
                        title
                        summary
                        visible
                        enabled
                        stringValue: currentValue
                        stringDefault: default
                        dialogTitle
                        dialogMessage
                    }
                    ... on ListPreference {
                        key
                        title
                        summary
                        visible
                        enabled
                        stringValue: currentValue
                        stringDefault: default
                        entries
                        entryValues
                    }
                    ... on MultiSelectListPreference {
                        key
                        title
                        summary
                        visible
                        enabled
                        stringListValue: currentValue
                        stringListDefault: default
                        entries
                        entryValues
                    }
                }
            }
        }
    )";

    std::string variables = "{\"id\":\"" + std::to_string(sourceId) + "\"}";

    brls::Logger::info("Fetching source preferences for sourceId: {}", sourceId);

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("GraphQL response is empty for source preferences");
        return false;
    }

    brls::Logger::debug("GraphQL response length: {} chars", response.size());

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) {
        brls::Logger::error("No 'data' in GraphQL response. Response: {}", response.substr(0, std::min(response.size(), (size_t)500)));
        return false;
    }

    std::string sourceJson = extractJsonObject(data, "source");
    if (sourceJson.empty()) {
        brls::Logger::error("No 'source' in data. Data: {}", data.substr(0, std::min(data.size(), (size_t)500)));
        return false;
    }

    std::string prefsJson = extractJsonArray(sourceJson, "preferences");
    if (prefsJson.empty()) {
        brls::Logger::info("No preferences array found or empty for source");
        preferences.clear();
        return true;  // Source might not have preferences
    }

    brls::Logger::info("Preferences JSON length: {} chars", prefsJson.size());

    preferences.clear();
    std::vector<std::string> items = splitJsonArray(prefsJson);
    brls::Logger::info("Split into {} preference items", items.size());

    // Log first item for debugging
    if (!items.empty()) {
        brls::Logger::info("First preference item JSON: {}", items[0].substr(0, std::min(items[0].size(), (size_t)500)));
    }

    for (const auto& item : items) {
        preferences.push_back(parseSourcePreferenceFromGraphQL(item));
    }

    brls::Logger::info("GraphQL: Fetched {} source preferences", preferences.size());
    return true;
}

bool SuwayomiClient::updateSourcePreferenceGraphQL(int64_t sourceId, const SourcePreferenceChange& change) {
    const char* query = R"(
        mutation UpdateSourcePreference($sourceId: LongString!, $change: SourcePreferenceChangeInput!) {
            updateSourcePreference(input: { source: $sourceId, change: $change }) {
                source {
                    id
                }
            }
        }
    )";

    // Build the change object based on which field is set
    std::string changeJson = "{\"position\":" + std::to_string(change.position);

    if (change.hasSwitchState) {
        changeJson += ",\"switchState\":" + std::string(change.switchState ? "true" : "false");
    } else if (change.hasCheckBoxState) {
        changeJson += ",\"checkBoxState\":" + std::string(change.checkBoxState ? "true" : "false");
    } else if (change.hasEditTextState) {
        // Escape the text value
        std::string escaped;
        for (char c : change.editTextState) {
            switch (c) {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default: escaped += c; break;
            }
        }
        changeJson += ",\"editTextState\":\"" + escaped + "\"";
    } else if (change.hasListState) {
        // Escape the value
        std::string escaped;
        for (char c : change.listState) {
            switch (c) {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                default: escaped += c; break;
            }
        }
        changeJson += ",\"listState\":\"" + escaped + "\"";
    } else if (change.hasMultiSelectState) {
        changeJson += ",\"multiSelectState\":[";
        for (size_t i = 0; i < change.multiSelectState.size(); i++) {
            if (i > 0) changeJson += ",";
            // Escape each value
            std::string escaped;
            for (char c : change.multiSelectState[i]) {
                switch (c) {
                    case '"': escaped += "\\\""; break;
                    case '\\': escaped += "\\\\"; break;
                    default: escaped += c; break;
                }
            }
            changeJson += "\"" + escaped + "\"";
        }
        changeJson += "]";
    }

    changeJson += "}";

    std::string variables = "{\"sourceId\":\"" + std::to_string(sourceId) + "\",\"change\":" + changeJson + "}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("Failed to update source preference");
        return false;
    }

    brls::Logger::debug("GraphQL: Updated source preference at position {}", change.position);
    return true;
}

bool SuwayomiClient::setSourceMetaGraphQL(int64_t sourceId, const std::string& key, const std::string& value) {
    const char* query = R"(
        mutation SetSourceMeta($sourceId: LongString!, $key: String!, $value: String!) {
            setSourceMeta(input: { meta: { sourceId: $sourceId, key: $key, value: $value } }) {
                meta {
                    key
                    value
                }
            }
        }
    )";

    // Escape key and value
    std::string escapedKey, escapedValue;
    for (char c : key) {
        switch (c) {
            case '"': escapedKey += "\\\""; break;
            case '\\': escapedKey += "\\\\"; break;
            default: escapedKey += c; break;
        }
    }
    for (char c : value) {
        switch (c) {
            case '"': escapedValue += "\\\""; break;
            case '\\': escapedValue += "\\\\"; break;
            case '\n': escapedValue += "\\n"; break;
            case '\r': escapedValue += "\\r"; break;
            case '\t': escapedValue += "\\t"; break;
            default: escapedValue += c; break;
        }
    }

    std::string variables = "{\"sourceId\":\"" + std::to_string(sourceId) +
                            "\",\"key\":\"" + escapedKey +
                            "\",\"value\":\"" + escapedValue + "\"}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("Failed to set source meta: {} = {}", key, value);
        return false;
    }

    brls::Logger::debug("GraphQL: Set source meta {} = {}", key, value);
    return true;
}

bool SuwayomiClient::deleteSourceMetaGraphQL(int64_t sourceId, const std::string& key) {
    const char* query = R"(
        mutation DeleteSourceMeta($sourceId: LongString!, $key: String!) {
            deleteSourceMeta(input: { sourceId: $sourceId, key: $key }) {
                source {
                    id
                }
            }
        }
    )";

    // Escape key
    std::string escapedKey;
    for (char c : key) {
        switch (c) {
            case '"': escapedKey += "\\\""; break;
            case '\\': escapedKey += "\\\\"; break;
            default: escapedKey += c; break;
        }
    }

    std::string variables = "{\"sourceId\":\"" + std::to_string(sourceId) +
                            "\",\"key\":\"" + escapedKey + "\"}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) {
        brls::Logger::error("Failed to delete source meta: {}", key);
        return false;
    }

    brls::Logger::debug("GraphQL: Deleted source meta {}", key);
    return true;
}

bool SuwayomiClient::fetchSourcesForExtensionGraphQL(const std::string& pkgName, std::vector<Source>& sources) {
    const char* query = R"(
        query GetExtensionSources($pkgName: String!) {
            extension(pkgName: $pkgName) {
                source {
                    nodes {
                        id
                        name
                        displayName
                        lang
                        iconUrl
                        isNsfw
                        supportsLatest
                        isConfigurable
                    }
                }
            }
        }
    )";

    // Escape pkgName
    std::string escapedPkg;
    for (char c : pkgName) {
        switch (c) {
            case '"': escapedPkg += "\\\""; break;
            case '\\': escapedPkg += "\\\\"; break;
            default: escapedPkg += c; break;
        }
    }

    std::string variables = "{\"pkgName\":\"" + escapedPkg + "\"}";

    std::string response = executeGraphQL(query, variables);
    if (response.empty()) return false;

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) return false;

    std::string extJson = extractJsonObject(data, "extension");
    if (extJson.empty()) return false;

    std::string sourceObj = extractJsonObject(extJson, "source");
    if (sourceObj.empty()) return false;

    std::string nodesJson = extractJsonArray(sourceObj, "nodes");
    if (nodesJson.empty()) {
        sources.clear();
        return true;  // Extension might not have sources yet
    }

    sources.clear();
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        sources.push_back(parseSourceFromGraphQL(item));
    }

    brls::Logger::debug("GraphQL: Fetched {} sources for extension {}", sources.size(), pkgName);
    return true;
}

// ============================================================================
// Public Source Preferences API
// ============================================================================

bool SuwayomiClient::fetchSourcePreferences(int64_t sourceId, std::vector<SourcePreference>& preferences) {
    return fetchSourcePreferencesGraphQL(sourceId, preferences);
}

bool SuwayomiClient::updateSourcePreference(int64_t sourceId, const SourcePreferenceChange& change) {
    return updateSourcePreferenceGraphQL(sourceId, change);
}

bool SuwayomiClient::setSourceMeta(int64_t sourceId, const std::string& key, const std::string& value) {
    return setSourceMetaGraphQL(sourceId, key, value);
}

bool SuwayomiClient::deleteSourceMeta(int64_t sourceId, const std::string& key) {
    return deleteSourceMetaGraphQL(sourceId, key);
}

bool SuwayomiClient::fetchSourcesForExtension(const std::string& pkgName, std::vector<Source>& sources) {
    return fetchSourcesForExtensionGraphQL(pkgName, sources);
}

// ============================================================================
// Image URL helpers
// ============================================================================

std::string SuwayomiClient::buildProxiedImageUrl(const std::string& externalUrl) const {
    if (externalUrl.empty()) {
        return externalUrl;
    }

    // Check if it's already a server URL (starts with server URL or is relative)
    if (externalUrl[0] == '/' ||
        (!m_serverUrl.empty() && externalUrl.find(m_serverUrl) == 0)) {
        // Already a server URL, return as-is (with server prefix if relative)
        if (externalUrl[0] == '/') {
            return m_serverUrl + externalUrl;
        }
        return externalUrl;
    }

    // Check if it's an external URL (http:// or https://)
    if (externalUrl.find("http://") != 0 && externalUrl.find("https://") != 0) {
        // Not a valid URL, return as-is
        return externalUrl;
    }

    // URL-encode the external URL for the proxy endpoint
    // Suwayomi Server uses /api/v1/imageUrl/fetch?url=<encoded-url> for proxying
    std::string encoded;
    for (char c : externalUrl) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
            encoded += hex;
        }
    }

    return m_serverUrl + "/api/v1/imageUrl/fetch?url=" + encoded;
}

// ============================================================================
// GraphQL Category Operations
// ============================================================================

bool SuwayomiClient::createCategoryGraphQL(const std::string& name) {
    const char* query = R"(
        mutation CreateCategory($name: String!) {
            createCategory(input: { name: $name }) {
                category {
                    id
                    name
                    order
                }
            }
        }
    )";

    // Escape the name for JSON
    std::string escapedName;
    for (char c : name) {
        if (c == '"' || c == '\\') {
            escapedName += '\\';
        }
        escapedName += c;
    }

    std::string variables = "{\"name\":\"" + escapedName + "\"}";
    std::string response = executeGraphQL(query, variables);

    if (response.empty()) {
        brls::Logger::error("GraphQL: createCategory - empty response");
        return false;
    }

    std::string data = extractJsonObject(response, "data");
    if (data.empty()) {
        brls::Logger::error("GraphQL: createCategory - no data in response");
        return false;
    }

    brls::Logger::info("GraphQL: Successfully created category '{}'", name);
    return true;
}

bool SuwayomiClient::deleteCategoryGraphQL(int categoryId) {
    const char* query = R"(
        mutation DeleteCategory($id: Int!) {
            deleteCategory(input: { categoryId: $id }) {
                category {
                    id
                }
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(categoryId) + "}";
    std::string response = executeGraphQL(query, variables);

    if (response.empty()) {
        brls::Logger::error("GraphQL: deleteCategory - empty response");
        return false;
    }

    brls::Logger::info("GraphQL: Successfully deleted category {}", categoryId);
    return true;
}

bool SuwayomiClient::updateCategoryGraphQL(int categoryId, const std::string& name, bool isDefault) {
    const char* query = R"(
        mutation UpdateCategory($id: Int!, $name: String, $default: Boolean) {
            updateCategory(input: { id: $id, patch: { name: $name, default: $default } }) {
                category {
                    id
                    name
                    order
                }
            }
        }
    )";

    // Escape the name for JSON
    std::string escapedName;
    for (char c : name) {
        if (c == '"' || c == '\\') {
            escapedName += '\\';
        }
        escapedName += c;
    }

    std::string variables = "{\"id\":" + std::to_string(categoryId) +
                            ",\"name\":\"" + escapedName + "\"" +
                            ",\"default\":" + (isDefault ? "true" : "false") + "}";
    std::string response = executeGraphQL(query, variables);

    if (response.empty()) {
        brls::Logger::error("GraphQL: updateCategory - empty response");
        return false;
    }

    brls::Logger::info("GraphQL: Successfully updated category {}", categoryId);
    return true;
}

bool SuwayomiClient::updateCategoryOrderGraphQL(int categoryId, int newPosition) {
    const char* query = R"(
        mutation UpdateCategoryOrder($id: Int!, $position: Int!) {
            updateCategoryOrder(input: { id: $id, position: $position }) {
                categories {
                    id
                    order
                }
            }
        }
    )";

    std::string variables = "{\"id\":" + std::to_string(categoryId) +
                            ",\"position\":" + std::to_string(newPosition) + "}";
    std::string response = executeGraphQL(query, variables);

    if (response.empty()) {
        brls::Logger::error("GraphQL: updateCategoryOrder - empty response");
        return false;
    }

    brls::Logger::info("GraphQL: Successfully reordered category {} to position {}", categoryId, newPosition);
    return true;
}

bool SuwayomiClient::triggerCategoryUpdateGraphQL(int categoryId) {
    const char* query = R"(
        mutation UpdateCategoryManga($categories: [Int!]!) {
            updateCategoryManga(input: { categories: $categories }) {
                updateStatus {
                    isRunning
                }
            }
        }
    )";

    std::string variables = "{\"categories\":[" + std::to_string(categoryId) + "]}";
    std::string response = executeGraphQL(query, variables);

    if (response.empty()) {
        brls::Logger::error("GraphQL: triggerCategoryUpdate - empty response");
        return false;
    }

    brls::Logger::info("GraphQL: Successfully triggered update for category {}", categoryId);
    return true;
}

bool SuwayomiClient::triggerLibraryUpdateGraphQL() {
    const char* query = R"(
        mutation UpdateLibraryManga {
            updateLibraryManga(input: {}) {
                updateStatus {
                    isRunning
                }
            }
        }
    )";

    std::string response = executeGraphQL(query);

    if (response.empty()) {
        brls::Logger::error("GraphQL: triggerLibraryUpdate - empty response");
        return false;
    }

    brls::Logger::info("GraphQL: Successfully triggered library update");
    return true;
}

} // namespace vitasuwayomi
