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

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

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

    // Header
    auto header = new brls::Label();
    header->setText("Downloads");
    header->setFontSize(24);
    header->setMargins(0, 0, 15, 0);
    this->addView(header);

    // === Download Queue Section (server downloads) ===
    m_queueSection = new brls::Box();
    m_queueSection->setAxis(brls::Axis::COLUMN);
    m_queueSection->setMargins(0, 0, 15, 0);
    m_queueSection->setVisibility(brls::Visibility::GONE);  // Hidden by default
    this->addView(m_queueSection);

    m_queueHeader = new brls::Label();
    m_queueHeader->setText("Server Download Queue");
    m_queueHeader->setFontSize(18);
    m_queueHeader->setMargins(0, 0, 10, 0);
    m_queueSection->addView(m_queueHeader);

    // Scrollable queue container with max height
    m_queueScroll = new brls::ScrollingFrame();
    m_queueScroll->setMaxHeight(300);  // Limit height to make it scrollable
    m_queueSection->addView(m_queueScroll);

    m_queueContainer = new brls::Box();
    m_queueContainer->setAxis(brls::Axis::COLUMN);
    m_queueScroll->setContentView(m_queueContainer);

    // Queue empty label (not used when section is hidden, but kept for error states)
    m_queueEmptyLabel = new brls::Label();
    m_queueEmptyLabel->setText("No downloads in queue");
    m_queueEmptyLabel->setFontSize(14);
    m_queueEmptyLabel->setTextColor(nvgRGBA(150, 150, 150, 255));
    m_queueEmptyLabel->setMargins(0, 0, 10, 0);
    m_queueEmptyLabel->setVisibility(brls::Visibility::GONE);

    // === Local Downloads Section ===
    m_localSection = new brls::Box();
    m_localSection->setAxis(brls::Axis::COLUMN);
    m_localSection->setGrow(1.0f);
    m_localSection->setVisibility(brls::Visibility::GONE);  // Hidden by default
    this->addView(m_localSection);

    m_localHeader = new brls::Label();
    m_localHeader->setText("Local Download Queue");
    m_localHeader->setFontSize(18);
    m_localHeader->setMargins(0, 0, 10, 0);
    m_localSection->addView(m_localHeader);

    // Scrollable local container with max height
    m_localScroll = new brls::ScrollingFrame();
    m_localScroll->setMaxHeight(300);  // Limit height to make it scrollable
    m_localSection->addView(m_localScroll);

    m_localContainer = new brls::Box();
    m_localContainer->setAxis(brls::Axis::COLUMN);
    m_localScroll->setContentView(m_localContainer);

    // Local empty label (not used when section is hidden)
    m_localEmptyLabel = new brls::Label();
    m_localEmptyLabel->setText("No downloaded manga.\nUse the download option on manga details to save for offline reading.");
    m_localEmptyLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_localEmptyLabel->setFontSize(14);
    m_localEmptyLabel->setTextColor(nvgRGBA(150, 150, 150, 255));
    m_localEmptyLabel->setVisibility(brls::Visibility::GONE);
}

void DownloadsTab::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);
    refresh();
}

void DownloadsTab::refresh() {
    refreshQueue();
    refreshLocalDownloads();
}

void DownloadsTab::refreshQueue() {
    // Clear existing queue items
    while (m_queueContainer->getChildren().size() > 0) {
        m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
    }

    // Fetch download queue from server
    asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<DownloadQueueItem> queue;

        if (!client.fetchDownloadQueue(queue)) {
            brls::sync([this]() {
                // Hide section on fetch failure (no active downloads to show)
                m_queueSection->setVisibility(brls::Visibility::GONE);
            });
            return;
        }

        brls::sync([this, queue]() {
            // Clear existing items again (in case of race)
            while (m_queueContainer->getChildren().size() > 0) {
                m_queueContainer->removeView(m_queueContainer->getChildren()[0]);
            }

            // Hide section if queue is empty
            if (queue.empty()) {
                m_queueSection->setVisibility(brls::Visibility::GONE);
                return;
            }

            // Show section when there are items
            m_queueSection->setVisibility(brls::Visibility::VISIBLE);

            for (const auto& item : queue) {
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

                // Add swipe gesture for removal
                row->addGestureRecognizer(new brls::PanGestureRecognizer(
                    [this, row, mangaId, chapterId, originalBgColor](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
                        static brls::Point touchStart;
                        static bool isValidSwipe = false;
                        const float SWIPE_THRESHOLD = 60.0f;
                        const float TAP_THRESHOLD = 15.0f;

                        if (status.state == brls::GestureState::START) {
                            touchStart = status.position;
                            isValidSwipe = false;
                        } else if (status.state == brls::GestureState::STAY) {
                            float dx = status.position.x - touchStart.x;
                            float dy = status.position.y - touchStart.y;

                            // Visual feedback during swipe
                            if (std::abs(dx) > std::abs(dy) * 1.5f && std::abs(dx) > TAP_THRESHOLD) {
                                isValidSwipe = true;
                                if (dx < -SWIPE_THRESHOLD * 0.5f) {
                                    // Left swipe - red tint for removal
                                    row->setBackgroundColor(nvgRGBA(231, 76, 60, 100));
                                } else {
                                    row->setBackgroundColor(originalBgColor);
                                }
                            }
                        } else if (status.state == brls::GestureState::END) {
                            row->setBackgroundColor(originalBgColor);

                            float dx = status.position.x - touchStart.x;
                            if (isValidSwipe && dx < -SWIPE_THRESHOLD) {
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
        });
    });
}

void DownloadsTab::refreshLocalDownloads() {
    // Clear existing local items
    while (m_localContainer->getChildren().size() > 0) {
        m_localContainer->removeView(m_localContainer->getChildren()[0]);
    }

    // Ensure manager is initialized and state is loaded
    DownloadsManager& mgr = DownloadsManager::getInstance();
    mgr.init();

    auto downloads = mgr.getDownloads();
    brls::Logger::info("DownloadsTab: Found {} local downloads", downloads.size());

    // Count chapters that are actively downloading or queued
    int activeChapterCount = 0;
    for (const auto& manga : downloads) {
        for (const auto& chapter : manga.chapters) {
            if (chapter.state == LocalDownloadState::QUEUED ||
                chapter.state == LocalDownloadState::DOWNLOADING) {
                activeChapterCount++;
            }
        }
    }

    // Hide section if no active downloads
    if (activeChapterCount == 0) {
        m_localSection->setVisibility(brls::Visibility::GONE);
        return;
    }

    // Show section when there are active downloads
    m_localSection->setVisibility(brls::Visibility::VISIBLE);

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

            // Add swipe gesture for removal and reordering
            row->addGestureRecognizer(new brls::PanGestureRecognizer(
                [this, row, mangaId, chapterIndex, originalBgColor](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
                    static brls::Point touchStart;
                    static bool isValidSwipe = false;
                    const float SWIPE_THRESHOLD = 60.0f;
                    const float VERTICAL_THRESHOLD = 40.0f;
                    const float TAP_THRESHOLD = 15.0f;

                    if (status.state == brls::GestureState::START) {
                        touchStart = status.position;
                        isValidSwipe = false;
                    } else if (status.state == brls::GestureState::STAY) {
                        float dx = status.position.x - touchStart.x;
                        float dy = status.position.y - touchStart.y;

                        // Visual feedback during swipe
                        if (std::abs(dx) > std::abs(dy) * 1.5f && std::abs(dx) > TAP_THRESHOLD) {
                            isValidSwipe = true;
                            if (dx < -SWIPE_THRESHOLD * 0.5f) {
                                // Left swipe - red tint for removal
                                row->setBackgroundColor(nvgRGBA(231, 76, 60, 100));
                            } else {
                                row->setBackgroundColor(originalBgColor);
                            }
                        } else if (std::abs(dy) > std::abs(dx) * 1.5f && std::abs(dy) > TAP_THRESHOLD) {
                            isValidSwipe = true;
                            if (dy < -VERTICAL_THRESHOLD * 0.5f) {
                                // Up swipe - blue tint for move up
                                row->setBackgroundColor(nvgRGBA(52, 152, 219, 100));
                            } else if (dy > VERTICAL_THRESHOLD * 0.5f) {
                                // Down swipe - blue tint for move down
                                row->setBackgroundColor(nvgRGBA(52, 152, 219, 100));
                            } else {
                                row->setBackgroundColor(originalBgColor);
                            }
                        }
                    } else if (status.state == brls::GestureState::END) {
                        row->setBackgroundColor(originalBgColor);

                        float dx = status.position.x - touchStart.x;
                        float dy = status.position.y - touchStart.y;

                        if (isValidSwipe) {
                            if (std::abs(dx) > std::abs(dy) * 1.5f && dx < -SWIPE_THRESHOLD) {
                                // Left swipe confirmed - remove from queue
                                DownloadsManager& mgr = DownloadsManager::getInstance();
                                mgr.cancelChapterDownload(mangaId, chapterIndex);
                                refresh();
                            } else if (std::abs(dy) > std::abs(dx) * 1.5f) {
                                DownloadsManager& mgr = DownloadsManager::getInstance();
                                if (dy < -VERTICAL_THRESHOLD) {
                                    // Up swipe confirmed - move up in queue
                                    if (mgr.moveChapterInQueue(mangaId, chapterIndex, -1)) {
                                        refresh();
                                    }
                                } else if (dy > VERTICAL_THRESHOLD) {
                                    // Down swipe confirmed - move down in queue
                                    if (mgr.moveChapterInQueue(mangaId, chapterIndex, 1)) {
                                        refresh();
                                    }
                                }
                            }
                        }
                    }
                }, brls::PanAxis::ANY));

            // Add row
            m_localContainer->addView(row);
        }
    }
}

void DownloadsTab::showDownloadOptions(const std::string& ratingKey, const std::string& title) {
    // Not implemented - download options are shown from manga detail view
}

} // namespace vitasuwayomi
