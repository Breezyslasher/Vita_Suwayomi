/**
 * VitaSuwayomi - Downloads Tab
 * View for managing offline downloads and showing download queue
 */

#pragma once

#include <borealis.hpp>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>

namespace vitasuwayomi {

// Forward declaration of DownloadQueueItem for caching
struct DownloadQueueItem;
struct LocalChapterDownload;

class DownloadsTab : public brls::Box {
public:
    DownloadsTab();
    ~DownloadsTab() override = default;

    void willAppear(bool resetState) override;
    void willDisappear(bool resetState) override;

private:
    void refresh();
    void refreshQueue();
    void refreshLocalDownloads();
    void showDownloadOptions(const std::string& ratingKey, const std::string& title);
    void startAutoRefresh();
    void stopAutoRefresh();

    // Queue section (server downloads)
    brls::Box* m_queueSection = nullptr;
    brls::Label* m_queueHeader = nullptr;
    brls::ScrollingFrame* m_queueScroll = nullptr;
    brls::Box* m_queueContainer = nullptr;
    brls::Label* m_queueEmptyLabel = nullptr;

    // Local downloads section
    brls::Box* m_localSection = nullptr;
    brls::Label* m_localHeader = nullptr;
    brls::ScrollingFrame* m_localScroll = nullptr;
    brls::Box* m_localContainer = nullptr;
    brls::Label* m_localEmptyLabel = nullptr;

    // Empty state (shown when no downloads in either queue)
    brls::Box* m_emptyStateBox = nullptr;

    // Track currently focused X button icon (like book detail view)
    brls::Image* m_currentFocusedIcon = nullptr;

    // Top action icons
    brls::Box* m_actionsRow = nullptr;
    brls::Image* m_pauseIcon = nullptr;
    brls::Image* m_clearIcon = nullptr;

    // Start/Stop button
    brls::Button* m_startStopBtn = nullptr;
    brls::Label* m_startStopLabel = nullptr;

    // Pause and Clear buttons (for d-pad navigation)
    brls::Button* m_pauseBtn = nullptr;
    brls::Button* m_clearBtn = nullptr;

    // Download status display
    brls::Label* m_downloadStatusLabel = nullptr;
    bool m_downloaderRunning = false;

    // Auto-refresh state (atomic for thread safety)
    std::atomic<bool> m_autoRefreshEnabled{false};
    std::atomic<bool> m_autoRefreshTimerActive{false};

    // Throttle progress updates to avoid excessive UI refreshes
    std::chrono::steady_clock::time_point m_lastProgressRefresh;
    static constexpr int PROGRESS_REFRESH_INTERVAL_MS = 500;  // Only refresh UI every 500ms

    // Cached queue state for smart refresh (only update when data changes)
    struct CachedQueueItem {
        int chapterId = 0;
        int mangaId = 0;
        int downloadedPages = 0;
        int pageCount = 0;
        int state = 0;  // DownloadState as int
    };
    std::vector<CachedQueueItem> m_lastServerQueue;

    struct CachedLocalItem {
        int mangaId = 0;
        int chapterIndex = 0;
        int downloadedPages = 0;
        int pageCount = 0;
        int state = 0;  // LocalDownloadState as int
    };
    std::vector<CachedLocalItem> m_lastLocalQueue;

    // UI element tracking for incremental updates (avoid full rebuilds)
    struct LocalRowElements {
        brls::Box* row = nullptr;
        brls::Label* progressLabel = nullptr;
        brls::Image* xButtonIcon = nullptr;
        int mangaId = 0;
        int chapterIndex = 0;
    };
    std::vector<LocalRowElements> m_localRowElements;

    struct ServerRowElements {
        brls::Box* row = nullptr;
        brls::Label* progressLabel = nullptr;
        brls::Image* xButtonIcon = nullptr;
        int chapterId = 0;
        int mangaId = 0;
    };
    std::vector<ServerRowElements> m_serverRowElements;

    // Helper methods for incremental updates (local queue)
    void updateLocalProgress(int mangaId, int chapterIndex, int downloadedPages, int pageCount, int state);
    void removeLocalItem(int mangaId, int chapterIndex);
    void updateNavigationRoutes();  // Update d-pad navigation between buttons and queue items
    void addLocalItem(int mangaId, int chapterIndex, const std::string& mangaTitle,
                      const std::string& chapterName, float chapterNumber,
                      int downloadedPages, int pageCount, int state);
    brls::Box* createLocalRow(int mangaId, int chapterIndex, const std::string& mangaTitle,
                              const std::string& chapterName, float chapterNumber,
                              int downloadedPages, int pageCount, int state,
                              brls::Label*& outProgressLabel, brls::Image*& outXButtonIcon);

    // Helper methods for incremental updates (server queue)
    brls::Box* createServerRow(int chapterId, int mangaId, const std::string& mangaTitle,
                               const std::string& chapterName, float chapterNumber,
                               int downloadedPages, int pageCount, int state,
                               int currentIndex, int queueSize,
                               brls::Label*& outProgressLabel, brls::Image*& outXButtonIcon);
    void addServerItem(int chapterId, int mangaId, const std::string& mangaTitle,
                       const std::string& chapterName, float chapterNumber,
                       int downloadedPages, int pageCount, int state,
                       int currentIndex, int queueSize);
    void removeServerItem(int chapterId);

    // Swipe gesture state (avoid static variables)
    struct SwipeState {
        brls::Point touchStart;
        bool isValidSwipe = false;
    };

    // Focus tracking for UI rebuilds
    int m_focusedServerIndex = -1;  // Index of focused item in server queue
    int m_focusedLocalIndex = -1;   // Index of focused item in local queue
    bool m_hadFocusOnServerQueue = false;
    bool m_hadFocusOnLocalQueue = false;
};

} // namespace vitasuwayomi
