/**
 * VitaSuwayomi - Audiobookshelf API Client
 * Handles all communication with Audiobookshelf servers
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace vitasuwayomi {

// Media types for Audiobookshelf
enum class MediaType {
    UNKNOWN,
    BOOK,           // Audiobook
    PODCAST,        // Podcast
    PODCAST_EPISODE // Podcast episode
};

// Audio track info (for multi-file audiobooks)
struct AudioTrack {
    int index = 0;
    std::string title;
    std::string contentUrl;
    float startOffset = 0.0f;  // Start offset in seconds
    float duration = 0.0f;     // Duration in seconds
    std::string mimeType;
};

// Audio file info for downloads
struct AudioFileInfo {
    std::string ino;           // File inode for download URL
    std::string filename;      // Original filename
    float duration = 0.0f;     // Duration in seconds
    int64_t size = 0;          // File size in bytes
    std::string mimeType;
};

// Chapter info
struct Chapter {
    int id = 0;
    std::string title;
    float start = 0.0f;   // Start time in seconds
    float end = 0.0f;     // End time in seconds
};

// Author info
struct Author {
    std::string id;
    std::string name;
    std::string description;
    std::string imagePath;
};

// Series info
struct Series {
    std::string id;
    std::string name;
    std::string sequence;  // Book number in series
};

// Genre/Category item (for browsing by genre)
struct GenreItem {
    std::string id;
    std::string name;
    std::string title;  // Display title (same as name)
    int itemCount = 0;
};

// Media item info (audiobook or podcast)
struct MediaItem {
    std::string id;                // Library item ID
    std::string libraryId;         // Parent library ID
    std::string title;
    std::string subtitle;          // For podcasts, episode title
    std::string description;       // Book description/summary
    std::string coverPath;         // Cover image path
    std::string type;              // "book" or "podcast"
    MediaType mediaType = MediaType::UNKNOWN;

    // Book metadata
    std::string authorName;
    std::string narratorName;
    std::string publishedYear;
    std::string publisher;
    std::string isbn;
    std::string asin;
    std::string language;
    std::vector<std::string> genres;
    std::vector<std::string> tags;

    // Series info
    std::string seriesName;
    std::string seriesSequence;

    // Duration and progress
    float duration = 0.0f;         // Total duration in seconds
    float currentTime = 0.0f;      // Current playback position in seconds
    float progress = 0.0f;         // Progress percentage (0.0 - 1.0)
    bool isFinished = false;
    int64_t progressLastUpdate = 0; // Timestamp of last progress update

    // Audio info
    std::vector<AudioTrack> audioTracks;
    std::vector<Chapter> chapters;
    int numTracks = 0;
    int numChapters = 0;

    // File info
    int64_t size = 0;              // Total file size in bytes
    std::string ebookFileFormat;   // For ebooks (epub, pdf, etc.)

    // For podcast episodes
    std::string episodeId;
    std::string podcastId;
    int episodeNumber = 0;
    int seasonNumber = 0;
    std::string pubDate;

    // For RSS episode downloads (enclosure info)
    std::string enclosureType;     // Audio MIME type (e.g., "audio/mpeg")
    std::string enclosureLength;   // File size from RSS
    std::string originalJson;      // Original JSON for download request

    // Local state (not from server)
    bool isDownloaded = false;     // Item is downloaded locally
};

// Library section info
struct Library {
    std::string id;
    std::string name;
    std::string icon;              // Library icon
    std::string mediaType;         // "book" or "podcast"
    int itemCount = 0;
    std::vector<std::string> folders;
};

// Collection info
struct Collection {
    std::string id;
    std::string libraryId;
    std::string name;
    std::string description;
    std::string coverPath;
    int bookCount = 0;
    std::vector<std::string> bookIds;
};

// Server info
struct ServerInfo {
    std::string version;
    bool isInit = false;
    std::string authMethods;       // Comma-separated auth methods
    std::string serverName;
};

// User info
struct User {
    std::string id;
    std::string username;
    std::string token;
    std::string type;              // "admin", "user", "guest"
    bool isActive = true;
    std::vector<std::string> librariesAccessible;  // Library IDs user can access
};

// Playback session info
struct PlaybackSession {
    std::string id;
    std::string libraryItemId;
    std::string episodeId;         // For podcasts
    std::string mediaType;
    float currentTime = 0.0f;
    float duration = 0.0f;
    float startTime = 0.0f;        // Where playback started
    std::string playMethod;        // "directplay" or "transcode"
    std::string deviceInfo;
    int64_t updatedAt = 0;
    std::vector<AudioTrack> audioTracks;  // Audio tracks with streaming URLs
};

// Personalized shelf (for home screen)
struct PersonalizedShelf {
    std::string id;
    std::string label;             // Display title
    std::string labelStringKey;    // i18n key
    std::string type;              // "book", "series", "authors", "podcast", "episode"
    std::vector<MediaItem> entities;
};

// iTunes podcast search result
struct PodcastSearchResult {
    std::string title;
    std::string author;
    std::string feedUrl;           // RSS feed URL
    std::string artworkUrl;        // Cover image URL
    std::string description;
    std::string genre;
    int trackCount = 0;            // Number of episodes
};

/**
 * Audiobookshelf API Client singleton
 */
class AudiobookshelfClient {
public:
    static AudiobookshelfClient& getInstance();

    // Authentication
    bool login(const std::string& username, const std::string& password);
    bool validateToken();          // Validate current token
    void logout();

    // Server info
    bool fetchServerInfo(ServerInfo& info);
    bool connectToServer(const std::string& url);

    // Current user
    bool fetchCurrentUser(User& user);
    bool fetchItemsInProgress(std::vector<MediaItem>& items);
    bool fetchListeningSessions(std::vector<PlaybackSession>& sessions);

    // Libraries
    bool fetchLibraries(std::vector<Library>& libraries);
    bool fetchLibrary(const std::string& libraryId, Library& library);
    bool fetchLibraryItems(const std::string& libraryId, std::vector<MediaItem>& items,
                           int page = 0, int limit = 50, const std::string& sort = "");
    bool fetchLibraryPersonalized(const std::string& libraryId, std::vector<PersonalizedShelf>& shelves);
    bool fetchLibrarySeries(const std::string& libraryId, std::vector<Series>& series);
    bool fetchLibraryCollections(const std::string& libraryId, std::vector<Collection>& collections);
    bool fetchLibraryAuthors(const std::string& libraryId, std::vector<Author>& authors);
    bool fetchRecentlyAdded(const std::string& libraryId, std::vector<MediaItem>& items);

    // Items
    bool fetchItem(const std::string& itemId, MediaItem& item);
    bool fetchItemWithProgress(const std::string& itemId, MediaItem& item);

    // Search
    bool search(const std::string& libraryId, const std::string& query, std::vector<MediaItem>& results);
    bool searchAll(const std::string& query, std::vector<MediaItem>& results);

    // Playback
    bool startPlaybackSession(const std::string& itemId, PlaybackSession& session,
                              const std::string& episodeId = "");
    bool syncPlaybackSession(const std::string& sessionId, float currentTime, float duration);
    bool closePlaybackSession(const std::string& sessionId, float currentTime,
                              float duration, float timeListened);
    std::string getStreamUrl(const std::string& itemId, const std::string& episodeId = "");
    std::string getDirectStreamUrl(const std::string& itemId, int fileIndex = 0);

    // File download (for local downloads - uses /api/items/{id}/file/{ino})
    std::string getFileDownloadUrl(const std::string& itemId, const std::string& episodeId = "");

    // Get all audio files for multi-file audiobooks
    bool getAudioFiles(const std::string& itemId, std::vector<AudioFileInfo>& files);
    std::string getFileDownloadUrlByIno(const std::string& itemId, const std::string& ino);

    // Progress
    bool updateProgress(const std::string& itemId, float currentTime, float duration,
                        bool isFinished = false, const std::string& episodeId = "");
    bool getProgress(const std::string& itemId, float& currentTime, float& progress,
                     bool& isFinished, const std::string& episodeId = "");
    bool removeItemFromContinueListening(const std::string& itemId);

    // Bookmarks
    bool createBookmark(const std::string& itemId, float time, const std::string& title);
    bool deleteBookmark(const std::string& itemId, float time);

    // Cover images
    std::string getCoverUrl(const std::string& itemId, int width = 400, int height = 400);
    std::string getAuthorImageUrl(const std::string& authorId, int width = 200, int height = 200);

    // Collections
    bool fetchCollection(const std::string& collectionId, Collection& collection);
    bool fetchCollectionBooks(const std::string& collectionId, std::vector<MediaItem>& books);

    // Series
    bool fetchSeriesBooks(const std::string& seriesId, std::vector<MediaItem>& books);

    // Authors
    bool fetchAuthor(const std::string& authorId, Author& author);
    bool fetchAuthorBooks(const std::string& authorId, std::vector<MediaItem>& books);

    // Podcasts
    bool fetchPodcastEpisodes(const std::string& podcastId, std::vector<MediaItem>& episodes);

    // Podcast Management (iTunes search and RSS)
    bool searchPodcasts(const std::string& query, std::vector<PodcastSearchResult>& results);
    bool addPodcastToLibrary(const std::string& libraryId, const PodcastSearchResult& podcast,
                             const std::string& folderId = "");
    bool checkNewEpisodes(const std::string& podcastId, std::vector<MediaItem>& newEpisodes);
    bool downloadEpisodesToServer(const std::string& podcastId, const std::vector<std::string>& episodeIds);
    bool downloadNewEpisodesToServer(const std::string& podcastId, const std::vector<MediaItem>& episodes);
    bool downloadAllNewEpisodes(const std::string& podcastId);

    // Stub methods for unsupported features (Audiobookshelf doesn't have these)
    bool fetchPlaylists(std::vector<MediaItem>& playlists) { playlists.clear(); return false; }
    bool fetchEPGGrid(std::vector<MediaItem>& channels, int hours) { channels.clear(); return false; }
    bool fetchByGenre(const std::string& libraryId, const std::string& genre, std::vector<MediaItem>& items) { items.clear(); return false; }
    bool fetchByGenreKey(const std::string& libraryId, const std::string& genreKey, std::vector<MediaItem>& items) { items.clear(); return false; }

    // Configuration
    void setAuthToken(const std::string& token) { m_authToken = token; }
    const std::string& getAuthToken() const { return m_authToken; }
    void setServerUrl(const std::string& url) { m_serverUrl = url; }
    const std::string& getServerUrl() const { return m_serverUrl; }
    const User& getCurrentUser() const { return m_currentUser; }

    // Check if client is authenticated (has valid token and server URL)
    bool isAuthenticated() const { return !m_authToken.empty() && !m_serverUrl.empty(); }

private:
    AudiobookshelfClient() = default;
    ~AudiobookshelfClient() = default;

    std::string buildApiUrl(const std::string& endpoint);
    MediaType parseMediaType(const std::string& typeStr);

    // JSON parsing helpers
    std::string extractJsonValue(const std::string& json, const std::string& key);
    int extractJsonInt(const std::string& json, const std::string& key);
    float extractJsonFloat(const std::string& json, const std::string& key);
    bool extractJsonBool(const std::string& json, const std::string& key);
    int64_t extractJsonInt64(const std::string& json, const std::string& key);
    std::string extractJsonArray(const std::string& json, const std::string& key);
    std::string extractJsonObject(const std::string& json, const std::string& key);

    // Parse complex objects
    MediaItem parseMediaItem(const std::string& json);
    Chapter parseChapter(const std::string& json);
    AudioTrack parseAudioTrack(const std::string& json);

    std::string m_authToken;
    std::string m_serverUrl;
    User m_currentUser;
    ServerInfo m_serverInfo;
};

} // namespace vitasuwayomi
