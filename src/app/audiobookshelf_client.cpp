/**
 * VitaSuwayomi - Audiobookshelf API Client implementation
 */

#include "app/audiobookshelf_client.hpp"
#include "app/application.hpp"
#include "utils/http_client.hpp"

#include <borealis.hpp>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace vitasuwayomi {

AudiobookshelfClient& AudiobookshelfClient::getInstance() {
    static AudiobookshelfClient instance;
    return instance;
}

std::string AudiobookshelfClient::buildApiUrl(const std::string& endpoint) {
    std::string url = m_serverUrl;

    // Remove trailing slash
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    url += endpoint;

    return url;
}

MediaType AudiobookshelfClient::parseMediaType(const std::string& typeStr) {
    if (typeStr == "book") return MediaType::BOOK;
    if (typeStr == "podcast") return MediaType::PODCAST;
    if (typeStr == "podcastEpisode") return MediaType::PODCAST_EPISODE;
    return MediaType::UNKNOWN;
}

// JSON parsing helpers
std::string AudiobookshelfClient::extractJsonValue(const std::string& json, const std::string& key) {
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

int AudiobookshelfClient::extractJsonInt(const std::string& json, const std::string& key) {
    std::string value = extractJsonValue(json, key);
    if (value.empty()) return 0;
    return atoi(value.c_str());
}

float AudiobookshelfClient::extractJsonFloat(const std::string& json, const std::string& key) {
    std::string value = extractJsonValue(json, key);
    if (value.empty()) return 0.0f;
    return (float)atof(value.c_str());
}

bool AudiobookshelfClient::extractJsonBool(const std::string& json, const std::string& key) {
    std::string value = extractJsonValue(json, key);
    return (value == "true" || value == "1");
}

int64_t AudiobookshelfClient::extractJsonInt64(const std::string& json, const std::string& key) {
    std::string value = extractJsonValue(json, key);
    if (value.empty()) return 0;
    return std::stoll(value);
}

std::string AudiobookshelfClient::extractJsonArray(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) {
        brls::Logger::debug("extractJsonArray: key '{}' not found", key);
        return "";
    }

    // Find the colon after the key
    size_t colonPos = json.find(':', keyPos + searchKey.length());
    if (colonPos == std::string::npos) {
        brls::Logger::debug("extractJsonArray: no colon after key '{}'", key);
        return "";
    }

    // Find the array bracket after the colon
    size_t arrStart = json.find('[', colonPos);
    if (arrStart == std::string::npos) {
        brls::Logger::debug("extractJsonArray: no '[' after key '{}'", key);
        return "";
    }

    // Make sure there's nothing but whitespace between colon and bracket
    // This prevents matching arrays from other fields
    for (size_t i = colonPos + 1; i < arrStart; i++) {
        char c = json[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            brls::Logger::debug("extractJsonArray: non-whitespace '{}' between colon and '[' for key '{}'", c, key);
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

    std::string result = json.substr(arrStart, arrEnd - arrStart);
    brls::Logger::debug("extractJsonArray: found array for '{}' with {} chars", key, result.length());
    return result;
}

std::string AudiobookshelfClient::extractJsonObject(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) {
        brls::Logger::debug("extractJsonObject: key '{}' not found", key);
        return "";
    }

    // Find the colon after the key
    size_t colonPos = json.find(':', keyPos + searchKey.length());
    if (colonPos == std::string::npos) {
        brls::Logger::debug("extractJsonObject: no colon after key '{}'", key);
        return "";
    }

    // Find the object bracket after the colon
    size_t objStart = json.find('{', colonPos);
    if (objStart == std::string::npos) {
        brls::Logger::debug("extractJsonObject: no '{{' after key '{}'", key);
        return "";
    }

    // Make sure there's nothing but whitespace between colon and bracket
    for (size_t i = colonPos + 1; i < objStart; i++) {
        char c = json[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            brls::Logger::debug("extractJsonObject: non-whitespace '{}' between colon and '{{' for key '{}'", c, key);
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

    std::string result = json.substr(objStart, objEnd - objStart);
    brls::Logger::debug("extractJsonObject: found object for '{}' with {} chars", key, result.length());
    return result;
}

MediaItem AudiobookshelfClient::parseMediaItem(const std::string& json) {
    MediaItem item;

    item.id = extractJsonValue(json, "id");
    item.libraryId = extractJsonValue(json, "libraryId");

    // Get media metadata (nested object)
    std::string mediaObj = extractJsonObject(json, "media");
    std::string metadataObj = extractJsonObject(mediaObj.empty() ? json : mediaObj, "metadata");

    if (!metadataObj.empty()) {
        item.title = extractJsonValue(metadataObj, "title");
        item.subtitle = extractJsonValue(metadataObj, "subtitle");
        item.description = extractJsonValue(metadataObj, "description");
        // For audiobooks: authorName, for podcasts: author
        item.authorName = extractJsonValue(metadataObj, "authorName");
        if (item.authorName.empty()) {
            // Podcasts use "author" field for the creator/feed owner
            item.authorName = extractJsonValue(metadataObj, "author");
        }
        // If still empty, try parsing the authors array (expanded format)
        if (item.authorName.empty()) {
            std::string authorsArray = extractJsonArray(metadataObj, "authors");
            if (!authorsArray.empty() && authorsArray != "[]") {
                // Extract author names from array and join them
                std::vector<std::string> authorNames;
                size_t pos = 0;
                while ((pos = authorsArray.find("\"name\"", pos)) != std::string::npos) {
                    size_t colonPos = authorsArray.find(':', pos);
                    if (colonPos != std::string::npos) {
                        size_t nameStart = authorsArray.find('"', colonPos + 1);
                        if (nameStart != std::string::npos) {
                            size_t nameEnd = authorsArray.find('"', nameStart + 1);
                            if (nameEnd != std::string::npos) {
                                std::string name = authorsArray.substr(nameStart + 1, nameEnd - nameStart - 1);
                                if (!name.empty()) {
                                    authorNames.push_back(name);
                                }
                                pos = nameEnd;
                            }
                        }
                    }
                    pos++;
                }
                // Join author names with ", "
                for (size_t i = 0; i < authorNames.size(); ++i) {
                    if (i > 0) item.authorName += ", ";
                    item.authorName += authorNames[i];
                }
                if (!item.authorName.empty()) {
                    brls::Logger::debug("Parsed authors from array: {}", item.authorName);
                }
            }
        }
        item.narratorName = extractJsonValue(metadataObj, "narratorName");
        item.publishedYear = extractJsonValue(metadataObj, "publishedYear");
        item.publisher = extractJsonValue(metadataObj, "publisher");
        item.isbn = extractJsonValue(metadataObj, "isbn");
        item.asin = extractJsonValue(metadataObj, "asin");
        item.language = extractJsonValue(metadataObj, "language");
        item.seriesName = extractJsonValue(metadataObj, "seriesName");
        item.seriesSequence = extractJsonValue(metadataObj, "sequence");
    } else {
        // Fallback to direct fields
        item.title = extractJsonValue(json, "title");
        item.description = extractJsonValue(json, "description");
    }

    // If title still empty, try other fields
    if (item.title.empty()) {
        item.title = extractJsonValue(json, "name");
    }

    // Media type
    item.type = extractJsonValue(json, "mediaType");
    if (item.type.empty()) {
        item.type = extractJsonValue(mediaObj, "mediaType");
    }
    item.mediaType = parseMediaType(item.type);

    // Duration and progress
    item.duration = extractJsonFloat(mediaObj.empty() ? json : mediaObj, "duration");
    item.numTracks = extractJsonInt(mediaObj.empty() ? json : mediaObj, "numTracks");
    item.numChapters = extractJsonInt(mediaObj.empty() ? json : mediaObj, "numChapters");
    item.size = extractJsonInt64(mediaObj.empty() ? json : mediaObj, "size");

    // Progress info (from userMediaProgress or mediaProgress)
    std::string progressObj = extractJsonObject(json, "userMediaProgress");
    if (progressObj.empty()) {
        progressObj = extractJsonObject(json, "mediaProgress");
    }
    if (!progressObj.empty()) {
        item.currentTime = extractJsonFloat(progressObj, "currentTime");
        item.progress = extractJsonFloat(progressObj, "progress");
        item.isFinished = extractJsonBool(progressObj, "isFinished");
        item.progressLastUpdate = extractJsonInt64(progressObj, "lastUpdate");
    }

    // Cover path
    item.coverPath = extractJsonValue(json, "coverPath");
    if (item.coverPath.empty()) {
        item.coverPath = extractJsonValue(mediaObj, "coverPath");
    }

    return item;
}

Chapter AudiobookshelfClient::parseChapter(const std::string& json) {
    Chapter ch;
    ch.id = extractJsonInt(json, "id");
    ch.title = extractJsonValue(json, "title");
    ch.start = extractJsonFloat(json, "start");
    ch.end = extractJsonFloat(json, "end");
    return ch;
}

AudioTrack AudiobookshelfClient::parseAudioTrack(const std::string& json) {
    AudioTrack track;
    track.index = extractJsonInt(json, "index");
    track.title = extractJsonValue(json, "title");
    track.contentUrl = extractJsonValue(json, "contentUrl");
    track.startOffset = extractJsonFloat(json, "startOffset");
    track.duration = extractJsonFloat(json, "duration");
    track.mimeType = extractJsonValue(json, "mimeType");
    return track;
}

bool AudiobookshelfClient::login(const std::string& username, const std::string& password) {
    brls::Logger::info("Attempting login for user: {}", username);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/login");
    req.method = "POST";
    req.headers["Accept"] = "application/json";
    req.headers["Content-Type"] = "application/json";

    // Build JSON body
    req.body = "{\"username\":\"" + username + "\",\"password\":\"" + password + "\"}";

    HttpResponse resp = client.request(req);

    if (resp.statusCode == 200) {
        // Extract user object and token
        std::string userObj = extractJsonObject(resp.body, "user");
        m_authToken = extractJsonValue(userObj, "token");

        if (!m_authToken.empty()) {
            m_currentUser.id = extractJsonValue(userObj, "id");
            m_currentUser.username = extractJsonValue(userObj, "username");
            m_currentUser.token = m_authToken;
            m_currentUser.type = extractJsonValue(userObj, "type");

            brls::Logger::info("Login successful for user: {}", m_currentUser.username);
            Application::getInstance().setAuthToken(m_authToken);
            return true;
        }
    }

    brls::Logger::error("Login failed: {}", resp.statusCode);
    return false;
}

bool AudiobookshelfClient::validateToken() {
    if (m_authToken.empty()) return false;

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/me");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);
    return resp.statusCode == 200;
}

void AudiobookshelfClient::logout() {
    // Audiobookshelf doesn't have a logout endpoint, just clear local state
    m_authToken.clear();
    m_currentUser = User();
    Application::getInstance().setAuthToken("");
    Application::getInstance().setServerUrl("");
}

bool AudiobookshelfClient::fetchServerInfo(ServerInfo& info) {
    brls::Logger::info("Fetching server info from: {}", m_serverUrl);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/status");
    req.method = "GET";
    req.headers["Accept"] = "application/json";

    HttpResponse resp = client.request(req);

    if (resp.statusCode == 200) {
        info.isInit = extractJsonBool(resp.body, "isInit");
        info.authMethods = extractJsonValue(resp.body, "authMethods");
        info.serverName = extractJsonValue(resp.body, "serverName");

        // Try to get version from /ping endpoint
        HttpRequest pingReq;
        pingReq.url = buildApiUrl("/ping");
        pingReq.method = "GET";
        HttpResponse pingResp = client.request(pingReq);
        if (pingResp.statusCode == 200) {
            info.version = extractJsonValue(pingResp.body, "version");
        }

        m_serverInfo = info;
        brls::Logger::info("Server: {} v{}", info.serverName, info.version);
        return true;
    }

    brls::Logger::error("Failed to fetch server info: {}", resp.statusCode);
    return false;
}

bool AudiobookshelfClient::connectToServer(const std::string& url) {
    brls::Logger::info("Connecting to server: {}", url);

    m_serverUrl = url;

    // Verify connection with status endpoint
    ServerInfo info;
    if (fetchServerInfo(info)) {
        Application::getInstance().setServerUrl(url);
        return true;
    }

    // Try /ping as fallback
    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/ping");
    req.method = "GET";
    req.timeout = 30;

    HttpResponse resp = client.request(req);

    if (resp.statusCode == 200 || resp.body.find("pong") != std::string::npos) {
        brls::Logger::info("Connected to server (ping successful)");
        Application::getInstance().setServerUrl(url);
        return true;
    }

    brls::Logger::error("Connection failed: {}", resp.statusCode);
    return false;
}

bool AudiobookshelfClient::fetchCurrentUser(User& user) {
    brls::Logger::debug("Fetching current user");

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/me");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch current user: {}", resp.statusCode);
        return false;
    }

    user.id = extractJsonValue(resp.body, "id");
    user.username = extractJsonValue(resp.body, "username");
    user.type = extractJsonValue(resp.body, "type");
    user.isActive = extractJsonBool(resp.body, "isActive");
    user.token = m_authToken;

    m_currentUser = user;
    brls::Logger::info("Current user: {} ({})", user.username, user.type);
    return true;
}

bool AudiobookshelfClient::fetchItemsInProgress(std::vector<MediaItem>& items) {
    brls::Logger::debug("Fetching items in progress");

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/me/items-in-progress");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch items in progress: {}", resp.statusCode);
        return false;
    }

    items.clear();

    // Parse libraryItems array
    std::string itemsArray = extractJsonArray(resp.body, "libraryItems");
    if (itemsArray.empty()) {
        // Try direct array response
        itemsArray = resp.body;
    }

    size_t pos = 0;
    while ((pos = itemsArray.find("\"id\"", pos)) != std::string::npos) {
        size_t objStart = itemsArray.rfind('{', pos);
        if (objStart == std::string::npos) {
            pos++;
            continue;
        }

        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < itemsArray.length()) {
            if (itemsArray[objEnd] == '{') braceCount++;
            else if (itemsArray[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string obj = itemsArray.substr(objStart, objEnd - objStart);
        MediaItem item = parseMediaItem(obj);

        if (!item.id.empty() && !item.title.empty()) {
            items.push_back(item);
        }

        pos = objEnd;
    }

    brls::Logger::info("Found {} items in progress", items.size());
    return true;
}

bool AudiobookshelfClient::fetchListeningSessions(std::vector<PlaybackSession>& sessions) {
    brls::Logger::debug("Fetching listening sessions");

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/me/listening-sessions");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch listening sessions: {}", resp.statusCode);
        return false;
    }

    sessions.clear();

    size_t pos = 0;
    while ((pos = resp.body.find("\"id\"", pos)) != std::string::npos) {
        size_t objStart = resp.body.rfind('{', pos);
        if (objStart == std::string::npos) {
            pos++;
            continue;
        }

        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < resp.body.length()) {
            if (resp.body[objEnd] == '{') braceCount++;
            else if (resp.body[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string obj = resp.body.substr(objStart, objEnd - objStart);

        PlaybackSession session;
        session.id = extractJsonValue(obj, "id");
        session.libraryItemId = extractJsonValue(obj, "libraryItemId");
        session.episodeId = extractJsonValue(obj, "episodeId");
        session.mediaType = extractJsonValue(obj, "mediaType");
        session.currentTime = extractJsonFloat(obj, "currentTime");
        session.duration = extractJsonFloat(obj, "duration");
        session.playMethod = extractJsonValue(obj, "playMethod");
        session.updatedAt = extractJsonInt64(obj, "updatedAt");

        if (!session.id.empty()) {
            sessions.push_back(session);
        }

        pos = objEnd;
    }

    brls::Logger::info("Found {} listening sessions", sessions.size());
    return true;
}

bool AudiobookshelfClient::fetchLibraries(std::vector<Library>& libraries) {
    brls::Logger::debug("Fetching libraries");

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/libraries");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch libraries: {}", resp.statusCode);
        return false;
    }

    libraries.clear();

    // Parse libraries array
    std::string libsArray = extractJsonArray(resp.body, "libraries");
    if (libsArray.empty()) {
        libsArray = resp.body;
    }

    size_t pos = 0;
    while ((pos = libsArray.find("\"id\"", pos)) != std::string::npos) {
        size_t objStart = libsArray.rfind('{', pos);
        if (objStart == std::string::npos) {
            pos++;
            continue;
        }

        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < libsArray.length()) {
            if (libsArray[objEnd] == '{') braceCount++;
            else if (libsArray[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string obj = libsArray.substr(objStart, objEnd - objStart);

        Library lib;
        lib.id = extractJsonValue(obj, "id");
        lib.name = extractJsonValue(obj, "name");
        lib.icon = extractJsonValue(obj, "icon");
        lib.mediaType = extractJsonValue(obj, "mediaType");

        // Get stats for item count
        std::string statsObj = extractJsonObject(obj, "stats");
        if (!statsObj.empty()) {
            lib.itemCount = extractJsonInt(statsObj, "totalItems");
        }

        if (!lib.id.empty() && !lib.name.empty()) {
            libraries.push_back(lib);
        }

        pos = objEnd;
    }

    brls::Logger::info("Found {} libraries", libraries.size());
    return !libraries.empty();
}

bool AudiobookshelfClient::fetchLibrary(const std::string& libraryId, Library& library) {
    brls::Logger::debug("Fetching library: {}", libraryId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/libraries/" + libraryId);
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch library: {}", resp.statusCode);
        return false;
    }

    library.id = extractJsonValue(resp.body, "id");
    library.name = extractJsonValue(resp.body, "name");
    library.icon = extractJsonValue(resp.body, "icon");
    library.mediaType = extractJsonValue(resp.body, "mediaType");

    return true;
}

bool AudiobookshelfClient::fetchLibraryItems(const std::string& libraryId, std::vector<MediaItem>& items,
                                              int page, int limit, const std::string& sort) {
    brls::Logger::debug("Fetching library items: library={}, page={}, limit={}", libraryId, page, limit);

    HttpClient client;
    HttpRequest req;
    std::string url = buildApiUrl("/api/libraries/" + libraryId + "/items");
    url += "?page=" + std::to_string(page) + "&limit=" + std::to_string(limit);
    if (!sort.empty()) {
        url += "&sort=" + sort;
    }

    req.url = url;
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch library items: {}", resp.statusCode);
        return false;
    }

    items.clear();

    // Get library mediaType from response to set on items that don't have it
    std::string libraryMediaType = extractJsonValue(resp.body, "mediaType");
    if (libraryMediaType.empty()) {
        // Try to get it from the library info
        Library lib;
        if (fetchLibrary(libraryId, lib)) {
            libraryMediaType = lib.mediaType;
        }
    }
    MediaType defaultMediaType = parseMediaType(libraryMediaType);
    brls::Logger::debug("Library media type: {} (enum: {})", libraryMediaType, static_cast<int>(defaultMediaType));

    // Parse results array
    std::string resultsArray = extractJsonArray(resp.body, "results");
    if (resultsArray.empty()) {
        resultsArray = resp.body;
    }

    size_t pos = 0;
    while ((pos = resultsArray.find("\"id\"", pos)) != std::string::npos) {
        size_t objStart = resultsArray.rfind('{', pos);
        if (objStart == std::string::npos) {
            pos++;
            continue;
        }

        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < resultsArray.length()) {
            if (resultsArray[objEnd] == '{') braceCount++;
            else if (resultsArray[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string obj = resultsArray.substr(objStart, objEnd - objStart);
        MediaItem item = parseMediaItem(obj);

        // If mediaType wasn't set from item JSON, use library's mediaType
        if (item.mediaType == MediaType::UNKNOWN && defaultMediaType != MediaType::UNKNOWN) {
            item.mediaType = defaultMediaType;
            item.type = libraryMediaType;
        }

        if (!item.id.empty() && !item.title.empty()) {
            items.push_back(item);
        }

        pos = objEnd;
    }

    brls::Logger::info("Found {} items in library {}", items.size(), libraryId);
    return true;
}

bool AudiobookshelfClient::fetchLibraryPersonalized(const std::string& libraryId, std::vector<PersonalizedShelf>& shelves) {
    brls::Logger::debug("Fetching personalized content for library: {}", libraryId);

    // Get library info for mediaType
    Library lib;
    fetchLibrary(libraryId, lib);
    MediaType defaultMediaType = parseMediaType(lib.mediaType);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/libraries/" + libraryId + "/personalized");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch personalized content: {}", resp.statusCode);
        return false;
    }

    shelves.clear();

    // Parse shelves - the response is an array of shelf objects
    size_t pos = 0;
    while ((pos = resp.body.find("\"id\"", pos)) != std::string::npos) {
        size_t objStart = resp.body.rfind('{', pos);
        if (objStart == std::string::npos) {
            pos++;
            continue;
        }

        // Check if this is a shelf object (has "label" field nearby)
        size_t checkEnd = std::min(objStart + 500, resp.body.length());
        std::string checkStr = resp.body.substr(objStart, checkEnd - objStart);
        if (checkStr.find("\"label\"") == std::string::npos &&
            checkStr.find("\"labelStringKey\"") == std::string::npos) {
            pos++;
            continue;
        }

        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < resp.body.length()) {
            if (resp.body[objEnd] == '{') braceCount++;
            else if (resp.body[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string obj = resp.body.substr(objStart, objEnd - objStart);

        PersonalizedShelf shelf;
        shelf.id = extractJsonValue(obj, "id");
        shelf.label = extractJsonValue(obj, "label");
        shelf.labelStringKey = extractJsonValue(obj, "labelStringKey");
        shelf.type = extractJsonValue(obj, "type");

        // Parse entities array
        std::string entitiesArray = extractJsonArray(obj, "entities");
        if (!entitiesArray.empty()) {
            size_t entPos = 0;
            while ((entPos = entitiesArray.find("\"id\"", entPos)) != std::string::npos) {
                size_t entStart = entitiesArray.rfind('{', entPos);
                if (entStart == std::string::npos) {
                    entPos++;
                    continue;
                }

                int entBraceCount = 1;
                size_t entEnd = entStart + 1;
                while (entBraceCount > 0 && entEnd < entitiesArray.length()) {
                    if (entitiesArray[entEnd] == '{') entBraceCount++;
                    else if (entitiesArray[entEnd] == '}') entBraceCount--;
                    entEnd++;
                }

                std::string entObj = entitiesArray.substr(entStart, entEnd - entStart);
                MediaItem item = parseMediaItem(entObj);

                // Set mediaType from library if not set
                if (item.mediaType == MediaType::UNKNOWN && defaultMediaType != MediaType::UNKNOWN) {
                    item.mediaType = defaultMediaType;
                    item.type = lib.mediaType;
                }

                if (!item.id.empty() && !item.title.empty()) {
                    shelf.entities.push_back(item);
                }

                entPos = entEnd;
            }
        }

        if (!shelf.label.empty() || !shelf.labelStringKey.empty()) {
            brls::Logger::debug("fetchLibraryPersonalized: Found shelf id='{}' label='{}' labelStringKey='{}' type='{}' entities={}",
                               shelf.id, shelf.label, shelf.labelStringKey, shelf.type, shelf.entities.size());
            shelves.push_back(shelf);
        }

        pos = objEnd;
    }

    brls::Logger::info("Found {} personalized shelves", shelves.size());
    return true;
}

bool AudiobookshelfClient::fetchLibrarySeries(const std::string& libraryId, std::vector<Series>& series) {
    brls::Logger::debug("Fetching series for library: {}", libraryId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/libraries/" + libraryId + "/series");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch series: {}", resp.statusCode);
        return false;
    }

    series.clear();

    std::string resultsArray = extractJsonArray(resp.body, "results");
    if (resultsArray.empty()) {
        resultsArray = resp.body;
    }

    size_t pos = 0;
    while ((pos = resultsArray.find("\"id\"", pos)) != std::string::npos) {
        size_t objStart = resultsArray.rfind('{', pos);
        if (objStart == std::string::npos) {
            pos++;
            continue;
        }

        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < resultsArray.length()) {
            if (resultsArray[objEnd] == '{') braceCount++;
            else if (resultsArray[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string obj = resultsArray.substr(objStart, objEnd - objStart);

        Series s;
        s.id = extractJsonValue(obj, "id");
        s.name = extractJsonValue(obj, "name");

        if (!s.id.empty() && !s.name.empty()) {
            series.push_back(s);
        }

        pos = objEnd;
    }

    brls::Logger::info("Found {} series", series.size());
    return true;
}

bool AudiobookshelfClient::fetchLibraryCollections(const std::string& libraryId, std::vector<Collection>& collections) {
    brls::Logger::debug("Fetching collections for library: {}", libraryId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/libraries/" + libraryId + "/collections");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch collections: {}", resp.statusCode);
        return false;
    }

    collections.clear();

    std::string resultsArray = extractJsonArray(resp.body, "results");
    if (resultsArray.empty()) {
        resultsArray = resp.body;
    }

    size_t pos = 0;
    while ((pos = resultsArray.find("\"id\"", pos)) != std::string::npos) {
        size_t objStart = resultsArray.rfind('{', pos);
        if (objStart == std::string::npos) {
            pos++;
            continue;
        }

        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < resultsArray.length()) {
            if (resultsArray[objEnd] == '{') braceCount++;
            else if (resultsArray[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string obj = resultsArray.substr(objStart, objEnd - objStart);

        Collection c;
        c.id = extractJsonValue(obj, "id");
        c.libraryId = extractJsonValue(obj, "libraryId");
        c.name = extractJsonValue(obj, "name");
        c.description = extractJsonValue(obj, "description");
        c.bookCount = extractJsonInt(obj, "numBooks");

        if (!c.id.empty() && !c.name.empty()) {
            collections.push_back(c);
        }

        pos = objEnd;
    }

    brls::Logger::info("Found {} collections", collections.size());
    return true;
}

bool AudiobookshelfClient::fetchLibraryAuthors(const std::string& libraryId, std::vector<Author>& authors) {
    brls::Logger::debug("Fetching authors for library: {}", libraryId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/libraries/" + libraryId + "/authors");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch authors: {}", resp.statusCode);
        return false;
    }

    authors.clear();

    std::string authorsArray = extractJsonArray(resp.body, "authors");
    if (authorsArray.empty()) {
        authorsArray = resp.body;
    }

    size_t pos = 0;
    while ((pos = authorsArray.find("\"id\"", pos)) != std::string::npos) {
        size_t objStart = authorsArray.rfind('{', pos);
        if (objStart == std::string::npos) {
            pos++;
            continue;
        }

        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < authorsArray.length()) {
            if (authorsArray[objEnd] == '{') braceCount++;
            else if (authorsArray[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string obj = authorsArray.substr(objStart, objEnd - objStart);

        Author a;
        a.id = extractJsonValue(obj, "id");
        a.name = extractJsonValue(obj, "name");
        a.description = extractJsonValue(obj, "description");
        a.imagePath = extractJsonValue(obj, "imagePath");

        if (!a.id.empty() && !a.name.empty()) {
            authors.push_back(a);
        }

        pos = objEnd;
    }

    brls::Logger::info("Found {} authors", authors.size());
    return true;
}

bool AudiobookshelfClient::fetchRecentlyAdded(const std::string& libraryId, std::vector<MediaItem>& items) {
    brls::Logger::debug("Fetching recently added for library: {}", libraryId);

    // Get library info for mediaType
    Library lib;
    fetchLibrary(libraryId, lib);
    MediaType defaultMediaType = parseMediaType(lib.mediaType);

    // Use library items with sort by addedAt descending
    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/libraries/" + libraryId + "/items?sort=addedAt&desc=1&limit=50");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch recently added: {}", resp.statusCode);
        return false;
    }

    items.clear();

    std::string resultsArray = extractJsonArray(resp.body, "results");
    if (resultsArray.empty()) {
        resultsArray = resp.body;
    }

    size_t pos = 0;
    while ((pos = resultsArray.find("\"id\"", pos)) != std::string::npos) {
        size_t objStart = resultsArray.rfind('{', pos);
        if (objStart == std::string::npos) {
            pos++;
            continue;
        }

        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < resultsArray.length()) {
            if (resultsArray[objEnd] == '{') braceCount++;
            else if (resultsArray[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string obj = resultsArray.substr(objStart, objEnd - objStart);
        MediaItem item = parseMediaItem(obj);

        // Set mediaType from library if not set
        if (item.mediaType == MediaType::UNKNOWN && defaultMediaType != MediaType::UNKNOWN) {
            item.mediaType = defaultMediaType;
            item.type = lib.mediaType;
        }

        if (!item.id.empty() && !item.title.empty()) {
            items.push_back(item);
        }

        pos = objEnd;
    }

    brls::Logger::info("Found {} recently added items", items.size());
    return true;
}

bool AudiobookshelfClient::fetchItem(const std::string& itemId, MediaItem& item) {
    brls::Logger::debug("Fetching item: {}", itemId);

    HttpClient client;
    HttpRequest req;
    // Use expanded=1 to get full response including chapters and audio files
    req.url = buildApiUrl("/api/items/" + itemId + "?expanded=1&include=progress");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch item: {}", resp.statusCode);
        return false;
    }

    brls::Logger::debug("Response body length: {} chars", resp.body.length());

    item = parseMediaItem(resp.body);

    // Extract media object for chapters and tracks
    // Kodi addon: item.get('media', {}).get('chapters', [])
    std::string mediaObj = extractJsonObject(resp.body, "media");
    brls::Logger::info("Media object found: {} ({} chars)", !mediaObj.empty() ? "yes" : "no", mediaObj.length());

    // Debug: show part of media object to verify structure
    if (!mediaObj.empty() && mediaObj.length() > 100) {
        brls::Logger::debug("Media object preview: {}", mediaObj.substr(0, 300));
    }

    // Parse chapters from media.chapters using extractJsonArray (like Kodi addon does)
    std::string chaptersArray = extractJsonArray(mediaObj, "chapters");
    brls::Logger::info("Chapters array: {} chars", chaptersArray.length());
    if (!chaptersArray.empty() && chaptersArray.length() > 10) {
        brls::Logger::debug("Chapters preview: {}", chaptersArray.substr(0, std::min((size_t)200, chaptersArray.length())));
    }

    // Parse individual chapters from media.chapters
    if (!chaptersArray.empty() && chaptersArray != "[]") {
        brls::Logger::debug("Parsing chapters array from media.chapters...");
        size_t pos = 0;

        // Look for chapter objects - they have "start" field
        while ((pos = chaptersArray.find("\"start\"", pos)) != std::string::npos) {
            // Find the start of this chapter object
            size_t objStart = chaptersArray.rfind('{', pos);
            if (objStart == std::string::npos) {
                pos++;
                continue;
            }

            // Find the end of this chapter object
            int braceCount = 1;
            size_t objEnd = objStart + 1;
            while (braceCount > 0 && objEnd < chaptersArray.length()) {
                if (chaptersArray[objEnd] == '{') braceCount++;
                else if (chaptersArray[objEnd] == '}') braceCount--;
                objEnd++;
            }

            std::string chObj = chaptersArray.substr(objStart, objEnd - objStart);
            Chapter ch = parseChapter(chObj);

            // Add chapter if it looks valid
            if (ch.end > ch.start) {
                item.chapters.push_back(ch);
                brls::Logger::debug("Added chapter: '{}' ({:.1f}s - {:.1f}s)",
                    ch.title, ch.start, ch.end);
            }

            pos = objEnd;
        }
        brls::Logger::info("Parsed {} chapters from media.chapters", item.chapters.size());
    } else {
        brls::Logger::debug("No chapters in media.chapters, will check audioFiles");
    }

    // If no chapters found in media.chapters, check audioFiles[0].chapters (M4B files)
    if (item.chapters.empty() && !mediaObj.empty()) {
        std::string audioFilesArray = extractJsonArray(mediaObj, "audioFiles");
        if (!audioFilesArray.empty()) {
            // Get first audio file object
            size_t firstObjStart = audioFilesArray.find('{');
            if (firstObjStart != std::string::npos) {
                int braceCount = 1;
                size_t firstObjEnd = firstObjStart + 1;
                while (braceCount > 0 && firstObjEnd < audioFilesArray.length()) {
                    if (audioFilesArray[firstObjEnd] == '{') braceCount++;
                    else if (audioFilesArray[firstObjEnd] == '}') braceCount--;
                    firstObjEnd++;
                }
                std::string firstAudioFile = audioFilesArray.substr(firstObjStart, firstObjEnd - firstObjStart);

                // Try to get chapters from this audio file using extractJsonArray
                std::string afChaptersArray = extractJsonArray(firstAudioFile, "chapters");
                if (!afChaptersArray.empty() && afChaptersArray != "[]") {
                    brls::Logger::debug("Found chapters in audioFiles[0]: {} chars", afChaptersArray.length());
                    size_t pos = 0;
                    while ((pos = afChaptersArray.find("\"start\"", pos)) != std::string::npos) {
                        size_t objStart = afChaptersArray.rfind('{', pos);
                        if (objStart == std::string::npos) { pos++; continue; }

                        int bCount = 1;
                        size_t objEnd = objStart + 1;
                        while (bCount > 0 && objEnd < afChaptersArray.length()) {
                            if (afChaptersArray[objEnd] == '{') bCount++;
                            else if (afChaptersArray[objEnd] == '}') bCount--;
                            objEnd++;
                        }

                        std::string chObj = afChaptersArray.substr(objStart, objEnd - objStart);
                        Chapter ch = parseChapter(chObj);
                        if (ch.end > ch.start) {
                            item.chapters.push_back(ch);
                            brls::Logger::debug("Added chapter from audioFile: '{}' ({:.1f}s - {:.1f}s)",
                                ch.title, ch.start, ch.end);
                        }
                        pos = objEnd;
                    }
                    brls::Logger::info("Parsed {} chapters from audioFiles[0].chapters", item.chapters.size());
                }
            }
        }
    }

    if (item.chapters.empty()) {
        brls::Logger::warning("No chapters found in media.chapters or audioFiles");
    }

    // Parse audio tracks
    std::string tracksArray = extractJsonArray(resp.body, "audioFiles");
    if (tracksArray.empty() && !mediaObj.empty()) {
        tracksArray = extractJsonArray(mediaObj, "audioFiles");
    }
    if (!tracksArray.empty()) {
        size_t pos = 0;
        int trackIdx = 0;
        while ((pos = tracksArray.find("\"ino\"", pos)) != std::string::npos) {
            size_t objStart = tracksArray.rfind('{', pos);
            if (objStart == std::string::npos) {
                pos++;
                continue;
            }

            int braceCount = 1;
            size_t objEnd = objStart + 1;
            while (braceCount > 0 && objEnd < tracksArray.length()) {
                if (tracksArray[objEnd] == '{') braceCount++;
                else if (tracksArray[objEnd] == '}') braceCount--;
                objEnd++;
            }

            std::string trackObj = tracksArray.substr(objStart, objEnd - objStart);
            AudioTrack track;
            track.index = trackIdx++;
            std::string metaObj = extractJsonObject(trackObj, "metadata");
            track.title = extractJsonValue(trackObj, "metadata");
            if (track.title.empty() && !metaObj.empty()) {
                // Try getting filename from metadata object
                track.title = extractJsonValue(metaObj, "filename");
            }
            track.duration = extractJsonFloat(trackObj, "duration");
            track.mimeType = extractJsonValue(trackObj, "mimeType");

            // Parse startOffset for multi-file audiobooks (used for chapter generation)
            if (!metaObj.empty()) {
                // Try different field names for start offset
                float offset = extractJsonFloat(metaObj, "startOffset");
                if (offset == 0.0f && trackIdx > 1) {
                    // Calculate from previous tracks if not explicitly set
                    float accumulatedDuration = 0.0f;
                    for (const auto& prevTrack : item.audioTracks) {
                        accumulatedDuration += prevTrack.duration;
                    }
                    track.startOffset = accumulatedDuration;
                } else {
                    track.startOffset = offset;
                }
            }

            item.audioTracks.push_back(track);

            pos = objEnd;
        }
    }

    // If still no chapters and we have multiple audio files, create chapters from files
    if (item.chapters.empty() && item.audioTracks.size() > 1) {
        brls::Logger::info("Creating chapters from {} audio files", item.audioTracks.size());
        float currentOffset = 0.0f;
        for (size_t i = 0; i < item.audioTracks.size(); ++i) {
            const auto& track = item.audioTracks[i];
            Chapter ch;
            ch.id = static_cast<int>(i);
            ch.title = track.title;
            // Clean up filename-based titles (remove extension)
            size_t dotPos = ch.title.rfind('.');
            if (dotPos != std::string::npos && dotPos > ch.title.length() - 6) {
                ch.title = ch.title.substr(0, dotPos);
            }
            ch.start = (track.startOffset > 0) ? track.startOffset : currentOffset;
            ch.end = ch.start + track.duration;
            currentOffset = ch.end;

            if (ch.end > ch.start) {
                item.chapters.push_back(ch);
                brls::Logger::debug("Created chapter from file: '{}' ({:.1f}s - {:.1f}s)",
                    ch.title, ch.start, ch.end);
            }
        }
        brls::Logger::info("Created {} chapters from audio files", item.chapters.size());
    }

    brls::Logger::info("Fetched item: {} ({} chapters, {} tracks)",
                        item.title, item.chapters.size(), item.audioTracks.size());
    return true;
}

bool AudiobookshelfClient::fetchItemWithProgress(const std::string& itemId, MediaItem& item) {
    brls::Logger::debug("Fetching item with progress: {}", itemId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/items/" + itemId + "?include=progress&expanded=1");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch item with progress: {}", resp.statusCode);
        return false;
    }

    item = parseMediaItem(resp.body);

    // Extract chapters from media.chapters using extractJsonArray (same as fetchItem)
    std::string mediaObj = extractJsonObject(resp.body, "media");
    if (!mediaObj.empty()) {
        // First try media.chapters
        std::string chaptersArray = extractJsonArray(mediaObj, "chapters");
        if (!chaptersArray.empty() && chaptersArray != "[]") {
            size_t pos = 0;
            while ((pos = chaptersArray.find("\"start\"", pos)) != std::string::npos) {
                size_t objStart = chaptersArray.rfind('{', pos);
                if (objStart == std::string::npos) { pos++; continue; }

                int braceCount = 1;
                size_t objEnd = objStart + 1;
                while (braceCount > 0 && objEnd < chaptersArray.length()) {
                    if (chaptersArray[objEnd] == '{') braceCount++;
                    else if (chaptersArray[objEnd] == '}') braceCount--;
                    objEnd++;
                }

                std::string chObj = chaptersArray.substr(objStart, objEnd - objStart);
                Chapter ch = parseChapter(chObj);
                if (ch.end > ch.start) {
                    item.chapters.push_back(ch);
                }
                pos = objEnd;
            }
            brls::Logger::debug("Parsed {} chapters from media.chapters", item.chapters.size());
        }

        // If no chapters found, check audioFiles[0].chapters (M4B files)
        if (item.chapters.empty()) {
            std::string audioFilesArray = extractJsonArray(mediaObj, "audioFiles");
            if (!audioFilesArray.empty()) {
                size_t firstObjStart = audioFilesArray.find('{');
                if (firstObjStart != std::string::npos) {
                    int braceCount = 1;
                    size_t firstObjEnd = firstObjStart + 1;
                    while (braceCount > 0 && firstObjEnd < audioFilesArray.length()) {
                        if (audioFilesArray[firstObjEnd] == '{') braceCount++;
                        else if (audioFilesArray[firstObjEnd] == '}') braceCount--;
                        firstObjEnd++;
                    }
                    std::string firstAudioFile = audioFilesArray.substr(firstObjStart, firstObjEnd - firstObjStart);

                    std::string afChaptersArray = extractJsonArray(firstAudioFile, "chapters");
                    if (!afChaptersArray.empty() && afChaptersArray != "[]") {
                        size_t pos = 0;
                        while ((pos = afChaptersArray.find("\"start\"", pos)) != std::string::npos) {
                            size_t objStart = afChaptersArray.rfind('{', pos);
                            if (objStart == std::string::npos) { pos++; continue; }

                            int bCount = 1;
                            size_t objEnd = objStart + 1;
                            while (bCount > 0 && objEnd < afChaptersArray.length()) {
                                if (afChaptersArray[objEnd] == '{') bCount++;
                                else if (afChaptersArray[objEnd] == '}') bCount--;
                                objEnd++;
                            }

                            std::string chObj = afChaptersArray.substr(objStart, objEnd - objStart);
                            Chapter ch = parseChapter(chObj);
                            if (ch.end > ch.start) {
                                item.chapters.push_back(ch);
                            }
                            pos = objEnd;
                        }
                        brls::Logger::debug("Parsed {} chapters from audioFiles[0].chapters", item.chapters.size());
                    }
                }
            }
        }
    }

    return true;
}

bool AudiobookshelfClient::search(const std::string& libraryId, const std::string& query, std::vector<MediaItem>& results) {
    brls::Logger::debug("Searching library {} for: {}", libraryId, query);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/libraries/" + libraryId + "/search?q=" + HttpClient::urlEncode(query));
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Search failed: {}", resp.statusCode);
        return false;
    }

    results.clear();

    // Parse book results
    std::string booksArray = extractJsonArray(resp.body, "book");
    if (booksArray.empty()) {
        booksArray = extractJsonArray(resp.body, "books");
    }
    if (!booksArray.empty()) {
        size_t pos = 0;
        while ((pos = booksArray.find("\"libraryItem\"", pos)) != std::string::npos) {
            std::string itemObj = extractJsonObject(booksArray.substr(pos), "libraryItem");
            if (!itemObj.empty()) {
                MediaItem item = parseMediaItem(itemObj);
                if (!item.id.empty() && !item.title.empty()) {
                    results.push_back(item);
                }
            }
            pos++;
        }
    }

    // Also parse podcast results
    std::string podcastsArray = extractJsonArray(resp.body, "podcast");
    if (podcastsArray.empty()) {
        podcastsArray = extractJsonArray(resp.body, "podcasts");
    }
    if (!podcastsArray.empty()) {
        size_t pos = 0;
        while ((pos = podcastsArray.find("\"libraryItem\"", pos)) != std::string::npos) {
            std::string itemObj = extractJsonObject(podcastsArray.substr(pos), "libraryItem");
            if (!itemObj.empty()) {
                MediaItem item = parseMediaItem(itemObj);
                if (!item.id.empty() && !item.title.empty()) {
                    results.push_back(item);
                }
            }
            pos++;
        }
    }

    brls::Logger::info("Found {} search results for '{}'", results.size(), query);
    return true;
}

bool AudiobookshelfClient::searchAll(const std::string& query, std::vector<MediaItem>& results) {
    brls::Logger::debug("Searching all libraries for: {}", query);

    // Get all libraries and search each
    std::vector<Library> libraries;
    if (!fetchLibraries(libraries)) {
        return false;
    }

    results.clear();
    for (const auto& lib : libraries) {
        std::vector<MediaItem> libResults;
        if (search(lib.id, query, libResults)) {
            results.insert(results.end(), libResults.begin(), libResults.end());
        }
    }

    brls::Logger::info("Found {} total search results for '{}'", results.size(), query);
    return true;
}

bool AudiobookshelfClient::startPlaybackSession(const std::string& itemId, PlaybackSession& session,
                                                  const std::string& episodeId) {
    brls::Logger::debug("Starting playback session for item: {}", itemId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/items/" + itemId + "/play");
    if (!episodeId.empty()) {
        req.url += "/" + episodeId;
    }
    req.method = "POST";
    req.headers["Accept"] = "application/json";
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    // Request body with device info - force direct play for Vita
    req.body = "{\"deviceInfo\":{\"clientName\":\"VitaSuwayomi\",\"clientVersion\":\"1.0.0\",\"deviceId\":\"vita-abs-client\"},\"forceDirectPlay\":true,\"forceTranscode\":false,\"supportedMimeTypes\":[\"audio/mpeg\",\"audio/mp4\",\"audio/x-m4a\",\"audio/aac\",\"audio/ogg\",\"audio/flac\"]}";

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to start playback session: {}", resp.statusCode);
        return false;
    }

    session.id = extractJsonValue(resp.body, "id");
    session.libraryItemId = itemId;
    session.episodeId = episodeId;
    session.currentTime = extractJsonFloat(resp.body, "currentTime");
    session.duration = extractJsonFloat(resp.body, "duration");
    session.playMethod = extractJsonValue(resp.body, "playMethod");

    // Parse audioTracks array to get streaming URLs
    session.audioTracks.clear();
    std::string tracksArray = extractJsonArray(resp.body, "audioTracks");
    brls::Logger::debug("audioTracks array length: {}", tracksArray.length());

    // Debug: show preview of audioTracks array
    if (!tracksArray.empty() && tracksArray.length() > 50) {
        brls::Logger::debug("audioTracks preview: {}", tracksArray.substr(0, std::min((size_t)300, tracksArray.length())));
    }

    if (!tracksArray.empty()) {
        // Properly iterate through array - find each top-level object
        size_t pos = 0;
        int trackCount = 0;

        // Skip opening bracket of array
        while (pos < tracksArray.length() && tracksArray[pos] != '{') {
            pos++;
        }

        while (pos < tracksArray.length()) {
            // Find start of object
            if (tracksArray[pos] != '{') {
                pos++;
                continue;
            }

            size_t objStart = pos;
            int braceCount = 1;
            pos++; // Move past opening brace

            // Find matching closing brace
            while (braceCount > 0 && pos < tracksArray.length()) {
                if (tracksArray[pos] == '{') braceCount++;
                else if (tracksArray[pos] == '}') braceCount--;
                pos++;
            }

            std::string trackObj = tracksArray.substr(objStart, pos - objStart);
            trackCount++;
            brls::Logger::debug("Track #{} object ({} chars): {}", trackCount, trackObj.length(),
                               trackObj.substr(0, std::min((size_t)200, trackObj.length())));

            AudioTrack track;
            track.index = extractJsonInt(trackObj, "index");
            track.title = extractJsonValue(trackObj, "title");
            track.contentUrl = extractJsonValue(trackObj, "contentUrl");
            track.startOffset = extractJsonFloat(trackObj, "startOffset");
            track.duration = extractJsonFloat(trackObj, "duration");
            track.mimeType = extractJsonValue(trackObj, "mimeType");

            brls::Logger::debug("Parsed track: index={}, title={}, duration={}, contentUrl={}",
                               track.index, track.title, track.duration, track.contentUrl);

            if (!track.contentUrl.empty()) {
                session.audioTracks.push_back(track);
                brls::Logger::debug("Added audio track {} url={}", track.index, track.contentUrl);
            } else {
                brls::Logger::warning("Track #{} has empty contentUrl", trackCount);
            }

            // Skip to next object (skip commas, whitespace)
            while (pos < tracksArray.length() && tracksArray[pos] != '{') {
                pos++;
            }
        }
        brls::Logger::debug("Finished parsing audioTracks, found {} track objects", trackCount);
    } else {
        brls::Logger::warning("audioTracks array is empty");
    }

    brls::Logger::info("Started playback session: {} with {} audio tracks", session.id, session.audioTracks.size());
    return true;
}

bool AudiobookshelfClient::syncPlaybackSession(const std::string& sessionId, float currentTime, float duration) {
    brls::Logger::debug("Syncing playback session: {} at {}s", sessionId, currentTime);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/session/" + sessionId + "/sync");
    req.method = "POST";
    req.headers["Accept"] = "application/json";
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    char body[256];
    snprintf(body, sizeof(body),
             "{\"currentTime\":%.2f,\"duration\":%.2f,\"timeListened\":1}",
             currentTime, duration);
    req.body = body;

    HttpResponse resp = client.request(req);
    return resp.statusCode == 200;
}

bool AudiobookshelfClient::closePlaybackSession(const std::string& sessionId, float currentTime,
                                                  float duration, float timeListened) {
    brls::Logger::debug("Closing playback session: {}", sessionId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/session/" + sessionId + "/close");
    req.method = "POST";
    req.headers["Accept"] = "application/json";
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    char body[256];
    snprintf(body, sizeof(body),
             "{\"currentTime\":%.2f,\"duration\":%.2f,\"timeListened\":%.2f}",
             currentTime, duration, timeListened);
    req.body = body;

    HttpResponse resp = client.request(req);
    return resp.statusCode == 200;
}

std::string AudiobookshelfClient::getStreamUrl(const std::string& itemId, const std::string& episodeId) {
    // This method now expects a relative contentUrl from a playback session's audioTracks
    // If itemId looks like a relative URL (starts with /), use it directly
    if (!itemId.empty() && itemId[0] == '/') {
        std::string url = m_serverUrl + itemId;
        // Add token if not already present
        if (url.find("token=") == std::string::npos) {
            url += (url.find('?') != std::string::npos ? "&" : "?");
            url += "token=" + m_authToken;
        }
        return url;
    }

    // Fallback: build direct file URL (for first audio file)
    // This is a fallback and may not work for all items
    std::string url = m_serverUrl + "/api/items/" + itemId + "/file/0";
    url += "?token=" + m_authToken;
    return url;
}

std::string AudiobookshelfClient::getDirectStreamUrl(const std::string& itemId, int fileIndex) {
    // Direct file streaming URL
    std::string url = m_serverUrl + "/api/items/" + itemId + "/file/" + std::to_string(fileIndex);
    url += "?token=" + m_authToken;
    return url;
}

std::string AudiobookshelfClient::getFileDownloadUrl(const std::string& itemId, const std::string& episodeId) {
    // Fetch item to get file info (like Kodi addon does)
    // Kodi: item = self.get_library_item_by_id(iid, expanded=1, episode=episode_id)
    brls::Logger::info("Getting file download URL for item: {}, episode: {}",
                       itemId, episodeId.empty() ? "(none)" : episodeId);

    HttpClient client;
    HttpRequest req;
    // Use expanded=1 for file URL like Kodi does in get_file_url()
    req.url = buildApiUrl("/api/items/" + itemId + "?expanded=1");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to get item for download URL: {}", resp.statusCode);
        return "";
    }

    brls::Logger::debug("Response length: {} chars", resp.body.length());

    std::string fileIno;
    std::string mediaObj = extractJsonObject(resp.body, "media");

    if (mediaObj.empty()) {
        brls::Logger::error("Media object not found in response");
        return "";
    }

    brls::Logger::debug("Media object: {} chars", mediaObj.length());

    if (!episodeId.empty()) {
        // Podcast episode - find the episode and get its audioFile.ino
        // Kodi: episodes = item.get('media', {}).get('episodes', [])
        brls::Logger::info("Looking for podcast episode: {}", episodeId);

        std::string episodesArray = extractJsonArray(mediaObj, "episodes");
        brls::Logger::debug("Episodes array: {} chars", episodesArray.length());

        if (!episodesArray.empty()) {
            size_t pos = 0;
            while ((pos = episodesArray.find("\"id\"", pos)) != std::string::npos) {
                size_t objStart = episodesArray.rfind('{', pos);
                if (objStart == std::string::npos) { pos++; continue; }

                int braceCount = 1;
                size_t objEnd = objStart + 1;
                while (braceCount > 0 && objEnd < episodesArray.length()) {
                    if (episodesArray[objEnd] == '{') braceCount++;
                    else if (episodesArray[objEnd] == '}') braceCount--;
                    objEnd++;
                }

                std::string epObj = episodesArray.substr(objStart, objEnd - objStart);
                std::string epId = extractJsonValue(epObj, "id");

                if (epId == episodeId) {
                    brls::Logger::info("Found episode: {}", episodeId);
                    // Kodi: audio_file = episode_data['audioFile']
                    //       ino = audio_file.get('ino')
                    std::string audioFileObj = extractJsonObject(epObj, "audioFile");
                    if (!audioFileObj.empty()) {
                        fileIno = extractJsonValue(audioFileObj, "ino");
                        brls::Logger::info("Episode audio file ino: {}", fileIno);
                    } else {
                        brls::Logger::warning("Episode has no audioFile - not downloaded on server?");
                    }
                    break;
                }
                pos = objEnd;
            }

            if (fileIno.empty()) {
                brls::Logger::error("Episode {} not found in episodes list", episodeId);
            }
        } else {
            brls::Logger::error("No episodes array found in media object");
        }
    } else {
        // Audiobook - get audio file ino from media.audioFiles
        // Kodi: audio_files = media.get('audioFiles', [])
        //       sorted_files = sorted(audio_files, key=lambda x: x.get('index', 0))
        //       ino = sorted_files[0].get('ino')
        brls::Logger::info("Looking for audiobook audio files");

        std::string audioFilesArray = extractJsonArray(mediaObj, "audioFiles");
        brls::Logger::debug("audioFiles array: {} chars", audioFilesArray.length());

        if (!audioFilesArray.empty()) {
            // Find first audio file (lowest index)
            size_t objStart = audioFilesArray.find('{');
            if (objStart != std::string::npos) {
                int braceCount = 1;
                size_t objEnd = objStart + 1;
                while (braceCount > 0 && objEnd < audioFilesArray.length()) {
                    if (audioFilesArray[objEnd] == '{') braceCount++;
                    else if (audioFilesArray[objEnd] == '}') braceCount--;
                    objEnd++;
                }
                std::string firstFile = audioFilesArray.substr(objStart, objEnd - objStart);
                fileIno = extractJsonValue(firstFile, "ino");
                brls::Logger::info("First audio file ino: {}", fileIno);
            }
        }

        // Fallback: check media.tracks for contentUrl (single file like m4b)
        // Kodi: tracks = media.get('tracks', [])
        //       content_url = tracks[0].get('contentUrl')
        if (fileIno.empty()) {
            brls::Logger::debug("No ino found, checking tracks for contentUrl");
            std::string tracksArray = extractJsonArray(mediaObj, "tracks");
            if (!tracksArray.empty()) {
                size_t objStart = tracksArray.find('{');
                if (objStart != std::string::npos) {
                    int braceCount = 1;
                    size_t objEnd = objStart + 1;
                    while (braceCount > 0 && objEnd < tracksArray.length()) {
                        if (tracksArray[objEnd] == '{') braceCount++;
                        else if (tracksArray[objEnd] == '}') braceCount--;
                        objEnd++;
                    }
                    std::string firstTrack = tracksArray.substr(objStart, objEnd - objStart);
                    std::string contentUrl = extractJsonValue(firstTrack, "contentUrl");
                    if (!contentUrl.empty()) {
                        // Use contentUrl directly
                        std::string url = m_serverUrl + contentUrl + "?token=" + m_authToken;
                        brls::Logger::info("Using track contentUrl: {}", url);
                        return url;
                    }
                }
            }
        }

        // Fallback to libraryFiles if audioFiles doesn't have ino
        if (fileIno.empty()) {
            brls::Logger::debug("Trying libraryFiles fallback");
            std::string libFilesArray = extractJsonArray(resp.body, "libraryFiles");
            if (!libFilesArray.empty()) {
                // Find first audio file
                size_t pos = 0;
                while ((pos = libFilesArray.find("\"ino\"", pos)) != std::string::npos) {
                    size_t objStart = libFilesArray.rfind('{', pos);
                    if (objStart == std::string::npos) { pos++; continue; }

                    int braceCount = 1;
                    size_t objEnd = objStart + 1;
                    while (braceCount > 0 && objEnd < libFilesArray.length()) {
                        if (libFilesArray[objEnd] == '{') braceCount++;
                        else if (libFilesArray[objEnd] == '}') braceCount--;
                        objEnd++;
                    }

                    std::string fileObj = libFilesArray.substr(objStart, objEnd - objStart);
                    std::string fileType = extractJsonValue(fileObj, "fileType");

                    // Check if it's an audio file
                    if (fileType == "audio") {
                        fileIno = extractJsonValue(fileObj, "ino");
                        break;
                    }
                    pos = objEnd;
                }
            }
        }
    }

    if (fileIno.empty()) {
        brls::Logger::error("Could not find file ino for item: {}", itemId);
        // Fallback to old method
        return getDirectStreamUrl(itemId, 0);
    }

    // Build URL: /api/items/{id}/file/{ino}?token={token}
    std::string url = m_serverUrl + "/api/items/" + itemId + "/file/" + fileIno;
    url += "?token=" + m_authToken;

    brls::Logger::debug("File download URL: {}", url);
    return url;
}

bool AudiobookshelfClient::getAudioFiles(const std::string& itemId, std::vector<AudioFileInfo>& files) {
    brls::Logger::debug("Getting audio files for item: {}", itemId);

    files.clear();

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/items/" + itemId);
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to get item for audio files: {}", resp.statusCode);
        return false;
    }

    std::string mediaObj = extractJsonObject(resp.body, "media");
    std::string audioFilesArray = extractJsonArray(mediaObj, "audioFiles");

    if (audioFilesArray.empty()) {
        brls::Logger::debug("No audio files in item");
        return false;
    }

    // Parse each audio file
    size_t pos = 0;
    while (true) {
        size_t objStart = audioFilesArray.find('{', pos);
        if (objStart == std::string::npos) break;

        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < audioFilesArray.length()) {
            if (audioFilesArray[objEnd] == '{') braceCount++;
            else if (audioFilesArray[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string fileObj = audioFilesArray.substr(objStart, objEnd - objStart);

        AudioFileInfo info;
        info.ino = extractJsonValue(fileObj, "ino");

        // Get metadata from nested object
        std::string metadataObj = extractJsonObject(fileObj, "metadata");
        if (!metadataObj.empty()) {
            info.filename = extractJsonValue(metadataObj, "filename");
            // Use int64 for file size to support files > 2GB
            info.size = extractJsonInt64(metadataObj, "size");
        }

        info.duration = extractJsonFloat(fileObj, "duration");
        info.mimeType = extractJsonValue(fileObj, "mimeType");

        if (!info.ino.empty()) {
            files.push_back(info);
            brls::Logger::debug("Found audio file: {} (ino: {})", info.filename, info.ino);
        }

        pos = objEnd;
    }

    brls::Logger::info("Found {} audio files for item", files.size());
    return !files.empty();
}

std::string AudiobookshelfClient::getFileDownloadUrlByIno(const std::string& itemId, const std::string& ino) {
    std::string url = m_serverUrl + "/api/items/" + itemId + "/file/" + ino;
    url += "?token=" + m_authToken;
    return url;
}

bool AudiobookshelfClient::updateProgress(const std::string& itemId, float currentTime, float duration,
                                           bool isFinished, const std::string& episodeId) {
    brls::Logger::debug("Updating progress for item: {} at {}s", itemId, currentTime);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/me/progress/" + itemId);
    if (!episodeId.empty()) {
        req.url += "/" + episodeId;
    }
    req.method = "PATCH";
    req.headers["Accept"] = "application/json";
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    float progress = duration > 0 ? currentTime / duration : 0;
    char body[256];
    snprintf(body, sizeof(body),
             "{\"currentTime\":%.2f,\"progress\":%.4f,\"duration\":%.2f,\"isFinished\":%s}",
             currentTime, progress, duration, isFinished ? "true" : "false");
    req.body = body;

    HttpResponse resp = client.request(req);
    return resp.statusCode == 200;
}

bool AudiobookshelfClient::getProgress(const std::string& itemId, float& currentTime, float& progress,
                                        bool& isFinished, const std::string& episodeId) {
    brls::Logger::info("Getting progress for item: {} episode: {}", itemId, episodeId.empty() ? "(none)" : episodeId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/me/progress/" + itemId);
    if (!episodeId.empty()) {
        req.url += "/" + episodeId;
    }
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    brls::Logger::debug("getProgress URL: {}", req.url);

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::warning("getProgress failed: status={} body={}", resp.statusCode, resp.body.substr(0, 200));
        return false;
    }

    brls::Logger::debug("getProgress response: {}", resp.body.substr(0, 300));

    currentTime = extractJsonFloat(resp.body, "currentTime");
    progress = extractJsonFloat(resp.body, "progress");
    isFinished = extractJsonBool(resp.body, "isFinished");

    brls::Logger::info("getProgress result: currentTime={}s progress={} finished={}",
                      currentTime, progress, isFinished ? "yes" : "no");

    return true;
}

bool AudiobookshelfClient::removeItemFromContinueListening(const std::string& itemId) {
    brls::Logger::debug("Removing item from continue listening: {}", itemId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/me/progress/" + itemId + "/remove-from-continue-listening");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);
    return resp.statusCode == 200;
}

bool AudiobookshelfClient::createBookmark(const std::string& itemId, float time, const std::string& title) {
    brls::Logger::debug("Creating bookmark for item: {} at {}s", itemId, time);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/me/item/" + itemId + "/bookmark");
    req.method = "POST";
    req.headers["Accept"] = "application/json";
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    char body[256];
    snprintf(body, sizeof(body), "{\"time\":%.2f,\"title\":\"%s\"}", time, title.c_str());
    req.body = body;

    HttpResponse resp = client.request(req);
    return resp.statusCode == 200;
}

bool AudiobookshelfClient::deleteBookmark(const std::string& itemId, float time) {
    brls::Logger::debug("Deleting bookmark for item: {} at {}s", itemId, time);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/me/item/" + itemId + "/bookmark/" + std::to_string((int)time));
    req.method = "DELETE";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);
    return resp.statusCode == 200;
}

std::string AudiobookshelfClient::getCoverUrl(const std::string& itemId, int width, int height) {
    if (itemId.empty()) return "";

    std::string url = m_serverUrl + "/api/items/" + itemId + "/cover";
    url += "?width=" + std::to_string(width);
    url += "&height=" + std::to_string(height);
    url += "&format=jpeg";  // Request JPEG format for NanoVG compatibility
    url += "&token=" + m_authToken;

    return url;
}

std::string AudiobookshelfClient::getAuthorImageUrl(const std::string& authorId, int width, int height) {
    if (authorId.empty()) return "";

    std::string url = m_serverUrl + "/api/authors/" + authorId + "/image";
    url += "?width=" + std::to_string(width);
    url += "&height=" + std::to_string(height);
    url += "&token=" + m_authToken;

    return url;
}

bool AudiobookshelfClient::fetchCollection(const std::string& collectionId, Collection& collection) {
    brls::Logger::debug("Fetching collection: {}", collectionId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/collections/" + collectionId);
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch collection: {}", resp.statusCode);
        return false;
    }

    collection.id = extractJsonValue(resp.body, "id");
    collection.libraryId = extractJsonValue(resp.body, "libraryId");
    collection.name = extractJsonValue(resp.body, "name");
    collection.description = extractJsonValue(resp.body, "description");

    return true;
}

bool AudiobookshelfClient::fetchCollectionBooks(const std::string& collectionId, std::vector<MediaItem>& books) {
    brls::Logger::debug("Fetching collection books: {}", collectionId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/collections/" + collectionId);
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch collection books: {}", resp.statusCode);
        return false;
    }

    books.clear();

    std::string booksArray = extractJsonArray(resp.body, "books");
    if (!booksArray.empty()) {
        size_t pos = 0;
        while ((pos = booksArray.find("\"id\"", pos)) != std::string::npos) {
            size_t objStart = booksArray.rfind('{', pos);
            if (objStart == std::string::npos) {
                pos++;
                continue;
            }

            int braceCount = 1;
            size_t objEnd = objStart + 1;
            while (braceCount > 0 && objEnd < booksArray.length()) {
                if (booksArray[objEnd] == '{') braceCount++;
                else if (booksArray[objEnd] == '}') braceCount--;
                objEnd++;
            }

            std::string obj = booksArray.substr(objStart, objEnd - objStart);
            MediaItem item = parseMediaItem(obj);

            if (!item.id.empty() && !item.title.empty()) {
                books.push_back(item);
            }

            pos = objEnd;
        }
    }

    brls::Logger::info("Found {} books in collection", books.size());
    return true;
}

bool AudiobookshelfClient::fetchSeriesBooks(const std::string& seriesId, std::vector<MediaItem>& books) {
    brls::Logger::debug("Fetching series books: {}", seriesId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/series/" + seriesId);
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch series books: {}", resp.statusCode);
        return false;
    }

    books.clear();

    std::string booksArray = extractJsonArray(resp.body, "books");
    if (!booksArray.empty()) {
        size_t pos = 0;
        while ((pos = booksArray.find("\"id\"", pos)) != std::string::npos) {
            size_t objStart = booksArray.rfind('{', pos);
            if (objStart == std::string::npos) {
                pos++;
                continue;
            }

            int braceCount = 1;
            size_t objEnd = objStart + 1;
            while (braceCount > 0 && objEnd < booksArray.length()) {
                if (booksArray[objEnd] == '{') braceCount++;
                else if (booksArray[objEnd] == '}') braceCount--;
                objEnd++;
            }

            std::string obj = booksArray.substr(objStart, objEnd - objStart);
            MediaItem item = parseMediaItem(obj);

            if (!item.id.empty() && !item.title.empty()) {
                books.push_back(item);
            }

            pos = objEnd;
        }
    }

    brls::Logger::info("Found {} books in series", books.size());
    return true;
}

bool AudiobookshelfClient::fetchAuthor(const std::string& authorId, Author& author) {
    brls::Logger::debug("Fetching author: {}", authorId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/authors/" + authorId);
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch author: {}", resp.statusCode);
        return false;
    }

    author.id = extractJsonValue(resp.body, "id");
    author.name = extractJsonValue(resp.body, "name");
    author.description = extractJsonValue(resp.body, "description");
    author.imagePath = extractJsonValue(resp.body, "imagePath");

    return true;
}

bool AudiobookshelfClient::fetchAuthorBooks(const std::string& authorId, std::vector<MediaItem>& books) {
    brls::Logger::debug("Fetching author books: {}", authorId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/authors/" + authorId + "?include=items");
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch author books: {}", resp.statusCode);
        return false;
    }

    books.clear();

    std::string itemsArray = extractJsonArray(resp.body, "libraryItems");
    if (!itemsArray.empty()) {
        size_t pos = 0;
        while ((pos = itemsArray.find("\"id\"", pos)) != std::string::npos) {
            size_t objStart = itemsArray.rfind('{', pos);
            if (objStart == std::string::npos) {
                pos++;
                continue;
            }

            int braceCount = 1;
            size_t objEnd = objStart + 1;
            while (braceCount > 0 && objEnd < itemsArray.length()) {
                if (itemsArray[objEnd] == '{') braceCount++;
                else if (itemsArray[objEnd] == '}') braceCount--;
                objEnd++;
            }

            std::string obj = itemsArray.substr(objStart, objEnd - objStart);
            MediaItem item = parseMediaItem(obj);

            if (!item.id.empty() && !item.title.empty()) {
                books.push_back(item);
            }

            pos = objEnd;
        }
    }

    brls::Logger::info("Found {} books by author", books.size());
    return true;
}

bool AudiobookshelfClient::fetchPodcastEpisodes(const std::string& podcastId, std::vector<MediaItem>& episodes) {
    brls::Logger::debug("Fetching podcast episodes: {}", podcastId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/items/" + podcastId);
    req.method = "GET";
    req.headers["Accept"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("Failed to fetch podcast episodes: {}", resp.statusCode);
        return false;
    }

    episodes.clear();

    std::string mediaObj = extractJsonObject(resp.body, "media");
    std::string episodesArray = extractJsonArray(mediaObj, "episodes");

    if (!episodesArray.empty()) {
        size_t pos = 0;
        while ((pos = episodesArray.find("\"id\"", pos)) != std::string::npos) {
            size_t objStart = episodesArray.rfind('{', pos);
            if (objStart == std::string::npos) {
                pos++;
                continue;
            }

            int braceCount = 1;
            size_t objEnd = objStart + 1;
            while (braceCount > 0 && objEnd < episodesArray.length()) {
                if (episodesArray[objEnd] == '{') braceCount++;
                else if (episodesArray[objEnd] == '}') braceCount--;
                objEnd++;
            }

            std::string obj = episodesArray.substr(objStart, objEnd - objStart);

            MediaItem ep;
            ep.episodeId = extractJsonValue(obj, "id");
            ep.id = podcastId;  // Parent podcast ID
            ep.podcastId = podcastId;
            ep.title = extractJsonValue(obj, "title");
            ep.description = extractJsonValue(obj, "description");
            ep.pubDate = extractJsonValue(obj, "pubDate");
            ep.duration = extractJsonFloat(obj, "duration");
            ep.episodeNumber = extractJsonInt(obj, "episode");
            ep.seasonNumber = extractJsonInt(obj, "season");
            ep.mediaType = MediaType::PODCAST_EPISODE;
            ep.type = "podcastEpisode";

            if (!ep.episodeId.empty() && !ep.title.empty()) {
                episodes.push_back(ep);
            }

            pos = objEnd;
        }
    }

    brls::Logger::info("Found {} podcast episodes", episodes.size());
    return true;
}

bool AudiobookshelfClient::searchPodcasts(const std::string& query, std::vector<PodcastSearchResult>& results) {
    brls::Logger::debug("Searching iTunes for podcasts: {}", query);

    results.clear();

    // URL encode the query
    std::string encodedQuery = HttpClient::urlEncode(query);

    // Search iTunes API
    HttpClient client;
    HttpRequest req;
    req.url = "https://itunes.apple.com/search?term=" + encodedQuery + "&media=podcast&limit=20";
    req.method = "GET";
    req.headers["Accept"] = "application/json";

    HttpResponse resp = client.request(req);

    if (resp.statusCode != 200) {
        brls::Logger::error("iTunes search failed: {}", resp.statusCode);
        return false;
    }

    // Parse results array
    std::string resultsArray = extractJsonArray(resp.body, "results");
    if (resultsArray.empty()) {
        brls::Logger::debug("No podcast results found");
        return true;
    }

    size_t pos = 0;
    while ((pos = resultsArray.find("\"feedUrl\"", pos)) != std::string::npos) {
        size_t objStart = resultsArray.rfind('{', pos);
        if (objStart == std::string::npos) {
            pos++;
            continue;
        }

        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < resultsArray.length()) {
            if (resultsArray[objEnd] == '{') braceCount++;
            else if (resultsArray[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string obj = resultsArray.substr(objStart, objEnd - objStart);

        PodcastSearchResult result;
        result.title = extractJsonValue(obj, "collectionName");
        result.author = extractJsonValue(obj, "artistName");
        result.feedUrl = extractJsonValue(obj, "feedUrl");
        result.artworkUrl = extractJsonValue(obj, "artworkUrl600");
        if (result.artworkUrl.empty()) {
            result.artworkUrl = extractJsonValue(obj, "artworkUrl100");
        }
        result.genre = extractJsonValue(obj, "primaryGenreName");
        result.trackCount = extractJsonInt(obj, "trackCount");

        if (!result.feedUrl.empty() && !result.title.empty()) {
            results.push_back(result);
        }

        pos = objEnd;
    }

    brls::Logger::info("Found {} podcasts from iTunes", results.size());
    return true;
}

bool AudiobookshelfClient::addPodcastToLibrary(const std::string& libraryId, const PodcastSearchResult& podcast,
                                                const std::string& folderId) {
    brls::Logger::debug("Adding podcast '{}' to library {} from feed: {}", podcast.title, libraryId, podcast.feedUrl);

    // Always fetch library to get folder path (needed to create podcast subfolder)
    std::string folder = folderId;
    std::string folderPath;

    HttpClient libClient;
    HttpRequest libReq;
    libReq.url = buildApiUrl("/api/libraries/" + libraryId);
    libReq.method = "GET";
    libReq.headers["Accept"] = "application/json";
    libReq.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse libResp = libClient.request(libReq);
    if (libResp.statusCode == 200) {
        // Extract folder info from library
        std::string foldersArray = extractJsonArray(libResp.body, "folders");
        if (!foldersArray.empty()) {
            // If no folder ID provided, use the first one
            if (folder.empty()) {
                folder = extractJsonValue(foldersArray, "id");
            }
            // Always get the folder path for creating podcast subfolder
            folderPath = extractJsonValue(foldersArray, "fullPath");
            if (folderPath.empty()) {
                folderPath = extractJsonValue(foldersArray, "path");
            }
            brls::Logger::debug("Using folder ID: {} path: {}", folder, folderPath);
        }
    }

    if (folder.empty()) {
        brls::Logger::error("No folder ID available for library {}", libraryId);
        return false;
    }

    if (folderPath.empty()) {
        brls::Logger::error("No folder path available for library {}", libraryId);
        return false;
    }

    // Create a sanitized folder name from the podcast title
    auto sanitizeFolderName = [](const std::string& name) -> std::string {
        std::string result;
        for (char c : name) {
            // Replace invalid filesystem characters with underscore
            if (c == '/' || c == '\\' || c == ':' || c == '*' ||
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                result += '_';
            } else {
                result += c;
            }
        }
        // Trim trailing spaces and dots (Windows compatibility)
        while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
            result.pop_back();
        }
        return result;
    };

    // Create the full path with podcast's own folder
    std::string podcastFolderName = sanitizeFolderName(podcast.title);
    std::string fullPodcastPath = folderPath;
    if (!fullPodcastPath.empty() && fullPodcastPath.back() != '/') {
        fullPodcastPath += '/';
    }
    fullPodcastPath += podcastFolderName;

    brls::Logger::info("Creating podcast folder: {}", fullPodcastPath);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/podcasts");
    req.method = "POST";
    req.timeout = 60;  // Longer timeout for podcast creation (server fetches RSS)
    req.headers["Accept"] = "application/json";
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    // Helper to escape JSON strings
    auto escapeJson = [](const std::string& s) -> std::string {
        std::string result;
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c;
            }
        }
        return result;
    };

    // Build request body with proper media.metadata structure
    // Match the Kodi addon's structure exactly
    std::string body = "{";
    body += "\"path\":\"" + escapeJson(fullPodcastPath) + "\",";
    body += "\"folderId\":\"" + folder + "\",";
    body += "\"libraryId\":\"" + libraryId + "\",";
    body += "\"media\":{\"metadata\":{";
    body += "\"title\":\"" + escapeJson(podcast.title) + "\",";
    body += "\"feedUrl\":\"" + escapeJson(podcast.feedUrl) + "\"";
    if (!podcast.author.empty()) {
        body += ",\"author\":\"" + escapeJson(podcast.author) + "\"";
    }
    if (!podcast.artworkUrl.empty()) {
        body += ",\"imageUrl\":\"" + escapeJson(podcast.artworkUrl) + "\"";
    }
    body += "}},";  // Close metadata and media
    body += "\"autoDownloadEpisodes\":false";
    body += "}";

    brls::Logger::debug("Add podcast request body: {}", body);
    req.body = body;

    HttpResponse resp = client.request(req);

    if (resp.statusCode == 200 || resp.statusCode == 201) {
        brls::Logger::info("Successfully added podcast '{}' to library", podcast.title);
        return true;
    }

    brls::Logger::error("Failed to add podcast: {} - {}", resp.statusCode, resp.body);
    return false;
}

bool AudiobookshelfClient::checkNewEpisodes(const std::string& podcastId, std::vector<MediaItem>& newEpisodes) {
    brls::Logger::debug("Checking for new episodes for podcast: {}", podcastId);

    newEpisodes.clear();

    HttpClient client;

    // Step 1: Get podcast item to get feedUrl and existing episodes
    HttpRequest itemReq;
    itemReq.url = buildApiUrl("/api/items/" + podcastId);
    itemReq.method = "GET";
    itemReq.headers["Accept"] = "application/json";
    itemReq.headers["Authorization"] = "Bearer " + m_authToken;

    HttpResponse itemResp = client.request(itemReq);
    if (itemResp.statusCode != 200) {
        brls::Logger::error("Failed to get podcast item: {}", itemResp.statusCode);
        return false;
    }

    // Extract feedUrl from metadata
    std::string mediaObj = extractJsonObject(itemResp.body, "media");
    std::string metadataObj = extractJsonObject(mediaObj, "metadata");
    std::string feedUrl = extractJsonValue(metadataObj, "feedUrl");

    if (feedUrl.empty()) {
        brls::Logger::error("Podcast has no RSS feed URL");
        return false;
    }

    brls::Logger::debug("Podcast feed URL: {}", feedUrl);

    // Get existing episode GUIDs/titles for comparison
    std::vector<std::string> existingGuids;
    std::vector<std::string> existingTitles;
    std::string existingEpisodes = extractJsonArray(mediaObj, "episodes");
    if (!existingEpisodes.empty()) {
        size_t pos = 0;
        while ((pos = existingEpisodes.find("\"id\"", pos)) != std::string::npos) {
            size_t objStart = existingEpisodes.rfind('{', pos);
            if (objStart == std::string::npos) { pos++; continue; }

            int braceCount = 1;
            size_t objEnd = objStart + 1;
            while (braceCount > 0 && objEnd < existingEpisodes.length()) {
                if (existingEpisodes[objEnd] == '{') braceCount++;
                else if (existingEpisodes[objEnd] == '}') braceCount--;
                objEnd++;
            }

            std::string obj = existingEpisodes.substr(objStart, objEnd - objStart);
            std::string guid = extractJsonValue(obj, "guid");
            std::string title = extractJsonValue(obj, "title");
            if (!guid.empty()) existingGuids.push_back(guid);
            if (!title.empty()) existingTitles.push_back(title);
            pos = objEnd;
        }
    }

    brls::Logger::debug("Found {} existing episodes in library", existingGuids.size());

    // Step 2: Fetch RSS feed via server's podcast/feed endpoint
    HttpRequest feedReq;
    feedReq.url = buildApiUrl("/api/podcasts/feed");
    feedReq.method = "POST";
    feedReq.timeout = 60;  // RSS fetch can be slow
    feedReq.headers["Accept"] = "application/json";
    feedReq.headers["Content-Type"] = "application/json";
    feedReq.headers["Authorization"] = "Bearer " + m_authToken;
    feedReq.body = "{\"rssFeed\":\"" + feedUrl + "\"}";

    brls::Logger::debug("Fetching RSS feed from server...");
    HttpResponse feedResp = client.request(feedReq);

    if (feedResp.statusCode != 200) {
        brls::Logger::error("Failed to fetch RSS feed: {} - {}", feedResp.statusCode, feedResp.body);
        return false;
    }

    // Parse episodes from RSS feed response
    std::string podcastObj = extractJsonObject(feedResp.body, "podcast");
    std::string rssEpisodes = extractJsonArray(podcastObj, "episodes");

    if (rssEpisodes.empty()) {
        brls::Logger::debug("No episodes in RSS feed");
        return true;
    }

    // Step 3: Find new episodes (not in existing library)
    size_t pos = 0;
    while ((pos = rssEpisodes.find("\"title\"", pos)) != std::string::npos) {
        size_t objStart = rssEpisodes.rfind('{', pos);
        if (objStart == std::string::npos) {
            pos++;
            continue;
        }

        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < rssEpisodes.length()) {
            if (rssEpisodes[objEnd] == '{') braceCount++;
            else if (rssEpisodes[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string obj = rssEpisodes.substr(objStart, objEnd - objStart);

        std::string title = extractJsonValue(obj, "title");
        std::string guid = extractJsonValue(obj, "guid");

        // Check if already exists (by guid or title)
        bool exists = false;
        if (!guid.empty()) {
            for (const auto& g : existingGuids) {
                if (g == guid) { exists = true; break; }
            }
        }
        if (!exists && !title.empty()) {
            for (const auto& t : existingTitles) {
                if (t == title) { exists = true; break; }
            }
        }

        if (!exists && !title.empty()) {
            MediaItem ep;
            ep.episodeId = guid;
            ep.podcastId = podcastId;
            ep.id = podcastId;
            ep.title = title;
            ep.description = extractJsonValue(obj, "description");
            ep.pubDate = extractJsonValue(obj, "pubDate");
            ep.mediaType = MediaType::PODCAST_EPISODE;
            ep.type = "podcastEpisode";

            // Store enclosure info for download - this is the audio URL
            std::string enclosureObj = extractJsonObject(obj, "enclosure");
            if (!enclosureObj.empty()) {
                ep.coverPath = extractJsonValue(enclosureObj, "url");  // Reusing coverPath for enclosure URL
                ep.enclosureType = extractJsonValue(enclosureObj, "type");
                ep.enclosureLength = extractJsonValue(enclosureObj, "length");
            }

            // Store original JSON for download request
            ep.originalJson = obj;

            newEpisodes.push_back(ep);
        }

        pos = objEnd;
    }

    brls::Logger::info("Found {} new episodes not in library", newEpisodes.size());
    return true;
}

bool AudiobookshelfClient::downloadEpisodesToServer(const std::string& podcastId,
                                                     const std::vector<std::string>& episodeIds) {
    if (episodeIds.empty()) {
        brls::Logger::debug("No episodes to download");
        return true;
    }

    brls::Logger::debug("Downloading {} episodes to server for podcast: {}", episodeIds.size(), podcastId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/podcasts/" + podcastId + "/download-episodes");
    req.method = "POST";
    req.headers["Accept"] = "application/json";
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    // Build episodes array (for episodes that already exist in library)
    std::string body = "[";
    for (size_t i = 0; i < episodeIds.size(); ++i) {
        body += "\"" + episodeIds[i] + "\"";
        if (i < episodeIds.size() - 1) body += ",";
    }
    body += "]";
    req.body = body;

    brls::Logger::debug("Download episodes request: {}", body);
    HttpResponse resp = client.request(req);

    if (resp.statusCode == 200) {
        brls::Logger::info("Successfully queued {} episodes for download on server", episodeIds.size());
        return true;
    }

    brls::Logger::error("Failed to download episodes: {} - {}", resp.statusCode, resp.body);
    return false;
}

bool AudiobookshelfClient::downloadNewEpisodesToServer(const std::string& podcastId,
                                                        const std::vector<MediaItem>& episodes) {
    if (episodes.empty()) {
        brls::Logger::debug("No new episodes to download");
        return true;
    }

    brls::Logger::debug("Downloading {} new episodes to server for podcast: {}", episodes.size(), podcastId);

    HttpClient client;
    HttpRequest req;
    req.url = buildApiUrl("/api/podcasts/" + podcastId + "/download-episodes");
    req.method = "POST";
    req.timeout = 60;  // Longer timeout for downloading
    req.headers["Accept"] = "application/json";
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "Bearer " + m_authToken;

    // Helper to escape JSON strings
    auto escapeJson = [](const std::string& s) -> std::string {
        std::string result;
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c;
            }
        }
        return result;
    };

    // Build array of episode objects matching Kodi addon format:
    // {title, guid, enclosure: {url, type, length}, description, pubDate, season, episode}
    std::string body = "[";
    for (size_t i = 0; i < episodes.size(); ++i) {
        const auto& ep = episodes[i];
        body += "{";
        body += "\"title\":\"" + escapeJson(ep.title) + "\"";

        // GUID (episode identifier)
        if (!ep.episodeId.empty()) {
            body += ",\"guid\":\"" + escapeJson(ep.episodeId) + "\"";
        }

        // Enclosure object with audio URL, type, and length
        // coverPath is being used to store enclosure URL from checkNewEpisodes
        if (!ep.coverPath.empty()) {
            body += ",\"enclosure\":{";
            body += "\"url\":\"" + escapeJson(ep.coverPath) + "\"";
            if (!ep.enclosureType.empty()) {
                body += ",\"type\":\"" + escapeJson(ep.enclosureType) + "\"";
            }
            if (!ep.enclosureLength.empty()) {
                body += ",\"length\":\"" + escapeJson(ep.enclosureLength) + "\"";
            }
            body += "}";
        }

        if (!ep.description.empty()) {
            body += ",\"description\":\"" + escapeJson(ep.description) + "\"";
        }
        if (!ep.pubDate.empty()) {
            body += ",\"pubDate\":\"" + escapeJson(ep.pubDate) + "\"";
        }

        // Optional season/episode numbers
        if (ep.seasonNumber > 0) {
            body += ",\"season\":" + std::to_string(ep.seasonNumber);
        }
        if (ep.episodeNumber > 0) {
            body += ",\"episode\":" + std::to_string(ep.episodeNumber);
        }

        body += "}";
        if (i < episodes.size() - 1) body += ",";
    }
    body += "]";

    brls::Logger::debug("Download new episodes request: {}", body);
    req.body = body;

    HttpResponse resp = client.request(req);

    // Success can be 200 or empty response for some versions
    if (resp.statusCode == 200 || resp.statusCode == 204) {
        brls::Logger::info("Successfully queued {} new episodes for download on server", episodes.size());
        return true;
    }

    brls::Logger::error("Failed to download new episodes: {} - {}", resp.statusCode, resp.body);
    return false;
}

bool AudiobookshelfClient::downloadAllNewEpisodes(const std::string& podcastId) {
    brls::Logger::debug("Downloading all new episodes for podcast: {}", podcastId);

    // First check for new episodes
    std::vector<MediaItem> newEpisodes;
    if (!checkNewEpisodes(podcastId, newEpisodes)) {
        return false;
    }

    if (newEpisodes.empty()) {
        brls::Logger::info("No new episodes to download");
        return true;
    }

    // Use full episode data for new RSS episodes
    return downloadNewEpisodesToServer(podcastId, newEpisodes);
}

} // namespace vitasuwayomi
