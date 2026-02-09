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

// Auto-refresh interval in milliseconds (3 seconds)
static const int AUTO_REFRESH_INTERVAL_MS = 3000;

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

    // Start/Stop button tap action - toggle downloader state for both server and local
    m_startStopBtn->addGestureRecognizer(new brls::TapGestureRecognizer(m_startStopBtn, [this]() {
        asyncRun([this]() {
            SuwayomiClient& client = SuwayomiClient::getInstance();
            DownloadsManager& mgr = DownloadsManager::getInstance();
            bool success = false;
            if (m_downloaderRunning) {
                // Stop both server and local downloads
                success = client.stopDownloads();
                mgr.pauseDownloads();
            } else {
                // Start both server and local downloads
                success = client.startDownloads();
                mgr.startDownloads();
            }
            if (success) {
                brls::sync([this]() {
                    refreshQueue();
                    refreshLocalDownloads();
                });
            }
        });
    }));
    m_actionsRow->addView(m_startStopBtn);

    // Stop/Pause button (stop current downloads)
    auto* pauseBtn = new brls::Button();
    pauseBtn->setWidth(60);
    pauseBtn->setHeight(32);
    pauseBtn->setCornerRadius(6);
    pauseBtn->setMarginRight(8);
    pauseBtn->setPaddingLeft(8);
    pauseBtn->setPaddingRight(8);
    pauseBtn->setJustifyContent(brls::JustifyContent::CENTER);
    pauseBtn->setAlignItems(brls::AlignItems::CENTER);

    auto* pauseLabel = new brls::Label();
    pauseLabel->setText("Stop");
    pauseLabel->setFontSize(12);
    pauseBtn->addView(pauseLabel);

    // Pause button tap action - stop both server and local downloads
    pauseBtn->addGestureRecognizer(new brls::TapGestureRecognizer(pauseBtn, [this]() {
        pauseAllDownloads();
        // Also pause local downloads
        DownloadsManager& mgr = DownloadsManager::getInstance();
        mgr.pauseDownloads();
        refreshLocalDownloads();
    }));
    m_actionsRow->addView(pauseBtn);

    // Clear button
    auto* clearBtn = new brls::Button();
    clearBtn->setWidth(70);
    clearBtn->setHeight(32);
    clearBtn->setCornerRadius(6);
    clearBtn->setPaddingLeft(8);
    clearBtn->setPaddingRight(8);
    clearBtn->setJustifyContent(brls::JustifyContent::CENTER);
    clearBtn->setAlignItems(brls::AlignItems::CENTER);

    m_clearIcon = new brls::Image();
    m_clearIcon->setWidth(16);
    m_clearIcon->setHeight(16);
    m_clearIcon->setScalingType(brls::ImageScalingType::FIT);
    m_clearIcon->setImageFromFile("app0:resources/icons/delete.png");
    m_clearIcon->setMarginRight(4);
    clearBtn->addView(m_clearIcon);

    auto* clearLabel = new brls::Label();
    clearLabel->setText("Clear");
    clearLabel->setFontSize(12);
    clearBtn->addView(clearLabel);

    // Clear button tap action - clear server download queue
    clearBtn->addGestureRecognizer(new brls::TapGestureRecognizer(clearBtn, [this]() {
        clearAllDownloads();
    }));
    m_actionsRow->addView(clearBtn);

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
}

void DownloadsTab::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);

    // Initialize last progress refresh time
    m_lastProgressRefresh = std::chrono::steady_clock::now();

    // Register progress callback for real-time UI updates during local downloads
    // Throttled to avoid excessive UI refreshes (max once every 500ms)
    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.setProgressCallback([this](int downloadedPages, int totalPages) {
        // Check if enough time has passed since last refresh (throttle)
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastProgressRefresh).count();

        // Only refresh if 500ms has passed, or if we're on the last page (chapter complete)
        if (elapsed >= PROGRESS_REFRESH_INTERVAL_MS || downloadedPages == totalPages) {
            m_lastProgressRefresh = now;
            // Schedule a refresh on the main thread - check if auto-refresh still enabled
            if (m_autoRefreshEnabled.load()) {
                brls::sync([this]() {
                    if (m_autoRefreshEnabled.load()) {
                        refreshLocalDownloads();
                    }
                });
            }
        }
    });

    // Register chapter completion callback - remove completed chapter from UI directly
    // This is more efficient than refreshing the entire list
    mgr.setChapterCompletionCallback([this](int mangaId, int chapterIndex, bool success) {
        // Only sync if the tab is still active
        if (!m_autoRefreshEnabled.load()) {
            return;
        }

        brls::sync([this, mangaId, chapterIndex]() {
            if (!m_autoRefreshEnabled.load() || !m_localContainer) {
                return;
            }

            // Find the index of this chapter in our cache
            int removeIndex = -1;
            for (size_t i = 0; i < m_lastLocalQueue.size(); i++) {
                if (m_lastLocalQueue[i].mangaId == mangaId &&
                    m_lastLocalQueue[i].chapterIndex == chapterIndex) {
                    removeIndex = static_cast<int>(i);
                    break;
                }
            }

            if (removeIndex < 0) {
                return;  // Chapter not found in our list (already removed or not displayed)
            }

            // Check if focus is on the item we're removing
            brls::View* currentFocus = brls::Application::getCurrentFocus();
            auto& children = m_localContainer->getChildren();
            bool hadFocusOnRemovedItem = (removeIndex < static_cast<int>(children.size()) &&
                                          children[removeIndex] == currentFocus);

            // Remove the item from the UI
            if (removeIndex < static_cast<int>(children.size())) {
                m_localContainer->removeView(children[removeIndex]);
            }

            // Remove from cache
            m_lastLocalQueue.erase(m_lastLocalQueue.begin() + removeIndex);

            // Handle focus and visibility after removal
            if (m_lastLocalQueue.empty()) {
                // No more local downloads - hide the section
                m_localSection->setVisibility(brls::Visibility::GONE);
                // Clear status if server queue is also empty
                if (m_lastServerQueue.empty() && m_downloadStatusLabel) {
                    m_downloadStatusLabel->setText("");
                }
                // Move focus to buttons
                if (m_startStopBtn) {
                    brls::Application::giveFocus(m_startStopBtn);
                }
            } else if (hadFocusOnRemovedItem) {
                // Focus was on removed item - move to next item or previous
                auto& newChildren = m_localContainer->getChildren();
                if (!newChildren.empty()) {
                    int newIndex = std::min(removeIndex, static_cast<int>(newChildren.size()) - 1);
                    brls::Application::giveFocus(newChildren[newIndex]);
                }
            }
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
}

void DownloadsTab::refresh() {
    refreshQueue();
    refreshLocalDownloads();
}

void DownloadsTab::refreshQueue() {
    // Save focus state before refresh - check if focus is on a server queue item
    brls::View* currentFocus = brls::Application::getCurrentFocus();
    m_focusedServerIndex = -1;
    m_hadFocusOnServerQueue = false;
    if (currentFocus && m_queueContainer) {
        auto& children = m_queueContainer->getChildren();
        for (size_t i = 0; i < children.size(); i++) {
            if (children[i] == currentFocus) {
                m_focusedServerIndex = static_cast<int>(i);
                m_hadFocusOnServerQueue = true;
                break;
            }
        }
    }

    // Fetch download queue and status from server
    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<DownloadQueueItem> queue;

        if (!client.fetchDownloadQueue(queue)) {
            brls::sync([this]() {
                // Hide entire section on fetch failure or empty
                m_queueSection->setVisibility(brls::Visibility::GONE);
                m_lastServerQueue.clear();
                // Clear container
                while (m_queueContainer->getChildren().size() > 0) {
                    m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
                }

                // Only clear status if no local downloads exist
                if (m_lastLocalQueue.empty()) {
                    m_downloadStatusLabel->setText("");
                }

                // If focus was on server queue, try to move to local queue or buttons
                if (m_hadFocusOnServerQueue) {
                    if (m_localContainer && !m_localContainer->getChildren().empty()) {
                        brls::Application::giveFocus(m_localContainer->getChildren()[0]);
                    } else if (m_startStopBtn) {
                        brls::Application::giveFocus(m_startStopBtn);
                    }
                }
            });
            return;
        }

        // Build new cache and check if anything changed
        std::vector<CachedQueueItem> newCache;
        newCache.reserve(queue.size());
        for (const auto& item : queue) {
            CachedQueueItem cached;
            cached.chapterId = item.chapterId;
            cached.mangaId = item.mangaId;
            cached.downloadedPages = item.downloadedPages;
            cached.state = static_cast<int>(item.state);
            newCache.push_back(cached);
        }

        // Compare with last state - skip UI update if nothing changed
        bool hasChanged = (newCache.size() != m_lastServerQueue.size());
        if (!hasChanged) {
            for (size_t i = 0; i < newCache.size(); i++) {
                if (newCache[i].chapterId != m_lastServerQueue[i].chapterId ||
                    newCache[i].mangaId != m_lastServerQueue[i].mangaId ||
                    newCache[i].downloadedPages != m_lastServerQueue[i].downloadedPages ||
                    newCache[i].state != m_lastServerQueue[i].state) {
                    hasChanged = true;
                    break;
                }
            }
        }

        // Determine downloader state based on queue items
        bool isDownloading = false;
        for (const auto& item : queue) {
            if (item.state == DownloadState::DOWNLOADING) {
                isDownloading = true;
                break;
            }
        }

        brls::sync([this, queue, isDownloading, hasChanged, newCache]() {
            // Always update the cache
            m_lastServerQueue = newCache;
            // Update downloader state
            m_downloaderRunning = isDownloading;
            // Only update button label if no local downloads exist
            // (refreshLocalDownloads will set correct label based on local state)
            if (m_startStopLabel && m_lastLocalQueue.empty()) {
                m_startStopLabel->setText(m_downloaderRunning ? "Pause" : "Start");
            }

            // Update status label
            if (m_downloadStatusLabel) {
                if (queue.empty()) {
                    // Only clear if no local downloads exist (local refresh will set proper status)
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

            // Hide entire section if queue is empty
            if (queue.empty()) {
                m_queueSection->setVisibility(brls::Visibility::GONE);
                // Clear container if there were items before
                while (m_queueContainer->getChildren().size() > 0) {
                    m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
                }
                // Transfer focus when server queue becomes empty
                if (m_hadFocusOnServerQueue) {
                    if (m_localContainer && !m_localContainer->getChildren().empty()) {
                        brls::Application::giveFocus(m_localContainer->getChildren()[0]);
                    } else if (m_startStopBtn) {
                        brls::Application::giveFocus(m_startStopBtn);
                    }
                } else if (m_startStopBtn && brls::Application::getCurrentFocus() == nullptr) {
                    brls::Application::giveFocus(m_startStopBtn);
                }
                return;
            }

            // Show section and scroll when there are items
            m_queueSection->setVisibility(brls::Visibility::VISIBLE);
            m_queueEmptyLabel->setVisibility(brls::Visibility::GONE);
            m_queueScroll->setVisibility(brls::Visibility::VISIBLE);

            // Skip UI rebuild if nothing changed
            if (!hasChanged) {
                return;
            }

            // Clear existing items and rebuild
            while (m_queueContainer->getChildren().size() > 0) {
                m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
            }

            int queueIndex = 0;
            int queueSize = static_cast<int>(queue.size());
            for (const auto& item : queue) {
                int currentIndex = queueIndex++;  // Capture current position
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
                if (item.state == DownloadState::DOWNLOADING) {
                    originalBgColor = nvgRGBA(30, 60, 30, 200);  // Green tint for active
                } else if (item.state == DownloadState::ERROR) {
                    originalBgColor = nvgRGBA(60, 30, 30, 200);  // Red tint for error
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
                titleLabel->setText(item.mangaTitle);
                titleLabel->setFontSize(16);
                infoBox->addView(titleLabel);

                // Chapter name
                auto chapterLabel = new brls::Label();
                std::string chapterText = item.chapterName;
                if (item.chapterNumber > 0) {
                    chapterText = "Ch. " + std::to_string(static_cast<int>(item.chapterNumber));
                    if (!item.chapterName.empty()) {
                        chapterText += " - " + item.chapterName;
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

                // Progress label (x/x pages)
                auto progressLabel = new brls::Label();
                progressLabel->setFontSize(16);
                progressLabel->setMargins(0, 0, 0, 10);

                std::string progressText;
                if (item.state == DownloadState::DOWNLOADING) {
                    progressText = std::to_string(item.downloadedPages) + "/" +
                                   std::to_string(item.pageCount) + " pages";
                } else if (item.state == DownloadState::QUEUED) {
                    progressText = "Queued";
                } else if (item.state == DownloadState::DOWNLOADED) {
                    progressText = "Done";
                } else if (item.state == DownloadState::ERROR) {
                    progressText = "Error";
                } else {
                    progressText = std::to_string(item.downloadedPages) + "/" +
                                   std::to_string(item.pageCount);
                }
                progressLabel->setText(progressText);

                // Color based on state
                if (item.state == DownloadState::DOWNLOADING) {
                    progressLabel->setTextColor(nvgRGBA(100, 200, 100, 255));  // Green
                } else if (item.state == DownloadState::ERROR) {
                    progressLabel->setTextColor(nvgRGBA(200, 100, 100, 255));  // Red
                }

                statusBox->addView(progressLabel);

                // X button icon indicator - only visible when focused (like book detail view)
                auto* xButtonIcon = new brls::Image();
                xButtonIcon->setWidth(24);
                xButtonIcon->setHeight(24);
                xButtonIcon->setScalingType(brls::ImageScalingType::FIT);
                xButtonIcon->setImageFromFile("app0:resources/images/square_button.png");
                xButtonIcon->setMarginLeft(8);
                xButtonIcon->setVisibility(brls::Visibility::INVISIBLE);  // Hidden by default
                statusBox->addView(xButtonIcon);

                row->addView(statusBox);

                // Show X button icon when this row gets focus, hide previous
                row->getFocusEvent()->subscribe([this, xButtonIcon](brls::View* view) {
                    // Hide previously focused icon
                    if (m_currentFocusedIcon && m_currentFocusedIcon != xButtonIcon) {
                        m_currentFocusedIcon->setVisibility(brls::Visibility::INVISIBLE);
                    }
                    // Show this icon
                    xButtonIcon->setVisibility(brls::Visibility::VISIBLE);
                    m_currentFocusedIcon = xButtonIcon;
                });

                // X button action - remove from server queue
                int mangaId = item.mangaId;
                int chapterId = item.chapterId;
                row->registerAction("Remove", brls::ControllerButton::BUTTON_X,
                    [this, mangaId, chapterId](brls::View* view) {
                        asyncRun([this, mangaId, chapterId]() {
                            SuwayomiClient& client = SuwayomiClient::getInstance();
                            // Remove from server queue using existing method
                            std::vector<int> chapterIds = {chapterId};
                            client.deleteChapterDownloads(chapterIds, mangaId);
                            brls::sync([this]() {
                                refresh();
                            });
                        });
                        return true;
                    });

                // Move Up action (LB button) - move to earlier position in queue
                row->registerAction("Move Up", brls::ControllerButton::BUTTON_LB,
                    [this, chapterId, mangaId, currentIndex](brls::View* view) {
                        if (currentIndex > 0) {
                            asyncRun([this, chapterId, mangaId, currentIndex]() {
                                SuwayomiClient& client = SuwayomiClient::getInstance();
                                if (client.reorderDownload(chapterId, mangaId, 0, currentIndex - 1)) {
                                    brls::sync([this]() {
                                        refresh();
                                    });
                                }
                            });
                        }
                        return true;
                    });

                // Move Down action (RB button) - move to later position in queue
                row->registerAction("Move Down", brls::ControllerButton::BUTTON_RB,
                    [this, chapterId, mangaId, currentIndex, queueSize](brls::View* view) {
                        if (currentIndex < queueSize - 1) {
                            asyncRun([this, chapterId, mangaId, currentIndex]() {
                                SuwayomiClient& client = SuwayomiClient::getInstance();
                                if (client.reorderDownload(chapterId, mangaId, 0, currentIndex + 1)) {
                                    brls::sync([this]() {
                                        refresh();
                                    });
                                }
                            });
                        }
                        return true;
                    });

                // Add swipe gesture for removal - use shared_ptr to avoid static variables
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

                            // Visual feedback during swipe
                            if (std::abs(dx) > std::abs(dy) * 1.5f && std::abs(dx) > TAP_THRESHOLD) {
                                swipeState->isValidSwipe = true;
                                if (dx < -SWIPE_THRESHOLD * 0.5f) {
                                    // Left swipe - red tint for removal
                                    row->setBackgroundColor(nvgRGBA(231, 76, 60, 100));
                                } else {
                                    row->setBackgroundColor(originalBgColor);
                                }
                            }
                        } else if (status.state == brls::GestureState::END) {
                            row->setBackgroundColor(originalBgColor);

                            float dx = status.position.x - swipeState->touchStart.x;
                            if (swipeState->isValidSwipe && dx < -SWIPE_THRESHOLD) {
                                // Left swipe confirmed - remove from queue
                                asyncRun([this, mangaId, chapterId]() {
                                    SuwayomiClient& client = SuwayomiClient::getInstance();
                                    std::vector<int> chapterIds = {chapterId};
                                    client.deleteChapterDownloads(chapterIds, mangaId);
                                    brls::sync([this]() {
                                        refresh();
                                    });
                                });
                            }
                        }
                    }, brls::PanAxis::HORIZONTAL));

                // Add row
                m_queueContainer->addView(row);
            }

            // Restore focus after rebuild if we had focus on server queue
            if (m_hadFocusOnServerQueue && m_focusedServerIndex >= 0) {
                auto& children = m_queueContainer->getChildren();
                if (!children.empty()) {
                    int newIndex = std::min(m_focusedServerIndex, static_cast<int>(children.size()) - 1);
                    brls::Application::giveFocus(children[newIndex]);
                }
                m_hadFocusOnServerQueue = false;
            }
        });
    });
}

void DownloadsTab::refreshLocalDownloads() {
    // Save focus state before refresh - check if focus is on a local queue item
    brls::View* currentFocus = brls::Application::getCurrentFocus();
    m_focusedLocalIndex = -1;
    m_hadFocusOnLocalQueue = false;
    if (currentFocus && m_localContainer) {
        auto& children = m_localContainer->getChildren();
        for (size_t i = 0; i < children.size(); i++) {
            if (children[i] == currentFocus) {
                m_focusedLocalIndex = static_cast<int>(i);
                m_hadFocusOnLocalQueue = true;
                break;
            }
        }
    }

    // Ensure manager is initialized and state is loaded
    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.init();

    auto downloads = mgr.getDownloads();

    // Build new cache and count active chapters
    std::vector<CachedLocalItem> newCache;
    for (const auto& manga : downloads) {
        for (const auto& chapter : manga.chapters) {
            if (chapter.state == LocalDownloadState::QUEUED ||
                chapter.state == LocalDownloadState::DOWNLOADING) {
                CachedLocalItem cached;
                cached.mangaId = manga.mangaId;
                cached.chapterIndex = chapter.chapterIndex;
                cached.downloadedPages = chapter.downloadedPages;
                cached.pageCount = chapter.pageCount;
                cached.state = static_cast<int>(chapter.state);
                newCache.push_back(cached);
            }
        }
    }

    // Compare with last state - skip UI update if nothing changed
    bool hasChanged = (newCache.size() != m_lastLocalQueue.size());
    if (!hasChanged) {
        for (size_t i = 0; i < newCache.size(); i++) {
            if (newCache[i].mangaId != m_lastLocalQueue[i].mangaId ||
                newCache[i].chapterIndex != m_lastLocalQueue[i].chapterIndex ||
                newCache[i].downloadedPages != m_lastLocalQueue[i].downloadedPages ||
                newCache[i].pageCount != m_lastLocalQueue[i].pageCount ||
                newCache[i].state != m_lastLocalQueue[i].state) {
                hasChanged = true;
                break;
            }
        }
    }

    // Update cache
    m_lastLocalQueue = newCache;

    // Hide entire section if no active downloads
    if (newCache.empty()) {
        m_localSection->setVisibility(brls::Visibility::GONE);
        // Clear container if there were items before
        while (m_localContainer->getChildren().size() > 0) {
            m_localContainer->removeView(m_localContainer->getChildren()[0]);
        }
        // Transfer focus to start/stop button when queue becomes empty
        // Check if focus was on local queue or is now null
        if (m_startStopBtn && (m_hadFocusOnLocalQueue || brls::Application::getCurrentFocus() == nullptr)) {
            brls::Application::giveFocus(m_startStopBtn);
        }
        m_hadFocusOnLocalQueue = false;
        return;
    }

    // Show section and scroll when there are items
    m_localSection->setVisibility(brls::Visibility::VISIBLE);
    m_localEmptyLabel->setVisibility(brls::Visibility::GONE);
    m_localScroll->setVisibility(brls::Visibility::VISIBLE);

    // Update status label for local downloads if server queue is empty
    bool localDownloading = false;
    for (const auto& item : newCache) {
        if (item.state == static_cast<int>(LocalDownloadState::DOWNLOADING)) {
            localDownloading = true;
            break;
        }
    }

    // If server queue is empty but local downloads exist, update status
    if (m_lastServerQueue.empty() && m_downloadStatusLabel) {
        if (localDownloading) {
            m_downloadStatusLabel->setText("• Downloading (Local)");
            m_downloadStatusLabel->setTextColor(nvgRGBA(100, 200, 100, 255));
            m_downloaderRunning = true;
            if (m_startStopLabel) {
                m_startStopLabel->setText("Pause");
            }
        } else if (!newCache.empty()) {
            m_downloadStatusLabel->setText("• Queued (Local)");
            m_downloadStatusLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
        }
    }

    // Skip UI rebuild if nothing changed
    if (!hasChanged) {
        return;
    }

    // Clear existing local items and rebuild
    while (m_localContainer->getChildren().size() > 0) {
        m_localContainer->removeView(m_localContainer->getChildren()[0]);
    }

    // Show each chapter as a queue item (like server downloads)
    for (const auto& manga : downloads) {
        for (const auto& chapter : manga.chapters) {
            // Only show queued or downloading chapters
            if (chapter.state != LocalDownloadState::QUEUED &&
                chapter.state != LocalDownloadState::DOWNLOADING) {
                continue;
            }

            int mangaId = manga.mangaId;
            int chapterIndex = chapter.chapterIndex;

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
            if (chapter.state == LocalDownloadState::DOWNLOADING) {
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
            titleLabel->setText(manga.title);
            titleLabel->setFontSize(16);
            infoBox->addView(titleLabel);

            // Chapter name
            auto chapterLabel = new brls::Label();
            std::string chapterText;
            if (chapter.chapterNumber > 0) {
                chapterText = "Ch. " + std::to_string(static_cast<int>(chapter.chapterNumber));
                if (!chapter.name.empty()) {
                    chapterText += " - " + chapter.name;
                }
            } else {
                chapterText = "Ch. " + std::to_string(chapter.chapterIndex);
                if (!chapter.name.empty()) {
                    chapterText += " - " + chapter.name;
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
            if (chapter.state == LocalDownloadState::DOWNLOADING) {
                if (chapter.pageCount > 0) {
                    progressText = std::to_string(chapter.downloadedPages) + "/" +
                                   std::to_string(chapter.pageCount) + " pages";
                } else {
                    progressText = "Downloading...";
                }
                progressLabel->setTextColor(nvgRGBA(100, 200, 100, 255));  // Green
            } else {
                progressText = "Queued";
            }
            progressLabel->setText(progressText);

            statusBox->addView(progressLabel);

            // X button icon indicator - only visible when focused (like book detail view)
            auto* xButtonIcon = new brls::Image();
            xButtonIcon->setWidth(24);
            xButtonIcon->setHeight(24);
            xButtonIcon->setScalingType(brls::ImageScalingType::FIT);
            xButtonIcon->setImageFromFile("app0:resources/images/square_button.png");
            xButtonIcon->setMarginLeft(8);
            xButtonIcon->setVisibility(brls::Visibility::INVISIBLE);  // Hidden by default
            statusBox->addView(xButtonIcon);

            row->addView(statusBox);

            // Show X button icon when this row gets focus, hide previous
            row->getFocusEvent()->subscribe([this, xButtonIcon](brls::View* view) {
                // Hide previously focused icon
                if (m_currentFocusedIcon && m_currentFocusedIcon != xButtonIcon) {
                    m_currentFocusedIcon->setVisibility(brls::Visibility::INVISIBLE);
                }
                // Show this icon
                xButtonIcon->setVisibility(brls::Visibility::VISIBLE);
                m_currentFocusedIcon = xButtonIcon;
            });

            // X button action - remove from local queue
            row->registerAction("Remove", brls::ControllerButton::BUTTON_X,
                [this, mangaId, chapterIndex](brls::View* view) {
                    DownloadsManager& mgr = DownloadsManager::getInstance();
                    mgr.cancelChapterDownload(mangaId, chapterIndex);
                    refresh();
                    return true;
                });

            // Up button action - move up in queue
            row->registerAction("Move Up", brls::ControllerButton::BUTTON_LB,
                [this, mangaId, chapterIndex](brls::View* view) {
                    DownloadsManager& mgr = DownloadsManager::getInstance();
                    if (mgr.moveChapterInQueue(mangaId, chapterIndex, -1)) {
                        refresh();
                    }
                    return true;
                });

            // Down button action - move down in queue
            row->registerAction("Move Down", brls::ControllerButton::BUTTON_RB,
                [this, mangaId, chapterIndex](brls::View* view) {
                    DownloadsManager& mgr = DownloadsManager::getInstance();
                    if (mgr.moveChapterInQueue(mangaId, chapterIndex, 1)) {
                        refresh();
                    }
                    return true;
                });

            // Add swipe gesture for removal (horizontal only, like server downloads)
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

                        // Visual feedback during swipe
                        if (std::abs(dx) > std::abs(dy) * 1.5f && std::abs(dx) > TAP_THRESHOLD) {
                            localSwipeState->isValidSwipe = true;
                            if (dx < -SWIPE_THRESHOLD * 0.5f) {
                                // Left swipe - red tint for removal
                                row->setBackgroundColor(nvgRGBA(231, 76, 60, 100));
                            } else {
                                row->setBackgroundColor(originalBgColor);
                            }
                        }
                    } else if (status.state == brls::GestureState::END) {
                        row->setBackgroundColor(originalBgColor);

                        float dx = status.position.x - localSwipeState->touchStart.x;
                        if (localSwipeState->isValidSwipe && dx < -SWIPE_THRESHOLD) {
                            // Left swipe confirmed - remove from queue
                            DownloadsManager& mgr = DownloadsManager::getInstance();
                            mgr.cancelChapterDownload(mangaId, chapterIndex);
                            refresh();
                        }
                    }
                }, brls::PanAxis::HORIZONTAL));

            // Add row
            m_localContainer->addView(row);
        }
    }

    // Restore focus after rebuild if we had focus on local queue
    if (m_hadFocusOnLocalQueue && m_focusedLocalIndex >= 0) {
        auto& children = m_localContainer->getChildren();
        if (!children.empty()) {
            int newIndex = std::min(m_focusedLocalIndex, static_cast<int>(children.size()) - 1);
            brls::Application::giveFocus(children[newIndex]);
        }
        m_hadFocusOnLocalQueue = false;
    }
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
            // Sleep for the interval
            std::this_thread::sleep_for(std::chrono::milliseconds(AUTO_REFRESH_INTERVAL_MS));

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

void DownloadsTab::pauseAllDownloads() {
    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        if (client.stopDownloads()) {
            brls::sync([this]() {
                m_downloaderRunning = false;
                if (m_startStopLabel) {
                    m_startStopLabel->setText("Start");
                }
                if (m_downloadStatusLabel) {
                    m_downloadStatusLabel->setText("• Stopped");
                    m_downloadStatusLabel->setTextColor(nvgRGBA(200, 150, 100, 255));
                }
                refreshQueue();
            });
        }
    });
}

void DownloadsTab::clearAllDownloads() {
    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        if (client.clearDownloadQueue()) {
            brls::sync([this]() {
                m_downloaderRunning = false;
                if (m_startStopLabel) {
                    m_startStopLabel->setText("Start");
                }
                if (m_downloadStatusLabel) {
                    m_downloadStatusLabel->setText("");
                }
                refresh();
            });
        }
    });
}

} // namespace vitasuwayomi
