/**
 * VitaSuwayomi - Suwayomi Server API Client implementation
 */

#include "app/suwayomi_client.hpp"
#include "utils/http_client.hpp"
#include "utils/image_loader.hpp"

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

    if (!response.success || response.statusCode != 200) {
        brls::Logger::warning("GraphQL request failed: {} ({})", response.error, response.statusCode);
        return "";
    }

    // Check for GraphQL errors
    if (response.body.find("\"errors\"") != std::string::npos) {
        std::string errors = extractJsonArray(response.body, "errors");
        brls::Logger::warning("GraphQL errors: {}", errors.substr(0, 200));
        return "";
    }

    brls::Logger::debug("GraphQL response: {}", response.body.substr(0, 300));
    return response.body;
}

// Helper to create an HTTP client with authentication headers
vitasuwayomi::HttpClient SuwayomiClient::createHttpClient() {
    vitasuwayomi::HttpClient http;
    http.setDefaultHeader("Accept", "application/json");

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

    // GraphQL might have unreadCount directly
    manga.unreadCount = extractJsonInt(json, "unreadCount");

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
    const char* query = R"(
        query GetHistory($offset: Int!, $limit: Int!) {
            chapters(
                offset: $offset
                first: $limit
                filter: { lastReadAt: { greaterThan: 0 } }
                orderBy: LAST_READ_AT
                orderByType: DESC
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
    // Use the mangas query with categoryId filter - this is the correct Suwayomi API
    // The filter ensures only manga assigned to this specific category are returned
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
                    inLibrary
                    unreadCount
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

    brls::Logger::info("GraphQL: Fetched {} manga for category {} (filter method)", manga.size(), categoryId);
    return true;
}

// Fallback method using category.mangas query (for older Suwayomi versions)
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
                        inLibrary
                        unreadCount
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

    // Also clear image loader credentials
    ImageLoader::setAuthCredentials("", "");
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
    // Try GraphQL first (primary API)
    if (installExtensionGraphQL(pkgName)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for install extension, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/extension/install/" + pkgName);
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::updateExtension(const std::string& pkgName) {
    // Try GraphQL first (primary API)
    if (updateExtensionGraphQL(pkgName)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for update extension, falling back to REST...");
    vitasuwayomi::HttpClient http = createHttpClient();

    std::string url = buildApiUrl("/extension/update/" + pkgName);
    vitasuwayomi::HttpResponse response = http.get(url);

    return response.success && response.statusCode == 200;
}

bool SuwayomiClient::uninstallExtension(const std::string& pkgName) {
    // Try GraphQL first (primary API)
    if (uninstallExtensionGraphQL(pkgName)) {
        return true;
    }

    // REST fallback
    brls::Logger::info("GraphQL failed for uninstall extension, falling back to REST...");
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

bool SuwayomiClient::queueChapterDownloads(const std::vector<int>& chapterIds) {
    // Use GraphQL API which expects actual chapter IDs
    brls::Logger::info("SuwayomiClient: Queueing {} chapters for download", chapterIds.size());
    return enqueueChapterDownloadsGraphQL(chapterIds);
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
    std::vector<std::string> items = splitJsonArray(nodesJson);
    for (const auto& item : items) {
        extensions.push_back(parseExtensionFromGraphQL(item));
    }

    brls::Logger::debug("GraphQL: Fetched {} extensions", extensions.size());
    return true;
}

bool SuwayomiClient::installExtensionGraphQL(const std::string& pkgName) {
    const char* query = R"(
        mutation InstallExtension($pkgName: String!) {
            installExternalExtension(input: { extensionPkgName: $pkgName }) {
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

    std::string variables = "{\"pkgName\":\"" + escapedPkg + "\"}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::updateExtensionGraphQL(const std::string& pkgName) {
    const char* query = R"(
        mutation UpdateExtension($pkgName: String!) {
            updateExtension(input: { pkgName: $pkgName }) {
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

    std::string variables = "{\"pkgName\":\"" + escapedPkg + "\"}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
}

bool SuwayomiClient::uninstallExtensionGraphQL(const std::string& pkgName) {
    const char* query = R"(
        mutation UninstallExtension($pkgName: String!) {
            uninstallExtension(input: { pkgName: $pkgName }) {
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

    std::string variables = "{\"pkgName\":\"" + escapedPkg + "\"}";
    std::string response = executeGraphQL(query, variables);
    return !response.empty();
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

} // namespace vitasuwayomi
