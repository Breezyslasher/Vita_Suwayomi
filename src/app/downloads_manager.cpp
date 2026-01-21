/**
 * VitaSuwayomi - Downloads Manager implementation
 * Handles local manga chapter downloads for offline reading
 */

#include "app/downloads_manager.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/http_client.hpp"
#include "utils/image_loader.hpp"

#include <borealis.hpp>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <thread>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#endif

namespace vitasuwayomi {

static const char* DOWNLOADS_BASE_PATH = "ux0:data/VitaSuwayomi/downloads";
static const char* STATE_FILE_PATH = "ux0:data/VitaSuwayomi/downloads_state.json";

// Helper to create directory (Vita-compatible)
static bool createDirectory(const std::string& path) {
#ifdef __vita__
    int ret = sceIoMkdir(path.c_str(), 0777);
    return ret >= 0 || ret == 0x80010011;  // Success or already exists
#else
    // For non-Vita builds (testing)
    return true;
#endif
}

// Helper to check if file exists
static bool fileExists(const std::string& path) {
#ifdef __vita__
    SceIoStat stat;
    return sceIoGetstat(path.c_str(), &stat) >= 0;
#else
    std::ifstream f(path);
    return f.good();
#endif
}

// Helper to delete file
static bool deleteFile(const std::string& path) {
#ifdef __vita__
    return sceIoRemove(path.c_str()) >= 0;
#else
    return std::remove(path.c_str()) == 0;
#endif
}

// Helper to escape JSON strings
static std::string escapeJsonString(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

DownloadsManager& DownloadsManager::getInstance() {
    static DownloadsManager instance;
    return instance;
}

bool DownloadsManager::init() {
    if (m_initialized) return true;

    brls::Logger::info("DownloadsManager: Initializing...");

    m_downloadsPath = DOWNLOADS_BASE_PATH;

    // Create base downloads directory
    createDirectory("ux0:data");
    createDirectory("ux0:data/VitaSuwayomi");
    createDirectory(m_downloadsPath);

    // Load saved state
    loadState();

    m_initialized = true;
    brls::Logger::info("DownloadsManager: Initialized with {} downloads", m_downloads.size());

    return true;
}

bool DownloadsManager::queueChapterDownload(int mangaId, int chapterId, int chapterIndex,
                                             const std::string& mangaTitle,
                                             const std::string& chapterName) {
    std::lock_guard<std::mutex> lock(m_mutex);

    brls::Logger::info("DownloadsManager: Queueing chapter {} (id={}) for manga {} ({})",
                       chapterIndex, chapterId, mangaId, mangaTitle);

    // Find or create manga download item
    DownloadItem* manga = nullptr;
    for (auto& item : m_downloads) {
        if (item.mangaId == mangaId) {
            manga = &item;
            break;
        }
    }

    if (!manga) {
        // Create new manga download entry
        DownloadItem newItem;
        newItem.mangaId = mangaId;
        newItem.title = mangaTitle;
        newItem.state = LocalDownloadState::QUEUED;
        newItem.localPath = createMangaDir(mangaId, mangaTitle);
        m_downloads.push_back(newItem);
        manga = &m_downloads.back();
    }

    // Check if chapter already exists
    for (auto& ch : manga->chapters) {
        if (ch.chapterIndex == chapterIndex) {
            brls::Logger::debug("Chapter {} already in download queue", chapterIndex);
            return true;  // Already queued
        }
    }

    // Add chapter to queue
    DownloadedChapter chapter;
    chapter.chapterId = chapterId;
    chapter.chapterIndex = chapterIndex;
    chapter.name = chapterName;
    chapter.state = LocalDownloadState::QUEUED;
    manga->chapters.push_back(chapter);
    manga->totalChapters = static_cast<int>(manga->chapters.size());

    saveStateUnlocked();
    return true;
}

bool DownloadsManager::queueChaptersDownload(int mangaId,
                                              const std::vector<std::pair<int,int>>& chapters,
                                              const std::string& mangaTitle) {
    // chapters is a vector of pairs: (chapterId, chapterIndex)
    for (const auto& ch : chapters) {
        if (!queueChapterDownload(mangaId, ch.first, ch.second, mangaTitle)) {
            return false;
        }
    }
    return true;
}

void DownloadsManager::startDownloads() {
    if (m_downloading) return;

    m_downloading = true;
    brls::Logger::info("DownloadsManager: Starting downloads");

    // Run downloads in background thread
    std::thread([this]() {
        while (m_downloading) {
            DownloadedChapter* nextChapter = nullptr;
            int mangaId = 0;

            // Find next queued chapter
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto& manga : m_downloads) {
                    for (auto& chapter : manga.chapters) {
                        if (chapter.state == LocalDownloadState::QUEUED) {
                            nextChapter = &chapter;
                            mangaId = manga.mangaId;
                            chapter.state = LocalDownloadState::DOWNLOADING;
                            break;
                        }
                    }
                    if (nextChapter) break;
                }
            }

            if (!nextChapter) {
                // No more chapters to download
                m_downloading = false;
                brls::Logger::info("DownloadsManager: All downloads complete");
                break;
            }

            // Download the chapter
            downloadChapter(mangaId, *nextChapter);
        }
    }).detach();
}

void DownloadsManager::pauseDownloads() {
    m_downloading = false;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& manga : m_downloads) {
        for (auto& chapter : manga.chapters) {
            if (chapter.state == LocalDownloadState::DOWNLOADING) {
                chapter.state = LocalDownloadState::PAUSED;
            }
        }
    }

    saveStateUnlocked();
}

bool DownloadsManager::cancelDownload(int mangaId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it) {
        if (it->mangaId == mangaId) {
            // Cancel all chapters
            for (auto& chapter : it->chapters) {
                if (chapter.state == LocalDownloadState::QUEUED ||
                    chapter.state == LocalDownloadState::DOWNLOADING) {
                    chapter.state = LocalDownloadState::FAILED;
                }
            }
            saveStateUnlocked();
            return true;
        }
    }

    return false;
}

bool DownloadsManager::cancelChapterDownload(int mangaId, int chapterIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& manga : m_downloads) {
        if (manga.mangaId == mangaId) {
            for (auto& chapter : manga.chapters) {
                if (chapter.chapterIndex == chapterIndex) {
                    if (chapter.state == LocalDownloadState::QUEUED ||
                        chapter.state == LocalDownloadState::DOWNLOADING) {
                        chapter.state = LocalDownloadState::FAILED;
                        saveStateUnlocked();
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool DownloadsManager::deleteMangaDownload(int mangaId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it) {
        if (it->mangaId == mangaId) {
            // Delete all chapter files
            for (auto& chapter : it->chapters) {
                for (auto& page : chapter.pages) {
                    if (!page.localPath.empty()) {
                        deleteFile(page.localPath);
                    }
                }
            }

            // Delete cover
            if (!it->localCoverPath.empty()) {
                deleteFile(it->localCoverPath);
            }

            m_downloads.erase(it);
            saveStateUnlocked();
            return true;
        }
    }

    return false;
}

bool DownloadsManager::deleteChapterDownload(int mangaId, int chapterIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& manga : m_downloads) {
        if (manga.mangaId == mangaId) {
            for (auto it = manga.chapters.begin(); it != manga.chapters.end(); ++it) {
                if (it->chapterIndex == chapterIndex) {
                    // Delete page files
                    for (auto& page : it->pages) {
                        if (!page.localPath.empty()) {
                            deleteFile(page.localPath);
                        }
                    }

                    manga.chapters.erase(it);
                    manga.totalChapters = static_cast<int>(manga.chapters.size());

                    // If no chapters left, remove manga entry
                    if (manga.chapters.empty()) {
                        for (auto mit = m_downloads.begin(); mit != m_downloads.end(); ++mit) {
                            if (mit->mangaId == mangaId) {
                                m_downloads.erase(mit);
                                break;
                            }
                        }
                    }

                    saveStateUnlocked();
                    return true;
                }
            }
        }
    }

    return false;
}

std::vector<DownloadItem> DownloadsManager::getDownloads() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_downloads;
}

DownloadItem* DownloadsManager::getMangaDownload(int mangaId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& item : m_downloads) {
        if (item.mangaId == mangaId) {
            return &item;
        }
    }

    return nullptr;
}

DownloadedChapter* DownloadsManager::getChapterDownload(int mangaId, int chapterIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& manga : m_downloads) {
        if (manga.mangaId == mangaId) {
            for (auto& chapter : manga.chapters) {
                if (chapter.chapterIndex == chapterIndex) {
                    return &chapter;
                }
            }
        }
    }

    return nullptr;
}

bool DownloadsManager::isMangaDownloaded(int mangaId) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& item : m_downloads) {
        if (item.mangaId == mangaId) {
            // Check if any chapters are downloaded
            for (const auto& chapter : item.chapters) {
                if (chapter.state == LocalDownloadState::COMPLETED) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool DownloadsManager::isChapterDownloaded(int mangaId, int chapterIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& manga : m_downloads) {
        if (manga.mangaId == mangaId) {
            for (const auto& chapter : manga.chapters) {
                if (chapter.chapterIndex == chapterIndex) {
                    return chapter.state == LocalDownloadState::COMPLETED;
                }
            }
        }
    }

    return false;
}

std::string DownloadsManager::getPagePath(int mangaId, int chapterIndex, int pageIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& manga : m_downloads) {
        if (manga.mangaId == mangaId) {
            for (const auto& chapter : manga.chapters) {
                if (chapter.chapterIndex == chapterIndex) {
                    for (const auto& page : chapter.pages) {
                        if (page.index == pageIndex && page.downloaded) {
                            return page.localPath;
                        }
                    }
                }
            }
        }
    }

    return "";
}

std::vector<std::string> DownloadsManager::getChapterPages(int mangaId, int chapterIndex) const {
    std::vector<std::string> pages;
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& manga : m_downloads) {
        if (manga.mangaId == mangaId) {
            for (const auto& chapter : manga.chapters) {
                if (chapter.chapterIndex == chapterIndex) {
                    for (const auto& page : chapter.pages) {
                        if (page.downloaded) {
                            pages.push_back(page.localPath);
                        }
                    }
                    break;
                }
            }
            break;
        }
    }

    return pages;
}

void DownloadsManager::updateReadingProgress(int mangaId, int chapterIndex, int lastPageRead) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& manga : m_downloads) {
        if (manga.mangaId == mangaId) {
            manga.lastChapterRead = chapterIndex;
            manga.lastPageRead = lastPageRead;
            manga.lastReadTime = std::time(nullptr);

            for (auto& chapter : manga.chapters) {
                if (chapter.chapterIndex == chapterIndex) {
                    chapter.lastPageRead = lastPageRead;
                    chapter.lastReadTime = std::time(nullptr);
                    break;
                }
            }

            saveStateUnlocked();
            break;
        }
    }
}

void DownloadsManager::syncProgressToServer() {
    // Sync local reading progress to Suwayomi server
    SuwayomiClient& client = SuwayomiClient::getInstance();

    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& manga : m_downloads) {
        for (const auto& chapter : manga.chapters) {
            if (chapter.state == LocalDownloadState::COMPLETED && chapter.lastPageRead > 0) {
                client.updateChapterProgress(manga.mangaId, chapter.chapterIndex,
                                              chapter.lastPageRead);
            }
        }
    }
}

void DownloadsManager::syncProgressFromServer() {
    // Sync reading progress from Suwayomi server
    SuwayomiClient& client = SuwayomiClient::getInstance();

    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& manga : m_downloads) {
        // Fetch chapters from server to get latest progress
        std::vector<Chapter> serverChapters;
        if (client.fetchChapters(manga.mangaId, serverChapters)) {
            for (auto& localChapter : manga.chapters) {
                for (const auto& serverChapter : serverChapters) {
                    if (serverChapter.index == localChapter.chapterIndex) {
                        localChapter.lastPageRead = serverChapter.lastPageRead;
                        break;
                    }
                }
            }
        }
    }

    saveStateUnlocked();
}

void DownloadsManager::saveState() {
    std::lock_guard<std::mutex> lock(m_mutex);
    saveStateUnlocked();
}

void DownloadsManager::saveStateUnlocked() {
#ifdef __vita__
    std::stringstream ss;
    ss << "{\n\"downloads\":[\n";

    for (size_t i = 0; i < m_downloads.size(); ++i) {
        const auto& item = m_downloads[i];
        ss << "{\n"
           << "\"mangaId\":" << item.mangaId << ",\n"
           << "\"title\":\"" << escapeJsonString(item.title) << "\",\n"
           << "\"author\":\"" << escapeJsonString(item.author) << "\",\n"
           << "\"localPath\":\"" << escapeJsonString(item.localPath) << "\",\n"
           << "\"localCoverPath\":\"" << escapeJsonString(item.localCoverPath) << "\",\n"
           << "\"state\":" << static_cast<int>(item.state) << ",\n"
           << "\"totalBytes\":" << item.totalBytes << ",\n"
           << "\"lastChapterRead\":" << item.lastChapterRead << ",\n"
           << "\"lastPageRead\":" << item.lastPageRead << ",\n"
           << "\"lastReadTime\":" << item.lastReadTime << ",\n"
           << "\"chapters\":[\n";

        for (size_t j = 0; j < item.chapters.size(); ++j) {
            const auto& ch = item.chapters[j];
            ss << "{\n"
               << "\"chapterId\":" << ch.chapterId << ",\n"
               << "\"chapterIndex\":" << ch.chapterIndex << ",\n"
               << "\"name\":\"" << escapeJsonString(ch.name) << "\",\n"
               << "\"chapterNumber\":" << ch.chapterNumber << ",\n"
               << "\"localPath\":\"" << escapeJsonString(ch.localPath) << "\",\n"
               << "\"pageCount\":" << ch.pageCount << ",\n"
               << "\"downloadedPages\":" << ch.downloadedPages << ",\n"
               << "\"state\":" << static_cast<int>(ch.state) << ",\n"
               << "\"lastPageRead\":" << ch.lastPageRead << ",\n"
               << "\"pages\":[\n";

            for (size_t k = 0; k < ch.pages.size(); ++k) {
                const auto& pg = ch.pages[k];
                ss << "{"
                   << "\"index\":" << pg.index << ","
                   << "\"localPath\":\"" << escapeJsonString(pg.localPath) << "\","
                   << "\"size\":" << pg.size << ","
                   << "\"downloaded\":" << (pg.downloaded ? "true" : "false")
                   << "}";
                if (k < ch.pages.size() - 1) ss << ",";
                ss << "\n";
            }

            ss << "]\n}";
            if (j < item.chapters.size() - 1) ss << ",";
            ss << "\n";
        }

        ss << "]\n}";
        if (i < m_downloads.size() - 1) ss << ",";
        ss << "\n";
    }

    ss << "]\n}\n";

    std::string json = ss.str();

    SceUID fd = sceIoOpen(STATE_FILE_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, json.c_str(), json.length());
        sceIoClose(fd);
        brls::Logger::debug("DownloadsManager: State saved ({} bytes)", json.length());
    } else {
        brls::Logger::error("DownloadsManager: Failed to save state");
    }
#endif
}

// Helper to extract int from JSON
static int extractJsonInt(const std::string& json, const std::string& key, int defaultVal = 0) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return defaultVal;
    pos += searchKey.length();
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\n')) pos++;
    // Extract number
    std::string numStr;
    while (pos < json.length() && (isdigit(json[pos]) || json[pos] == '-')) {
        numStr += json[pos++];
    }
    return numStr.empty() ? defaultVal : std::stoi(numStr);
}

// Helper to extract float from JSON
static float extractJsonFloat(const std::string& json, const std::string& key, float defaultVal = 0.0f) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return defaultVal;
    pos += searchKey.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\n')) pos++;
    std::string numStr;
    while (pos < json.length() && (isdigit(json[pos]) || json[pos] == '-' || json[pos] == '.')) {
        numStr += json[pos++];
    }
    return numStr.empty() ? defaultVal : std::stof(numStr);
}

// Helper to extract string from JSON
static std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";
    pos += searchKey.length();
    std::string result;
    while (pos < json.length() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.length()) {
            pos++;
            if (json[pos] == 'n') result += '\n';
            else if (json[pos] == 't') result += '\t';
            else if (json[pos] == '"') result += '"';
            else if (json[pos] == '\\') result += '\\';
            else result += json[pos];
        } else {
            result += json[pos];
        }
        pos++;
    }
    return result;
}

// Helper to extract bool from JSON
static bool extractJsonBool(const std::string& json, const std::string& key, bool defaultVal = false) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return defaultVal;
    pos += searchKey.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\n')) pos++;
    if (pos + 4 <= json.length() && json.substr(pos, 4) == "true") return true;
    if (pos + 5 <= json.length() && json.substr(pos, 5) == "false") return false;
    return defaultVal;
}

// Helper to find matching bracket
static size_t findMatchingBracket(const std::string& json, size_t start, char open, char close) {
    int depth = 1;
    for (size_t i = start; i < json.length(); i++) {
        if (json[i] == open) depth++;
        else if (json[i] == close) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}

void DownloadsManager::loadState() {
#ifdef __vita__
    SceUID fd = sceIoOpen(STATE_FILE_PATH, SCE_O_RDONLY, 0);
    if (fd < 0) {
        brls::Logger::debug("DownloadsManager: No saved state found");
        return;
    }

    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    if (size <= 0 || size > 1024 * 1024) {  // Max 1MB
        sceIoClose(fd);
        return;
    }

    std::string content;
    content.resize(size);
    sceIoRead(fd, &content[0], size);
    sceIoClose(fd);

    brls::Logger::debug("DownloadsManager: Loading state ({} bytes)", content.length());

    m_downloads.clear();

    // Find downloads array
    size_t pos = content.find("\"downloads\":");
    if (pos == std::string::npos) {
        brls::Logger::warning("DownloadsManager: No downloads key found in state");
        return;
    }

    // Find the opening bracket of downloads array
    pos = content.find('[', pos);
    if (pos == std::string::npos) return;

    size_t arrayEnd = findMatchingBracket(content, pos + 1, '[', ']');
    if (arrayEnd == std::string::npos) return;

    std::string downloadsArray = content.substr(pos + 1, arrayEnd - pos - 1);

    // Parse each manga object in the downloads array
    size_t mangaStart = 0;
    while ((mangaStart = downloadsArray.find('{', mangaStart)) != std::string::npos) {
        size_t mangaEnd = findMatchingBracket(downloadsArray, mangaStart + 1, '{', '}');
        if (mangaEnd == std::string::npos) break;

        std::string mangaJson = downloadsArray.substr(mangaStart, mangaEnd - mangaStart + 1);

        DownloadItem item;
        item.mangaId = extractJsonInt(mangaJson, "mangaId");
        item.title = extractJsonString(mangaJson, "title");
        item.author = extractJsonString(mangaJson, "author");
        item.localPath = extractJsonString(mangaJson, "localPath");
        item.localCoverPath = extractJsonString(mangaJson, "localCoverPath");
        item.state = static_cast<LocalDownloadState>(extractJsonInt(mangaJson, "state"));
        item.totalBytes = extractJsonInt(mangaJson, "totalBytes");
        item.lastChapterRead = extractJsonInt(mangaJson, "lastChapterRead");
        item.lastPageRead = extractJsonInt(mangaJson, "lastPageRead");
        item.lastReadTime = extractJsonInt(mangaJson, "lastReadTime");

        // Parse chapters array
        size_t chaptersPos = mangaJson.find("\"chapters\":");
        if (chaptersPos != std::string::npos) {
            size_t chaptersStart = mangaJson.find('[', chaptersPos);
            if (chaptersStart != std::string::npos) {
                size_t chaptersEnd = findMatchingBracket(mangaJson, chaptersStart + 1, '[', ']');
                if (chaptersEnd != std::string::npos) {
                    std::string chaptersArray = mangaJson.substr(chaptersStart + 1, chaptersEnd - chaptersStart - 1);

                    size_t chStart = 0;
                    while ((chStart = chaptersArray.find('{', chStart)) != std::string::npos) {
                        size_t chEnd = findMatchingBracket(chaptersArray, chStart + 1, '{', '}');
                        if (chEnd == std::string::npos) break;

                        std::string chJson = chaptersArray.substr(chStart, chEnd - chStart + 1);

                        DownloadedChapter chapter;
                        chapter.chapterId = extractJsonInt(chJson, "chapterId");
                        chapter.chapterIndex = extractJsonInt(chJson, "chapterIndex");
                        chapter.name = extractJsonString(chJson, "name");
                        chapter.chapterNumber = extractJsonFloat(chJson, "chapterNumber");
                        chapter.localPath = extractJsonString(chJson, "localPath");
                        chapter.pageCount = extractJsonInt(chJson, "pageCount");
                        chapter.downloadedPages = extractJsonInt(chJson, "downloadedPages");
                        chapter.state = static_cast<LocalDownloadState>(extractJsonInt(chJson, "state"));
                        chapter.lastPageRead = extractJsonInt(chJson, "lastPageRead");

                        // Parse pages array
                        size_t pagesPos = chJson.find("\"pages\":");
                        if (pagesPos != std::string::npos) {
                            size_t pagesStart = chJson.find('[', pagesPos);
                            if (pagesStart != std::string::npos) {
                                size_t pagesEnd = findMatchingBracket(chJson, pagesStart + 1, '[', ']');
                                if (pagesEnd != std::string::npos) {
                                    std::string pagesArray = chJson.substr(pagesStart + 1, pagesEnd - pagesStart - 1);

                                    size_t pgStart = 0;
                                    while ((pgStart = pagesArray.find('{', pgStart)) != std::string::npos) {
                                        size_t pgEnd = pagesArray.find('}', pgStart);
                                        if (pgEnd == std::string::npos) break;

                                        std::string pgJson = pagesArray.substr(pgStart, pgEnd - pgStart + 1);

                                        DownloadedPage page;
                                        page.index = extractJsonInt(pgJson, "index");
                                        page.localPath = extractJsonString(pgJson, "localPath");
                                        page.size = extractJsonInt(pgJson, "size");
                                        page.downloaded = extractJsonBool(pgJson, "downloaded");

                                        chapter.pages.push_back(page);
                                        pgStart = pgEnd + 1;
                                    }
                                }
                            }
                        }

                        item.chapters.push_back(chapter);
                        chStart = chEnd + 1;
                    }
                }
            }
        }

        item.totalChapters = static_cast<int>(item.chapters.size());
        item.completedChapters = 0;
        for (const auto& ch : item.chapters) {
            if (ch.state == LocalDownloadState::COMPLETED) {
                item.completedChapters++;
            }
        }

        if (item.mangaId > 0) {
            m_downloads.push_back(item);
            brls::Logger::debug("DownloadsManager: Loaded manga {} with {} chapters",
                               item.mangaId, item.chapters.size());
        }

        mangaStart = mangaEnd + 1;
    }

    brls::Logger::info("DownloadsManager: State loaded with {} downloads", m_downloads.size());
#endif
}

void DownloadsManager::setProgressCallback(DownloadProgressCallback callback) {
    m_progressCallback = callback;
}

void DownloadsManager::setChapterCompletionCallback(ChapterCompletionCallback callback) {
    m_chapterCompletionCallback = callback;
}

std::string DownloadsManager::getDownloadsPath() const {
    return m_downloadsPath;
}

std::string DownloadsManager::downloadCoverImage(int mangaId, const std::string& coverUrl) {
    if (coverUrl.empty()) return "";

    std::string localPath = m_downloadsPath + "/manga_" + std::to_string(mangaId) + "/cover.jpg";

    // Check if already downloaded
    if (fileExists(localPath)) {
        return localPath;
    }

    // Download cover using authenticated HTTP client (same approach as ImageLoader)
    HttpClient http;

    // Add authentication if credentials are set
    const std::string& authUser = ImageLoader::getAuthUsername();
    const std::string& authPass = ImageLoader::getAuthPassword();
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
        http.setDefaultHeader("Authorization", "Basic " + encoded);
    }

    HttpResponse resp = http.get(coverUrl);

    if (!resp.success || resp.body.empty()) {
        brls::Logger::error("DownloadsManager: Failed to download cover from {}", coverUrl);
        return "";
    }

#ifdef __vita__
    // Ensure directory exists
    createDirectory(m_downloadsPath + "/manga_" + std::to_string(mangaId));

    SceUID fd = sceIoOpen(localPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, resp.body.data(), resp.body.size());
        sceIoClose(fd);
        brls::Logger::debug("DownloadsManager: Cover saved to {}", localPath);
        return localPath;
    }
#endif

    return "";
}

std::string DownloadsManager::getLocalCoverPath(int mangaId) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& item : m_downloads) {
        if (item.mangaId == mangaId && !item.localCoverPath.empty()) {
            return item.localCoverPath;
        }
    }

    return "";
}

int DownloadsManager::getTotalDownloadedChapters() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    int count = 0;
    for (const auto& manga : m_downloads) {
        for (const auto& chapter : manga.chapters) {
            if (chapter.state == LocalDownloadState::COMPLETED) {
                count++;
            }
        }
    }

    return count;
}

int64_t DownloadsManager::getTotalDownloadSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    int64_t total = 0;
    for (const auto& manga : m_downloads) {
        total += manga.totalBytes;
    }

    return total;
}

void DownloadsManager::downloadChapter(int mangaId, DownloadedChapter& chapter) {
    brls::Logger::info("DownloadsManager: Downloading chapter {} (id={}) for manga {}",
                       chapter.chapterIndex, chapter.chapterId, mangaId);

    try {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        // Fetch pages from server using chapter ID (not index)
        brls::Logger::info("DownloadsManager: Fetching pages for chapter id={}", chapter.chapterId);
        std::vector<Page> pages;
        if (!client.fetchChapterPages(mangaId, chapter.chapterId, pages)) {
            brls::Logger::error("DownloadsManager: Failed to fetch pages for chapter {} (id={})",
                               chapter.chapterIndex, chapter.chapterId);
            std::lock_guard<std::mutex> lock(m_mutex);
            chapter.state = LocalDownloadState::FAILED;
            saveStateUnlocked();
            return;
        }
        brls::Logger::info("DownloadsManager: Got {} pages for chapter {}", pages.size(), chapter.chapterId);

        chapter.pageCount = static_cast<int>(pages.size());
        chapter.downloadedPages = 0;
        chapter.pages.clear();

        // Find manga to get the local path
        std::string mangaDir;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& manga : m_downloads) {
                if (manga.mangaId == mangaId) {
                    mangaDir = manga.localPath;
                    break;
                }
            }
        }

        if (mangaDir.empty()) {
            brls::Logger::error("DownloadsManager: Manga dir not found for {}", mangaId);
            std::lock_guard<std::mutex> lock(m_mutex);
            chapter.state = LocalDownloadState::FAILED;
            saveStateUnlocked();
            return;
        }

        brls::Logger::info("DownloadsManager: Creating chapter dir in {}", mangaDir);

        // Create chapter directory
        std::string chapterDir = createChapterDir(mangaDir, chapter.chapterIndex, chapter.name);
        chapter.localPath = chapterDir;

        brls::Logger::info("DownloadsManager: Starting page downloads to {}", chapterDir);

        // Download each page
        for (size_t i = 0; i < pages.size(); i++) {
            const auto& page = pages[i];

            if (!m_downloading) {
                // Download was paused/cancelled
                brls::Logger::info("DownloadsManager: Download paused/cancelled");
                std::lock_guard<std::mutex> lock(m_mutex);
                chapter.state = LocalDownloadState::PAUSED;
                saveStateUnlocked();
                return;
            }

            DownloadedPage downloadedPage;
            downloadedPage.index = page.index;

            // Use the page's imageUrl that was already fetched
            std::string imageUrl = page.imageUrl;
            brls::Logger::info("DownloadsManager: Downloading page {} of {}", page.index + 1, pages.size());

            if (downloadPage(mangaId, chapter.chapterIndex, page.index, imageUrl, downloadedPage.localPath)) {
                downloadedPage.downloaded = true;
                chapter.downloadedPages++;
                brls::Logger::info("DownloadsManager: Page {} downloaded successfully", page.index);
            } else {
                brls::Logger::error("DownloadsManager: Page {} download failed", page.index);
            }

            chapter.pages.push_back(downloadedPage);

            // Update progress callback
            if (m_progressCallback) {
                m_progressCallback(chapter.downloadedPages, chapter.pageCount);
            }

            // Small delay between downloads to avoid overwhelming the system
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Mark as completed if all pages downloaded
        if (chapter.downloadedPages == chapter.pageCount) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                chapter.state = LocalDownloadState::COMPLETED;

                // Update manga's completed chapters count
                for (auto& manga : m_downloads) {
                    if (manga.mangaId == mangaId) {
                        manga.completedChapters = 0;
                        for (const auto& ch : manga.chapters) {
                            if (ch.state == LocalDownloadState::COMPLETED) {
                                manga.completedChapters++;
                            }
                        }
                        break;
                    }
                }

                saveStateUnlocked();
            }
            brls::Logger::info("DownloadsManager: Chapter {} download completed", chapter.chapterIndex);

            // Notify completion callback
            if (m_chapterCompletionCallback) {
                m_chapterCompletionCallback(mangaId, chapter.chapterIndex, true);
            }
        } else {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                chapter.state = LocalDownloadState::FAILED;
                saveStateUnlocked();
            }
            brls::Logger::error("DownloadsManager: Chapter {} incomplete ({}/{})",
                               chapter.chapterIndex, chapter.downloadedPages, chapter.pageCount);

            // Notify completion callback (failure)
            if (m_chapterCompletionCallback) {
                m_chapterCompletionCallback(mangaId, chapter.chapterIndex, false);
            }
        }
    } catch (const std::exception& e) {
        brls::Logger::error("DownloadsManager: Exception downloading chapter {}: {}",
                           chapter.chapterIndex, e.what());
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            chapter.state = LocalDownloadState::FAILED;
            saveStateUnlocked();
        }
        // Notify completion callback (failure)
        if (m_chapterCompletionCallback) {
            m_chapterCompletionCallback(mangaId, chapter.chapterIndex, false);
        }
    } catch (...) {
        brls::Logger::error("DownloadsManager: Unknown exception downloading chapter {}",
                           chapter.chapterIndex);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            chapter.state = LocalDownloadState::FAILED;
            saveStateUnlocked();
        }
        // Notify completion callback (failure)
        if (m_chapterCompletionCallback) {
            m_chapterCompletionCallback(mangaId, chapter.chapterIndex, false);
        }
    }
}

bool DownloadsManager::downloadPage(int mangaId, int chapterIndex, int pageIndex,
                                     const std::string& imageUrl, std::string& localPath) {
    if (imageUrl.empty()) {
        brls::Logger::error("DownloadsManager: Empty URL for page {}", pageIndex);
        return false;
    }

    // Construct local path
    localPath = m_downloadsPath + "/manga_" + std::to_string(mangaId) +
                "/chapter_" + std::to_string(chapterIndex) +
                "/page_" + std::to_string(pageIndex) + ".jpg";

    brls::Logger::debug("DownloadsManager: Downloading page {} from {} to {}", pageIndex, imageUrl, localPath);

    // Create HTTP client with authentication (same approach as ImageLoader)
    HttpClient http;

    // Add authentication if credentials are set
    const std::string& authUser = ImageLoader::getAuthUsername();
    const std::string& authPass = ImageLoader::getAuthPassword();
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
        http.setDefaultHeader("Authorization", "Basic " + encoded);
    }

    // Stream directly to file - no memory buffering (like NOBORU does)
    if (http.downloadToFile(imageUrl, localPath)) {
        brls::Logger::debug("DownloadsManager: Page {} downloaded successfully", pageIndex);
        return true;
    }

    brls::Logger::error("DownloadsManager: Failed to download page {} from {}", pageIndex, imageUrl);
    return false;
}

std::string DownloadsManager::createMangaDir(int mangaId, const std::string& title) {
    std::string path = m_downloadsPath + "/manga_" + std::to_string(mangaId);
    createDirectory(path);
    return path;
}

std::string DownloadsManager::createChapterDir(const std::string& mangaDir, int chapterIndex,
                                                const std::string& chapterName) {
    std::string path = mangaDir + "/chapter_" + std::to_string(chapterIndex);
    createDirectory(path);
    return path;
}

} // namespace vitasuwayomi
