/**
 * VitaSuwayomi - Downloads Tab Implementation
 * Shows download queue and downloaded manga for offline reading
 */

#include "view/downloads_tab.hpp"
#include "app/downloads_manager.hpp"
#include "app/suwayomi_client.hpp"
#include "app/application.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"
#include <fstream>
#include <memory>
#include <thread>
#include <chrono>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

// Auto-refresh interval in milliseconds (3 seconds for small queues, 5 seconds for large)
static const int AUTO_REFRESH_INTERVAL_MS = 3000;
static const int AUTO_REFRESH_INTERVAL_LARGE_MS = 5000;
static const int LARGE_QUEUE_THRESHOLD = 50;

namespace vitasuwayomi {

// Helper to load local cover image on Vita
static void loadLocalCoverImage(brls::Image* image, const std::string& localPath) {
    if (localPath.empty() || !image) return;

#ifdef __vita__
    SceUID fd = sceIoOpen(localPath.c_str(), SCE_O_RDONLY, 0);
    if (fd >= 0) {
        SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoLseek(fd, 0, SCE_SEEK_SET);

        if (size > 0 && size < 10 * 1024 * 1024) {  // Max 10MB
            std::vector<uint8_t> data(size);
            if (sceIoRead(fd, data.data(), size) == size) {
                image->setImageFromMem(data.data(), data.size());
            }
        }
        sceIoClose(fd);
    }
#else
    std::ifstream file(localPath, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (size > 0 && size < 10 * 1024 * 1024) {
            std::vector<uint8_t> data(size);
            if (file.read(reinterpret_cast<char*>(data.data()), size)) {
                image->setImageFromMem(data.data(), data.size());
            }
        }
        file.close();
    }
#endif
}

DownloadsTab::DownloadsTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setPadding(20);
    this->setGrow(1.0f);

    // Header row with title and action icons
    auto headerRow = new brls::Box();
    headerRow->setAxis(brls::Axis::ROW);
    headerRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    headerRow->setAlignItems(brls::AlignItems::CENTER);
    headerRow->setMargins(0, 0, 15, 0);
    this->addView(headerRow);

    // Left side: title and status
    auto titleBox = new brls::Box();
    titleBox->setAxis(brls::Axis::ROW);
    titleBox->setAlignItems(brls::AlignItems::CENTER);
    titleBox->setShrink(0);  // Prevent title from shrinking
    titleBox->setGrow(1.0f);  // Allow title to expand and prevent truncation
    headerRow->addView(titleBox);

    auto header = new brls::Label();
    header->setText("Downloads");
    header->setFontSize(24);
    header->setSingleLine(true);  // Prevent text wrapping
    titleBox->addView(header);

    // Download status label (shows "Downloading" / "Stopped")
    m_downloadStatusLabel = new brls::Label();
    m_downloadStatusLabel->setText("");
    m_downloadStatusLabel->setFontSize(14);
    m_downloadStatusLabel->setMarginLeft(15);
    m_downloadStatusLabel->setTextColor(nvgRGBA(150, 150, 150, 255));
    titleBox->addView(m_downloadStatusLabel);

    // Actions row with Start/Stop, Pause and Clear buttons
    m_actionsRow = new brls::Box();
    m_actionsRow->setAxis(brls::Axis::ROW);
    m_actionsRow->setAlignItems(brls::AlignItems::CENTER);
    headerRow->addView(m_actionsRow);

    // Start/Stop toggle button
    m_startStopBtn = new brls::Button();
    m_startStopBtn->setWidth(70);
    m_startStopBtn->setHeight(32);
    m_startStopBtn->setCornerRadius(6);
    m_startStopBtn->setMarginRight(8);
    m_startStopBtn->setPaddingLeft(8);
    m_startStopBtn->setPaddingRight(8);
    m_startStopBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_startStopBtn->setAlignItems(brls::AlignItems::CENTER);

    m_startStopLabel = new brls::Label();
    m_startStopLabel->setText("Start");
    m_startStopLabel->setFontSize(12);
    m_startStopBtn->addView(m_startStopLabel);

    // Start/Stop button action - toggle downloader state for both server and local
    // brls::Button handles both touch and controller input via registerClickAction
    m_startStopBtn->registerClickAction([this](brls::View*) {
        asyncRun([this]() {
            SuwayomiClient& client = SuwayomiClient::getInstance();
            DownloadsManager& mgr = DownloadsManager::getInstance();
            bool success = false;
            bool wasRunning = m_downloaderRunning;
            if (m_downloaderRunning) {
                // Stop both server and local downloads
                success = client.stopDownloads();
                mgr.pauseDownloads();
            } else {
                // Start both server and local downloads
                success = client.startDownloads();
                mgr.startDownloads();
            }
            brls::sync([this, success, wasRunning]() {
                if (success) {
                    brls::Application::notify(wasRunning ? "Downloads paused" : "Downloads started");
                }
                refreshQueue();
                refreshLocalDownloads();
            });
        });
        return true;
    });
    m_actionsRow->addView(m_startStopBtn);

    // Stop/Pause button (stop current downloads)
    m_pauseBtn = new brls::Button();
    m_pauseBtn->setWidth(60);
    m_pauseBtn->setHeight(32);
    m_pauseBtn->setCornerRadius(6);
    m_pauseBtn->setMarginRight(8);
    m_pauseBtn->setPaddingLeft(8);
    m_pauseBtn->setPaddingRight(8);
    m_pauseBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_pauseBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* pauseLabel = new brls::Label();
    pauseLabel->setText("Stop");
    pauseLabel->setFontSize(12);
    m_pauseBtn->addView(pauseLabel);

    // Pause button action - stop both server and local downloads
    // brls::Button handles both touch and controller input via registerClickAction
    m_pauseBtn->registerClickAction([this](brls::View*) {
        asyncRun([this]() {
            SuwayomiClient& client = SuwayomiClient::getInstance();
            DownloadsManager& mgr = DownloadsManager::getInstance();

            // Stop both server and local downloads
            bool success = client.stopDownloads();
            mgr.pauseDownloads();

            brls::sync([this, success]() {
                m_downloaderRunning = false;
                if (m_startStopLabel) {
                    m_startStopLabel->setText("Start");
                }
                if (m_downloadStatusLabel) {
                    m_downloadStatusLabel->setText("• Stopped");
                    m_downloadStatusLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
                }
                if (success) {
                    brls::Application::notify("Downloads stopped");
                }
                refreshQueue();
                refreshLocalDownloads();
            });
        });
        return true;
    });
    m_actionsRow->addView(m_pauseBtn);

    // Clear button
    m_clearBtn = new brls::Button();
    m_clearBtn->setWidth(70);
    m_clearBtn->setHeight(32);
    m_clearBtn->setCornerRadius(6);
    m_clearBtn->setPaddingLeft(8);
    m_clearBtn->setPaddingRight(8);
    m_clearBtn->setJustifyContent(brls::JustifyContent::CENTER);
    m_clearBtn->setAlignItems(brls::AlignItems::CENTER);

    m_clearIcon = new brls::Image();
    m_clearIcon->setWidth(16);
    m_clearIcon->setHeight(16);
    m_clearIcon->setScalingType(brls::ImageScalingType::FIT);
    m_clearIcon->setImageFromFile("app0:resources/icons/delete.png");
    m_clearIcon->setMarginRight(4);
    m_clearBtn->addView(m_clearIcon);

    auto* clearLabel = new brls::Label();
    clearLabel->setText("Clear");
    clearLabel->setFontSize(12);
    m_clearBtn->addView(clearLabel);

    // Clear button action - clear both server and local download queues
    // brls::Button handles both touch and controller input via registerClickAction
    m_clearBtn->registerClickAction([this](brls::View*) {
        asyncRun([this]() {
            SuwayomiClient& client = SuwayomiClient::getInstance();
            DownloadsManager& mgr = DownloadsManager::getInstance();

            // Clear server queue
            bool success = client.clearDownloadQueue();

            // Clear local queue - cancel all queued chapters
            auto queuedChapters = mgr.getQueuedChapters();
            for (const auto& chapter : queuedChapters) {
                if (chapter.state == LocalDownloadState::QUEUED ||
                    chapter.state == LocalDownloadState::DOWNLOADING) {
                    mgr.cancelChapterDownload(chapter.mangaId, chapter.chapterIndex);
                }
            }

            brls::sync([this, success]() {
                m_downloaderRunning = false;
                if (m_startStopLabel) {
                    m_startStopLabel->setText("Start");
                }
                if (m_downloadStatusLabel) {
                    m_downloadStatusLabel->setText("");
                }
                if (success) {
                    brls::Application::notify("Queues cleared");
                }
                refresh();
            });
        });
        return true;
    });
    m_actionsRow->addView(m_clearBtn);

    // === Download Queue Section (server downloads) ===
    m_queueSection = new brls::Box();
    m_queueSection->setAxis(brls::Axis::COLUMN);
    m_queueSection->setGrow(1.0f);
    m_queueSection->setMargins(0, 0, 15, 0);
    m_queueSection->setVisibility(brls::Visibility::GONE);  // Start hidden - only show when downloads exist
    this->addView(m_queueSection);

    m_queueHeader = new brls::Label();
    m_queueHeader->setText("Server Download Queue");
    m_queueHeader->setFontSize(18);
    m_queueHeader->setMargins(0, 0, 10, 0);
    m_queueSection->addView(m_queueHeader);

    // Empty state label for server downloads
    m_queueEmptyLabel = new brls::Label();
    m_queueEmptyLabel->setText("No server downloads");
    m_queueEmptyLabel->setFontSize(14);
    m_queueEmptyLabel->setTextColor(nvgRGBA(120, 120, 120, 255));
    m_queueEmptyLabel->setMargins(10, 0, 10, 0);
    m_queueSection->addView(m_queueEmptyLabel);

    // Scrollable queue container
    m_queueScroll = new brls::ScrollingFrame();
    m_queueScroll->setGrow(1.0f);  // Allow proper scrolling
    m_queueScroll->setVisibility(brls::Visibility::GONE);  // Hidden when empty
    m_queueSection->addView(m_queueScroll);

    m_queueContainer = new brls::Box();
    m_queueContainer->setAxis(brls::Axis::COLUMN);
    m_queueScroll->setContentView(m_queueContainer);

    // === Local Downloads Section ===
    m_localSection = new brls::Box();
    m_localSection->setAxis(brls::Axis::COLUMN);
    m_localSection->setGrow(1.0f);
    m_localSection->setVisibility(brls::Visibility::GONE);  // Start hidden - only show when downloads exist
    this->addView(m_localSection);

    m_localHeader = new brls::Label();
    m_localHeader->setText("Local Download Queue");
    m_localHeader->setFontSize(18);
    m_localHeader->setMargins(0, 0, 10, 0);
    m_localSection->addView(m_localHeader);

    // Empty state label for local downloads
    m_localEmptyLabel = new brls::Label();
    m_localEmptyLabel->setText("No local downloads");
    m_localEmptyLabel->setFontSize(14);
    m_localEmptyLabel->setTextColor(nvgRGBA(120, 120, 120, 255));
    m_localEmptyLabel->setMargins(10, 0, 10, 0);
    m_localSection->addView(m_localEmptyLabel);

    // Scrollable local container
    m_localScroll = new brls::ScrollingFrame();
    m_localScroll->setGrow(1.0f);
    m_localScroll->setVisibility(brls::Visibility::GONE);  // Hidden when empty
    m_localSection->addView(m_localScroll);

    m_localContainer = new brls::Box();
    m_localContainer->setAxis(brls::Axis::COLUMN);
    m_localScroll->setContentView(m_localContainer);

    // Empty state (shown when no downloads in either queue)
    m_emptyStateBox = new brls::Box();
    m_emptyStateBox->setAxis(brls::Axis::COLUMN);
    m_emptyStateBox->setJustifyContent(brls::JustifyContent::CENTER);
    m_emptyStateBox->setAlignItems(brls::AlignItems::CENTER);
    m_emptyStateBox->setGrow(1.0f);
    m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);  // Start visible until data loads

    auto* emptyIcon = new brls::Label();
    emptyIcon->setText("No Downloads");
    emptyIcon->setFontSize(24);
    emptyIcon->setTextColor(nvgRGB(128, 128, 128));
    emptyIcon->setMarginBottom(10);
    m_emptyStateBox->addView(emptyIcon);

    auto* emptyHint = new brls::Label();
    emptyHint->setText("Queue chapters for download from manga details");
    emptyHint->setFontSize(16);
    emptyHint->setTextColor(nvgRGB(100, 100, 100));
    m_emptyStateBox->addView(emptyHint);

    this->addView(m_emptyStateBox);
}

void DownloadsTab::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);

    // Clear tracking vectors for fresh start
    m_currentFocusedIcon = nullptr;
    m_localRowElements.clear();
    m_serverRowElements.clear();
    m_lastLocalQueue.clear();
    m_lastServerQueue.clear();

    // Initialize last progress refresh time
    m_lastProgressRefresh = std::chrono::steady_clock::now();

    // Register progress callback for real-time UI updates during local downloads
    // Now uses incremental updates instead of full refresh
    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.setProgressCallback([this](int downloadedPages, int totalPages) {
        // For local downloads, we can update more frequently since we're just updating labels
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastProgressRefresh).count();

        // Update every 100ms for smooth progress, or immediately for completion
        const int FAST_PROGRESS_INTERVAL_MS = 100;
        if (elapsed >= FAST_PROGRESS_INTERVAL_MS || downloadedPages == totalPages) {
            m_lastProgressRefresh = now;
            if (m_autoRefreshEnabled.load()) {
                brls::sync([this]() {
                    if (m_autoRefreshEnabled.load()) {
                        // Use incremental refresh - much faster than full rebuild
                        refreshLocalDownloads();
                    }
                });
            }
        }
    });

    // Register chapter completion callback - remove completed chapter from UI directly
    mgr.setChapterCompletionCallback([this](int mangaId, int chapterIndex, bool success) {
        if (!m_autoRefreshEnabled.load()) {
            return;
        }

        brls::sync([this, mangaId, chapterIndex]() {
            if (!m_autoRefreshEnabled.load() || !m_localContainer) {
                return;
            }
            // Use the new efficient removal method
            removeLocalItem(mangaId, chapterIndex);
        });
    });

    refresh();
    startAutoRefresh();
}

void DownloadsTab::willDisappear(bool resetState) {
    brls::Box::willDisappear(resetState);

    // IMPORTANT: Stop auto-refresh FIRST to signal all callbacks to stop
    // This must happen before clearing callbacks to prevent race conditions
    stopAutoRefresh();

    // Small delay to allow any pending brls::sync calls to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Clear callbacks to avoid updates when tab is not visible
    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.setProgressCallback(nullptr);
    mgr.setChapterCompletionCallback(nullptr);

    // Clear tracking vectors to free memory
    m_localRowElements.clear();
    m_serverRowElements.clear();
}

void DownloadsTab::refresh() {
    refreshQueue();
    refreshLocalDownloads();
}

void DownloadsTab::refreshQueue() {
    // Fetch download queue and status from server
    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<DownloadQueueItem> queue;

        if (!client.fetchDownloadQueue(queue)) {
            brls::sync([this]() {
                // Hide entire section on fetch failure or empty
                m_queueSection->setVisibility(brls::Visibility::GONE);
                m_lastServerQueue.clear();
                m_serverRowElements.clear();
                while (m_queueContainer->getChildren().size() > 0) {
                    m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
                }

                if (m_lastLocalQueue.empty() && m_downloadStatusLabel) {
                    m_downloadStatusLabel->setText("");
                }

                // Show empty state if local queue is also empty
                if (m_lastLocalQueue.empty() && m_emptyStateBox) {
                    m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
                }

                if (m_startStopBtn && brls::Application::getCurrentFocus() == nullptr) {
                    brls::Application::giveFocus(m_startStopBtn);
                }
            });
            return;
        }

        // Build new cache
        std::vector<CachedQueueItem> newCache;
        newCache.reserve(queue.size());
        for (const auto& item : queue) {
            CachedQueueItem cached;
            cached.chapterId = item.chapterId;
            cached.mangaId = item.mangaId;
            cached.downloadedPages = item.downloadedPages;
            cached.pageCount = item.pageCount;
            cached.state = static_cast<int>(item.state);
            newCache.push_back(cached);
        }

        // Determine downloader state
        bool isDownloading = false;
        for (const auto& item : queue) {
            if (item.state == DownloadState::DOWNLOADING) {
                isDownloading = true;
                break;
            }
        }

        brls::sync([this, queue, isDownloading, newCache]() {
            m_downloaderRunning = isDownloading;
            if (m_startStopLabel && m_lastLocalQueue.empty()) {
                m_startStopLabel->setText(m_downloaderRunning ? "Pause" : "Start");
            }

            // Update status label
            if (m_downloadStatusLabel) {
                if (queue.empty()) {
                    if (m_lastLocalQueue.empty()) {
                        m_downloadStatusLabel->setText("");
                    }
                } else if (m_downloaderRunning) {
                    m_downloadStatusLabel->setText("• Downloading");
                    m_downloadStatusLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
                } else {
                    m_downloadStatusLabel->setText("• Stopped");
                    m_downloadStatusLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
                }
            }

            // Handle empty queue
            if (queue.empty()) {
                m_queueSection->setVisibility(brls::Visibility::GONE);
                m_lastServerQueue.clear();
                m_serverRowElements.clear();
                while (m_queueContainer->getChildren().size() > 0) {
                    m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
                }
                // Show empty state if local queue is also empty
                if (m_lastLocalQueue.empty() && m_emptyStateBox) {
                    m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
                }
                // Update navigation routes (may now point to local queue)
                updateNavigationRoutes();
                if (m_startStopBtn && brls::Application::getCurrentFocus() == nullptr) {
                    brls::Application::giveFocus(m_startStopBtn);
                }
                return;
            }

            // Hide empty state when we have items
            if (m_emptyStateBox) {
                m_emptyStateBox->setVisibility(brls::Visibility::GONE);
            }

            // Show section
            m_queueSection->setVisibility(brls::Visibility::VISIBLE);
            m_queueEmptyLabel->setVisibility(brls::Visibility::GONE);
            m_queueScroll->setVisibility(brls::Visibility::VISIBLE);

            // INCREMENTAL UPDATE for server queue
            // 1. Update existing items in place
            for (size_t i = 0; i < newCache.size() && i < m_lastServerQueue.size(); i++) {
                const auto& newItem = newCache[i];
                const auto& oldItem = m_lastServerQueue[i];

                // If same item, just update progress
                if (newItem.chapterId == oldItem.chapterId && newItem.mangaId == oldItem.mangaId) {
                    if (newItem.downloadedPages != oldItem.downloadedPages ||
                        newItem.state != oldItem.state) {
                        // Update progress label in place
                        if (i < m_serverRowElements.size() && m_serverRowElements[i].progressLabel) {
                            auto* progressLabel = m_serverRowElements[i].progressLabel;
                            std::string progressText;
                            if (newItem.state == static_cast<int>(DownloadState::DOWNLOADING)) {
                                progressText = std::to_string(newItem.downloadedPages) + "/" +
                                               std::to_string(newItem.pageCount) + " pages";
                                progressLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
                            } else if (newItem.state == static_cast<int>(DownloadState::QUEUED)) {
                                progressText = "Queued";
                                progressLabel->setTextColor(nvgRGBA(255, 255, 255, 255));
                            } else if (newItem.state == static_cast<int>(DownloadState::DOWNLOADED)) {
                                progressText = "Done";
                            } else if (newItem.state == static_cast<int>(DownloadState::ERROR)) {
                                progressText = "Error";
                                progressLabel->setTextColor(nvgRGBA(200, 100, 100, 255));
                            } else {
                                progressText = std::to_string(newItem.downloadedPages) + "/" +
                                               std::to_string(newItem.pageCount);
                            }
                            progressLabel->setText(progressText);

                            // Update background color
                            if (m_serverRowElements[i].row) {
                                if (newItem.state == static_cast<int>(DownloadState::DOWNLOADING)) {
                                    m_serverRowElements[i].row->setBackgroundColor(nvgRGBA(30, 60, 30, 200));
                                } else if (newItem.state == static_cast<int>(DownloadState::ERROR)) {
                                    m_serverRowElements[i].row->setBackgroundColor(nvgRGBA(60, 30, 30, 200));
                                } else {
                                    m_serverRowElements[i].row->setBackgroundColor(nvgRGBA(40, 40, 40, 200));
                                }
                            }
                        }
                    }
                }
            }

            // Determine structural changes: items to remove and add
            std::vector<int> toRemoveIds;
            for (const auto& elem : m_serverRowElements) {
                bool found = false;
                for (const auto& newItem : newCache) {
                    if (elem.chapterId == newItem.chapterId) { found = true; break; }
                }
                if (!found) toRemoveIds.push_back(elem.chapterId);
            }

            std::vector<size_t> toAddIndices;
            for (size_t i = 0; i < queue.size(); i++) {
                bool found = false;
                for (const auto& elem : m_serverRowElements) {
                    if (elem.chapterId == queue[i].chapterId) { found = true; break; }
                }
                if (!found) toAddIndices.push_back(i);
            }

            bool setChanged = !toRemoveIds.empty() || !toAddIndices.empty();

            // Check if order changed (only when the set of items is the same)
            bool orderChanged = false;
            if (!setChanged && newCache.size() == m_serverRowElements.size()) {
                for (size_t i = 0; i < newCache.size(); i++) {
                    if (newCache[i].chapterId != m_serverRowElements[i].chapterId) {
                        orderChanged = true;
                        break;
                    }
                }
            }

            // Update cache
            m_lastServerQueue = newCache;

            if (!setChanged && !orderChanged) {
                // Only progress updates, no structural change needed
                updateNavigationRoutes();
                return;
            }

            // FIX 1: Null m_currentFocusedIcon before any structural change
            // to prevent dangling pointer access during rebuild
            m_currentFocusedIcon = nullptr;

            if (setChanged) {
                // Incremental update: remove gone items, then add new items
                for (int chapterId : toRemoveIds) {
                    removeServerItem(chapterId);
                }

                int totalQueueSize = static_cast<int>(queue.size());
                for (size_t idx : toAddIndices) {
                    const auto& item = queue[idx];
                    addServerItem(item.chapterId, item.mangaId, item.mangaTitle,
                                  item.chapterName, item.chapterNumber,
                                  item.downloadedPages, item.pageCount,
                                  static_cast<int>(item.state),
                                  static_cast<int>(idx), totalQueueSize);
                }
            } else {
                // Order changed: full rebuild needed to update currentIndex in actions
                m_serverRowElements.clear();
                while (m_queueContainer->getChildren().size() > 0) {
                    m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
                }

                int queueIndex = 0;
                int totalQueueSize = static_cast<int>(queue.size());
                for (const auto& item : queue) {
                    addServerItem(item.chapterId, item.mangaId, item.mangaTitle,
                                  item.chapterName, item.chapterNumber,
                                  item.downloadedPages, item.pageCount,
                                  static_cast<int>(item.state),
                                  queueIndex++, totalQueueSize);
                }
            }

            // Update d-pad navigation routes after queue is built
            updateNavigationRoutes();
        });
    });
}

void DownloadsTab::refreshLocalDownloads() {
    // Ensure manager is initialized and state is loaded
    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.init();

    auto downloads = mgr.getDownloads();

    // Build new state from current downloads
    struct NewItemInfo {
        int mangaId;
        int chapterIndex;
        std::string mangaTitle;
        std::string chapterName;
        float chapterNumber;
        int downloadedPages;
        int pageCount;
        int state;
    };
    std::vector<NewItemInfo> newItems;

    for (const auto& manga : downloads) {
        for (const auto& chapter : manga.chapters) {
            if (chapter.state == LocalDownloadState::QUEUED ||
                chapter.state == LocalDownloadState::DOWNLOADING) {
                NewItemInfo info;
                info.mangaId = manga.mangaId;
                info.chapterIndex = chapter.chapterIndex;
                info.mangaTitle = manga.title;
                info.chapterName = chapter.name;
                info.chapterNumber = chapter.chapterNumber;
                info.downloadedPages = chapter.downloadedPages;
                info.pageCount = chapter.pageCount;
                info.state = static_cast<int>(chapter.state);
                newItems.push_back(info);
            }
        }
    }

    // Handle empty state
    if (newItems.empty()) {
        // Clear all items
        m_localRowElements.clear();
        m_lastLocalQueue.clear();
        while (m_localContainer && m_localContainer->getChildren().size() > 0) {
            m_localContainer->removeView(m_localContainer->getChildren()[0]);
        }
        m_localSection->setVisibility(brls::Visibility::GONE);

        // Show empty state if server queue is also empty
        if (m_lastServerQueue.empty() && m_emptyStateBox) {
            m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
        }

        // Update navigation routes (local queue now empty, may affect routing)
        updateNavigationRoutes();

        // Update status if no server downloads either
        if (m_lastServerQueue.empty() && m_downloadStatusLabel) {
            m_downloadStatusLabel->setText("");
        }

        // Transfer focus
        if (m_startStopBtn && brls::Application::getCurrentFocus() == nullptr) {
            brls::Application::giveFocus(m_startStopBtn);
        }
        return;
    }

    // Hide empty state when we have items
    if (m_emptyStateBox) {
        m_emptyStateBox->setVisibility(brls::Visibility::GONE);
    }

    // Show section
    m_localSection->setVisibility(brls::Visibility::VISIBLE);
    m_localEmptyLabel->setVisibility(brls::Visibility::GONE);
    m_localScroll->setVisibility(brls::Visibility::VISIBLE);

    // Update status label
    bool localDownloading = false;
    for (const auto& item : newItems) {
        if (item.state == static_cast<int>(LocalDownloadState::DOWNLOADING)) {
            localDownloading = true;
            break;
        }
    }

    if (m_lastServerQueue.empty() && m_downloadStatusLabel) {
        if (localDownloading) {
            m_downloadStatusLabel->setText("• Downloading (Local)");
            m_downloadStatusLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
            m_downloaderRunning = true;
            if (m_startStopLabel) {
                m_startStopLabel->setText("Pause");
            }
        } else {
            m_downloadStatusLabel->setText("• Queued (Local)");
            m_downloadStatusLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
        }
    }

    // INCREMENTAL UPDATE: Check what changed
    // 1. Find items to remove (in old but not in new)
    std::vector<std::pair<int, int>> toRemove;  // mangaId, chapterIndex
    for (const auto& elem : m_localRowElements) {
        bool found = false;
        for (const auto& newItem : newItems) {
            if (elem.mangaId == newItem.mangaId && elem.chapterIndex == newItem.chapterIndex) {
                found = true;
                break;
            }
        }
        if (!found) {
            toRemove.push_back({elem.mangaId, elem.chapterIndex});
        }
    }

    // 2. Find items to add (in new but not in old)
    std::vector<NewItemInfo> toAdd;
    for (const auto& newItem : newItems) {
        bool found = false;
        for (const auto& elem : m_localRowElements) {
            if (elem.mangaId == newItem.mangaId && elem.chapterIndex == newItem.chapterIndex) {
                found = true;
                break;
            }
        }
        if (!found) {
            toAdd.push_back(newItem);
        }
    }

    // 3. Find items to update (same item, different progress)
    for (const auto& newItem : newItems) {
        for (size_t i = 0; i < m_lastLocalQueue.size(); i++) {
            const auto& cached = m_lastLocalQueue[i];
            if (cached.mangaId == newItem.mangaId && cached.chapterIndex == newItem.chapterIndex) {
                // Check if progress changed
                if (cached.downloadedPages != newItem.downloadedPages ||
                    cached.pageCount != newItem.pageCount ||
                    cached.state != newItem.state) {
                    // Update in place - no rebuild needed!
                    updateLocalProgress(newItem.mangaId, newItem.chapterIndex,
                                        newItem.downloadedPages, newItem.pageCount, newItem.state);
                    // Update cache
                    m_lastLocalQueue[i].downloadedPages = newItem.downloadedPages;
                    m_lastLocalQueue[i].pageCount = newItem.pageCount;
                    m_lastLocalQueue[i].state = newItem.state;
                }
                break;
            }
        }
    }

    // 4. Remove items that are no longer in the queue
    for (const auto& item : toRemove) {
        removeLocalItem(item.first, item.second);
    }

    // 5. Add new items
    for (const auto& item : toAdd) {
        addLocalItem(item.mangaId, item.chapterIndex, item.mangaTitle,
                     item.chapterName, item.chapterNumber,
                     item.downloadedPages, item.pageCount, item.state);
    }

    // Update navigation routes if items were added or removed
    if (!toAdd.empty() || !toRemove.empty()) {
        updateNavigationRoutes();
    }

    // Note: We don't need to restore focus because we're doing incremental updates
    // Items are updated in-place, so focus is preserved automatically
}

void DownloadsTab::showDownloadOptions(const std::string& ratingKey, const std::string& title) {
    // Not implemented - download options are shown from manga detail view
}

void DownloadsTab::startAutoRefresh() {
    // Use compare_exchange to atomically check and set (prevent race conditions)
    bool expected = false;
    if (!m_autoRefreshTimerActive.compare_exchange_strong(expected, true)) {
        return;  // Already running
    }

    m_autoRefreshEnabled.store(true);

    // Start auto-refresh loop in background thread
    asyncRun([this]() {
        while (m_autoRefreshEnabled.load()) {
            // Adaptive interval - use longer interval for large queues to reduce UI thread pressure
            int totalItems = static_cast<int>(m_lastServerQueue.size() + m_lastLocalQueue.size());
            int refreshInterval = (totalItems > LARGE_QUEUE_THRESHOLD) ?
                                  AUTO_REFRESH_INTERVAL_LARGE_MS : AUTO_REFRESH_INTERVAL_MS;

            // Sleep for the interval
            std::this_thread::sleep_for(std::chrono::milliseconds(refreshInterval));

            // Check if still enabled after sleep (use atomic load)
            if (!m_autoRefreshEnabled.load()) {
                break;
            }

            // Trigger refresh on main thread for both server and local queues
            // Capture enabled state check inside sync to prevent use-after-free
            brls::sync([this]() {
                if (m_autoRefreshEnabled.load() && this->getVisibility() == brls::Visibility::VISIBLE) {
                    refreshQueue();
                    refreshLocalDownloads();
                }
            });
        }
        m_autoRefreshTimerActive.store(false);
    });
}

void DownloadsTab::stopAutoRefresh() {
    // Set to false - timer will stop on next iteration
    m_autoRefreshEnabled.store(false);
    // Note: We don't wait for the thread to stop here to avoid blocking the UI
    // The atomic checks in the callbacks will prevent any issues
}


// Helper: Create a local download row with all UI elements
brls::Box* DownloadsTab::createLocalRow(int mangaId, int chapterIndex, const std::string& mangaTitle,
                                        const std::string& chapterName, float chapterNumber,
                                        int downloadedPages, int pageCount, int state,
                                        brls::Label*& outProgressLabel, brls::Image*& outXButtonIcon) {
    auto row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setPadding(8);
    row->setMargins(0, 0, 8, 0);
    row->setCornerRadius(6);
    row->setFocusable(true);

    // Background color based on state
    NVGcolor originalBgColor;
    if (state == static_cast<int>(LocalDownloadState::DOWNLOADING)) {
        originalBgColor = nvgRGBA(30, 60, 30, 200);  // Green tint for active
    } else {
        originalBgColor = nvgRGBA(40, 40, 40, 200);
    }
    row->setBackgroundColor(originalBgColor);

    // Info box
    auto infoBox = new brls::Box();
    infoBox->setAxis(brls::Axis::COLUMN);
    infoBox->setGrow(1.0f);

    // Manga title
    auto titleLabel = new brls::Label();
    titleLabel->setText(mangaTitle);
    titleLabel->setFontSize(16);
    infoBox->addView(titleLabel);

    // Chapter name
    auto chapterLabel = new brls::Label();
    std::string chapterText;
    if (chapterNumber > 0) {
        chapterText = "Ch. " + std::to_string(static_cast<int>(chapterNumber));
        if (!chapterName.empty()) {
            chapterText += " - " + chapterName;
        }
    } else {
        chapterText = "Ch. " + std::to_string(chapterIndex);
        if (!chapterName.empty()) {
            chapterText += " - " + chapterName;
        }
    }
    chapterLabel->setText(chapterText);
    chapterLabel->setFontSize(14);
    chapterLabel->setTextColor(nvgRGBA(180, 180, 180, 255));
    infoBox->addView(chapterLabel);

    row->addView(infoBox);

    // Status box (progress + X button icon)
    auto statusBox = new brls::Box();
    statusBox->setAxis(brls::Axis::ROW);
    statusBox->setAlignItems(brls::AlignItems::CENTER);

    // Progress label
    auto progressLabel = new brls::Label();
    progressLabel->setFontSize(16);
    progressLabel->setMargins(0, 0, 0, 10);

    std::string progressText;
    if (state == static_cast<int>(LocalDownloadState::DOWNLOADING)) {
        if (pageCount > 0) {
            progressText = std::to_string(downloadedPages) + "/" +
                           std::to_string(pageCount) + " pages";
        } else {
            progressText = "Downloading...";
        }
        progressLabel->setTextColor(nvgRGBA(100, 200, 100, 255));  // Green
    } else {
        progressText = "Queued";
    }
    progressLabel->setText(progressText);
    outProgressLabel = progressLabel;

    statusBox->addView(progressLabel);

    // X button icon indicator - only visible when focused
    auto* xButtonIcon = new brls::Image();
    xButtonIcon->setWidth(24);
    xButtonIcon->setHeight(24);
    xButtonIcon->setScalingType(brls::ImageScalingType::FIT);
    xButtonIcon->setImageFromFile("app0:resources/images/square_button.png");
    xButtonIcon->setMarginLeft(8);
    xButtonIcon->setVisibility(brls::Visibility::INVISIBLE);
    outXButtonIcon = xButtonIcon;
    statusBox->addView(xButtonIcon);

    row->addView(statusBox);

    // Show X button icon when this row gets focus
    row->getFocusEvent()->subscribe([this, xButtonIcon](brls::View* view) {
        if (m_currentFocusedIcon && m_currentFocusedIcon != xButtonIcon) {
            // Validate that m_currentFocusedIcon still points to a live UI element
            bool isValid = false;
            for (const auto& elem : m_serverRowElements) {
                if (elem.xButtonIcon == m_currentFocusedIcon) { isValid = true; break; }
            }
            if (!isValid) {
                for (const auto& elem : m_localRowElements) {
                    if (elem.xButtonIcon == m_currentFocusedIcon) { isValid = true; break; }
                }
            }
            if (isValid) {
                m_currentFocusedIcon->setVisibility(brls::Visibility::INVISIBLE);
            } else {
                m_currentFocusedIcon = nullptr;
            }
        }
        xButtonIcon->setVisibility(brls::Visibility::VISIBLE);
        m_currentFocusedIcon = xButtonIcon;
    });

    // X button action - remove from local queue
    row->registerAction("Remove", brls::ControllerButton::BUTTON_X,
        [this, mangaId, chapterIndex](brls::View* view) {
            DownloadsManager& mgr = DownloadsManager::getInstance();
            mgr.cancelChapterDownload(mangaId, chapterIndex);
            removeLocalItem(mangaId, chapterIndex);
            return true;
        });

    // Up button action - move up in queue
    row->registerAction("Move Up", brls::ControllerButton::BUTTON_LB,
        [this, mangaId, chapterIndex](brls::View* view) {
            DownloadsManager& mgr = DownloadsManager::getInstance();
            if (mgr.moveChapterInQueue(mangaId, chapterIndex, -1)) {
                refreshLocalDownloads();  // Need full refresh for reordering
            }
            return true;
        });

    // Down button action - move down in queue
    row->registerAction("Move Down", brls::ControllerButton::BUTTON_RB,
        [this, mangaId, chapterIndex](brls::View* view) {
            DownloadsManager& mgr = DownloadsManager::getInstance();
            if (mgr.moveChapterInQueue(mangaId, chapterIndex, 1)) {
                refreshLocalDownloads();  // Need full refresh for reordering
            }
            return true;
        });

    // Add swipe gesture for removal
    auto localSwipeState = std::make_shared<SwipeState>();
    row->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this, row, mangaId, chapterIndex, originalBgColor, localSwipeState](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
            const float SWIPE_THRESHOLD = 60.0f;
            const float TAP_THRESHOLD = 15.0f;

            if (status.state == brls::GestureState::START) {
                localSwipeState->touchStart = status.position;
                localSwipeState->isValidSwipe = false;
            } else if (status.state == brls::GestureState::STAY) {
                float dx = status.position.x - localSwipeState->touchStart.x;
                float dy = status.position.y - localSwipeState->touchStart.y;

                if (std::abs(dx) > std::abs(dy) * 1.5f && std::abs(dx) > TAP_THRESHOLD) {
                    localSwipeState->isValidSwipe = true;
                    if (dx < -SWIPE_THRESHOLD * 0.5f) {
                        row->setBackgroundColor(nvgRGBA(231, 76, 60, 100));
                    } else {
                        row->setBackgroundColor(originalBgColor);
                    }
                }
            } else if (status.state == brls::GestureState::END) {
                row->setBackgroundColor(originalBgColor);

                float dx = status.position.x - localSwipeState->touchStart.x;
                if (localSwipeState->isValidSwipe && dx < -SWIPE_THRESHOLD) {
                    DownloadsManager& mgr = DownloadsManager::getInstance();
                    mgr.cancelChapterDownload(mangaId, chapterIndex);
                    removeLocalItem(mangaId, chapterIndex);
                }
            }
        }, brls::PanAxis::HORIZONTAL));

    return row;
}

// Helper: Update progress for an existing local download item (no rebuild)
void DownloadsTab::updateLocalProgress(int mangaId, int chapterIndex, int downloadedPages, int pageCount, int state) {
    for (auto& elem : m_localRowElements) {
        if (elem.mangaId == mangaId && elem.chapterIndex == chapterIndex) {
            if (elem.progressLabel) {
                std::string progressText;
                if (state == static_cast<int>(LocalDownloadState::DOWNLOADING)) {
                    if (pageCount > 0) {
                        progressText = std::to_string(downloadedPages) + "/" +
                                       std::to_string(pageCount) + " pages";
                    } else {
                        progressText = "Downloading...";
                    }
                    elem.progressLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
                } else {
                    progressText = "Queued";
                    elem.progressLabel->setTextColor(nvgRGBA(255, 255, 255, 255));
                }
                elem.progressLabel->setText(progressText);

                // Update background color based on state
                if (elem.row) {
                    if (state == static_cast<int>(LocalDownloadState::DOWNLOADING)) {
                        elem.row->setBackgroundColor(nvgRGBA(30, 60, 30, 200));
                    } else {
                        elem.row->setBackgroundColor(nvgRGBA(40, 40, 40, 200));
                    }
                }
            }
            return;
        }
    }
}

// Helper: Remove a local download item from the UI
void DownloadsTab::removeLocalItem(int mangaId, int chapterIndex) {
    // Find and remove from tracking vector
    int removeIdx = -1;
    for (size_t i = 0; i < m_localRowElements.size(); i++) {
        if (m_localRowElements[i].mangaId == mangaId &&
            m_localRowElements[i].chapterIndex == chapterIndex) {
            removeIdx = static_cast<int>(i);
            break;
        }
    }

    if (removeIdx < 0) return;

    auto& elem = m_localRowElements[removeIdx];

    // Null m_currentFocusedIcon if it belongs to this row being removed
    if (m_currentFocusedIcon == elem.xButtonIcon) {
        m_currentFocusedIcon = nullptr;
    }

    // Check if this item has focus
    brls::View* currentFocus = brls::Application::getCurrentFocus();
    bool hadFocus = (elem.row == currentFocus);

    // Remove from container
    if (m_localContainer && elem.row) {
        m_localContainer->removeView(elem.row);
    }

    // Remove from cache
    for (auto it = m_lastLocalQueue.begin(); it != m_lastLocalQueue.end(); ++it) {
        if (it->mangaId == mangaId && it->chapterIndex == chapterIndex) {
            m_lastLocalQueue.erase(it);
            break;
        }
    }

    // Remove from tracking
    m_localRowElements.erase(m_localRowElements.begin() + removeIdx);

    // Handle empty state and focus
    if (m_localRowElements.empty()) {
        m_localSection->setVisibility(brls::Visibility::GONE);
        if (m_lastServerQueue.empty() && m_downloadStatusLabel) {
            m_downloadStatusLabel->setText("");
        }
        // Show empty state if server queue is also empty
        if (m_lastServerQueue.empty() && m_emptyStateBox) {
            m_emptyStateBox->setVisibility(brls::Visibility::VISIBLE);
        }
        // Update navigation routes since local queue is now empty
        updateNavigationRoutes();
        if (m_startStopBtn) {
            brls::Application::giveFocus(m_startStopBtn);
        }
    } else if (hadFocus) {
        // Move focus to next item
        int newIdx = std::min(removeIdx, static_cast<int>(m_localRowElements.size()) - 1);
        if (newIdx >= 0 && m_localRowElements[newIdx].row) {
            brls::Application::giveFocus(m_localRowElements[newIdx].row);
        }
        // Update navigation routes since first item may have changed
        if (removeIdx == 0) {
            updateNavigationRoutes();
        }
    }
}

// Helper: Add a new local download item to the UI
void DownloadsTab::addLocalItem(int mangaId, int chapterIndex, const std::string& mangaTitle,
                                const std::string& chapterName, float chapterNumber,
                                int downloadedPages, int pageCount, int state) {
    brls::Label* progressLabel = nullptr;
    brls::Image* xButtonIcon = nullptr;

    auto* row = createLocalRow(mangaId, chapterIndex, mangaTitle, chapterName, chapterNumber,
                               downloadedPages, pageCount, state, progressLabel, xButtonIcon);

    // Add to container
    m_localContainer->addView(row);

    // Track the elements
    LocalRowElements elem;
    elem.row = row;
    elem.progressLabel = progressLabel;
    elem.xButtonIcon = xButtonIcon;
    elem.mangaId = mangaId;
    elem.chapterIndex = chapterIndex;
    m_localRowElements.push_back(elem);

    // Add to cache
    CachedLocalItem cached;
    cached.mangaId = mangaId;
    cached.chapterIndex = chapterIndex;
    cached.downloadedPages = downloadedPages;
    cached.pageCount = pageCount;
    cached.state = state;
    m_lastLocalQueue.push_back(cached);

    // Ensure section is visible
    m_localSection->setVisibility(brls::Visibility::VISIBLE);
    m_localEmptyLabel->setVisibility(brls::Visibility::GONE);
    m_localScroll->setVisibility(brls::Visibility::VISIBLE);
}

void DownloadsTab::updateNavigationRoutes() {
    // Find the first focusable queue item (server queue takes priority)
    brls::View* firstQueueItem = nullptr;
    brls::View* firstServerItem = nullptr;
    brls::View* lastServerItem = nullptr;
    brls::View* firstLocalItem = nullptr;

    // Get server queue items
    if (!m_serverRowElements.empty()) {
        if (m_serverRowElements[0].row) {
            firstServerItem = m_serverRowElements[0].row;
        }
        if (m_serverRowElements.back().row) {
            lastServerItem = m_serverRowElements.back().row;
        }
    }

    // Get local queue items
    if (!m_localRowElements.empty() && m_localRowElements[0].row) {
        firstLocalItem = m_localRowElements[0].row;
    }

    // First queue item is server if exists, otherwise local
    firstQueueItem = firstServerItem ? firstServerItem : firstLocalItem;

    // Set up navigation from action buttons DOWN to first queue item
    if (firstQueueItem) {
        if (m_startStopBtn) {
            m_startStopBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, firstQueueItem);
        }
        if (m_pauseBtn) {
            m_pauseBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, firstQueueItem);
        }
        if (m_clearBtn) {
            m_clearBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, firstQueueItem);
        }

        // Set up navigation from first queue item UP to pause button
        firstQueueItem->setCustomNavigationRoute(brls::FocusDirection::UP, m_pauseBtn);
    } else {
        // No queue items, clear custom navigation routes
        if (m_startStopBtn) {
            m_startStopBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, nullptr);
        }
        if (m_pauseBtn) {
            m_pauseBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, nullptr);
        }
        if (m_clearBtn) {
            m_clearBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, nullptr);
        }
    }

    // Set up navigation between server and local queues when both exist
    if (lastServerItem && firstLocalItem) {
        // Last server item DOWN -> first local item
        lastServerItem->setCustomNavigationRoute(brls::FocusDirection::DOWN, firstLocalItem);
        // First local item UP -> last server item
        firstLocalItem->setCustomNavigationRoute(brls::FocusDirection::UP, lastServerItem);
    } else if (firstLocalItem && !firstServerItem) {
        // Only local queue exists, first item UP -> pause button
        firstLocalItem->setCustomNavigationRoute(brls::FocusDirection::UP, m_pauseBtn);
    }
}

// Helper: Create a server download row with all UI elements (mirrors createLocalRow)
brls::Box* DownloadsTab::createServerRow(int chapterId, int mangaId, const std::string& mangaTitle,
                                         const std::string& chapterName, float chapterNumber,
                                         int downloadedPages, int pageCount, int state,
                                         int currentIndex, int queueSize,
                                         brls::Label*& outProgressLabel, brls::Image*& outXButtonIcon) {
    auto row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setPadding(8);
    row->setMargins(0, 0, 8, 0);
    row->setCornerRadius(6);
    row->setFocusable(true);

    NVGcolor originalBgColor;
    if (state == static_cast<int>(DownloadState::DOWNLOADING)) {
        originalBgColor = nvgRGBA(30, 60, 30, 200);
    } else if (state == static_cast<int>(DownloadState::ERROR)) {
        originalBgColor = nvgRGBA(60, 30, 30, 200);
    } else {
        originalBgColor = nvgRGBA(40, 40, 40, 200);
    }
    row->setBackgroundColor(originalBgColor);

    auto infoBox = new brls::Box();
    infoBox->setAxis(brls::Axis::COLUMN);
    infoBox->setGrow(1.0f);

    auto titleLabel = new brls::Label();
    titleLabel->setText(mangaTitle);
    titleLabel->setFontSize(16);
    infoBox->addView(titleLabel);

    auto chapterLabel = new brls::Label();
    std::string chapterText = chapterName;
    if (chapterNumber > 0) {
        chapterText = "Ch. " + std::to_string(static_cast<int>(chapterNumber));
        if (!chapterName.empty()) {
            chapterText += " - " + chapterName;
        }
    }
    chapterLabel->setText(chapterText);
    chapterLabel->setFontSize(14);
    chapterLabel->setTextColor(nvgRGBA(180, 180, 180, 255));
    infoBox->addView(chapterLabel);

    row->addView(infoBox);

    auto statusBox = new brls::Box();
    statusBox->setAxis(brls::Axis::ROW);
    statusBox->setAlignItems(brls::AlignItems::CENTER);

    auto progressLabel = new brls::Label();
    progressLabel->setFontSize(16);
    progressLabel->setMargins(0, 0, 0, 10);

    std::string progressText;
    if (state == static_cast<int>(DownloadState::DOWNLOADING)) {
        progressText = std::to_string(downloadedPages) + "/" +
                       std::to_string(pageCount) + " pages";
        progressLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
    } else if (state == static_cast<int>(DownloadState::QUEUED)) {
        progressText = "Queued";
    } else if (state == static_cast<int>(DownloadState::DOWNLOADED)) {
        progressText = "Done";
    } else if (state == static_cast<int>(DownloadState::ERROR)) {
        progressText = "Error";
        progressLabel->setTextColor(nvgRGBA(200, 100, 100, 255));
    } else {
        progressText = std::to_string(downloadedPages) + "/" + std::to_string(pageCount);
    }
    progressLabel->setText(progressText);
    outProgressLabel = progressLabel;

    statusBox->addView(progressLabel);

    auto* xButtonIcon = new brls::Image();
    xButtonIcon->setWidth(24);
    xButtonIcon->setHeight(24);
    xButtonIcon->setScalingType(brls::ImageScalingType::FIT);
    xButtonIcon->setImageFromFile("app0:resources/images/square_button.png");
    xButtonIcon->setMarginLeft(8);
    xButtonIcon->setVisibility(brls::Visibility::INVISIBLE);
    outXButtonIcon = xButtonIcon;
    statusBox->addView(xButtonIcon);

    row->addView(statusBox);

    // FIX 2: Focus handler with null-check validation to guard against dangling pointer
    row->getFocusEvent()->subscribe([this, xButtonIcon](brls::View* view) {
        if (m_currentFocusedIcon && m_currentFocusedIcon != xButtonIcon) {
            // Validate that m_currentFocusedIcon still points to a live UI element
            bool isValid = false;
            for (const auto& elem : m_serverRowElements) {
                if (elem.xButtonIcon == m_currentFocusedIcon) { isValid = true; break; }
            }
            if (!isValid) {
                for (const auto& elem : m_localRowElements) {
                    if (elem.xButtonIcon == m_currentFocusedIcon) { isValid = true; break; }
                }
            }
            if (isValid) {
                m_currentFocusedIcon->setVisibility(brls::Visibility::INVISIBLE);
            } else {
                m_currentFocusedIcon = nullptr;
            }
        }
        xButtonIcon->setVisibility(brls::Visibility::VISIBLE);
        m_currentFocusedIcon = xButtonIcon;
    });

    row->registerAction("Remove", brls::ControllerButton::BUTTON_X,
        [this, mangaId, chapterId](brls::View* view) {
            asyncRun([this, mangaId, chapterId]() {
                SuwayomiClient& client = SuwayomiClient::getInstance();
                std::vector<int> chapterIds = {chapterId};
                client.deleteChapterDownloads(chapterIds, mangaId);
                brls::sync([this]() {
                    refreshQueue();
                });
            });
            return true;
        });

    row->registerAction("Move Up", brls::ControllerButton::BUTTON_LB,
        [this, chapterId, mangaId, currentIndex](brls::View* view) {
            if (currentIndex > 0) {
                asyncRun([this, chapterId, mangaId, currentIndex]() {
                    SuwayomiClient& client = SuwayomiClient::getInstance();
                    if (client.reorderDownload(chapterId, mangaId, 0, currentIndex - 1)) {
                        brls::sync([this]() {
                            refreshQueue();
                        });
                    }
                });
            }
            return true;
        });

    row->registerAction("Move Down", brls::ControllerButton::BUTTON_RB,
        [this, chapterId, mangaId, currentIndex, queueSize](brls::View* view) {
            if (currentIndex < queueSize - 1) {
                asyncRun([this, chapterId, mangaId, currentIndex]() {
                    SuwayomiClient& client = SuwayomiClient::getInstance();
                    if (client.reorderDownload(chapterId, mangaId, 0, currentIndex + 1)) {
                        brls::sync([this]() {
                            refreshQueue();
                        });
                    }
                });
            }
            return true;
        });

    auto swipeState = std::make_shared<SwipeState>();
    row->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this, row, mangaId, chapterId, originalBgColor, swipeState](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
            const float SWIPE_THRESHOLD = 60.0f;
            const float TAP_THRESHOLD = 15.0f;

            if (status.state == brls::GestureState::START) {
                swipeState->touchStart = status.position;
                swipeState->isValidSwipe = false;
            } else if (status.state == brls::GestureState::STAY) {
                float dx = status.position.x - swipeState->touchStart.x;
                float dy = status.position.y - swipeState->touchStart.y;

                if (std::abs(dx) > std::abs(dy) * 1.5f && std::abs(dx) > TAP_THRESHOLD) {
                    swipeState->isValidSwipe = true;
                    if (dx < -SWIPE_THRESHOLD * 0.5f) {
                        row->setBackgroundColor(nvgRGBA(231, 76, 60, 100));
                    } else {
                        row->setBackgroundColor(originalBgColor);
                    }
                }
            } else if (status.state == brls::GestureState::END) {
                row->setBackgroundColor(originalBgColor);

                float dx = status.position.x - swipeState->touchStart.x;
                if (swipeState->isValidSwipe && dx < -SWIPE_THRESHOLD) {
                    asyncRun([this, mangaId, chapterId]() {
                        SuwayomiClient& client = SuwayomiClient::getInstance();
                        std::vector<int> chapterIds = {chapterId};
                        client.deleteChapterDownloads(chapterIds, mangaId);
                        brls::sync([this]() {
                            refreshQueue();
                        });
                    });
                }
            }
        }, brls::PanAxis::HORIZONTAL));

    return row;
}

// Helper: Add a new server download item to the UI
void DownloadsTab::addServerItem(int chapterId, int mangaId, const std::string& mangaTitle,
                                  const std::string& chapterName, float chapterNumber,
                                  int downloadedPages, int pageCount, int state,
                                  int currentIndex, int queueSize) {
    brls::Label* progressLabel = nullptr;
    brls::Image* xButtonIcon = nullptr;

    auto* row = createServerRow(chapterId, mangaId, mangaTitle, chapterName, chapterNumber,
                                downloadedPages, pageCount, state, currentIndex, queueSize,
                                progressLabel, xButtonIcon);

    m_queueContainer->addView(row);

    ServerRowElements elem;
    elem.row = row;
    elem.progressLabel = progressLabel;
    elem.xButtonIcon = xButtonIcon;
    elem.chapterId = chapterId;
    elem.mangaId = mangaId;
    m_serverRowElements.push_back(elem);
}

// Helper: Remove a server download item from the UI
void DownloadsTab::removeServerItem(int chapterId) {
    int removeIdx = -1;
    for (size_t i = 0; i < m_serverRowElements.size(); i++) {
        if (m_serverRowElements[i].chapterId == chapterId) {
            removeIdx = static_cast<int>(i);
            break;
        }
    }

    if (removeIdx < 0) return;

    auto& elem = m_serverRowElements[removeIdx];

    // Null m_currentFocusedIcon if it belongs to this row being removed
    if (m_currentFocusedIcon == elem.xButtonIcon) {
        m_currentFocusedIcon = nullptr;
    }

    // Check if this item has focus
    brls::View* currentFocus = brls::Application::getCurrentFocus();
    bool hadFocus = (elem.row == currentFocus);

    // Remove from container
    if (m_queueContainer && elem.row) {
        m_queueContainer->removeView(elem.row);
    }

    // Remove from tracking
    m_serverRowElements.erase(m_serverRowElements.begin() + removeIdx);

    // Transfer focus if this row had it
    if (hadFocus) {
        if (!m_serverRowElements.empty()) {
            int newIdx = std::min(removeIdx, static_cast<int>(m_serverRowElements.size()) - 1);
            if (newIdx >= 0 && m_serverRowElements[newIdx].row) {
                brls::Application::giveFocus(m_serverRowElements[newIdx].row);
            }
        } else if (!m_localRowElements.empty() && m_localRowElements[0].row) {
            brls::Application::giveFocus(m_localRowElements[0].row);
        } else if (m_startStopBtn) {
            brls::Application::giveFocus(m_startStopBtn);
        }
    }
}

} // namespace vitasuwayomi
