/**
 * VitaSuwayomi - Manga Reader Activity implementation
 * NOBORU-style UI with tap to show/hide controls and touch navigation
 */

#include "activity/reader_activity.hpp"
#include "view/rotatable_label.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"
#include "view/webtoon_scroll_view.hpp"

#include <borealis.hpp>
#include <cmath>
#include <chrono>

// Register custom views for XML creation
namespace {
    struct RegisterCustomViews {
        RegisterCustomViews() {
            brls::Application::registerXMLView("WebtoonScrollView", vitasuwayomi::WebtoonScrollView::create);
            brls::Application::registerXMLView("RotatableLabel", vitasuwayomi::RotatableLabel::create);
        }
    };
    static RegisterCustomViews __registerCustomViews;
}

namespace vitasuwayomi {

ReaderActivity::ReaderActivity(int mangaId, int chapterIndex, const std::string& mangaTitle)
    : m_mangaId(mangaId)
    , m_chapterIndex(chapterIndex)
    , m_mangaTitle(mangaTitle)
    , m_startPage(0) {
    brls::Logger::info("ReaderActivity: manga={}, chapter={}", mangaId, chapterIndex);
}

ReaderActivity::ReaderActivity(int mangaId, int chapterIndex, int startPage, const std::string& mangaTitle)
    : m_mangaId(mangaId)
    , m_chapterIndex(chapterIndex)
    , m_mangaTitle(mangaTitle)
    , m_startPage(startPage) {
    brls::Logger::info("ReaderActivity: manga={}, chapter={}, startPage={}",
                       mangaId, chapterIndex, startPage);
}

brls::View* ReaderActivity::createContentView() {
    return brls::View::createFromXMLResource("activity/reader.xml");
}

void ReaderActivity::onContentAvailable() {
    brls::Logger::info("ReaderActivity: content available for manga {}", m_mangaId);

    // Load settings - priority order:
    // 1. Server meta (per-manga settings synced to server)
    // 2. Local per-manga settings cache
    // 3. Global defaults

    AppSettings& appSettings = Application::getInstance().getSettings();

    // Start with global defaults
    ReadingMode readingMode = appSettings.readingMode;
    PageScaleMode pageScaleMode = appSettings.pageScaleMode;
    int imageRotation = appSettings.imageRotation;

    brls::Logger::info("ReaderActivity: global defaults - readingMode={}, pageScaleMode={}, rotation={}",
                       static_cast<int>(readingMode), static_cast<int>(pageScaleMode), imageRotation);
    brls::Logger::info("ReaderActivity: local cache has {} per-manga settings",
                       appSettings.mangaReaderSettings.size());

    // Try to load from server meta first (synchronously for initial display)
    std::map<std::string, std::string> serverMeta;
    bool hasServerSettings = false;

    // Also load webtoon settings
    bool cropBorders = appSettings.cropBorders;
    int webtoonSidePadding = appSettings.webtoonSidePadding;

    brls::Logger::info("ReaderActivity: fetching server meta for manga {}...", m_mangaId);
    if (SuwayomiClient::getInstance().fetchMangaMeta(m_mangaId, serverMeta)) {
        brls::Logger::info("ReaderActivity: server returned {} meta entries", serverMeta.size());
        for (const auto& entry : serverMeta) {
            brls::Logger::info("ReaderActivity: server meta [{}] = {}", entry.first, entry.second);
        }

        // Check for reader settings in server meta
        // Standard keys used by Tachiyomi/Mihon clients
        auto readerModeIt = serverMeta.find("readerMode");
        auto rotationIt = serverMeta.find("rotation");
        auto scaleModeIt = serverMeta.find("scaleType");
        auto cropBordersIt = serverMeta.find("cropBorders");
        auto sidePaddingIt = serverMeta.find("webtoonSidePadding");
        auto isWebtoonIt = serverMeta.find("isWebtoonFormat");

        if (readerModeIt != serverMeta.end()) {
            int mode = std::atoi(readerModeIt->second.c_str());
            if (mode >= 0 && mode <= 3) {
                readingMode = static_cast<ReadingMode>(mode);
                hasServerSettings = true;
            }
        }

        if (rotationIt != serverMeta.end()) {
            imageRotation = std::atoi(rotationIt->second.c_str());
            // Validate rotation
            if (imageRotation != 0 && imageRotation != 90 &&
                imageRotation != 180 && imageRotation != 270) {
                imageRotation = 0;
            }
            hasServerSettings = true;
        }

        if (scaleModeIt != serverMeta.end()) {
            int scale = std::atoi(scaleModeIt->second.c_str());
            if (scale >= 0 && scale <= 3) {
                pageScaleMode = static_cast<PageScaleMode>(scale);
                hasServerSettings = true;
            }
        }

        if (cropBordersIt != serverMeta.end()) {
            cropBorders = (cropBordersIt->second == "true" || cropBordersIt->second == "1");
            hasServerSettings = true;
        }

        if (sidePaddingIt != serverMeta.end()) {
            webtoonSidePadding = std::atoi(sidePaddingIt->second.c_str());
            if (webtoonSidePadding < 0 || webtoonSidePadding > 20) {
                webtoonSidePadding = 0;
            }
            hasServerSettings = true;
        }

        if (isWebtoonIt != serverMeta.end()) {
            m_settings.isWebtoonFormat = (isWebtoonIt->second == "true" || isWebtoonIt->second == "1");
            hasServerSettings = true;
        }

        if (hasServerSettings) {
            brls::Logger::info("ReaderActivity: loaded settings from server for manga {}", m_mangaId);
        }
    }

    // If no server settings, check local per-manga cache
    bool hasLocalSettings = false;
    if (!hasServerSettings) {
        brls::Logger::info("ReaderActivity: no server settings, checking local cache for manga {}", m_mangaId);
        auto it = appSettings.mangaReaderSettings.find(m_mangaId);
        if (it != appSettings.mangaReaderSettings.end()) {
            readingMode = it->second.readingMode;
            pageScaleMode = it->second.pageScaleMode;
            imageRotation = it->second.imageRotation;
            cropBorders = it->second.cropBorders;
            webtoonSidePadding = it->second.webtoonSidePadding;
            m_settings.isWebtoonFormat = it->second.isWebtoonFormat;
            hasLocalSettings = true;
            brls::Logger::info("ReaderActivity: FOUND local settings - readingMode={}, pageScaleMode={}, rotation={}, webtoon={}",
                              static_cast<int>(readingMode), static_cast<int>(pageScaleMode), imageRotation,
                              m_settings.isWebtoonFormat ? "true" : "false");
        } else {
            brls::Logger::info("ReaderActivity: NO local settings found, using global defaults");
        }
    } else {
        brls::Logger::info("ReaderActivity: using server settings (skipping local cache)");
    }

    // Auto-detect webtoon format if enabled and no custom settings exist
    if (!hasServerSettings && !hasLocalSettings && appSettings.webtoonDetection) {
        Manga mangaInfo;
        if (SuwayomiClient::getInstance().fetchManga(m_mangaId, mangaInfo)) {
            if (mangaInfo.isWebtoon()) {
                // Apply webtoon-optimized defaults
                readingMode = ReadingMode::WEBTOON;
                pageScaleMode = PageScaleMode::FIT_WIDTH;
                imageRotation = 0;  // No rotation for webtoons
                m_settings.isWebtoonFormat = true;  // Set webtoon format flag for page splitting
                brls::Logger::info("ReaderActivity: auto-detected webtoon format for '{}', applying webtoon defaults", mangaInfo.title);
            }
        }
    }

    // Map ReadingMode to ReaderDirection
    switch (readingMode) {
        case ReadingMode::LEFT_TO_RIGHT:
            m_settings.direction = ReaderDirection::LEFT_TO_RIGHT;
            break;
        case ReadingMode::RIGHT_TO_LEFT:
            m_settings.direction = ReaderDirection::RIGHT_TO_LEFT;
            break;
        case ReadingMode::VERTICAL:
        case ReadingMode::WEBTOON:
            m_settings.direction = ReaderDirection::TOP_TO_BOTTOM;
            break;
    }

    // Map imageRotation to ImageRotation enum
    switch (imageRotation) {
        case 90:  m_settings.rotation = ImageRotation::ROTATE_90; break;
        case 180: m_settings.rotation = ImageRotation::ROTATE_180; break;
        case 270: m_settings.rotation = ImageRotation::ROTATE_270; break;
        default:  m_settings.rotation = ImageRotation::ROTATE_0; break;
    }

    // Map PageScaleMode to ReaderScaleMode
    switch (pageScaleMode) {
        case PageScaleMode::FIT_SCREEN:
            m_settings.scaleMode = ReaderScaleMode::FIT_SCREEN;
            break;
        case PageScaleMode::FIT_WIDTH:
            m_settings.scaleMode = ReaderScaleMode::FIT_WIDTH;
            break;
        case PageScaleMode::FIT_HEIGHT:
            m_settings.scaleMode = ReaderScaleMode::FIT_HEIGHT;
            break;
        case PageScaleMode::ORIGINAL:
            m_settings.scaleMode = ReaderScaleMode::ORIGINAL;
            break;
    }

    // Set webtoon-specific settings
    m_settings.cropBorders = cropBorders;
    m_settings.webtoonSidePadding = webtoonSidePadding;

    brls::Logger::debug("ReaderActivity: loaded settings - direction={}, rotation={}, scaleMode={}, cropBorders={}",
                        static_cast<int>(m_settings.direction),
                        static_cast<int>(m_settings.rotation),
                        static_cast<int>(m_settings.scaleMode),
                        m_settings.cropBorders);

    // Set manga title in top bar
    if (mangaLabel) {
        mangaLabel->setText(m_mangaTitle);
    }

    // Start with controls hidden, page counter visible
    hideControls();
    showPageCounter();

    // Close reader with Circle button (back)
    this->registerAction("Close", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        brls::Application::popActivity();
        return true;
    });

    // Set up controller input - L/R for chapter navigation only
    this->registerAction("Previous Chapter", brls::ControllerButton::BUTTON_LB, [this](brls::View*) {
        previousChapter();
        return true;
    });

    this->registerAction("Next Chapter", brls::ControllerButton::BUTTON_RB, [this](brls::View*) {
        nextChapter();
        return true;
    });

    // Toggle controls with Y or Start button (NOBORU style)
    this->registerAction("Toggle Controls", brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        toggleControls();
        return true;
    });

    this->registerAction("Menu", brls::ControllerButton::BUTTON_START, [this](brls::View*) {
        toggleControls();
        return true;
    });

    // Set background color based on dark/light mode (shows when page doesn't fill screen)
    updateMarginColors();

    // Hide preview image initially
    if (previewImage) {
        previewImage->setVisibility(brls::Visibility::GONE);
    }

    // NOBORU-style touch handling with swipe controls
    // Features:
    // - Real-time swipe showing next/prev page sliding in
    // - Responsive swipe detection with lower thresholds
    // - Double-tap to zoom in/out
    // - Tap to toggle UI controls
    // - Swipe for page navigation respecting reading direction
    if (pageImage) {
        pageImage->setFocusable(true);

        // Initialize double-tap tracking
        m_lastTapTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        m_lastTapPosition = {0, 0};

        // Tap gesture for instant controls toggle (no hold delay)
        pageImage->addGestureRecognizer(new brls::TapGestureRecognizer(
            [this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
                if (status.state == brls::GestureState::END) {
                    // Check for double-tap
                    auto now = std::chrono::steady_clock::now();
                    auto timeSinceLastTap = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - m_lastTapTime).count();

                    float tapDistance = std::sqrt(
                        std::pow(status.position.x - m_lastTapPosition.x, 2) +
                        std::pow(status.position.y - m_lastTapPosition.y, 2));

                    if (timeSinceLastTap < DOUBLE_TAP_THRESHOLD_MS &&
                        tapDistance < DOUBLE_TAP_DISTANCE) {
                        // Double-tap - zoom toggle
                        handleDoubleTap(status.position);
                        m_lastTapTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                    } else {
                        // Single tap - toggle controls immediately
                        m_lastTapTime = now;
                        m_lastTapPosition = status.position;
                        toggleControls();
                    }
                }
            }));

        // Pan gesture for swipe detection (NOBORU style with page preview)
        // Swipe direction changes based on rotation:
        // - 0°/180°: horizontal swipes (left/right)
        // - 90°/270°: vertical swipes (up/down)
        // - 180°/270°: inverted direction compared to 0°/90°
        pageImage->addGestureRecognizer(new brls::PanGestureRecognizer(
            [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
                const float TAP_THRESHOLD = 15.0f;       // Max movement for tap
                const float PAGE_TURN_THRESHOLD = 80.0f; // Threshold to complete page turn
                const float SWIPE_START_THRESHOLD = 20.0f; // When to start showing preview

                // Determine swipe axis and inversion based on rotation
                bool useVerticalSwipe = (m_settings.rotation == ImageRotation::ROTATE_90 ||
                                         m_settings.rotation == ImageRotation::ROTATE_270);
                bool invertDirection = (m_settings.rotation == ImageRotation::ROTATE_180 ||
                                        m_settings.rotation == ImageRotation::ROTATE_270);

                if (status.state == brls::GestureState::START) {
                    m_isPanning = false;
                    m_isSwipeAnimating = false;
                    m_touchStart = status.position;
                    m_touchCurrent = status.position;
                    m_swipeOffset = 0.0f;
                    m_previewPageIndex = -1;
                } else if (status.state == brls::GestureState::STAY) {
                    // Real-time swipe tracking (STAY = finger still on screen)
                    m_touchCurrent = status.position;
                    float dx = m_touchCurrent.x - m_touchStart.x;
                    float dy = m_touchCurrent.y - m_touchStart.y;

                    // Raw delta follows the finger (for visual feedback)
                    float rawDelta = useVerticalSwipe ? dy : dx;
                    float crossDelta = useVerticalSwipe ? dx : dy;

                    // Logical delta accounts for rotation inversion (for page turn logic)
                    float logicalDelta = invertDirection ? -rawDelta : rawDelta;

                    // Only track swipes in the primary direction
                    if (std::abs(rawDelta) > std::abs(crossDelta) && std::abs(rawDelta) > SWIPE_START_THRESHOLD) {
                        m_isSwipeAnimating = true;
                        m_swipeOffset = rawDelta;  // Store raw for visual

                        // Determine which page we're swiping to based on logical direction
                        bool swipingPositive = logicalDelta > 0;
                        bool wantNextPage = (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) ? swipingPositive : !swipingPositive;

                        int previewIndex = wantNextPage ? m_currentPage + 1 : m_currentPage - 1;

                        // Load preview page if not already loaded
                        if (previewIndex != m_previewPageIndex && previewIndex >= 0 &&
                            previewIndex < static_cast<int>(m_pages.size())) {
                            m_previewPageIndex = previewIndex;
                            m_swipingToNext = wantNextPage;
                            loadPreviewPage(previewIndex);
                        }

                        // Update visual positions - page follows finger (use raw delta)
                        updateSwipePreview(rawDelta);
                    }
                } else if (status.state == brls::GestureState::END) {
                    m_touchCurrent = status.position;

                    // Calculate distance moved
                    float dx = m_touchCurrent.x - m_touchStart.x;
                    float dy = m_touchCurrent.y - m_touchStart.y;
                    float distance = std::sqrt(dx * dx + dy * dy);

                    // Raw delta for threshold checking (how far finger actually moved)
                    float rawDelta = useVerticalSwipe ? dy : dx;

                    if (distance < TAP_THRESHOLD) {
                        // It's a tap - handled by TapGestureRecognizer for instant response
                        resetSwipeState();
                    } else if (m_isSwipeAnimating) {
                        // Complete swipe animation - use raw delta for threshold
                        float absSwipe = std::abs(rawDelta);

                        if (absSwipe >= PAGE_TURN_THRESHOLD && m_previewPageIndex >= 0) {
                            // Swipe was long enough - turn the page
                            completeSwipeAnimation(true);
                        } else {
                            // Swipe too short - snap back
                            completeSwipeAnimation(false);
                        }
                    } else {
                        // Secondary axis swipe (fallback navigation)
                        // Use logical inversion for correct page direction
                        float crossDelta = useVerticalSwipe ? dx : dy;
                        float logicalCross = invertDirection ? -crossDelta : crossDelta;

                        if (std::abs(crossDelta) >= PAGE_TURN_THRESHOLD) {
                            if (logicalCross > 0) {
                                previousPage();
                            } else {
                                nextPage();
                            }
                        }
                        resetSwipeState();
                    }

                    m_isPanning = false;
                }
            }, brls::PanAxis::ANY));
    }

    // Container fallback - simpler touch handling without preview
    if (container) {
        // Tap gesture for instant controls toggle on container
        container->addGestureRecognizer(new brls::TapGestureRecognizer(
            [this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
                if (status.state == brls::GestureState::END) {
                    // Check for double-tap
                    auto now = std::chrono::steady_clock::now();
                    auto timeSinceLastTap = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - m_lastTapTime).count();

                    float tapDistance = std::sqrt(
                        std::pow(status.position.x - m_lastTapPosition.x, 2) +
                        std::pow(status.position.y - m_lastTapPosition.y, 2));

                    if (timeSinceLastTap < DOUBLE_TAP_THRESHOLD_MS &&
                        tapDistance < DOUBLE_TAP_DISTANCE) {
                        handleDoubleTap(status.position);
                        m_lastTapTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                    } else {
                        m_lastTapTime = now;
                        m_lastTapPosition = status.position;
                        toggleControls();
                    }
                }
            }));

        // Pan gesture for swipe navigation on container
        container->addGestureRecognizer(new brls::PanGestureRecognizer(
            [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
                const float PAGE_TURN_THRESHOLD = 80.0f;

                if (status.state == brls::GestureState::START) {
                    m_touchStart = status.position;
                } else if (status.state == brls::GestureState::END) {
                    float dx = status.position.x - m_touchStart.x;
                    float dy = status.position.y - m_touchStart.y;

                    // Only process swipes, not taps (handled by TapGestureRecognizer)
                    if (std::abs(dx) > std::abs(dy) && std::abs(dx) >= PAGE_TURN_THRESHOLD) {
                        if (dx > 0) {
                            m_settings.direction == ReaderDirection::RIGHT_TO_LEFT ? nextPage() : previousPage();
                        } else {
                            m_settings.direction == ReaderDirection::RIGHT_TO_LEFT ? previousPage() : nextPage();
                        }
                    } else if (std::abs(dy) >= PAGE_TURN_THRESHOLD) {
                        dy > 0 ? previousPage() : nextPage();
                    }
                }
            }, brls::PanAxis::ANY));
    }

    // Back button in top bar
    if (backBtn) {
        backBtn->registerClickAction([this](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
        backBtn->addGestureRecognizer(new brls::TapGestureRecognizer(backBtn));
    }

    // Set up page slider
    if (pageSlider) {
        pageSlider->getProgressEvent()->subscribe([this](float progress) {
            if (m_pages.empty()) return;
            int targetPage = static_cast<int>(progress * (m_pages.size() - 1));
            if (targetPage != m_currentPage) {
                goToPage(targetPage);
            }
        });
    }

    // Set up chapter navigation buttons
    if (prevChapterBtn) {
        prevChapterBtn->registerClickAction([this](brls::View*) {
            previousChapter();
            return true;
        });
        prevChapterBtn->addGestureRecognizer(new brls::TapGestureRecognizer(prevChapterBtn));
    }

    if (nextChapterBtn) {
        nextChapterBtn->registerClickAction([this](brls::View*) {
            nextChapter();
            return true;
        });
        nextChapterBtn->addGestureRecognizer(new brls::TapGestureRecognizer(nextChapterBtn));
    }

    // Set up settings button
    if (settingsBtn) {
        settingsBtn->registerClickAction([this](brls::View*) {
            showSettings();
            return true;
        });
        settingsBtn->addGestureRecognizer(new brls::TapGestureRecognizer(settingsBtn));
    }

    // Set up settings overlay panel
    if (settingsOverlay) {
        // Tap on overlay background (outside panel) closes settings
        settingsOverlay->addGestureRecognizer(new brls::TapGestureRecognizer(
            [this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
                if (status.state == brls::GestureState::END) {
                    // Check if tap is outside the settings panel
                    if (settingsPanel) {
                        brls::Rect panelRect = settingsPanel->getFrame();
                        if (status.position.x < panelRect.getMinX() ||
                            status.position.x > panelRect.getMaxX() ||
                            status.position.y < panelRect.getMinY() ||
                            status.position.y > panelRect.getMaxY()) {
                            hideSettings();
                        }
                    } else {
                        hideSettings();
                    }
                }
            }));
    }

    // Settings panel buttons
    if (settingsFormatBtn) {
        settingsFormatBtn->registerClickAction([this](brls::View*) {
            m_settings.isWebtoonFormat = !m_settings.isWebtoonFormat;

            // Apply mode defaults
            if (m_settings.isWebtoonFormat) {
                m_settings.direction = ReaderDirection::TOP_TO_BOTTOM;
                m_settings.scaleMode = ReaderScaleMode::FIT_WIDTH;
            } else {
                m_settings.direction = ReaderDirection::RIGHT_TO_LEFT;
                m_settings.scaleMode = ReaderScaleMode::FIT_SCREEN;
            }

            updateDirectionLabel();
            applySettings();
            saveSettingsToApp();
            updateSettingsLabels();

            // Clear the webtoon scroll view if switching away from webtoon
            if (webtoonScroll && !m_settings.isWebtoonFormat) {
                webtoonScroll->clearPages();
            }

            // Reload pages for page splitting and mode change
            m_pages.clear();
            loadPages();
            return true;
        });
        settingsFormatBtn->addGestureRecognizer(new brls::TapGestureRecognizer(settingsFormatBtn));
    }

    if (settingsDirBtn) {
        settingsDirBtn->registerClickAction([this](brls::View*) {
            // Direction only changes in manga mode (webtoon is locked to vertical)
            if (!m_settings.isWebtoonFormat) {
                if (m_settings.direction == ReaderDirection::LEFT_TO_RIGHT) {
                    m_settings.direction = ReaderDirection::RIGHT_TO_LEFT;
                } else {
                    m_settings.direction = ReaderDirection::LEFT_TO_RIGHT;
                }
                updateDirectionLabel();
                saveSettingsToApp();
                updateSettingsLabels();
            }
            return true;
        });
        settingsDirBtn->addGestureRecognizer(new brls::TapGestureRecognizer(settingsDirBtn));
    }

    if (settingsRotBtn) {
        settingsRotBtn->registerClickAction([this](brls::View*) {
            switch (m_settings.rotation) {
                case ImageRotation::ROTATE_0:
                    m_settings.rotation = ImageRotation::ROTATE_90;
                    break;
                case ImageRotation::ROTATE_90:
                    m_settings.rotation = ImageRotation::ROTATE_180;
                    break;
                case ImageRotation::ROTATE_180:
                    m_settings.rotation = ImageRotation::ROTATE_270;
                    break;
                case ImageRotation::ROTATE_270:
                    m_settings.rotation = ImageRotation::ROTATE_0;
                    break;
            }
            applySettings();
            saveSettingsToApp();
            updateSettingsLabels();
            return true;
        });
        settingsRotBtn->addGestureRecognizer(new brls::TapGestureRecognizer(settingsRotBtn));
    }

    // Update direction label
    updateDirectionLabel();

    // Apply loaded settings (rotation, scaling, etc.)
    applySettings();

    // Load pages asynchronously
    loadPages();
}

void ReaderActivity::loadPages() {
    brls::Logger::debug("Loading pages for chapter {}", m_chapterIndex);

    // Capture webtoon mode flag for async task - use format setting
    bool isWebtoonMode = m_settings.isWebtoonFormat;
    int mangaId = m_mangaId;
    int chapterIndex = m_chapterIndex;

    vitasuwayomi::asyncTask<bool>([this, isWebtoonMode, mangaId, chapterIndex]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        DownloadsManager& localMgr = DownloadsManager::getInstance();
        localMgr.init();

        std::vector<Page> rawPages;
        bool loadedFromLocal = false;

        // First check if chapter is downloaded locally
        if (localMgr.isChapterDownloaded(mangaId, chapterIndex)) {
            brls::Logger::info("ReaderActivity: Chapter {} is downloaded locally, loading from disk", chapterIndex);

            std::vector<std::string> localPaths = localMgr.getChapterPages(mangaId, chapterIndex);
            if (!localPaths.empty()) {
                brls::Logger::info("ReaderActivity: Found {} local pages", localPaths.size());

                // Convert local paths to Page objects
                for (size_t i = 0; i < localPaths.size(); i++) {
                    Page page;
                    page.index = static_cast<int>(i);
                    page.url = localPaths[i];      // Use local path as URL
                    page.imageUrl = localPaths[i]; // Local file path
                    page.segment = 0;
                    page.totalSegments = 1;
                    page.originalIndex = static_cast<int>(i);
                    rawPages.push_back(page);
                }
                loadedFromLocal = true;
            } else {
                brls::Logger::warning("ReaderActivity: Local chapter marked as downloaded but no pages found");
            }
        }

        // Fall back to server if not available locally
        if (!loadedFromLocal) {
            brls::Logger::info("ReaderActivity: Fetching chapter {} from server", chapterIndex);
            if (!client.fetchChapterPages(mangaId, chapterIndex, rawPages)) {
                brls::Logger::error("Failed to fetch chapter pages");
                return false;
            }
        }

        // For webtoon mode, check for tall images that need splitting
        if (isWebtoonMode && !rawPages.empty()) {
            brls::Logger::info("Webtoon mode: checking {} pages for tall images", rawPages.size());
            m_pages.clear();

            for (size_t i = 0; i < rawPages.size(); i++) {
                const Page& rawPage = rawPages[i];
                int width, height, suggestedSegments;

                // Get image dimensions to check if splitting is needed
                if (ImageLoader::getImageDimensions(rawPage.imageUrl, width, height, suggestedSegments) &&
                    suggestedSegments > 1) {
                    // Split this page into multiple segments
                    brls::Logger::info("Page {} ({}x{}) split into {} segments",
                                       i, width, height, suggestedSegments);

                    for (int seg = 0; seg < suggestedSegments; seg++) {
                        Page segmentPage;
                        segmentPage.index = static_cast<int>(m_pages.size());
                        segmentPage.url = rawPage.url;
                        segmentPage.imageUrl = rawPage.imageUrl;
                        segmentPage.segment = seg;
                        segmentPage.totalSegments = suggestedSegments;
                        segmentPage.originalIndex = static_cast<int>(i);
                        m_pages.push_back(segmentPage);
                    }
                } else {
                    // Single page (no splitting needed)
                    Page singlePage = rawPage;
                    singlePage.index = static_cast<int>(m_pages.size());
                    singlePage.segment = 0;
                    singlePage.totalSegments = 1;
                    singlePage.originalIndex = static_cast<int>(i);
                    m_pages.push_back(singlePage);
                }
            }

            brls::Logger::info("Webtoon mode: {} raw pages expanded to {} virtual pages",
                               rawPages.size(), m_pages.size());
        } else {
            // Normal mode - use pages as-is
            m_pages = std::move(rawPages);
        }

        // Also fetch chapter details for the name
        Chapter chapter;
        if (client.fetchChapter(mangaId, chapterIndex, chapter)) {
            m_chapterName = chapter.name;
        }

        // Fetch all chapters for navigation
        client.fetchChapters(mangaId, m_chapters);
        m_totalChapters = static_cast<int>(m_chapters.size());

        return true;
    }, [this, isWebtoonMode](bool success) {
        if (!success || m_pages.empty()) {
            brls::Logger::error("No pages to display");

            if (chapterLabel) {
                chapterLabel->setText("Failed to load chapter");
            }
            return;
        }

        brls::Logger::info("Loaded {} pages", m_pages.size());

        // Update UI labels
        if (chapterLabel) {
            std::string label = m_chapterName.empty() ?
                "Chapter " + std::to_string(m_chapterIndex + 1) : m_chapterName;
            chapterLabel->setText(label);
        }

        if (chapterProgress) {
            chapterProgress->setText("Ch. " + std::to_string(m_chapterIndex + 1) +
                                     " of " + std::to_string(m_totalChapters));
        }

        // Start from saved position or beginning
        m_currentPage = std::min(m_startPage, static_cast<int>(m_pages.size()) - 1);
        m_currentPage = std::max(0, m_currentPage);

        // Check if we should use continuous scroll mode for webtoon
        if (isWebtoonMode) {
            m_continuousScrollMode = true;

            // Hide single-page views, show scroll view
            if (pageImage) {
                pageImage->setVisibility(brls::Visibility::GONE);
            }
            if (previewImage) {
                previewImage->setVisibility(brls::Visibility::GONE);
            }
            if (webtoonScroll) {
                webtoonScroll->setVisibility(brls::Visibility::VISIBLE);
                webtoonScroll->setSidePadding(m_settings.webtoonSidePadding);
                webtoonScroll->setRotation(static_cast<float>(m_settings.rotation));

                // Set up progress callback
                webtoonScroll->setProgressCallback([this](int currentPage, int totalPages, float scrollPercent) {
                    m_currentPage = currentPage;
                    updatePageDisplay();
                    updateProgress();
                });

                // Set up tap callback to toggle controls
                webtoonScroll->setTapCallback([this]() {
                    toggleControls();
                });

                // Load all pages into the scroll view
                webtoonScroll->setPages(m_pages, 960.0f);  // PS Vita screen width
                webtoonScroll->scrollToPage(m_currentPage);
            }
        } else {
            m_continuousScrollMode = false;

            // Use single-page mode
            if (webtoonScroll) {
                webtoonScroll->setVisibility(brls::Visibility::GONE);
            }
            if (pageImage) {
                pageImage->setVisibility(brls::Visibility::VISIBLE);
            }

            loadPage(m_currentPage);
        }

        updatePageDisplay();
    });
}

void ReaderActivity::loadPage(int index) {
    if (index < 0 || index >= static_cast<int>(m_pages.size())) {
        return;
    }

    const Page& page = m_pages[index];
    std::string imageUrl = page.imageUrl;

    if (page.totalSegments > 1) {
        brls::Logger::debug("Loading page {} segment {}/{} from: {}",
                           index, page.segment + 1, page.totalSegments, imageUrl);
    } else {
        brls::Logger::debug("Loading page {} from: {}", index, imageUrl);
    }

    // Show page counter when navigating
    showPageCounter();
    schedulePageCounterHide();

    // Load image using RotatableImage
    if (pageImage) {
        int currentPageAtLoad = m_currentPage;

        if (page.totalSegments > 1) {
            // Load specific segment for webtoon splitting
            ImageLoader::loadAsyncFullSizeSegment(
                imageUrl, page.segment, page.totalSegments,
                [this, index, currentPageAtLoad](RotatableImage* img) {
                    if (index == currentPageAtLoad) {
                        brls::Logger::debug("ReaderActivity: Page {} (segment) loaded", index);
                    }
                }, pageImage);
        } else {
            // Load full image normally
            ImageLoader::loadAsyncFullSize(imageUrl, [this, index, currentPageAtLoad](RotatableImage* img) {
                if (index == currentPageAtLoad) {
                    brls::Logger::debug("ReaderActivity: Page {} loaded", index);
                }
            }, pageImage);
        }
    }

    // Preload adjacent pages
    preloadAdjacentPages();
}

void ReaderActivity::preloadAdjacentPages() {
    // Preload next 3 pages for smoother swiping/reading
    for (int i = 1; i <= 3; i++) {
        int nextIdx = m_currentPage + i;
        if (nextIdx < static_cast<int>(m_pages.size())) {
            // Note: For segmented pages, preloadFullSize loads the full image which will be
            // segmented on demand. This is acceptable since the raw image needs to be fetched anyway.
            ImageLoader::preloadFullSize(m_pages[nextIdx].imageUrl);
        }
    }

    // Preload previous page (for going back)
    if (m_currentPage > 0) {
        ImageLoader::preloadFullSize(m_pages[m_currentPage - 1].imageUrl);
    }
}

void ReaderActivity::updatePageDisplay() {
    // Update page counter (top-right overlay with rotation support)
    if (pageCounter) {
        const Page& page = m_pages[m_currentPage];
        if (page.totalSegments > 1) {
            // Show segment info: "Page-Segment/Total" e.g. "1-2/10"
            pageCounter->setText(std::to_string(page.originalIndex + 1) + "-" +
                              std::to_string(page.segment + 1) + "/" +
                              std::to_string(m_pages.size()));
        } else {
            pageCounter->setText(std::to_string(m_currentPage + 1) + "/" +
                              std::to_string(m_pages.size()));
        }
    }

    // Update slider page label (in bottom bar)
    if (sliderPageLabel) {
        const Page& page = m_pages[m_currentPage];
        if (page.totalSegments > 1) {
            // Show more detail: "Page X-Y of Z"
            sliderPageLabel->setText("Page " + std::to_string(page.originalIndex + 1) + "-" +
                                     std::to_string(page.segment + 1) +
                                     " of " + std::to_string(m_pages.size()));
        } else {
            sliderPageLabel->setText("Page " + std::to_string(m_currentPage + 1) +
                                     " of " + std::to_string(m_pages.size()));
        }
    }

    // Update slider position
    if (pageSlider && !m_pages.empty()) {
        float progress = static_cast<float>(m_currentPage) /
                        static_cast<float>(std::max(1, static_cast<int>(m_pages.size()) - 1));
        pageSlider->setProgress(progress);
    }
}

void ReaderActivity::updateDirectionLabel() {
    if (directionLabel) {
        switch (m_settings.direction) {
            case ReaderDirection::LEFT_TO_RIGHT:
                directionLabel->setText("LTR");
                break;
            case ReaderDirection::RIGHT_TO_LEFT:
                directionLabel->setText("RTL");
                break;
            case ReaderDirection::TOP_TO_BOTTOM:
                directionLabel->setText("TTB");
                break;
        }
    }
}

void ReaderActivity::updateProgress() {
    // Save reading progress to server
    vitasuwayomi::asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        client.updateChapterProgress(m_mangaId, m_chapterIndex, m_currentPage);
    });
}

void ReaderActivity::nextPage() {
    if (m_currentPage < static_cast<int>(m_pages.size()) - 1) {
        m_currentPage++;
        updatePageDisplay();
        loadPage(m_currentPage);
        updateProgress();
    } else {
        // End of chapter - mark as read and go to next automatically
        markChapterAsRead();

        if (m_chapterIndex < m_totalChapters - 1) {
            nextChapter();
        } else {
            brls::Application::notify("End of manga");
        }
    }
}

void ReaderActivity::previousPage() {
    if (m_currentPage > 0) {
        m_currentPage--;
        updatePageDisplay();
        loadPage(m_currentPage);
        updateProgress();
    } else if (m_chapterIndex > 0) {
        // Beginning of chapter - go to previous chapter automatically
        previousChapter();
    }
}

void ReaderActivity::goToPage(int pageIndex) {
    if (pageIndex >= 0 && pageIndex < static_cast<int>(m_pages.size())) {
        m_currentPage = pageIndex;
        updatePageDisplay();
        loadPage(m_currentPage);
        updateProgress();
    }
}

void ReaderActivity::nextChapter() {
    if (m_chapterIndex < m_totalChapters - 1) {
        markChapterAsRead();

        m_chapterIndex++;
        m_currentPage = 0;
        m_cachedImages.clear();

        // Use preloaded pages if available for instant transition
        if (m_nextChapterLoaded && !m_nextChapterPages.empty()) {
            m_pages = std::move(m_nextChapterPages);
            m_nextChapterLoaded = false;
            m_nextChapterPages.clear();

            // Update UI
            if (chapterLabel) {
                chapterLabel->setText("Chapter " + std::to_string(m_chapterIndex + 1));
            }
            if (chapterProgress) {
                chapterProgress->setText("Ch. " + std::to_string(m_chapterIndex + 1) +
                                         " of " + std::to_string(m_totalChapters));
            }

            // Handle webtoon mode vs single-page mode
            if (m_continuousScrollMode && webtoonScroll) {
                // Update webtoon scroll view with new pages and scroll to beginning
                webtoonScroll->setPages(m_pages, 960.0f);  // PS Vita screen width
                webtoonScroll->scrollToPage(0);
            } else {
                loadPage(m_currentPage);
            }

            updatePageDisplay();

            // Start preloading next chapter
            preloadNextChapter();
        } else {
            m_pages.clear();
            loadPages();
        }
    }
}

void ReaderActivity::previousChapter() {
    if (m_chapterIndex > 0) {
        m_chapterIndex--;
        // Reset preloaded chapter since we're going backwards
        m_nextChapterLoaded = false;
        m_nextChapterPages.clear();
        m_currentPage = 0;
        m_pages.clear();
        m_cachedImages.clear();

        // Clear webtoon scroll view to reset scroll position for new chapter
        if (m_continuousScrollMode && webtoonScroll) {
            webtoonScroll->clearPages();
        }

        loadPages();
    }
}

void ReaderActivity::markChapterAsRead() {
    int mangaId = m_mangaId;
    int chapterIndex = m_chapterIndex;
    int totalChapters = m_totalChapters;

    vitasuwayomi::asyncRun([mangaId, chapterIndex, totalChapters]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        if (client.markChapterRead(mangaId, chapterIndex)) {
            // Update reading statistics on the main thread
            brls::sync([chapterIndex, totalChapters]() {
                // Check if this was the last unread chapter (manga completed)
                bool mangaCompleted = (chapterIndex == totalChapters - 1);
                Application::getInstance().updateReadingStatistics(true, mangaCompleted);
            });
        }
    });
}

void ReaderActivity::toggleControls() {
    m_controlsVisible = !m_controlsVisible;

    if (m_controlsVisible) {
        showControls();
    } else {
        hideControls();
    }
}

void ReaderActivity::showControls() {
    // Instant show - no animation delay
    if (topBar) {
        topBar->setAlpha(1.0f);
        topBar->setVisibility(brls::Visibility::VISIBLE);
    }
    if (bottomBar) {
        bottomBar->setAlpha(1.0f);
        bottomBar->setVisibility(brls::Visibility::VISIBLE);
    }
    // Hide page counter when controls are visible (it's redundant)
    hidePageCounter();
    m_controlsVisible = true;
}

void ReaderActivity::hideControls() {
    // Instant hide - no animation delay
    if (topBar) {
        topBar->setAlpha(0.0f);
        topBar->setVisibility(brls::Visibility::GONE);
    }
    if (bottomBar) {
        bottomBar->setAlpha(0.0f);
        bottomBar->setVisibility(brls::Visibility::GONE);
    }
    // Show page counter when controls are hidden
    showPageCounter();
    m_controlsVisible = false;
}

void ReaderActivity::showPageCounter() {
    if (pageCounter) {
        pageCounter->setAlpha(1.0f);
        pageCounter->setVisibility(brls::Visibility::VISIBLE);
    }
}

void ReaderActivity::hidePageCounter() {
    if (pageCounter) {
        pageCounter->setAlpha(0.0f);
        pageCounter->setVisibility(brls::Visibility::GONE);
    }
}

void ReaderActivity::schedulePageCounterHide() {
    // Auto-hide page counter after 1.5 seconds (like NOBORU)
    // Note: In a real implementation, you'd use a timer/delayed callback
    // For now, the page counter stays visible until controls are shown
}

void ReaderActivity::updatePageCounterRotation() {
    if (!pageCounter) return;

    // Get rotation in degrees
    float rotation = static_cast<float>(m_settings.rotation);

    // Apply rotation to the RotatableLabel (text will rotate with it)
    pageCounter->setRotation(rotation);

    // Keep counter fixed at physical top-right corner of screen
    // Position is set in XML (positionTop=10, positionRight=10)
    // No translation needed - counter stays in place, only text rotates
    pageCounter->setTranslationX(0.0f);
    pageCounter->setTranslationY(0.0f);

    brls::Logger::debug("ReaderActivity: Page counter rotation set to {}°", static_cast<int>(rotation));
}

void ReaderActivity::showSettings() {
    if (m_settingsVisible) return;

    // Update labels before showing
    updateSettingsLabels();

    // Show the overlay
    if (settingsOverlay) {
        settingsOverlay->setVisibility(brls::Visibility::VISIBLE);
        m_settingsVisible = true;

        // Register circle button to close settings while overlay is visible
        this->registerAction("Close Settings", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
            hideSettings();
            return true;
        });
    }
}

void ReaderActivity::hideSettings() {
    if (!m_settingsVisible) return;

    if (settingsOverlay) {
        settingsOverlay->setVisibility(brls::Visibility::GONE);
        m_settingsVisible = false;

        // Restore normal circle button behavior (close reader)
        this->registerAction("Close", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
    }
}

void ReaderActivity::updateSettingsLabels() {
    // Update format label
    if (settingsFormatLabel) {
        settingsFormatLabel->setText(m_settings.isWebtoonFormat ? "Webtoon" : "Manga");
    }

    // Update direction label
    if (settingsDirLabel) {
        if (m_settings.isWebtoonFormat) {
            settingsDirLabel->setText("Vertical (locked)");
        } else {
            settingsDirLabel->setText(
                m_settings.direction == ReaderDirection::LEFT_TO_RIGHT
                    ? "Left to Right" : "Right to Left");
        }
    }

    // Update rotation label
    if (settingsRotLabel) {
        std::string rotText;
        switch (m_settings.rotation) {
            case ImageRotation::ROTATE_0: rotText = "0\u00B0"; break;
            case ImageRotation::ROTATE_90: rotText = "90\u00B0"; break;
            case ImageRotation::ROTATE_180: rotText = "180\u00B0"; break;
            case ImageRotation::ROTATE_270: rotText = "270\u00B0"; break;
        }
        settingsRotLabel->setText(rotText);
    }
}

void ReaderActivity::applySettings() {
    if (!pageImage) return;

    // Determine scaling type based on mode
    brls::ImageScalingType scalingType = brls::ImageScalingType::FIT;
    switch (m_settings.scaleMode) {
        case ReaderScaleMode::FIT_SCREEN:
        case ReaderScaleMode::FIT_WIDTH:
        case ReaderScaleMode::FIT_HEIGHT:
            scalingType = brls::ImageScalingType::FIT;
            break;
        case ReaderScaleMode::ORIGINAL:
            // FILL maintains aspect ratio but may crop - closest to original
            scalingType = brls::ImageScalingType::FILL;
            break;
    }

    // Apply scaling to both main and preview images
    pageImage->setScalingType(scalingType);
    if (previewImage) {
        previewImage->setScalingType(scalingType);
    }

    // Apply rotation to both main and preview images
    float rotation = static_cast<float>(m_settings.rotation);
    pageImage->setRotation(rotation);
    if (previewImage) {
        previewImage->setRotation(rotation);
    }

    // Apply rotation to webtoon scroll view if in continuous mode
    if (webtoonScroll) {
        webtoonScroll->setRotation(rotation);
    }

    // Update page counter position to stay upright relative to content
    updatePageCounterRotation();
}

void ReaderActivity::saveSettingsToApp() {
    AppSettings& appSettings = Application::getInstance().getSettings();

    // Save per-manga settings (not global defaults)
    MangaReaderSettings mangaSettings;

    // Map ReaderDirection to ReadingMode
    switch (m_settings.direction) {
        case ReaderDirection::LEFT_TO_RIGHT:
            mangaSettings.readingMode = ReadingMode::LEFT_TO_RIGHT;
            break;
        case ReaderDirection::RIGHT_TO_LEFT:
            mangaSettings.readingMode = ReadingMode::RIGHT_TO_LEFT;
            break;
        case ReaderDirection::TOP_TO_BOTTOM:
            mangaSettings.readingMode = ReadingMode::VERTICAL;
            break;
    }

    // Map ImageRotation to imageRotation int
    mangaSettings.imageRotation = static_cast<int>(m_settings.rotation);

    // Map ReaderScaleMode to PageScaleMode
    switch (m_settings.scaleMode) {
        case ReaderScaleMode::FIT_SCREEN:
            mangaSettings.pageScaleMode = PageScaleMode::FIT_SCREEN;
            break;
        case ReaderScaleMode::FIT_WIDTH:
            mangaSettings.pageScaleMode = PageScaleMode::FIT_WIDTH;
            break;
        case ReaderScaleMode::FIT_HEIGHT:
            mangaSettings.pageScaleMode = PageScaleMode::FIT_HEIGHT;
            break;
        case ReaderScaleMode::ORIGINAL:
            mangaSettings.pageScaleMode = PageScaleMode::ORIGINAL;
            break;
    }

    // Webtoon settings
    mangaSettings.cropBorders = m_settings.cropBorders;
    mangaSettings.webtoonSidePadding = m_settings.webtoonSidePadding;
    mangaSettings.isWebtoonFormat = m_settings.isWebtoonFormat;

    // Store per-manga settings locally (overrides defaults for this manga)
    appSettings.mangaReaderSettings[m_mangaId] = mangaSettings;

    brls::Logger::info("ReaderActivity: SAVING settings for manga {} - readingMode={}, pageScaleMode={}, rotation={}, webtoon={}",
                       m_mangaId, static_cast<int>(mangaSettings.readingMode),
                       static_cast<int>(mangaSettings.pageScaleMode), mangaSettings.imageRotation,
                       mangaSettings.isWebtoonFormat ? "true" : "false");
    brls::Logger::info("ReaderActivity: local cache now has {} per-manga settings",
                       appSettings.mangaReaderSettings.size());

    // Save to local disk
    bool saveResult = Application::getInstance().saveSettings();
    brls::Logger::info("ReaderActivity: saveSettings returned {}", saveResult ? "true" : "false");

    // Save to server asynchronously (using standard Tachiyomi/Mihon meta keys)
    int mangaId = m_mangaId;
    int readerMode = static_cast<int>(mangaSettings.readingMode);
    int rotation = mangaSettings.imageRotation;
    int scaleType = static_cast<int>(mangaSettings.pageScaleMode);
    bool cropBorders = mangaSettings.cropBorders;
    int webtoonSidePadding = mangaSettings.webtoonSidePadding;
    bool isWebtoonFormat = mangaSettings.isWebtoonFormat;

    vitasuwayomi::asyncTask<bool>([mangaId, readerMode, rotation, scaleType, cropBorders, webtoonSidePadding, isWebtoonFormat]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        // Save reader mode
        client.setMangaMeta(mangaId, "readerMode", std::to_string(readerMode));

        // Save rotation
        client.setMangaMeta(mangaId, "rotation", std::to_string(rotation));

        // Save scale type
        client.setMangaMeta(mangaId, "scaleType", std::to_string(scaleType));

        // Save webtoon settings
        client.setMangaMeta(mangaId, "cropBorders", cropBorders ? "true" : "false");
        client.setMangaMeta(mangaId, "webtoonSidePadding", std::to_string(webtoonSidePadding));
        client.setMangaMeta(mangaId, "isWebtoonFormat", isWebtoonFormat ? "true" : "false");

        return true;
    }, [mangaId](bool success) {
        if (success) {
            brls::Logger::info("ReaderActivity: saved settings to server for manga {}", mangaId);
        } else {
            brls::Logger::warning("ReaderActivity: failed to save settings to server for manga {}", mangaId);
        }
    });
}

void ReaderActivity::handleTouch(brls::Point point) {
    // Reserved for advanced touch handling (pinch-to-zoom, etc.)
}

void ReaderActivity::handleTouchNavigation(float x, float screenWidth) {
    // Touch navigation via tap zones is disabled
    // Tapping anywhere now only toggles controls
    // Page navigation is done via swipe gestures or controller buttons
    toggleControls();
}

void ReaderActivity::handleSwipe(brls::Point delta) {
    // Legacy function - swipe handling is now inline in gesture recognizers
    // Kept for compatibility with header declaration
}

void ReaderActivity::handleDoubleTap(brls::Point position) {
    if (m_isZoomed) {
        // Zoomed in - reset to normal view
        resetZoom();
    } else {
        // Not zoomed - zoom in to 2x centered on tap position
        zoomTo(2.0f, position);
    }
}

void ReaderActivity::resetZoom() {
    m_isZoomed = false;
    m_zoomLevel = 1.0f;
    m_zoomOffset = {0, 0};

    if (pageImage) {
        // Reset to fit scaling - maintains aspect ratio, shows margins
        pageImage->setScalingType(brls::ImageScalingType::FIT);
    }
}

void ReaderActivity::zoomTo(float level, brls::Point center) {
    m_isZoomed = true;
    m_zoomLevel = level;

    if (pageImage) {
        // Use FILL scaling for zoomed view
        pageImage->setScalingType(brls::ImageScalingType::FILL);

        std::string zoomText = "Zoom: " + std::to_string(static_cast<int>(level * 100)) + "%";
        brls::Application::notify(zoomText);
    }
}

void ReaderActivity::handlePinchZoom(float scaleFactor) {
    // Pinch-to-zoom - scale by the pinch factor
    float newZoom = m_zoomLevel * scaleFactor;

    // Clamp zoom between 0.5x and 4x
    newZoom = std::max(0.5f, std::min(4.0f, newZoom));

    if (newZoom != m_zoomLevel) {
        if (newZoom <= 1.05f && newZoom >= 0.95f) {
            // Near 1x - snap to fit
            resetZoom();
        } else {
            m_zoomLevel = newZoom;
            m_isZoomed = (newZoom > 1.0f);

            if (pageImage) {
                if (m_isZoomed) {
                    pageImage->setScalingType(brls::ImageScalingType::FILL);
                } else {
                    pageImage->setScalingType(brls::ImageScalingType::FIT);
                }
            }
        }
    }
}

// NOBORU-style swipe methods

void ReaderActivity::updateSwipePreview(float offset) {
    // Update visual positions during swipe - PUSH EFFECT
    // Current page is pushed off screen, preview page follows behind pushing it
    const float SCREEN_WIDTH = 960.0f;
    const float SCREEN_HEIGHT = 544.0f;

    if (!pageImage || !previewImage) return;

    // Show preview image
    previewImage->setVisibility(brls::Visibility::VISIBLE);

    // Determine if we should use vertical swipe based on rotation
    bool useVerticalSwipe = (m_settings.rotation == ImageRotation::ROTATE_90 ||
                             m_settings.rotation == ImageRotation::ROTATE_270);

    if (useVerticalSwipe) {
        // Vertical swipe for 90/270 rotation
        // Clamp offset to screen height
        offset = std::max(-SCREEN_HEIGHT, std::min(SCREEN_HEIGHT, offset));

        // Move current page with finger (vertically)
        pageImage->setTranslationY(offset);
        pageImage->setTranslationX(0.0f);

        // PUSH EFFECT: Preview page is always one screen behind current page
        // When current page is pushed off, preview slides in from behind
        if (offset > 0) {
            // Swiping down - preview pushes from top (above current page)
            previewImage->setTranslationY(offset - SCREEN_HEIGHT);
        } else {
            // Swiping up - preview pushes from bottom (below current page)
            previewImage->setTranslationY(offset + SCREEN_HEIGHT);
        }
        previewImage->setTranslationX(0.0f);
    } else {
        // Horizontal swipe for 0/180 rotation
        // Clamp offset to screen width
        offset = std::max(-SCREEN_WIDTH, std::min(SCREEN_WIDTH, offset));

        // Move current page with finger (horizontally)
        pageImage->setTranslationX(offset);
        pageImage->setTranslationY(0.0f);

        // PUSH EFFECT: Preview page is always one screen behind current page
        // When current page is pushed off, preview slides in from behind
        if (offset > 0) {
            // Swiping right - preview pushes from left (behind current page)
            previewImage->setTranslationX(offset - SCREEN_WIDTH);
        } else {
            // Swiping left - preview pushes from right (behind current page)
            previewImage->setTranslationX(offset + SCREEN_WIDTH);
        }
        previewImage->setTranslationY(0.0f);
    }
}

void ReaderActivity::loadPreviewPage(int index) {
    if (index < 0 || index >= static_cast<int>(m_pages.size())) {
        return;
    }

    if (!previewImage) return;

    // Apply current rotation to preview image before loading
    previewImage->setRotation(static_cast<float>(m_settings.rotation));

    const Page& page = m_pages[index];
    std::string imageUrl = page.imageUrl;

    if (page.totalSegments > 1) {
        brls::Logger::debug("Loading preview page {} segment {}/{}",
                           index, page.segment + 1, page.totalSegments);

        // Load specific segment for webtoon splitting
        ImageLoader::loadAsyncFullSizeSegment(
            imageUrl, page.segment, page.totalSegments,
            [this, index](RotatableImage* img) {
                brls::Logger::debug("Preview page {} (segment) loaded", index);
            }, previewImage);
    } else {
        brls::Logger::debug("Loading preview page {}", index);

        // Load the preview image (full size for manga reader)
        ImageLoader::loadAsyncFullSize(imageUrl, [this, index](RotatableImage* img) {
            brls::Logger::debug("Preview page {} loaded", index);
        }, previewImage);
    }
}

void ReaderActivity::completeSwipeAnimation(bool turnPage) {
    if (turnPage && m_previewPageIndex >= 0) {
        // Turn to the preview page
        m_currentPage = m_previewPageIndex;
        updatePageDisplay();
        loadPage(m_currentPage);
        updateProgress();

        // Preload next chapter if near end
        if (m_currentPage >= static_cast<int>(m_pages.size()) - 3) {
            preloadNextChapter();
        }
    }

    // Reset positions
    resetSwipeState();
}

void ReaderActivity::resetSwipeState() {
    m_isSwipeAnimating = false;
    m_swipeOffset = 0.0f;
    m_previewPageIndex = -1;

    // Reset page positions (both X and Y)
    if (pageImage) {
        pageImage->setTranslationX(0.0f);
        pageImage->setTranslationY(0.0f);
    }

    // Hide preview image
    if (previewImage) {
        previewImage->setVisibility(brls::Visibility::GONE);
        previewImage->setTranslationX(0.0f);
        previewImage->setTranslationY(0.0f);
    }
}

void ReaderActivity::updateMarginColors() {
    // Set background color based on dark/light mode
    // This shows when the manga page doesn't fill the screen
    NVGcolor bgColor;
    if (m_isDarkMode) {
        // Dark mode - dark gray background
        bgColor = nvgRGBA(26, 26, 46, 255);  // #1a1a2e
    } else {
        // Light mode - light gray/white background
        bgColor = nvgRGBA(240, 240, 245, 255);  // #f0f0f5
    }

    // Set container background
    if (container) {
        container->setBackgroundColor(bgColor);
    }

    // Set image background colors (prevents edge artifacts)
    if (pageImage) {
        pageImage->setBackgroundFillColor(bgColor);
    }
    if (previewImage) {
        previewImage->setBackgroundFillColor(bgColor);
    }
}

void ReaderActivity::preloadNextChapter() {
    // Preload next chapter pages for seamless transition
    if (m_nextChapterLoaded || m_chapterIndex >= m_totalChapters - 1) {
        return;  // Already loaded or no next chapter
    }

    brls::Logger::info("Preloading next chapter {}", m_chapterIndex + 1);

    vitasuwayomi::asyncTask<bool>([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        return client.fetchChapterPages(m_mangaId, m_chapterIndex + 1, m_nextChapterPages);
    }, [this](bool success) {
        if (success && !m_nextChapterPages.empty()) {
            m_nextChapterLoaded = true;
            brls::Logger::info("Next chapter preloaded: {} pages", m_nextChapterPages.size());

            // Preload first few images of next chapter (full size for manga reader)
            for (size_t i = 0; i < std::min(size_t(3), m_nextChapterPages.size()); i++) {
                ImageLoader::preloadFullSize(m_nextChapterPages[i].imageUrl);
            }
        }
    });
}

void ReaderActivity::updateReaderMode() {
    // Determine if we should use continuous scroll mode
    // Continuous scroll is used for Webtoon format
    bool shouldUseContinuousScroll = m_settings.isWebtoonFormat;

    if (shouldUseContinuousScroll == m_continuousScrollMode) {
        return;  // Mode hasn't changed
    }

    m_continuousScrollMode = shouldUseContinuousScroll;

    brls::Logger::info("ReaderActivity: Switching to {} mode",
                       m_continuousScrollMode ? "continuous scroll" : "single page");

    if (m_continuousScrollMode) {
        // Switch to continuous scroll (WebtoonScrollView)
        if (pageImage) {
            pageImage->setVisibility(brls::Visibility::GONE);
        }
        if (previewImage) {
            previewImage->setVisibility(brls::Visibility::GONE);
        }
        if (webtoonScroll) {
            webtoonScroll->setVisibility(brls::Visibility::VISIBLE);
            webtoonScroll->setSidePadding(m_settings.webtoonSidePadding);
            webtoonScroll->setRotation(static_cast<float>(m_settings.rotation));

            // Set up progress callback
            webtoonScroll->setProgressCallback([this](int currentPage, int totalPages, float scrollPercent) {
                m_currentPage = currentPage;
                updatePageDisplay();
                updateProgress();
            });

            // Set up tap callback to toggle controls
            webtoonScroll->setTapCallback([this]() {
                toggleControls();
            });

            // Load pages into the scroll view
            if (!m_pages.empty()) {
                webtoonScroll->setPages(m_pages, 960.0f);  // PS Vita screen width
                webtoonScroll->scrollToPage(m_currentPage);
            }
        }
    } else {
        // Switch to single page mode
        if (webtoonScroll) {
            webtoonScroll->setVisibility(brls::Visibility::GONE);
            webtoonScroll->clearPages();
        }
        if (pageImage) {
            pageImage->setVisibility(brls::Visibility::VISIBLE);
            loadPage(m_currentPage);
        }
    }
}

} // namespace vitasuwayomi
