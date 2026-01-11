/**
 * VitaSuwayomi - Downloads Manager Implementation
 * Handles offline media downloads and progress sync
 */

#include "app/downloads_manager.hpp"
#include "app/audiobookshelf_client.hpp"
#include "app/application.hpp"
#include "utils/http_client.hpp"
#include <borealis.hpp>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstdlib>
#include <thread>
#include <utility>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/kernel/threadmgr.h>
#endif

// FFmpeg for audio concatenation
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
}

namespace vitasuwayomi {

// Helper function to concatenate audio files using FFmpeg
// Uses stream copy for fast concatenation without re-encoding
static bool concatenateAudioFiles(const std::vector<std::string>& inputFiles,
                                   const std::string& outputPath,
                                   std::function<void(int, int)> progressCallback = nullptr) {
    if (inputFiles.empty()) {
        brls::Logger::error("concatenateAudioFiles: No input files provided");
        return false;
    }

    brls::Logger::info("concatenateAudioFiles: Combining {} files into {}", inputFiles.size(), outputPath);

    // Determine output format based on extension
    std::string outputExt = ".m4b";
    size_t dotPos = outputPath.rfind('.');
    if (dotPos != std::string::npos) {
        outputExt = outputPath.substr(dotPos);
    }

    // For MP3 files, use simple binary concatenation (MP3 frames are self-contained)
    if (outputExt == ".mp3") {
        brls::Logger::info("concatenateAudioFiles: Using binary concatenation for MP3");

        // Use heap allocation to avoid stack overflow on Vita
        const size_t BUFFER_SIZE = 8192;
        char* buffer = new char[BUFFER_SIZE];
        if (!buffer) {
            brls::Logger::error("concatenateAudioFiles: Failed to allocate buffer");
            return false;
        }

#ifdef __vita__
        SceUID outFd = sceIoOpen(outputPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (outFd < 0) {
            brls::Logger::error("concatenateAudioFiles: Could not create output file");
            delete[] buffer;
            return false;
        }

        int filesProcessed = 0;

        for (size_t fileIdx = 0; fileIdx < inputFiles.size(); fileIdx++) {
            const std::string& inputFile = inputFiles[fileIdx];

            SceUID inFd = sceIoOpen(inputFile.c_str(), SCE_O_RDONLY, 0);
            if (inFd < 0) {
                brls::Logger::warning("concatenateAudioFiles: Could not open {}", inputFile);
                continue;
            }

            int bytesRead;
            while ((bytesRead = sceIoRead(inFd, buffer, BUFFER_SIZE)) > 0) {
                sceIoWrite(outFd, buffer, bytesRead);
            }

            sceIoClose(inFd);
            filesProcessed++;

            if (progressCallback) {
                progressCallback(filesProcessed, static_cast<int>(inputFiles.size()));
            }
        }

        sceIoClose(outFd);
        delete[] buffer;
        brls::Logger::info("concatenateAudioFiles: Successfully concatenated {} MP3 files", filesProcessed);
        return filesProcessed > 0;
#else
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) {
            brls::Logger::error("concatenateAudioFiles: Could not create output file");
            delete[] buffer;
            return false;
        }

        int filesProcessed = 0;

        for (size_t fileIdx = 0; fileIdx < inputFiles.size(); fileIdx++) {
            const std::string& inputFile = inputFiles[fileIdx];

            std::ifstream inFile(inputFile, std::ios::binary);
            if (!inFile) {
                brls::Logger::warning("concatenateAudioFiles: Could not open {}", inputFile);
                continue;
            }

            while (inFile.read(buffer, BUFFER_SIZE) || inFile.gcount() > 0) {
                outFile.write(buffer, inFile.gcount());
            }

            filesProcessed++;

            if (progressCallback) {
                progressCallback(filesProcessed, static_cast<int>(inputFiles.size()));
            }
        }

        delete[] buffer;
        brls::Logger::info("concatenateAudioFiles: Successfully concatenated {} MP3 files", filesProcessed);
        return filesProcessed > 0;
#endif
    }

    // For non-MP3, use FFmpeg concat demuxer
    // Create a concat file list for the concat demuxer
    std::string concatListPath = outputPath + ".concat.txt";

#ifdef __vita__
    SceUID listFd = sceIoOpen(concatListPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (listFd >= 0) {
        for (const auto& file : inputFiles) {
            std::string line = "file '" + file + "'\n";
            sceIoWrite(listFd, line.c_str(), line.size());
        }
        sceIoClose(listFd);
    } else {
        brls::Logger::error("concatenateAudioFiles: Failed to create concat list file");
        return false;
    }
#else
    std::ofstream listFile(concatListPath);
    if (listFile.is_open()) {
        for (const auto& file : inputFiles) {
            listFile << "file '" << file << "'\n";
        }
        listFile.close();
    } else {
        brls::Logger::error("concatenateAudioFiles: Failed to create concat list file");
        return false;
    }
#endif

    // Open input using concat demuxer
    AVFormatContext* inputFmtCtx = nullptr;
    AVDictionary* options = nullptr;

    // Set safe option for concat demuxer to allow absolute paths
    av_dict_set(&options, "safe", "0", 0);

    std::string concatUrl = "concat:" + concatListPath;

    // Use the concat demuxer format
    const AVInputFormat* concatFmt = av_find_input_format("concat");
    if (!concatFmt) {
        brls::Logger::error("concatenateAudioFiles: concat demuxer not available");
        av_dict_free(&options);
        return false;
    }

    int ret = avformat_open_input(&inputFmtCtx, concatListPath.c_str(), concatFmt, &options);
    av_dict_free(&options);

    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        brls::Logger::error("concatenateAudioFiles: Could not open input: {}", errbuf);
        return false;
    }

    ret = avformat_find_stream_info(inputFmtCtx, nullptr);
    if (ret < 0) {
        brls::Logger::error("concatenateAudioFiles: Could not find stream info");
        avformat_close_input(&inputFmtCtx);
        return false;
    }

    // Create output format context (for non-MP3 formats)
    AVFormatContext* outputFmtCtx = nullptr;

    // Try multiple formats in order of preference
    // Note: "ipod" format may not be available on all platforms (like Vita)
    // MP3 is handled above via binary concatenation
    const char* formatOptions[] = { nullptr, nullptr, nullptr };
    int numFormats = 0;

    if (outputExt == ".ogg") {
        formatOptions[0] = "ogg";
        numFormats = 1;
    } else {
        // For m4b/m4a, try mp4 first (more compatible), then ipod, then mov
        formatOptions[0] = "mp4";
        formatOptions[1] = "ipod";
        formatOptions[2] = "mov";
        numFormats = 3;
    }

    const char* usedFormat = nullptr;
    for (int i = 0; i < numFormats; i++) {
        ret = avformat_alloc_output_context2(&outputFmtCtx, nullptr, formatOptions[i], outputPath.c_str());
        if (ret >= 0 && outputFmtCtx) {
            usedFormat = formatOptions[i];
            brls::Logger::info("concatenateAudioFiles: Using output format '{}'", usedFormat);
            break;
        }
        if (outputFmtCtx) {
            avformat_free_context(outputFmtCtx);
            outputFmtCtx = nullptr;
        }
    }

    if (ret < 0 || !outputFmtCtx) {
        brls::Logger::error("concatenateAudioFiles: Could not create output context");
        avformat_close_input(&inputFmtCtx);
        return false;
    }

    // Copy streams from input to output
    int audioStreamIndex = -1;
    for (unsigned int i = 0; i < inputFmtCtx->nb_streams; i++) {
        AVStream* inStream = inputFmtCtx->streams[i];
        if (inStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            AVStream* outStream = avformat_new_stream(outputFmtCtx, nullptr);
            if (!outStream) {
                brls::Logger::error("concatenateAudioFiles: Could not create output stream");
                avformat_close_input(&inputFmtCtx);
                avformat_free_context(outputFmtCtx);
                return false;
            }

            ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
            if (ret < 0) {
                brls::Logger::error("concatenateAudioFiles: Could not copy codec parameters");
                avformat_close_input(&inputFmtCtx);
                avformat_free_context(outputFmtCtx);
                return false;
            }

            outStream->codecpar->codec_tag = 0;
            audioStreamIndex = i;
            break;
        }
    }

    if (audioStreamIndex < 0) {
        brls::Logger::error("concatenateAudioFiles: No audio stream found");
        avformat_close_input(&inputFmtCtx);
        avformat_free_context(outputFmtCtx);
        return false;
    }

    // Open output file
    if (!(outputFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outputFmtCtx->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            brls::Logger::error("concatenateAudioFiles: Could not open output file: {}", errbuf);
            avformat_close_input(&inputFmtCtx);
            avformat_free_context(outputFmtCtx);
            return false;
        }
    }

    // Write header
    ret = avformat_write_header(outputFmtCtx, nullptr);
    if (ret < 0) {
        brls::Logger::error("concatenateAudioFiles: Could not write header");
        avformat_close_input(&inputFmtCtx);
        if (!(outputFmtCtx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outputFmtCtx->pb);
        avformat_free_context(outputFmtCtx);
        return false;
    }

    // Copy packets
    AVPacket* pkt = av_packet_alloc();
    int64_t packetsWritten = 0;
    int64_t lastProgressReport = 0;

    while (av_read_frame(inputFmtCtx, pkt) >= 0) {
        if (pkt->stream_index == audioStreamIndex) {
            // Rescale timestamps
            AVStream* inStream = inputFmtCtx->streams[audioStreamIndex];
            AVStream* outStream = outputFmtCtx->streams[0];

            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, inStream->time_base, outStream->time_base);

            ret = av_interleaved_write_frame(outputFmtCtx, pkt);
            if (ret < 0) {
                brls::Logger::warning("concatenateAudioFiles: Error writing packet");
            }

            packetsWritten++;

            // Report progress periodically
            if (progressCallback && packetsWritten - lastProgressReport > 1000) {
                progressCallback(static_cast<int>(packetsWritten), 0);
                lastProgressReport = packetsWritten;
            }
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    // Write trailer
    av_write_trailer(outputFmtCtx);

    // Cleanup
    avformat_close_input(&inputFmtCtx);
    if (!(outputFmtCtx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outputFmtCtx->pb);
    avformat_free_context(outputFmtCtx);

    // Remove concat list file
#ifdef __vita__
    sceIoRemove(concatListPath.c_str());
#else
    std::remove(concatListPath.c_str());
#endif

    brls::Logger::info("concatenateAudioFiles: Successfully combined {} files ({} packets)",
                       inputFiles.size(), packetsWritten);
    return true;
}

// Downloads directory on Vita
#ifdef __vita__
static const char* DOWNLOADS_DIR = "ux0:data/VitaSuwayomi/downloads";
static const char* STATE_FILE = "ux0:data/VitaSuwayomi/downloads/state.json";
#else
static const char* DOWNLOADS_DIR = "./downloads";
static const char* STATE_FILE = "./downloads/state.json";
#endif

DownloadsManager& DownloadsManager::getInstance() {
    static DownloadsManager instance;
    return instance;
}

bool DownloadsManager::init() {
    if (m_initialized) return true;

    m_downloadsPath = DOWNLOADS_DIR;

#ifdef __vita__
    // Create downloads directory if it doesn't exist
    sceIoMkdir("ux0:data/VitaSuwayomi", 0777);
    sceIoMkdir(DOWNLOADS_DIR, 0777);
#else
    // Create directory on other platforms
    std::system("mkdir -p ./downloads");
#endif

    // Load saved state
    loadState();

    m_initialized = true;
    brls::Logger::info("DownloadsManager: Initialized at {}", m_downloadsPath);
    return true;
}

bool DownloadsManager::queueDownload(const std::string& itemId, const std::string& title,
                                      const std::string& authorName, float duration,
                                      const std::string& mediaType,
                                      const std::string& seriesName,
                                      const std::string& episodeId) {
    brls::Logger::info("DownloadsManager::queueDownload called:");
    brls::Logger::info("  - itemId: {}", itemId);
    brls::Logger::info("  - title: {}", title);
    brls::Logger::info("  - mediaType: {}", mediaType);
    brls::Logger::info("  - episodeId: {}", episodeId.empty() ? "(none)" : episodeId);

    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if already in queue - for episodes, check both itemId AND episodeId
    for (const auto& item : m_downloads) {
        if (item.itemId == itemId) {
            // For podcast episodes, also check episodeId since multiple episodes share podcastId
            if (episodeId.empty() || item.episodeId == episodeId) {
                brls::Logger::warning("DownloadsManager: {} already in queue", title);
                return false;
            }
        }
    }

    DownloadItem item;
    item.itemId = itemId;
    item.title = title;
    item.authorName = authorName;
    item.parentTitle = seriesName.empty() ? authorName : seriesName;
    item.duration = duration;
    item.mediaType = mediaType;
    item.seriesName = seriesName;
    item.episodeId = episodeId;
    item.state = DownloadState::QUEUED;

    // Get cover URL from client
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
    item.coverUrl = client.getCoverUrl(itemId);

    // Generate local path - for episodes use episodeId to ensure unique filenames
    std::string extension;
    std::string fileId;
    if (!episodeId.empty()) {
        // Podcast episode - use episodeId for unique filename
        extension = ".mp3";
        fileId = episodeId;
    } else if (mediaType == "podcast") {
        extension = ".mp3";
        fileId = itemId;
    } else {
        // For audiobooks, use m4b (common audiobook format)
        extension = ".m4b";
        fileId = itemId;
    }
    std::string filename = fileId + extension;
    item.localPath = m_downloadsPath + "/" + filename;

    brls::Logger::info("DownloadsManager: Local path: {}", item.localPath);

    m_downloads.push_back(item);
    saveState();

    brls::Logger::info("DownloadsManager: Successfully queued {} for download (total in queue: {})",
                       title, m_downloads.size());
    return true;
}

void DownloadsManager::startDownloads() {
    if (m_downloading) return;
    m_downloading = true;

    brls::Logger::info("DownloadsManager: Starting download queue");

    // Process downloads in background
    std::thread([this]() {
        brls::Logger::info("DownloadsManager: Download thread started");

        while (m_downloading) {
            DownloadItem* nextItem = nullptr;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto& item : m_downloads) {
                    if (item.state == DownloadState::QUEUED) {
                        item.state = DownloadState::DOWNLOADING;
                        nextItem = &item;
                        brls::Logger::info("DownloadsManager: Found queued item: {}", item.title);
                        break;
                    }
                }
            }

            if (nextItem) {
                brls::Logger::info("DownloadsManager: Starting download of {}", nextItem->title);
                downloadItem(*nextItem);
            } else {
                // No more queued items
                brls::Logger::info("DownloadsManager: No more queued items");
                break;
            }
        }
        m_downloading = false;
        brls::Logger::info("DownloadsManager: Download thread finished");
    }).detach();
}

void DownloadsManager::pauseDownloads() {
    m_downloading = false;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& item : m_downloads) {
        if (item.state == DownloadState::DOWNLOADING) {
            item.state = DownloadState::PAUSED;
        }
    }
    saveState();
}

bool DownloadsManager::cancelDownload(const std::string& itemId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it) {
        if (it->itemId == itemId) {
            // Delete partial file if exists
            if (!it->localPath.empty()) {
#ifdef __vita__
                sceIoRemove(it->localPath.c_str());
#else
                std::remove(it->localPath.c_str());
#endif
            }
            m_downloads.erase(it);
            saveState();
            return true;
        }
    }
    return false;
}

bool DownloadsManager::deleteDownload(const std::string& itemId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it) {
        if (it->itemId == itemId) {
            // Delete audio file
            if (!it->localPath.empty()) {
#ifdef __vita__
                sceIoRemove(it->localPath.c_str());
#else
                std::remove(it->localPath.c_str());
#endif
                brls::Logger::info("DownloadsManager: Deleted file {}", it->localPath);
            }
            // Delete cover image if exists
            if (!it->localCoverPath.empty()) {
#ifdef __vita__
                sceIoRemove(it->localCoverPath.c_str());
#else
                std::remove(it->localCoverPath.c_str());
#endif
                brls::Logger::debug("DownloadsManager: Deleted cover {}", it->localCoverPath);
            }
            m_downloads.erase(it);
            saveStateUnlocked();
            brls::Logger::info("DownloadsManager: Deleted download {}", itemId);
            return true;
        }
    }
    return false;
}

bool DownloadsManager::deleteDownloadByEpisodeId(const std::string& itemId, const std::string& episodeId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_downloads.begin(); it != m_downloads.end(); ++it) {
        if (it->itemId == itemId && it->episodeId == episodeId) {
            // Delete audio file
            if (!it->localPath.empty()) {
#ifdef __vita__
                sceIoRemove(it->localPath.c_str());
#else
                std::remove(it->localPath.c_str());
#endif
                brls::Logger::info("DownloadsManager: Deleted file {}", it->localPath);
            }
            // Delete cover image if exists
            if (!it->localCoverPath.empty()) {
#ifdef __vita__
                sceIoRemove(it->localCoverPath.c_str());
#else
                std::remove(it->localCoverPath.c_str());
#endif
                brls::Logger::debug("DownloadsManager: Deleted cover {}", it->localCoverPath);
            }
            std::string title = it->title;
            m_downloads.erase(it);
            saveStateUnlocked();
            brls::Logger::info("DownloadsManager: Deleted episode download {} ({})", title, episodeId);
            return true;
        }
    }
    return false;
}

std::vector<DownloadItem> DownloadsManager::getDownloads() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_downloads;
}

DownloadItem* DownloadsManager::getDownload(const std::string& itemId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& item : m_downloads) {
        if (item.itemId == itemId) {
            return &item;
        }
    }
    return nullptr;
}

DownloadItem* DownloadsManager::getDownload(const std::string& itemId, const std::string& episodeId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& item : m_downloads) {
        if (item.itemId == itemId) {
            // For episodes, also check episodeId
            if (episodeId.empty() || item.episodeId == episodeId) {
                return &item;
            }
        }
    }
    return nullptr;
}

bool DownloadsManager::isDownloaded(const std::string& itemId, const std::string& episodeId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& item : m_downloads) {
        if (item.itemId == itemId && item.state == DownloadState::COMPLETED) {
            // For episodes, also check episodeId
            if (episodeId.empty() || item.episodeId == episodeId) {
                return true;
            }
        }
    }
    return false;
}

std::string DownloadsManager::getLocalPath(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& item : m_downloads) {
        if (item.itemId == itemId && item.state == DownloadState::COMPLETED) {
            return item.localPath;
        }
    }
    return "";
}

std::string DownloadsManager::getPlaybackPath(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& item : m_downloads) {
        if (item.itemId == itemId && item.state == DownloadState::COMPLETED) {
            // For multi-file audiobooks, return the first file
            if (item.numFiles > 1 && !item.files.empty()) {
                brls::Logger::debug("DownloadsManager: Multi-file audiobook, returning first file: {}",
                                   item.files[0].localPath);
                return item.files[0].localPath;
            }
            // Single file or direct path
            return item.localPath;
        }
    }
    return "";
}

void DownloadsManager::updateProgress(const std::string& itemId, float currentTime, const std::string& episodeId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& item : m_downloads) {
        // Match by itemId and episodeId (episodeId is empty for books, non-empty for podcasts)
        if (item.itemId == itemId && (episodeId.empty() || item.episodeId == episodeId)) {
            item.currentTime = currentTime;
            item.viewOffset = static_cast<int64_t>(currentTime * 1000.0f);  // Convert to milliseconds
            brls::Logger::debug("DownloadsManager: Updated progress for '{}' to {}s",
                               item.title, currentTime);
            break;
        }
    }
    // Don't save on every update - too frequent
}

void DownloadsManager::syncProgressToServer() {
    std::vector<DownloadItem> itemsToSync;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& item : m_downloads) {
            if (item.state == DownloadState::COMPLETED && item.currentTime > 0) {
                itemsToSync.push_back(item);
            }
        }
    }

    brls::Logger::info("DownloadsManager: Syncing {} items to server", itemsToSync.size());

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    for (auto& item : itemsToSync) {
        // Update progress on the Audiobookshelf server
        // Signature: updateProgress(itemId, currentTime, duration, isFinished, episodeId)
        bool isFinished = (item.duration > 0 && item.currentTime >= item.duration * 0.95f);
        if (client.updateProgress(item.itemId, item.currentTime, item.duration, isFinished, item.episodeId)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            // Update last synced time
            for (auto& d : m_downloads) {
                if (d.itemId == item.itemId) {
                    d.lastSynced = std::time(nullptr);
                    break;
                }
            }
            brls::Logger::debug("DownloadsManager: Synced progress for {}", item.title);
        }
    }

    saveState();
}

void DownloadsManager::syncProgressFromServer() {
    std::vector<std::pair<std::string, std::string>> itemsToFetch;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& item : m_downloads) {
            if (item.state == DownloadState::COMPLETED) {
                itemsToFetch.push_back({item.itemId, item.episodeId});
            }
        }
    }

    if (itemsToFetch.empty()) {
        brls::Logger::debug("DownloadsManager: No downloaded items to sync from server");
        return;
    }

    brls::Logger::info("DownloadsManager: Fetching progress from server for {} items", itemsToFetch.size());

    for (const auto& item : itemsToFetch) {
        fetchProgressFromServer(item.first, item.second);
    }

    saveState();
}

bool DownloadsManager::fetchProgressFromServer(const std::string& itemId, const std::string& episodeId) {
    brls::Logger::info("DownloadsManager::fetchProgressFromServer itemId={} episodeId={}",
                      itemId, episodeId.empty() ? "(none)" : episodeId);

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    float serverTime = 0.0f;
    float serverProgress = 0.0f;
    bool serverFinished = false;

    if (!client.getProgress(itemId, serverTime, serverProgress, serverFinished, episodeId)) {
        brls::Logger::warning("DownloadsManager: Could not fetch progress for {} from server", itemId);
        return false;
    }

    brls::Logger::info("DownloadsManager: Server returned progress {}s for {}", serverTime, itemId);

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& item : m_downloads) {
        brls::Logger::debug("DownloadsManager: Checking download item '{}' itemId={} episodeId={}",
                           item.title, item.itemId, item.episodeId.empty() ? "(none)" : item.episodeId);

        if (item.itemId == itemId && item.episodeId == episodeId) {
            // Only update if server progress is ahead of local progress
            if (serverTime > item.currentTime) {
                brls::Logger::info("DownloadsManager: Updating '{}' from {}s to {}s (from server)",
                                  item.title, item.currentTime, serverTime);
                item.currentTime = serverTime;
                item.viewOffset = static_cast<int64_t>(serverTime * 1000.0f);
            } else {
                brls::Logger::info("DownloadsManager: Local progress {}s >= server {}s for '{}', keeping local",
                                   item.currentTime, serverTime, item.title);
            }
            return true;
        }
    }

    brls::Logger::warning("DownloadsManager: No matching download found for itemId={} episodeId={}",
                         itemId, episodeId.empty() ? "(none)" : episodeId);
    return false;
}

void DownloadsManager::downloadItem(DownloadItem& item) {
    brls::Logger::info("DownloadsManager: Starting download of {}", item.title);
    brls::Logger::info("DownloadsManager: Item ID: {}, Episode ID: {}, Type: {}",
                       item.itemId, item.episodeId.empty() ? "(none)" : item.episodeId, item.mediaType);

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
    std::string serverUrl = client.getServerUrl();
    std::string token = client.getAuthToken();

    brls::Logger::debug("DownloadsManager: Server URL: {}", serverUrl);
    brls::Logger::debug("DownloadsManager: Auth token present: {}", !token.empty() ? "yes" : "no");

    if (serverUrl.empty() || token.empty()) {
        brls::Logger::error("DownloadsManager: Not connected to server");
        item.state = DownloadState::FAILED;
        saveState();
        return;
    }

    // Check if this is a multi-file audiobook
    std::vector<AudioFileInfo> audioFiles;
    if (item.episodeId.empty() && item.mediaType == "book") {
        brls::Logger::info("DownloadsManager: Checking for multi-file audiobook...");
        client.getAudioFiles(item.itemId, audioFiles);
        brls::Logger::info("DownloadsManager: Found {} audio files", audioFiles.size());
    }

    if (audioFiles.size() > 1) {
        // Multi-file audiobook - download all files and create M3U playlist
        brls::Logger::info("DownloadsManager: Multi-file audiobook with {} files - will create combined playlist", audioFiles.size());

        item.numFiles = static_cast<int>(audioFiles.size());
        item.files.clear();

        // Create folder for multi-file item
        std::string folderPath = m_downloadsPath + "/" + item.itemId;
#ifdef __vita__
        sceIoMkdir(folderPath.c_str(), 0777);
#else
        std::system(("mkdir -p \"" + folderPath + "\"").c_str());
#endif

        // Calculate total size
        item.totalBytes = 0;
        for (const auto& af : audioFiles) {
            DownloadFileInfo fi;
            fi.ino = af.ino;
            fi.filename = af.filename;
            fi.localPath = folderPath + "/" + af.filename;
            fi.size = af.size;
            fi.downloaded = false;
            item.files.push_back(fi);
            item.totalBytes += af.size;
        }

        // Download each file
        HttpClient http;
        http.setDefaultHeader("Authorization", "Bearer " + token);

        for (size_t i = 0; i < item.files.size() && m_downloading; ++i) {
            auto& fi = item.files[i];
            item.currentFileIndex = static_cast<int>(i);

            std::string url = client.getFileDownloadUrlByIno(item.itemId, fi.ino);
            brls::Logger::info("DownloadsManager: Downloading file {}/{}: {}",
                              i + 1, item.files.size(), fi.filename);

#ifdef __vita__
            SceUID fd = sceIoOpen(fi.localPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
            if (fd < 0) {
                brls::Logger::error("DownloadsManager: Failed to create file {}", fi.localPath);
                continue;
            }
#else
            std::ofstream file(fi.localPath, std::ios::binary);
            if (!file.is_open()) {
                brls::Logger::error("DownloadsManager: Failed to create file {}", fi.localPath);
                continue;
            }
#endif

            bool success = http.downloadFile(url,
                [&](const char* data, size_t size) {
#ifdef __vita__
                    sceIoWrite(fd, data, size);
#else
                    file.write(data, size);
#endif
                    item.downloadedBytes += size;
                    if (m_progressCallback && item.totalBytes > 0) {
                        m_progressCallback(static_cast<float>(item.downloadedBytes),
                                           static_cast<float>(item.totalBytes));
                    }
                    return m_downloading;
                },
                [&](int64_t total) {
                    brls::Logger::debug("DownloadsManager: File size: {} bytes", total);
                }
            );

#ifdef __vita__
            sceIoClose(fd);
#else
            file.close();
#endif

            fi.downloaded = success;
        }

        // Check if all files downloaded
        bool allComplete = true;
        for (const auto& fi : item.files) {
            if (!fi.downloaded) allComplete = false;
        }

        if (allComplete) {
            // Use FFmpeg to combine all files into a single audiobook
            brls::Logger::info("DownloadsManager: All {} files downloaded, combining with FFmpeg...", item.files.size());

            // Collect file paths in order for concatenation
            std::vector<std::string> filePaths;
            for (const auto& fi : item.files) {
                filePaths.push_back(fi.localPath);
            }

            // Determine output extension from source files (mp3 -> mp3, m4a -> m4b)
            std::string outputExt = ".mp3";  // Default to mp3 since Vita lacks mp4 muxer
            if (!item.files.empty()) {
                const std::string& firstFile = item.files[0].localPath;
                size_t dotPos = firstFile.rfind('.');
                if (dotPos != std::string::npos) {
                    std::string srcExt = firstFile.substr(dotPos);
                    if (srcExt == ".m4a" || srcExt == ".m4b" || srcExt == ".mp4") {
                        outputExt = ".m4b";
                    } else if (srcExt == ".ogg") {
                        outputExt = ".ogg";
                    } else if (srcExt == ".flac") {
                        outputExt = ".flac";
                    }
                    // else keep .mp3 for mp3 and other formats
                }
            }

            // Combined output file path
            std::string combinedPath = m_downloadsPath + "/" + item.itemId + outputExt;

            // Run FFmpeg concatenation
            bool concatSuccess = concatenateAudioFiles(filePaths, combinedPath,
                [this](int current, int total) {
                    brls::Logger::debug("DownloadsManager: Concatenation progress: {} packets", current);
                }
            );

            if (concatSuccess) {
                brls::Logger::info("DownloadsManager: Successfully combined {} files into {}", item.files.size(), combinedPath);

                // Delete individual files to save space
                for (const auto& fi : item.files) {
#ifdef __vita__
                    sceIoRemove(fi.localPath.c_str());
#else
                    std::remove(fi.localPath.c_str());
#endif
                }

                // Remove the temp folder
#ifdef __vita__
                sceIoRmdir(folderPath.c_str());
#else
                std::remove(folderPath.c_str());
#endif

                // Update item to point to combined file
                item.localPath = combinedPath;
                item.files.clear();
                item.numFiles = 1;
                item.state = DownloadState::COMPLETED;
                brls::Logger::info("DownloadsManager: Completed combined download: {}", item.title);

                // Download cover image for offline use
                if (!item.coverUrl.empty()) {
                    item.localCoverPath = downloadCoverImage(item.itemId, item.coverUrl);
                }

                // Fetch and store metadata (description, chapters) for offline use
                MediaItem mediaInfo;
                if (client.fetchItem(item.itemId, mediaInfo)) {
                    item.description = mediaInfo.description;
                    item.numChapters = static_cast<int>(mediaInfo.chapters.size());
                    item.chapters.clear();
                    for (const auto& ch : mediaInfo.chapters) {
                        DownloadChapter dch;
                        dch.title = ch.title;
                        dch.start = ch.start;
                        dch.end = ch.end;
                        item.chapters.push_back(dch);
                    }
                    brls::Logger::info("DownloadsManager: Stored {} chapters for offline use", item.chapters.size());
                }
            } else {
                brls::Logger::error("DownloadsManager: FFmpeg concatenation failed, falling back to first file");
                // Fallback: use first file if concatenation fails
                item.localPath = item.files[0].localPath;
                item.state = DownloadState::COMPLETED;
            }
        } else if (!m_downloading) {
            item.state = DownloadState::PAUSED;
        } else {
            item.state = DownloadState::FAILED;
        }
        saveState();
        return;
    }

    // Single file download (original logic)
    brls::Logger::info("DownloadsManager: Single file download mode");
    brls::Logger::info("DownloadsManager: Getting download URL for item: {}, episode: {}",
                       item.itemId, item.episodeId.empty() ? "(none)" : item.episodeId);

    std::string url = client.getFileDownloadUrl(item.itemId, item.episodeId);

    if (url.empty()) {
        brls::Logger::error("DownloadsManager: Failed to get download URL for {}", item.itemId);
        brls::Logger::error("DownloadsManager: This usually means the file ino could not be found");
        item.state = DownloadState::FAILED;
        saveState();
        return;
    }

    brls::Logger::info("DownloadsManager: Download URL: {}", url);
    brls::Logger::info("DownloadsManager: Local path: {}", item.localPath);

    // Open local file for writing
#ifdef __vita__
    SceUID fd = sceIoOpen(item.localPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        brls::Logger::error("DownloadsManager: Failed to create file {}", item.localPath);
        item.state = DownloadState::FAILED;
        saveState();
        return;
    }
#else
    std::ofstream file(item.localPath, std::ios::binary);
    if (!file.is_open()) {
        brls::Logger::error("DownloadsManager: Failed to create file {}", item.localPath);
        item.state = DownloadState::FAILED;
        saveState();
        return;
    }
#endif

    // Download with progress tracking
    HttpClient http;

    // Set auth header
    http.setDefaultHeader("Authorization", "Bearer " + token);

    bool success = http.downloadFile(url,
        [&](const char* data, size_t size) {
            // Write chunk to file
#ifdef __vita__
            sceIoWrite(fd, data, size);
#else
            file.write(data, size);
#endif
            item.downloadedBytes += size;

            // Call progress callback
            if (m_progressCallback && item.totalBytes > 0) {
                m_progressCallback(static_cast<float>(item.downloadedBytes),
                                   static_cast<float>(item.totalBytes));
            }

            return m_downloading; // Return false to cancel
        },
        [&](int64_t total) {
            item.totalBytes = total;
            brls::Logger::debug("DownloadsManager: Total size: {} bytes", total);
        }
    );

#ifdef __vita__
    sceIoClose(fd);
#else
    file.close();
#endif

    if (success && m_downloading) {
        item.state = DownloadState::COMPLETED;
        brls::Logger::info("DownloadsManager: Completed download of {}", item.title);

        // Download cover image for offline use
        if (!item.coverUrl.empty()) {
            item.localCoverPath = downloadCoverImage(item.itemId, item.coverUrl);
        }

        // Fetch and store metadata (description, chapters) for offline use
        MediaItem mediaInfo;
        if (client.fetchItem(item.itemId, mediaInfo)) {
            item.description = mediaInfo.description;
            item.numChapters = static_cast<int>(mediaInfo.chapters.size());
            item.chapters.clear();
            for (const auto& ch : mediaInfo.chapters) {
                DownloadChapter dch;
                dch.title = ch.title;
                dch.start = ch.start;
                dch.end = ch.end;
                item.chapters.push_back(dch);
            }
            brls::Logger::info("DownloadsManager: Stored {} chapters for offline use", item.chapters.size());
        }
    } else if (!m_downloading) {
        item.state = DownloadState::PAUSED;
        brls::Logger::info("DownloadsManager: Paused download of {}", item.title);
    } else {
        item.state = DownloadState::FAILED;
        brls::Logger::error("DownloadsManager: Failed to download {}", item.title);
        // Delete partial file
#ifdef __vita__
        sceIoRemove(item.localPath.c_str());
#else
        std::remove(item.localPath.c_str());
#endif
    }

    saveState();
}

// Helper to escape JSON strings
static std::string escapeJsonString(const std::string& str) {
    std::string result;
    result.reserve(str.size() + 10);
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

void DownloadsManager::saveState() {
    std::lock_guard<std::mutex> lock(m_mutex);
    saveStateUnlocked();
}

void DownloadsManager::saveStateUnlocked() {
    // Simple JSON-like format for state
    std::stringstream ss;
    ss << "{\n\"downloads\":[\n";

    for (size_t i = 0; i < m_downloads.size(); ++i) {
        const auto& item = m_downloads[i];
        ss << "{\n"
           << "\"itemId\":\"" << item.itemId << "\",\n"
           << "\"episodeId\":\"" << item.episodeId << "\",\n"
           << "\"title\":\"" << escapeJsonString(item.title) << "\",\n"
           << "\"authorName\":\"" << escapeJsonString(item.authorName) << "\",\n"
           << "\"parentTitle\":\"" << escapeJsonString(item.parentTitle) << "\",\n"
           << "\"localPath\":\"" << item.localPath << "\",\n"
           << "\"coverUrl\":\"" << item.coverUrl << "\",\n"
           << "\"localCoverPath\":\"" << item.localCoverPath << "\",\n"
           << "\"description\":\"" << escapeJsonString(item.description) << "\",\n"
           << "\"mediaType\":\"" << item.mediaType << "\",\n"
           << "\"seriesName\":\"" << escapeJsonString(item.seriesName) << "\",\n"
           << "\"totalBytes\":" << item.totalBytes << ",\n"
           << "\"downloadedBytes\":" << item.downloadedBytes << ",\n"
           << "\"duration\":" << item.duration << ",\n"
           << "\"currentTime\":" << item.currentTime << ",\n"
           << "\"viewOffset\":" << item.viewOffset << ",\n"
           << "\"numChapters\":" << item.numChapters << ",\n"
           << "\"numFiles\":" << item.numFiles << ",\n"
           << "\"state\":" << static_cast<int>(item.state) << ",\n"
           << "\"lastSynced\":" << item.lastSynced << ",\n";

        // Save chapters for offline use
        ss << "\"chapters\":[";
        for (size_t j = 0; j < item.chapters.size(); ++j) {
            const auto& ch = item.chapters[j];
            ss << "{"
               << "\"title\":\"" << escapeJsonString(ch.title) << "\","
               << "\"start\":" << ch.start << ","
               << "\"end\":" << ch.end
               << "}";
            if (j < item.chapters.size() - 1) ss << ",";
        }
        ss << "],\n";

        // Save multi-file info
        ss << "\"files\":[";
        for (size_t j = 0; j < item.files.size(); ++j) {
            const auto& fi = item.files[j];
            ss << "{"
               << "\"ino\":\"" << fi.ino << "\","
               << "\"filename\":\"" << fi.filename << "\","
               << "\"localPath\":\"" << fi.localPath << "\","
               << "\"size\":" << fi.size << ","
               << "\"downloaded\":" << (fi.downloaded ? "true" : "false")
               << "}";
            if (j < item.files.size() - 1) ss << ",";
        }
        ss << "]\n}";

        if (i < m_downloads.size() - 1) ss << ",";
        ss << "\n";
    }

    ss << "]\n}";

#ifdef __vita__
    SceUID fd = sceIoOpen(STATE_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) {
        std::string data = ss.str();
        sceIoWrite(fd, data.c_str(), data.size());
        sceIoClose(fd);
    }
#else
    std::ofstream file(STATE_FILE);
    if (file.is_open()) {
        file << ss.str();
        file.close();
    }
#endif

    brls::Logger::debug("DownloadsManager: Saved state ({} items)", m_downloads.size());
}

void DownloadsManager::loadState() {
    std::string content;

#ifdef __vita__
    SceUID fd = sceIoOpen(STATE_FILE, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        char buffer[4096];
        int read;
        while ((read = sceIoRead(fd, buffer, sizeof(buffer))) > 0) {
            content.append(buffer, read);
        }
        sceIoClose(fd);
    }
#else
    std::ifstream file(STATE_FILE);
    if (file.is_open()) {
        std::stringstream ss;
        ss << file.rdbuf();
        content = ss.str();
        file.close();
    }
#endif

    if (content.empty()) {
        brls::Logger::debug("DownloadsManager: No saved state found");
        return;
    }

    brls::Logger::info("DownloadsManager: Loading saved state...");

    // Helper to extract string value from JSON
    auto extractValue = [](const std::string& json, const std::string& key) -> std::string {
        std::string searchKey = "\"" + key + "\":";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos) return "";

        size_t valueStart = json.find_first_not_of(" \t\n\r", keyPos + searchKey.length());
        if (valueStart == std::string::npos) return "";

        if (json[valueStart] == '"') {
            size_t valueEnd = valueStart + 1;
            while (valueEnd < json.length()) {
                if (json[valueEnd] == '"' && json[valueEnd - 1] != '\\') break;
                valueEnd++;
            }
            return json.substr(valueStart + 1, valueEnd - valueStart - 1);
        } else if (json[valueStart] == 't' || json[valueStart] == 'f') {
            // Boolean
            if (json.substr(valueStart, 4) == "true") return "true";
            if (json.substr(valueStart, 5) == "false") return "false";
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
    };

    // Find downloads array
    size_t arrStart = content.find("[");
    if (arrStart == std::string::npos) {
        brls::Logger::warning("DownloadsManager: Invalid state format");
        return;
    }

    // Parse each download item object
    size_t pos = arrStart;
    while (true) {
        size_t objStart = content.find('{', pos);
        if (objStart == std::string::npos) break;

        // Skip the files array objects - look for itemId to identify download items
        size_t itemIdPos = content.find("\"itemId\"", objStart);
        if (itemIdPos == std::string::npos) break;

        // Check if this itemId is within a reasonable distance (not a nested object)
        if (itemIdPos - objStart > 50) {
            pos = objStart + 1;
            continue;
        }

        // Find the end of this object (matching braces)
        int braceCount = 1;
        size_t objEnd = objStart + 1;
        while (braceCount > 0 && objEnd < content.length()) {
            if (content[objEnd] == '{') braceCount++;
            else if (content[objEnd] == '}') braceCount--;
            objEnd++;
        }

        std::string itemJson = content.substr(objStart, objEnd - objStart);

        DownloadItem item;
        // Helper to unescape JSON strings
        auto unescapeJsonString = [](const std::string& str) -> std::string {
            std::string result;
            result.reserve(str.size());
            for (size_t i = 0; i < str.size(); ++i) {
                if (str[i] == '\\' && i + 1 < str.size()) {
                    switch (str[i + 1]) {
                        case '"': result += '"'; ++i; break;
                        case '\\': result += '\\'; ++i; break;
                        case 'n': result += '\n'; ++i; break;
                        case 'r': result += '\r'; ++i; break;
                        case 't': result += '\t'; ++i; break;
                        default: result += str[i]; break;
                    }
                } else {
                    result += str[i];
                }
            }
            return result;
        };

        item.itemId = extractValue(itemJson, "itemId");
        item.episodeId = extractValue(itemJson, "episodeId");
        item.title = unescapeJsonString(extractValue(itemJson, "title"));
        item.authorName = unescapeJsonString(extractValue(itemJson, "authorName"));
        item.parentTitle = unescapeJsonString(extractValue(itemJson, "parentTitle"));
        item.localPath = extractValue(itemJson, "localPath");
        item.coverUrl = extractValue(itemJson, "coverUrl");
        item.localCoverPath = extractValue(itemJson, "localCoverPath");
        item.description = unescapeJsonString(extractValue(itemJson, "description"));
        item.mediaType = extractValue(itemJson, "mediaType");
        item.seriesName = unescapeJsonString(extractValue(itemJson, "seriesName"));

        std::string totalBytesStr = extractValue(itemJson, "totalBytes");
        item.totalBytes = totalBytesStr.empty() ? 0 : std::stoll(totalBytesStr);

        std::string downloadedBytesStr = extractValue(itemJson, "downloadedBytes");
        item.downloadedBytes = downloadedBytesStr.empty() ? 0 : std::stoll(downloadedBytesStr);

        std::string durationStr = extractValue(itemJson, "duration");
        item.duration = durationStr.empty() ? 0.0f : std::stof(durationStr);

        std::string currentTimeStr = extractValue(itemJson, "currentTime");
        item.currentTime = currentTimeStr.empty() ? 0.0f : std::stof(currentTimeStr);

        std::string viewOffsetStr = extractValue(itemJson, "viewOffset");
        item.viewOffset = viewOffsetStr.empty() ? 0 : std::stoll(viewOffsetStr);

        std::string numChaptersStr = extractValue(itemJson, "numChapters");
        item.numChapters = numChaptersStr.empty() ? 0 : std::stoi(numChaptersStr);

        std::string numFilesStr = extractValue(itemJson, "numFiles");
        item.numFiles = numFilesStr.empty() ? 1 : std::stoi(numFilesStr);

        std::string stateStr = extractValue(itemJson, "state");
        item.state = stateStr.empty() ? DownloadState::QUEUED : static_cast<DownloadState>(std::stoi(stateStr));

        std::string lastSyncedStr = extractValue(itemJson, "lastSynced");
        item.lastSynced = lastSyncedStr.empty() ? 0 : std::stoll(lastSyncedStr);

        // Parse chapters array for offline playback
        size_t chaptersStart = itemJson.find("\"chapters\":[");
        if (chaptersStart != std::string::npos) {
            size_t chaptersArrStart = itemJson.find('[', chaptersStart);
            size_t chaptersArrEnd = itemJson.find(']', chaptersArrStart);
            if (chaptersArrStart != std::string::npos && chaptersArrEnd != std::string::npos) {
                std::string chaptersJson = itemJson.substr(chaptersArrStart, chaptersArrEnd - chaptersArrStart + 1);

                size_t chapterPos = 0;
                while (true) {
                    size_t chObjStart = chaptersJson.find('{', chapterPos);
                    if (chObjStart == std::string::npos) break;

                    size_t chObjEnd = chaptersJson.find('}', chObjStart);
                    if (chObjEnd == std::string::npos) break;

                    std::string chJson = chaptersJson.substr(chObjStart, chObjEnd - chObjStart + 1);

                    DownloadChapter ch;
                    ch.title = unescapeJsonString(extractValue(chJson, "title"));

                    std::string startStr = extractValue(chJson, "start");
                    ch.start = startStr.empty() ? 0.0f : std::stof(startStr);

                    std::string endStr = extractValue(chJson, "end");
                    ch.end = endStr.empty() ? 0.0f : std::stof(endStr);

                    if (!ch.title.empty()) {
                        item.chapters.push_back(ch);
                    }

                    chapterPos = chObjEnd + 1;
                }
            }
        }

        // Parse files array for multi-file downloads
        size_t filesStart = itemJson.find("\"files\":[");
        if (filesStart != std::string::npos) {
            size_t filesArrStart = itemJson.find('[', filesStart);
            size_t filesArrEnd = itemJson.find(']', filesArrStart);
            if (filesArrStart != std::string::npos && filesArrEnd != std::string::npos) {
                std::string filesJson = itemJson.substr(filesArrStart, filesArrEnd - filesArrStart + 1);

                size_t filePos = 0;
                while (true) {
                    size_t fileObjStart = filesJson.find('{', filePos);
                    if (fileObjStart == std::string::npos) break;

                    size_t fileObjEnd = filesJson.find('}', fileObjStart);
                    if (fileObjEnd == std::string::npos) break;

                    std::string fileJson = filesJson.substr(fileObjStart, fileObjEnd - fileObjStart + 1);

                    DownloadFileInfo fi;
                    fi.ino = extractValue(fileJson, "ino");
                    fi.filename = extractValue(fileJson, "filename");
                    fi.localPath = extractValue(fileJson, "localPath");

                    std::string sizeStr = extractValue(fileJson, "size");
                    fi.size = sizeStr.empty() ? 0 : std::stoll(sizeStr);

                    fi.downloaded = extractValue(fileJson, "downloaded") == "true";

                    if (!fi.localPath.empty()) {
                        item.files.push_back(fi);
                    }

                    filePos = fileObjEnd + 1;
                }
            }
        }

        if (!item.itemId.empty()) {
            m_downloads.push_back(item);
            brls::Logger::debug("DownloadsManager: Loaded download: {} (state: {})",
                               item.title, static_cast<int>(item.state));
        }

        pos = objEnd;
    }

    brls::Logger::info("DownloadsManager: Loaded {} downloads from state", m_downloads.size());
}

void DownloadsManager::setProgressCallback(DownloadProgressCallback callback) {
    m_progressCallback = callback;
}

std::string DownloadsManager::getDownloadsPath() const {
    return m_downloadsPath;
}

std::string DownloadsManager::downloadCoverImage(const std::string& itemId, const std::string& coverUrl) {
    if (coverUrl.empty()) {
        brls::Logger::debug("DownloadsManager: No cover URL provided for {}", itemId);
        return "";
    }

    // Ensure initialized so we have the downloads path
    if (m_downloadsPath.empty()) {
        init();
    }

    std::string coverPath = m_downloadsPath + "/" + itemId + "_cover.jpg";

    brls::Logger::info("DownloadsManager: Downloading cover for {} from {}", itemId, coverUrl);

    // Download the cover image
    HttpClient http;
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
    std::string token = client.getAuthToken();
    if (!token.empty()) {
        http.setDefaultHeader("Authorization", "Bearer " + token);
    }

    std::vector<char> imageData;
    bool success = http.downloadFile(coverUrl,
        [&imageData](const char* data, size_t size) {
            imageData.insert(imageData.end(), data, data + size);
            return true;
        },
        [](int64_t total) {}
    );

    if (!success || imageData.empty()) {
        brls::Logger::warning("DownloadsManager: Failed to download cover for {}", itemId);
        return "";
    }

    // Save to file
#ifdef __vita__
    SceUID fd = sceIoOpen(coverPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, imageData.data(), imageData.size());
        sceIoClose(fd);
        brls::Logger::info("DownloadsManager: Saved cover to {}", coverPath);
        return coverPath;
    } else {
        brls::Logger::error("DownloadsManager: Failed to save cover file {}", coverPath);
        return "";
    }
#else
    std::ofstream file(coverPath, std::ios::binary);
    if (file.is_open()) {
        file.write(imageData.data(), imageData.size());
        file.close();
        brls::Logger::info("DownloadsManager: Saved cover to {}", coverPath);
        return coverPath;
    } else {
        brls::Logger::error("DownloadsManager: Failed to save cover file {}", coverPath);
        return "";
    }
#endif
}

std::string DownloadsManager::getLocalCoverPath(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& item : m_downloads) {
        if (item.itemId == itemId && !item.localCoverPath.empty()) {
            return item.localCoverPath;
        }
    }
    return "";
}

bool DownloadsManager::registerCompletedDownload(const std::string& itemId, const std::string& episodeId,
                                                  const std::string& title, const std::string& authorName,
                                                  const std::string& localPath, int64_t fileSize,
                                                  float duration, const std::string& mediaType,
                                                  const std::string& coverUrl,
                                                  const std::string& description,
                                                  const std::vector<DownloadChapter>& chapters) {
    // Ensure manager is initialized
    init();

    brls::Logger::info("DownloadsManager: Registering download: {} at {}", title, localPath);

    // Download cover image (do this outside the lock to avoid blocking)
    std::string localCoverPath;
    if (!coverUrl.empty()) {
        localCoverPath = downloadCoverImage(itemId, coverUrl);
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if already registered
    for (auto& item : m_downloads) {
        if (item.itemId == itemId && item.episodeId == episodeId) {
            // Update existing entry
            item.localPath = localPath;
            item.totalBytes = fileSize;
            item.downloadedBytes = fileSize;
            item.state = DownloadState::COMPLETED;
            // Update metadata if provided
            if (!localCoverPath.empty()) {
                item.localCoverPath = localCoverPath;
                item.coverUrl = coverUrl;
            }
            if (!description.empty()) {
                item.description = description;
            }
            if (!chapters.empty()) {
                item.chapters = chapters;
                item.numChapters = static_cast<int>(chapters.size());
            }
            brls::Logger::info("DownloadsManager: Updated existing download: {}", title);
            saveStateUnlocked();  // Already holding the lock
            return true;
        }
    }

    // Create new download entry
    DownloadItem item;
    item.itemId = itemId;
    item.episodeId = episodeId;
    item.title = title;
    item.authorName = authorName;
    item.localPath = localPath;
    item.totalBytes = fileSize;
    item.downloadedBytes = fileSize;
    item.duration = duration;
    item.state = DownloadState::COMPLETED;
    item.mediaType = mediaType;
    item.numFiles = 1;
    item.coverUrl = coverUrl;
    item.localCoverPath = localCoverPath;
    item.description = description;
    item.chapters = chapters;
    item.numChapters = static_cast<int>(chapters.size());

    // Set parentTitle for proper display in player
    // For podcast episodes: parentTitle = podcast name (authorName parameter)
    // For books: parentTitle = author name
    if (!authorName.empty()) {
        item.parentTitle = authorName;
    }

    m_downloads.push_back(item);
    brls::Logger::info("DownloadsManager: Registered completed download: {} ({} bytes, cover: {})",
                       title, fileSize, !localCoverPath.empty() ? "yes" : "no");

    saveStateUnlocked();  // Already holding the lock
    return true;
}

} // namespace vitasuwayomi
