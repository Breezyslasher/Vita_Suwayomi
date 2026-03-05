/**
 * VitaSuwayomi - Manga Reader Activity implementation
 * NOBORU-style UI with tap to show/hide controls and touch navigation
 */

#include "activity/reader_activity.hpp"
#include "view/pinch_gesture.hpp"
#include "view/rotatable_label.hpp"
#include "view/rotatable_box.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"
#include "view/webtoon_scroll_view.hpp"

#include <borealis.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>

// Register custom views for XML creation
namespace {
    struct RegisterCustomViews {
        RegisterCustomViews() {
            brls::Application::registerXMLView("WebtoonScrollView", vitasuwayomi::WebtoonScrollView::create);
            brls::Application::registerXMLView("RotatableLabel", vitasuwayomi::RotatableLabel::create);
            brls::Application::registerXMLView("RotatableBox", vitasuwayomi::RotatableBox::create);
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

void ReaderActivity::findChapterPosition() {
    m_chapterPosition = -1;
    for (int i = 0; i < static_cast<int>(m_chapters.size()); i++) {
        if (m_chapters[i].id == m_chapterIndex) {
            m_chapterPosition = i;
            break;
        }
    }
}

std::string ReaderActivity::getChapterDisplayNumber() const {
    if (m_chapterPosition >= 0 && m_chapterPosition < static_cast<int>(m_chapters.size())) {
        float num = m_chapters[m_chapterPosition].chapterNumber;
        // Show as integer if whole number (e.g. "5" not "5.0")
        if (num == static_cast<int>(num)) {
            return std::to_string(static_cast<int>(num));
        }
        // Show one decimal for half-chapters (e.g. "5.5")
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", num);
        return buf;
    }
    // Fallback if chapter not found in list
    return std::to_string(m_chapterIndex);
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
    // Skip when offline to avoid pointless connection errors
    std::map<std::string, std::string> serverMeta;
    bool hasServerSettings = false;

    // Also load webtoon settings
    bool cropBorders = appSettings.cropBorders;
    int webtoonSidePadding = appSettings.webtoonSidePadding;

    if (Application::getInstance().isConnected()) {
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
    } else {
        brls::Logger::info("ReaderActivity: offline, skipping server meta fetch");
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
    // Skip when offline to avoid connection errors
    bool autoDetectedWebtoon = false;
    if (!hasServerSettings && !hasLocalSettings && appSettings.webtoonDetection &&
        Application::getInstance().isConnected()) {
        Manga mangaInfo;
        if (SuwayomiClient::getInstance().fetchManga(m_mangaId, mangaInfo)) {
            if (mangaInfo.isWebtoon()) {
                // Apply webtoon-optimized defaults
                readingMode = ReadingMode::WEBTOON;
                pageScaleMode = PageScaleMode::FIT_WIDTH;
                imageRotation = 0;  // No rotation for webtoons
                m_settings.isWebtoonFormat = true;
                autoDetectedWebtoon = true;
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

    // Apply keepScreenOn setting - prevent screen timeout during reading
    m_settings.keepScreenOn = appSettings.keepScreenOn;
    if (m_settings.keepScreenOn) {
        auto* platform = brls::Application::getPlatform();
        if (platform) {
            platform->disableScreenDimming(true, "Reading manga", "VitaSuwayomi");
            brls::Logger::info("ReaderActivity: keepScreenOn enabled, disabled screen dimming");
        }
    }

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

    // Set up controller input - L/R for chapter navigation
    this->registerAction("Previous Chapter", brls::ControllerButton::BUTTON_LB, [this](brls::View*) {
        previousChapter();
        return true;
    });

    this->registerAction("Next Chapter", brls::ControllerButton::BUTTON_RB, [this](brls::View*) {
        nextChapter();
        return true;
    });

    // D-pad for page navigation (direction-aware)
    this->registerAction("", brls::ControllerButton::BUTTON_LEFT, [this](brls::View*) {
        if (m_controlsVisible || m_settingsVisible) return false;  // Let UI handle it
        if (m_errorOverlay) return true;  // Block navigation while error overlay is shown
        if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT)
            nextPage();
        else
            previousPage();
        return true;
    });

    this->registerAction("", brls::ControllerButton::BUTTON_RIGHT, [this](brls::View*) {
        if (m_controlsVisible || m_settingsVisible) return false;
        if (m_errorOverlay) return true;  // Block navigation while error overlay is shown
        if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT)
            previousPage();
        else
            nextPage();
        return true;
    });

    this->registerAction("", brls::ControllerButton::BUTTON_UP, [this](brls::View*) {
        if (m_controlsVisible || m_settingsVisible) return false;
        if (m_errorOverlay) return true;  // Block navigation while error overlay is shown
        previousPage();
        return true;
    });

    this->registerAction("", brls::ControllerButton::BUTTON_DOWN, [this](brls::View*) {
        if (m_controlsVisible || m_settingsVisible) return false;
        if (m_errorOverlay) return true;  // Block navigation while error overlay is shown
        nextPage();
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

    // Hide preview images initially
    if (previewImage) {
        previewImage->setVisibility(brls::Visibility::GONE);
    }
    if (previewImageB) {
        previewImageB->setVisibility(brls::Visibility::GONE);
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
        m_pinchEndTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);

        // Tap gesture for controls toggle and optional tap-to-navigate zones
        pageImage->addGestureRecognizer(new brls::TapGestureRecognizer(
            [this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
                if (m_errorOverlay) return;  // Don't handle taps while error overlay is shown
                if (m_isPinching) return;    // Ignore taps during active pinch

                // Ignore taps that fire shortly after a pinch ends (finger-lift artifacts)
                auto timeSincePinch = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - m_pinchEndTime).count();
                if (timeSincePinch < 300) return;

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
                        m_lastTapTime = now;
                        m_lastTapPosition = status.position;

                        // Tap-to-navigate: left 1/3 = prev, right 1/3 = next, center = toggle
                        // Respects reading direction and rotation
                        AppSettings& appSettings = Application::getInstance().getSettings();
                        if (appSettings.tapToNavigate && !m_isZoomed && !m_continuousScrollMode) {
                            int zone = getTapZone(status.position);
                            if (zone == -1) {
                                // User's left zone
                                if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT)
                                    nextPage();
                                else
                                    previousPage();
                            } else if (zone == +1) {
                                // User's right zone
                                if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT)
                                    previousPage();
                                else
                                    nextPage();
                            } else {
                                toggleControls();
                            }
                        } else {
                            toggleControls();
                        }
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
                if (m_errorOverlay) return;  // Don't handle swipes while error overlay is shown
                const float TAP_THRESHOLD = 15.0f;       // Max movement for tap
                const float PAGE_TURN_THRESHOLD = 80.0f; // Threshold to complete page turn
                const float SWIPE_START_THRESHOLD = 20.0f; // When to start showing preview

                // Determine swipe axis and inversion based on rotation
                bool useVerticalSwipe = (m_settings.rotation == ImageRotation::ROTATE_90 ||
                                         m_settings.rotation == ImageRotation::ROTATE_270);
                bool invertDirection = (m_settings.rotation == ImageRotation::ROTATE_180 ||
                                        m_settings.rotation == ImageRotation::ROTATE_270);

                if (status.state == brls::GestureState::START) {
                    // If a page-turn animation is in progress, finalize it
                    // immediately so fast swiping doesn't lose page turns
                    if (m_completionAnimating) {
                        m_completionAnimating = false;
                        if (m_completionTurnPage) {
                            finalizePageTurn();
                        } else if (m_completionNavChapter) {
                            // Defer chapter navigation to next frame — it's a
                            // heavy operation that replaces all pages and should
                            // not run inside the gesture handler's stack.
                            bool navNext = m_completionNavNext;
                            m_completionNavChapter = false;
                            resetSwipeState();
                            std::weak_ptr<bool> wa = m_alive;
                            brls::sync([this, wa, navNext]() {
                                auto a = wa.lock();
                                if (!a || !*a) return;
                                if (navNext) nextChapter();
                                else previousChapter();
                            });
                        } else {
                            resetSwipeState();
                        }
                    }
                    m_isPanning = false;
                    m_isSwipeAnimating = false;
                    m_touchStart = status.position;
                    m_touchCurrent = status.position;
                    m_swipeOffset = 0.0f;
                    m_previewPageIndex = -2;  // sentinel: distinct from -1 (backward OOB)
                    m_swipeToChapter = false;
                    m_previewIsTransition = false;
                    m_swipingToNext = true;
                    brls::Logger::info("PAN START rot={} vert={} invert={} page={}/{}",
                        static_cast<int>(m_settings.rotation), useVerticalSwipe,
                        invertDirection, m_currentPage, static_cast<int>(m_pages.size()));
                } else if (status.state == brls::GestureState::STAY) {
                    // Suppress page navigation while pinch-to-zoom is active
                    if (m_isPinching) return;

                    // Guard against empty pages (can happen during async chapter load)
                    if (m_pages.empty()) return;

                    // Real-time swipe tracking (STAY = finger still on screen)
                    m_touchCurrent = status.position;
                    float dx = m_touchCurrent.x - m_touchStart.x;
                    float dy = m_touchCurrent.y - m_touchStart.y;

                    // If zoomed, use pan to scroll the zoomed image instead of page navigation
                    if (m_isZoomed) {
                        // Convert physical touch delta to view coords, then divide by zoom
                        // since the offset is in pre-scale space (applied before nvgScale).
                        // Without /zoom, panning feels too fast at higher zoom levels.
                        float scaleX = (pageImage ? pageImage->getWidth() : 960.0f) / 960.0f;
                        float scaleY = (pageImage ? pageImage->getHeight() : 544.0f) / 544.0f;
                        brls::Point newOffset = {
                            m_zoomOffset.x + dx * scaleX / m_zoomLevel,
                            m_zoomOffset.y + dy * scaleY / m_zoomLevel
                        };

                        // Apply offset to image
                        if (pageImage) {
                            pageImage->setZoomOffset(newOffset);
                        }

                        // Update touch start for next delta calculation
                        m_touchStart = m_touchCurrent;
                        m_zoomOffset = newOffset;
                        return;
                    }

                    // Raw delta follows the finger (for visual feedback)
                    float rawDelta = useVerticalSwipe ? dy : dx;
                    float crossDelta = useVerticalSwipe ? dx : dy;

                    // Logical delta accounts for rotation inversion (for page turn logic)
                    float logicalDelta = invertDirection ? -rawDelta : rawDelta;

                    // Only track swipes in the primary direction
                    if (std::abs(rawDelta) > std::abs(crossDelta) && std::abs(rawDelta) > SWIPE_START_THRESHOLD) {
                        m_isSwipeAnimating = true;

                        // Scale touch delta from physical screen coords to view coords
                        // for 1:1 finger-to-page tracking. Touch is in physical pixels
                        // (960x544) but NanoVG views use internal coords (~1280x726).
                        float physScreen = useVerticalSwipe ? 544.0f : 960.0f;
                        auto [svW, svH] = getSwipeViewSize();
                        float viewSize = useVerticalSwipe ? svH : svW;
                        float scaledDelta = rawDelta * (viewSize / physScreen);
                        m_swipeOffset = scaledDelta;

                        // Determine active swipe direction for page turn logic
                        bool swipingPositive = logicalDelta > 0;
                        bool wantNextPage = (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) ? swipingPositive : !swipingPositive;

                        // Set active preview index based on raw direction
                        // Positive raw offset → positive side preview, negative → negative side
                        int activeIdx = rawDelta > 0 ? m_posPreviewIdx : m_negPreviewIdx;
                        if (activeIdx != m_previewPageIndex) {
                            m_previewPageIndex = activeIdx;
                            m_swipingToNext = wantNextPage;

                            // Check if active side is a transition page for chapter navigation
                            bool activeIsTransition = rawDelta > 0 ? m_posIsTransition : m_negIsTransition;
                            m_previewIsTransition = activeIsTransition;
                            if (activeIsTransition && transitionBox) {
                                loadPreviewPage(activeIdx);  // Populate transitionBox text
                            }
                        }

                        // Update visual positions - all 3 pages slide together
                        updateSwipePreview(scaledDelta);
                    }
                } else if (status.state == brls::GestureState::END) {
                    // Don't turn the page if we were just pinch-zooming
                    // (also check cooldown — pinch END may fire before pan END)
                    if (m_isPinching) {
                        resetSwipeState();
                        return;
                    }
                    auto msSincePinch = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - m_pinchEndTime).count();
                    if (msSincePinch < 300) {
                        resetSwipeState();
                        return;
                    }

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

                        bool hasValidPreview = m_previewPageIndex >= 0 &&
                            m_previewPageIndex < static_cast<int>(m_pages.size());
                        if (absSwipe >= PAGE_TURN_THRESHOLD && (hasValidPreview || m_swipeToChapter)) {
                            // Swipe was long enough - turn the page
                            completeSwipeAnimation(true);
                        } else if (absSwipe >= PAGE_TURN_THRESHOLD) {
                            // No valid preview in that direction — on a transition page
                            // this triggers chapter navigation, otherwise just snap back
                            completeSwipeAnimation(isTransitionPage(m_currentPage));
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

        // Pinch-to-zoom gesture (Vita two-finger touch)
        // Uses focal-point zoom: the image point under the initial pinch center
        // stays visually fixed as you pinch in/out and move fingers.
        pageImage->addGestureRecognizer(new vitasuwayomi::PinchGestureRecognizer(
            [this](vitasuwayomi::PinchGestureStatus status, brls::Sound* soundToPlay) {
                if (status.state == brls::GestureState::START) {
                    m_isPinching = true;
                    m_initialPinchDistance = 0;
                    m_initialZoomLevel = m_zoomLevel;

                    // Invalidate any pending double-tap so lifting fingers after
                    // pinch can't accidentally trigger handleDoubleTap/resetZoom.
                    m_lastTapTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);

                    // Record initial pinch center and current zoom offset for focal-point tracking
                    m_pinchStartCenter = status.center;
                    m_pinchStartOffset = m_zoomOffset;
                } else if (status.state == brls::GestureState::STAY) {
                    float newZoom = m_initialZoomLevel * status.scaleFactor;
                    newZoom = std::max(1.0f, std::min(4.0f, newZoom));

                    if (std::abs(newZoom - m_zoomLevel) > 0.01f) {
                        m_zoomLevel = newZoom;
                        m_isZoomed = (newZoom > 1.01f);

                        if (pageImage) {
                            pageImage->setZoomLevel(newZoom);

                            // Focal-point zoom formula:
                            // The NVG transform is: P' = C + z*(P - C + off)
                            // where C = image center in view coords.
                            // To keep image point P at screen pos S:
                            //   off = (S-C)/z - (S0-C)/z0 + off0
                            brls::Rect frame = pageImage->getFrame();
                            float cx = frame.getMinX() + frame.getWidth() / 2.0f;
                            float cy = frame.getMinY() + frame.getHeight() / 2.0f;

                            brls::Point offset = {
                                (status.center.x - cx) / newZoom - (m_pinchStartCenter.x - cx) / m_initialZoomLevel + m_pinchStartOffset.x,
                                (status.center.y - cy) / newZoom - (m_pinchStartCenter.y - cy) / m_initialZoomLevel + m_pinchStartOffset.y
                            };
                            m_zoomOffset = offset;
                            pageImage->setZoomOffset(offset);
                        }
                    }
                } else if (status.state == brls::GestureState::END) {
                    m_isPinching = false;
                    m_pinchEndTime = std::chrono::steady_clock::now();

                    // If user pinched back down to 1.0x, clear zoomed state
                    // so swipe page navigation works again without needing double-tap
                    if (m_zoomLevel <= 1.0f) {
                        resetZoom();
                    }
                }
            }));
    }

    // Transition page gestures - tap and swipe like a regular page
    if (transitionBox) {
        transitionBox->setFocusable(true);
        // Draw background via NanoVG inside draw() so it follows the slide offset.
        // The XML backgroundColor attribute is drawn by borealis BEFORE draw() is
        // called, at the layout position, covering views below in z-order during swipes.
        transitionBox->setCustomBackground(Application::getInstance().getReaderBackground());
        // Also clear borealis-level background to fully transparent so the
        // framework doesn't draw anything at the layout position before draw().
        transitionBox->setBackgroundColor(nvgRGBA(0, 0, 0, 0));

        // Tap gesture on transition page - toggle controls or tap-to-navigate
        transitionBox->addGestureRecognizer(new brls::TapGestureRecognizer(
            [this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
                if (status.state == brls::GestureState::END) {
                    AppSettings& appSettings = Application::getInstance().getSettings();
                    if (appSettings.tapToNavigate) {
                        int zone = getTapZone(status.position);
                        if (zone == -1) {
                            if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT)
                                nextPage();
                            else
                                previousPage();
                        } else if (zone == +1) {
                            if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT)
                                previousPage();
                            else
                                nextPage();
                        } else {
                            toggleControls();
                        }
                    } else {
                        toggleControls();
                    }
                }
            }));

        // Pan/swipe gesture on transition page - same logic as pageImage
        transitionBox->addGestureRecognizer(new brls::PanGestureRecognizer(
            [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
                const float TAP_THRESHOLD = 15.0f;
                const float PAGE_TURN_THRESHOLD = 80.0f;
                const float SWIPE_START_THRESHOLD = 20.0f;

                bool useVerticalSwipe = (m_settings.rotation == ImageRotation::ROTATE_90 ||
                                         m_settings.rotation == ImageRotation::ROTATE_270);
                bool invertDirection = (m_settings.rotation == ImageRotation::ROTATE_180 ||
                                        m_settings.rotation == ImageRotation::ROTATE_270);

                if (status.state == brls::GestureState::START) {
                    if (m_completionAnimating) {
                        m_completionAnimating = false;
                        resetSwipeState();
                    }
                    m_isPanning = false;
                    m_isSwipeAnimating = false;
                    m_touchStart = status.position;
                    m_touchCurrent = status.position;
                    m_swipeOffset = 0.0f;
                    m_previewPageIndex = -2;  // sentinel: distinct from -1 (backward OOB)
                    m_swipeToChapter = false;
                    m_previewIsTransition = false;
                    m_swipingToNext = true;  // reset to avoid stale direction
                    brls::Logger::info("TSWIPE START: page={}/{} url={}",
                        m_currentPage, static_cast<int>(m_pages.size()),
                        m_currentPage < static_cast<int>(m_pages.size()) ? m_pages[m_currentPage].imageUrl : "?");
                } else if (status.state == brls::GestureState::STAY) {
                    // Guard against empty pages (can happen during async chapter load)
                    if (m_pages.empty()) return;

                    m_touchCurrent = status.position;
                    float dx = m_touchCurrent.x - m_touchStart.x;
                    float dy = m_touchCurrent.y - m_touchStart.y;

                    float rawDelta = useVerticalSwipe ? dy : dx;
                    float crossDelta = useVerticalSwipe ? dx : dy;
                    float logicalDelta = invertDirection ? -rawDelta : rawDelta;

                    if (std::abs(rawDelta) > std::abs(crossDelta) && std::abs(rawDelta) > SWIPE_START_THRESHOLD) {
                        m_isSwipeAnimating = true;

                        // Scale touch delta from physical screen coords to view coords
                        float physScreen = useVerticalSwipe ? 544.0f : 960.0f;
                        auto [svW, svH] = getSwipeViewSize();
                        float viewSize = useVerticalSwipe ? svH : svW;
                        float scaledDelta = rawDelta * (viewSize / physScreen);
                        m_swipeOffset = scaledDelta;

                        bool swipingPositive = logicalDelta > 0;
                        bool wantNextPage = (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) ? swipingPositive : !swipingPositive;

                        // Set active preview index based on raw direction
                        int activeIdx = rawDelta > 0 ? m_posPreviewIdx : m_negPreviewIdx;

                        // On a transition page, adjacent pages may be out of bounds (chapter boundary)
                        // In that case, try loading the cross-chapter page
                        if (activeIdx < 0 || activeIdx >= static_cast<int>(m_pages.size())) {
                            if (activeIdx != m_previewPageIndex) {
                                m_swipingToNext = wantNextPage;
                                m_swipeToChapter = false;
                                m_previewIsTransition = false;
                                const std::string& tUrl = m_pages[m_currentPage].imageUrl;

                                std::string chapterPageUrl;
                                RotatableImage* target = rawDelta > 0 ? previewImage : previewImageB;
                                if (wantNextPage && tUrl == TRANSITION_NEXT &&
                                    m_nextChapterLoaded && !m_nextChapterPages.empty()) {
                                    chapterPageUrl = m_nextChapterPages[0].imageUrl;
                                } else if (!wantNextPage && tUrl == TRANSITION_PREV &&
                                           m_prevChapterLoaded && !m_prevChapterPages.empty()) {
                                    chapterPageUrl = m_prevChapterPages.back().imageUrl;
                                }

                                if (!chapterPageUrl.empty() && target) {
                                    m_previewPageIndex = activeIdx;
                                    m_swipeToChapter = true;
                                    // Update the correct side's index so the view shows
                                    if (rawDelta > 0) m_posPreviewIdx = activeIdx;
                                    else m_negPreviewIdx = activeIdx;
                                    target->setRotation(static_cast<float>(m_settings.rotation));
                                    std::weak_ptr<bool> aliveWeak = m_alive;
                                    auto previewAlive = m_previewLoadAlive ? m_previewLoadAlive : m_alive;
                                    ImageLoader::loadAsyncFullSize(chapterPageUrl,
                                        [aliveWeak](RotatableImage* img) {
                                            auto alive = aliveWeak.lock();
                                            if (!alive || !*alive) return;
                                        }, target, previewAlive);
                                } else {
                                    m_previewPageIndex = activeIdx;
                                }
                            }
                        } else if (activeIdx != m_previewPageIndex) {
                            m_previewPageIndex = activeIdx;
                            m_swipingToNext = wantNextPage;
                            // Check if active side uses transitionBox
                            bool activeIsTransition = rawDelta > 0 ? m_posIsTransition : m_negIsTransition;
                            m_previewIsTransition = activeIsTransition;
                            if (activeIsTransition && transitionBox) {
                                loadPreviewPage(activeIdx);
                            }
                        }

                        updateSwipePreview(scaledDelta);
                    }
                } else if (status.state == brls::GestureState::END) {
                    if (m_pages.empty()) {
                        resetSwipeState();
                        return;
                    }
                    m_touchCurrent = status.position;
                    float dx = m_touchCurrent.x - m_touchStart.x;
                    float dy = m_touchCurrent.y - m_touchStart.y;
                    float distance = std::sqrt(dx * dx + dy * dy);
                    float rawDelta = useVerticalSwipe ? dy : dx;

                    brls::Logger::info("TSWIPE END: rawDelta={:.1f} distance={:.1f} isSwipeAnim={} "
                        "previewIdx={} swipeToChapter={} swipingToNext={} previewIsTransition={}",
                        rawDelta, distance, m_isSwipeAnimating,
                        m_previewPageIndex, m_swipeToChapter, m_swipingToNext, m_previewIsTransition);

                    if (distance < TAP_THRESHOLD) {
                        resetSwipeState();
                    } else if (m_isSwipeAnimating) {
                        float absSwipe = std::abs(rawDelta);
                        bool hasValidPreview = m_previewPageIndex >= 0 &&
                            m_previewPageIndex < static_cast<int>(m_pages.size());
                        if (absSwipe >= PAGE_TURN_THRESHOLD && (hasValidPreview || m_swipeToChapter)) {
                            completeSwipeAnimation(true);
                        } else if (absSwipe >= PAGE_TURN_THRESHOLD) {
                            // No preview page in that direction — on a transition page
                            // this triggers chapter navigation, otherwise just snap back
                            completeSwipeAnimation(true);
                        } else {
                            completeSwipeAnimation(false);
                        }
                    } else {
                        float crossDelta = useVerticalSwipe ? dx : dy;
                        float logicalCross = invertDirection ? -crossDelta : crossDelta;

                        if (std::abs(crossDelta) >= PAGE_TURN_THRESHOLD) {
                            if (logicalCross > 0)
                                previousPage();
                            else
                                nextPage();
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
                if (m_errorOverlay) return;  // Don't handle taps while error overlay is shown
                if (m_isPinching) return;    // Ignore taps during active pinch

                // Ignore taps that fire shortly after a pinch ends (finger-lift artifacts)
                auto timeSincePinch = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - m_pinchEndTime).count();
                if (timeSincePinch < 300) return;

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

                        // Same tap-to-navigate logic as pageImage
                        AppSettings& appSettings = Application::getInstance().getSettings();
                        if (appSettings.tapToNavigate && !m_isZoomed && !m_continuousScrollMode) {
                            int zone = getTapZone(status.position);
                            if (zone == -1) {
                                if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT)
                                    nextPage();
                                else
                                    previousPage();
                            } else if (zone == +1) {
                                if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT)
                                    previousPage();
                                else
                                    nextPage();
                            } else {
                                toggleControls();
                            }
                        } else {
                            toggleControls();
                        }
                    }
                }
            }));

        // Pan gesture for swipe navigation on container
        container->addGestureRecognizer(new brls::PanGestureRecognizer(
            [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
                if (m_errorOverlay) return;  // Don't handle swipes while error overlay is shown
                const float PAGE_TURN_THRESHOLD = 80.0f;

                if (status.state == brls::GestureState::START) {
                    m_touchStart = status.position;
                } else if (status.state == brls::GestureState::END) {
                    float dx = status.position.x - m_touchStart.x;
                    float dy = status.position.y - m_touchStart.y;

                    // Account for rotation when determining swipe direction
                    bool useVerticalSwipe = (m_settings.rotation == ImageRotation::ROTATE_90 ||
                                             m_settings.rotation == ImageRotation::ROTATE_270);
                    bool invertDirection = (m_settings.rotation == ImageRotation::ROTATE_180 ||
                                            m_settings.rotation == ImageRotation::ROTATE_270);

                    float primaryDelta = useVerticalSwipe ? dy : dx;
                    float crossDelta = useVerticalSwipe ? dx : dy;
                    float logicalPrimary = invertDirection ? -primaryDelta : primaryDelta;

                    if (std::abs(primaryDelta) > std::abs(crossDelta) && std::abs(primaryDelta) >= PAGE_TURN_THRESHOLD) {
                        if (logicalPrimary > 0) {
                            m_settings.direction == ReaderDirection::RIGHT_TO_LEFT ? nextPage() : previousPage();
                        } else {
                            m_settings.direction == ReaderDirection::RIGHT_TO_LEFT ? previousPage() : nextPage();
                        }
                    } else if (std::abs(crossDelta) >= PAGE_TURN_THRESHOLD) {
                        float logicalCross = invertDirection ? -crossDelta : crossDelta;
                        logicalCross > 0 ? previousPage() : nextPage();
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

    // Page slider in bottom bar
    if (pageSlider) {
        pageSlider->getProgressEvent()->subscribe([this](float progress) {
            if (m_updatingSlider) return;  // Ignore programmatic setProgress

            if (m_continuousScrollMode && webtoonScroll) {
                // Webtoon mode: slider maps to current chapter's page range
                int rawPage = webtoonScroll->getCurrentPage();
                int chapterId, chapterPos, pageInChapter, segPageCount;
                getWebtoonProgress(rawPage, chapterId, chapterPos, pageInChapter, segPageCount);
                if (segPageCount <= 0) return;

                // Find the segment for this chapter to get firstPage
                int segFirst = 0;
                for (const auto& seg : m_webtoonSegments) {
                    if (seg.chapterId == chapterId) {
                        segFirst = seg.firstPage;
                        break;
                    }
                }

                int targetDisplayPage = static_cast<int>(progress * std::max(1, segPageCount - 1) + 0.5f);
                int targetPage = segFirst + targetDisplayPage;
                if (targetPage != m_currentPage) {
                    goToPage(targetPage);
                }
                return;
            }

            // Manga mode
            if (m_pages.empty() || m_realPageCount <= 0) return;
            // Convert slider progress back to internal page index.
            // Progress was calculated as displayPage / (m_realPageCount - 1),
            // so reverse that to get the display page, then add the transition offset.
            int displayPage = static_cast<int>(progress * std::max(1, m_realPageCount - 1) + 0.5f);
            int targetPage = displayPage;
            // Adjust for the transition page prepended at index 0
            if (!m_pages.empty() && m_pages[0].imageUrl == TRANSITION_PREV) {
                targetPage = displayPage + 1;
            }
            // Clamp to valid range and skip transition pages
            targetPage = std::max(0, std::min(targetPage, static_cast<int>(m_pages.size()) - 1));
            if (isTransitionPage(targetPage)) return;
            if (targetPage != m_currentPage) {
                goToPage(targetPage);
            }
        });
    }

    // Chapter navigation buttons in bottom bar
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

    // Settings button in top bar
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

            // Immediately switch views between paged and webtoon mode
            updateReaderMode();

            // Reload pages for mode change
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

    if (settingsScaleBtn) {
        settingsScaleBtn->registerClickAction([this](brls::View*) {
            // Cycle through scale modes: FIT_SCREEN -> FIT_WIDTH -> FIT_HEIGHT -> ORIGINAL -> FIT_SCREEN
            switch (m_settings.scaleMode) {
                case ReaderScaleMode::FIT_SCREEN:
                    m_settings.scaleMode = ReaderScaleMode::FIT_WIDTH;
                    break;
                case ReaderScaleMode::FIT_WIDTH:
                    m_settings.scaleMode = ReaderScaleMode::FIT_HEIGHT;
                    break;
                case ReaderScaleMode::FIT_HEIGHT:
                    m_settings.scaleMode = ReaderScaleMode::ORIGINAL;
                    break;
                case ReaderScaleMode::ORIGINAL:
                    m_settings.scaleMode = ReaderScaleMode::FIT_SCREEN;
                    break;
            }
            applySettings();
            saveSettingsToApp();
            updateSettingsLabels();
            return true;
        });
        settingsScaleBtn->addGestureRecognizer(new brls::TapGestureRecognizer(settingsScaleBtn));
    }

    // Update direction label
    updateDirectionLabel();

    // Apply loaded settings (rotation, scaling, etc.)
    applySettings();

    // Persist auto-detected webtoon settings so they survive across reads
    if (autoDetectedWebtoon) {
        saveSettingsToApp();
    }

    // Load pages asynchronously
    loadPages();
}

void ReaderActivity::loadPages() {
    brls::Logger::debug("Loading pages for chapter {}", m_chapterIndex);

    // Capture webtoon mode flag for async task - use format setting
    bool isWebtoonMode = m_settings.isWebtoonFormat;
    int mangaId = m_mangaId;
    int chapterIndex = m_chapterIndex;
    std::weak_ptr<bool> aliveWeak = m_alive;

    // Use shared containers so the background thread writes to its own memory,
    // NOT to `this->m_pages` etc. which may be freed if the Activity is destroyed.
    auto sharedPages = std::make_shared<std::vector<Page>>();
    auto sharedChapterName = std::make_shared<std::string>();
    auto sharedChapters = std::make_shared<std::vector<Chapter>>();
    auto sharedTotalChapters = std::make_shared<int>(0);
    auto sharedLoadedFromLocal = std::make_shared<bool>(false);

    vitasuwayomi::asyncTask<bool>([mangaId, chapterIndex,
                                    sharedPages, sharedChapterName,
                                    sharedChapters, sharedTotalChapters,
                                    sharedLoadedFromLocal]() {
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
                *sharedLoadedFromLocal = true;

                // Use DownloadsManager metadata for chapter navigation instead of
                // blocking on network requests. This makes downloaded chapters load
                // instantly without waiting for server round-trips.
                {
                    std::unique_lock<std::mutex> dlLock;
                    auto* dlChapters = localMgr.getChapterDownloads(mangaId, dlLock);
                    if (dlChapters && !dlChapters->empty()) {
                        for (const auto& dlChapter : *dlChapters) {
                            if (dlChapter.state != LocalDownloadState::COMPLETED) continue;
                            Chapter ch;
                            ch.id = dlChapter.chapterId;
                            ch.name = dlChapter.name;
                            ch.chapterNumber = dlChapter.chapterNumber;
                            ch.index = dlChapter.chapterIndex;
                            ch.downloaded = true;
                            sharedChapters->push_back(ch);

                            // Get current chapter name
                            if (dlChapter.chapterId == chapterIndex || dlChapter.chapterIndex == chapterIndex) {
                                *sharedChapterName = dlChapter.name;
                            }
                        }
                        *sharedTotalChapters = static_cast<int>(sharedChapters->size());
                    }
                }  // dlLock released
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

        // Use pages as-is (no splitting)
        *sharedPages = std::move(rawPages);

        // For server-loaded pages, fetch chapter metadata now (required for navigation).
        // Downloaded chapters already got metadata from DownloadsManager above.
        if (!loadedFromLocal && Application::getInstance().isConnected()) {
            Chapter chapter;
            if (client.fetchChapter(mangaId, chapterIndex, chapter)) {
                *sharedChapterName = chapter.name;
            }
            client.fetchChapters(mangaId, *sharedChapters);
            *sharedTotalChapters = static_cast<int>(sharedChapters->size());
        }

        return true;
    }, [this, isWebtoonMode, aliveWeak,
        sharedPages, sharedChapterName,
        sharedChapters, sharedTotalChapters,
        sharedLoadedFromLocal](bool success) {
        auto alive = aliveWeak.lock();
        if (!alive || !*alive) return;

        // Copy results from shared containers into our members (safe - we're alive)
        m_pages = std::move(*sharedPages);
        m_chapterName = std::move(*sharedChapterName);
        m_chapters = std::move(*sharedChapters);
        m_totalChapters = *sharedTotalChapters;
        m_loadedFromLocal = *sharedLoadedFromLocal;

        // Sort chapters ascending by chapter number so navigation is consistent
        // regardless of server sort order (DESC or ASC)
        std::sort(m_chapters.begin(), m_chapters.end(),
                  [](const Chapter& a, const Chapter& b) {
                      return a.chapterNumber < b.chapterNumber;
                  });

        // Find current chapter's position in the sorted list
        findChapterPosition();

        if (!success || m_pages.empty()) {
            brls::Logger::error("No pages to display");

            if (chapterLabel) {
                chapterLabel->setText("Failed to load chapter");
            }
            showPageError("Failed to load chapter pages");
            return;
        }

        brls::Logger::info("Loaded {} pages", m_pages.size());

        // Insert fake transition pages at chapter boundaries
        // (must be done before calculating start page)
        m_realPageCount = static_cast<int>(m_pages.size());
        insertTransitionPages();

        // Update UI labels
        if (chapterLabel) {
            std::string label = m_chapterName.empty() ?
                "Chapter " + getChapterDisplayNumber() : m_chapterName;
            chapterLabel->setText(label);
        }

        if (chapterProgress) {
            chapterProgress->setText("Ch. " + getChapterDisplayNumber() +
                                     " of " + std::to_string(m_totalChapters));
        }

        // Start from saved position or beginning
        // (startPage was adjusted by insertTransitionPages if prev page was added)
        if (m_goToEndAfterLoad) {
            // Jump to the last real page (skip trailing transition pages)
            m_goToEndAfterLoad = false;
            m_currentPage = static_cast<int>(m_pages.size()) - 1;
            while (m_currentPage > 0 && isTransitionPage(m_currentPage)) {
                m_currentPage--;
            }
            brls::Logger::info("loadPages: goToEnd → page {}/{}", m_currentPage, static_cast<int>(m_pages.size()));
        } else {
            m_currentPage = std::min(m_startPage, static_cast<int>(m_pages.size()) - 1);
            m_currentPage = std::max(0, m_currentPage);
            // Make sure we don't start on a transition page - skip forward to first real page
            if (isTransitionPage(m_currentPage)) {
                for (int i = m_currentPage + 1; i < static_cast<int>(m_pages.size()); i++) {
                    if (!isTransitionPage(i)) {
                        m_currentPage = i;
                        break;
                    }
                }
            }
            brls::Logger::info("loadPages: startPage={} → page {}/{} url={}",
                m_startPage, m_currentPage, static_cast<int>(m_pages.size()),
                m_currentPage < static_cast<int>(m_pages.size()) ? m_pages[m_currentPage].imageUrl : "?");
        }

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

                // Set up progress callback with alive guard
                std::weak_ptr<bool> cbAlive = m_alive;
                webtoonScroll->setProgressCallback([this, cbAlive](int currentPage, int totalPages, float scrollPercent) {
                    auto a = cbAlive.lock();
                    if (!a || !*a) return;
                    m_currentPage = currentPage;
                    updatePageDisplay();
                    updateProgress();
                });

                // Set up tap callback with alive guard
                webtoonScroll->setTapCallback([this, cbAlive]() {
                    auto a = cbAlive.lock();
                    if (!a || !*a) return;
                    toggleControls();
                });

                // Set up chapter navigation callback for transition pages
                // Use smooth extend (append/prepend) instead of full chapter replacement
                webtoonScroll->setChapterNavigateCallback([this, cbAlive](bool next) {
                    auto a = cbAlive.lock();
                    if (!a || !*a) return;
                    webtoonExtendChapter(next);
                });

                // Load all pages into the scroll view
                // Use actual view width (internal rendering coords, ~1280) rather than
                // physical screen width (960) to avoid coordinate system mismatch.
                float viewW = webtoonScroll->getWidth();
                if (viewW <= 0) viewW = container ? container->getWidth() : 960.0f;
                webtoonScroll->setPages(m_pages, viewW, m_currentPage);

                // Build chapter-boundary map so progress saves to the right chapter
                initWebtoonSegments();

                // Set transition text for chapter separator pages
                setupWebtoonTransitionText();
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

        // Preload adjacent chapters for smooth swipe transitions
        preloadNextChapter();
        preloadPrevChapter();
    });
}

void ReaderActivity::loadPage(int index) {
    if (index < 0 || index >= static_cast<int>(m_pages.size())) {
        return;
    }

    hidePageError();  // Clear any previous error/transition box

    // Handle fake transition pages
    if (isTransitionPage(index)) {
        renderTransitionPage(index);
        // Still need to update adjacent preview indices so swiping from the
        // transition page knows where to go (next/prev chapter vs real page).
        // Without this, m_posPreviewIdx/m_negPreviewIdx are stale from the
        // previous real page and swipe goes in circles.
        preloadAdjacentPreviews();
        return;
    }

    // Hide transition page and restore page image if it was hidden
    bool wasOnTransition = transitionBox && transitionBox->getVisibility() != brls::Visibility::GONE;
    if (wasOnTransition) {
        transitionBox->setVisibility(brls::Visibility::GONE);
        // Clear stale image from the previous chapter so the old page
        // doesn't flash briefly while the new page loads asynchronously.
        if (pageImage) pageImage->clearImage();
    }
    if (pageImage && pageImage->getVisibility() != brls::Visibility::VISIBLE) {
        pageImage->setVisibility(brls::Visibility::VISIBLE);
    }
    // Transfer focus back to pageImage after leaving a transition page
    if (wasOnTransition && pageImage) {
        brls::Application::giveFocus(pageImage);
    }

    const Page& page = m_pages[index];
    std::string imageUrl = page.imageUrl;

    brls::Logger::debug("Loading page {} from: {}", index, imageUrl);

    // Show page counter when navigating
    showPageCounter();
    schedulePageCounterHide();

    // Track this load generation for timeout detection
    int loadGen = ++m_pageLoadGeneration;
    m_pageLoadSucceeded = false;

    // Invalidate any previous async load for pageImage so stale loads
    // (from a page we already swiped past) don't overwrite the new page.
    // Each load gets its own alive flag; setting the old one to false
    // makes executeRotatableLoad skip setImageFromMem for the old page.
    if (m_pageLoadAlive) *m_pageLoadAlive = false;
    m_pageLoadAlive = std::make_shared<bool>(true);

    // Load image using RotatableImage
    if (pageImage) {
        int currentPageAtLoad = m_currentPage;
        std::weak_ptr<bool> aliveWeak = m_alive;

        auto onLoaded = [this, aliveWeak, index, currentPageAtLoad, loadGen](RotatableImage* img) {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            if (index == currentPageAtLoad) {
                brls::Logger::debug("ReaderActivity: Page {} loaded", index);
                if (m_pageLoadGeneration == loadGen) {
                    m_pageLoadSucceeded = true;
                }
            }
        };

        ImageLoader::loadAsyncFullSize(imageUrl, onLoaded, pageImage, m_pageLoadAlive);

        // Set up a timeout: if page hasn't loaded after 15 seconds, show error
        // Only for server-streamed pages - local files load instantly from disk
        if (!m_loadedFromLocal) {
            vitasuwayomi::asyncRun([this, aliveWeak, loadGen, index]() {
                std::this_thread::sleep_for(std::chrono::seconds(15));
                brls::sync([this, aliveWeak, loadGen, index]() {
                    auto alive = aliveWeak.lock();
                    if (!alive || !*alive) return;
                    // Only show error if this is still the same load attempt and it hasn't succeeded
                    if (m_pageLoadGeneration == loadGen && !m_pageLoadSucceeded) {
                        showPageError("Failed to load page " + std::to_string(index + 1));
                    }
                });
            });
        }
    }

    // Preload adjacent pages (texture cache + preview views)
    preloadAdjacentPages();
    preloadAdjacentPreviews();
}

void ReaderActivity::preloadAdjacentPages() {
    // Preload next 3 pages for smoother swiping/reading
    for (int i = 1; i <= 3; i++) {
        int nextIdx = m_currentPage + i;
        if (nextIdx < static_cast<int>(m_pages.size()) && !isTransitionPage(nextIdx)) {
            ImageLoader::preloadFullSize(m_pages[nextIdx].imageUrl);
        }
    }

    // Preload previous page (for going back)
    if (m_currentPage > 0 && !isTransitionPage(m_currentPage - 1)) {
        ImageLoader::preloadFullSize(m_pages[m_currentPage - 1].imageUrl);
    }
}

void ReaderActivity::updatePageDisplay() {
    int displayPage = 0;
    int realPageCount = 0;

    if (m_continuousScrollMode && webtoonScroll) {
        // Webtoon mode: use segment tracking for chapter-relative page numbers
        int rawPage = webtoonScroll->getCurrentPage();
        if (webtoonScroll->isTransitionPage(rawPage)) return;

        int chapterId, chapterPos, pageInChapter, segPageCount;
        getWebtoonProgress(rawPage, chapterId, chapterPos, pageInChapter, segPageCount);
        displayPage = pageInChapter;
        realPageCount = segPageCount;
        if (realPageCount <= 0) return;

        // Update chapter label if user has scrolled into a different chapter
        if (chapterId != m_chapterIndex) {
            m_chapterIndex = chapterId;
            m_chapterPosition = chapterPos;
            if (chapterPos >= 0 && chapterPos < static_cast<int>(m_chapters.size())) {
                m_chapterName = m_chapters[chapterPos].name;
            }
            if (chapterLabel) {
                std::string label = m_chapterName.empty() ?
                    "Chapter " + getChapterDisplayNumber() : m_chapterName;
                chapterLabel->setText(label);
            }
            if (chapterProgress) {
                chapterProgress->setText("Ch. " + getChapterDisplayNumber() +
                                         " of " + std::to_string(m_totalChapters));
            }
        }
    } else {
        // Manga mode: use ReaderActivity's m_pages and m_realPageCount
        if (m_pages.empty() || m_currentPage < 0 || m_currentPage >= static_cast<int>(m_pages.size())) return;
        if (isTransitionPage(m_currentPage)) return;

        realPageCount = m_realPageCount;
        displayPage = m_currentPage;
        // If a prev transition page was inserted at index 0, adjust
        if (!m_pages.empty() && m_pages[0].imageUrl == TRANSITION_PREV) {
            displayPage = m_currentPage - 1;
        }
    }

    // Update page counter (top-right overlay with rotation support)
    if (pageCounter) {
        pageCounter->setText(std::to_string(displayPage + 1) + "/" +
                          std::to_string(realPageCount));
    }

    // Update slider page label (in bottom bar)
    if (sliderPageLabel) {
        sliderPageLabel->setText("Page " + std::to_string(displayPage + 1) +
                                 " of " + std::to_string(realPageCount));
    }

    // Update slider position (based on real pages only)
    if (pageSlider && realPageCount > 0) {
        float progress = static_cast<float>(displayPage) /
                        static_cast<float>(std::max(1, realPageCount - 1));
        m_updatingSlider = true;
        pageSlider->setProgress(progress);
        m_updatingSlider = false;
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
    int mangaId = m_mangaId;
    int chapterIndex = m_chapterIndex;
    int currentPage = 0;

    if (m_continuousScrollMode && webtoonScroll) {
        // Webtoon mode: use segment tracking for correct chapter/page
        int rawPage = webtoonScroll->getCurrentPage();
        if (webtoonScroll->isTransitionPage(rawPage)) return;

        int chapterId, chapterPos, pageInChapter, chapterPageCount;
        getWebtoonProgress(rawPage, chapterId, chapterPos, pageInChapter, chapterPageCount);
        chapterIndex = chapterId;
        currentPage = pageInChapter;
    } else {
        // Manga mode: use m_pages and m_currentPage
        if (m_pages.empty() || m_currentPage < 0 || m_currentPage >= static_cast<int>(m_pages.size())) return;
        if (isTransitionPage(m_currentPage)) return;

        currentPage = m_currentPage;
        // Adjust page index to exclude the prepended transition page
        if (m_pages[0].imageUrl == TRANSITION_PREV) {
            currentPage = m_currentPage - 1;
        }
    }

    if (Application::getInstance().isConnected()) {
        // Save reading progress to server
        vitasuwayomi::asyncRun([mangaId, chapterIndex, currentPage]() {
            SuwayomiClient& client = SuwayomiClient::getInstance();
            client.updateChapterProgress(mangaId, chapterIndex, currentPage);
        });
    } else {
        // Save progress locally when offline
        DownloadsManager::getInstance().updateReadingProgress(mangaId, chapterIndex, currentPage);
    }
}

void ReaderActivity::nextPage() {
    if (m_pages.empty()) return;
    if (m_currentPage < 0 || m_currentPage >= static_cast<int>(m_pages.size())) return;

    brls::Logger::info("NEXTPAGE: currentPage={}/{} url={}",
        m_currentPage, static_cast<int>(m_pages.size()),
        m_pages[m_currentPage].imageUrl);

    if (isTransitionPage(m_currentPage)) {
        const std::string& url = m_pages[m_currentPage].imageUrl;
        if (url == TRANSITION_NEXT) {
            // End-of-chapter transition: "next" means go to next chapter
            brls::Logger::info("NEXTPAGE: on TRANSITION_NEXT → nextChapter()");
            nextChapter();
        } else if (url == TRANSITION_PREV) {
            // Beginning-of-chapter transition: "next" means go to first real page
            brls::Logger::info("NEXTPAGE: on TRANSITION_PREV → first real page");
            if (m_currentPage < static_cast<int>(m_pages.size()) - 1) {
                m_currentPage++;
                updatePageDisplay();
                loadPage(m_currentPage);
            }
        }
        // TRANSITION_END: no-op
        return;
    }

    if (m_currentPage < static_cast<int>(m_pages.size()) - 1) {
        m_currentPage++;
        brls::Logger::info("NEXTPAGE: advancing to page {}", m_currentPage);
        updatePageDisplay();
        loadPage(m_currentPage);
        // Don't save progress for transition pages
        if (!isTransitionPage(m_currentPage)) {
            updateProgress();
        }
    }
}

void ReaderActivity::previousPage() {
    if (m_pages.empty()) return;
    if (m_currentPage < 0 || m_currentPage >= static_cast<int>(m_pages.size())) return;

    brls::Logger::info("PREVPAGE: currentPage={}/{} url={}",
        m_currentPage, static_cast<int>(m_pages.size()),
        m_pages[m_currentPage].imageUrl);

    if (isTransitionPage(m_currentPage)) {
        const std::string& url = m_pages[m_currentPage].imageUrl;
        if (url == TRANSITION_PREV) {
            // Beginning-of-chapter transition: "prev" means go to previous chapter
            brls::Logger::info("PREVPAGE: on TRANSITION_PREV → previousChapter()");
            previousChapter();
        } else if (url == TRANSITION_NEXT || url == TRANSITION_END) {
            // End-of-chapter transition: "prev" means go to last real page
            brls::Logger::info("PREVPAGE: on end transition → last real page");
            if (m_currentPage > 0) {
                m_currentPage--;
                updatePageDisplay();
                loadPage(m_currentPage);
            }
        }
        return;
    }

    if (m_currentPage > 0) {
        m_currentPage--;
        updatePageDisplay();
        loadPage(m_currentPage);
        // Don't save progress for transition pages
        if (!isTransitionPage(m_currentPage)) {
            updateProgress();
        }
    }
}

void ReaderActivity::goToPage(int pageIndex) {
    if (m_continuousScrollMode && webtoonScroll) {
        // Webtoon mode: scroll the webtoon view instead of loading a single page
        webtoonScroll->scrollToPage(pageIndex);
        // scrollToPage triggers updateCurrentPage which fires the progress callback,
        // updating m_currentPage/display/progress automatically
        return;
    }
    if (pageIndex >= 0 && pageIndex < static_cast<int>(m_pages.size())) {
        m_currentPage = pageIndex;
        updatePageDisplay();
        loadPage(m_currentPage);
        updateProgress();
    }
}

void ReaderActivity::nextChapter() {
    brls::Logger::info("NEXTCHAPTER: chapterPos={}/{} nextPreloaded={}", m_chapterPosition, m_totalChapters, m_nextChapterLoaded);
    if (m_chapterPosition >= 0 && m_chapterPosition < m_totalChapters - 1 &&
        m_chapterPosition + 1 < static_cast<int>(m_chapters.size())) {
        markChapterAsRead();

        m_chapterPosition++;
        m_chapterIndex = m_chapters[m_chapterPosition].id;
        m_chapterName = m_chapters[m_chapterPosition].name;
        m_currentPage = 0;
        m_goToEndAfterLoad = false;  // Always start at the beginning for next chapter
        m_prevChapterLoaded = false;
        m_prevChapterPages.clear();

        // Use preloaded pages if available for instant transition
        if (m_nextChapterLoaded && !m_nextChapterPages.empty()) {
            m_pages = std::move(m_nextChapterPages);
            m_nextChapterLoaded = false;
            m_nextChapterPages.clear();

            // Insert fake transition pages at chapter boundaries
            m_realPageCount = static_cast<int>(m_pages.size());
            m_startPage = 0;
            insertTransitionPages();
            // Start on first real page (skip prev transition if present)
            m_currentPage = isTransitionPage(0) ? 1 : 0;

            // Update UI
            if (chapterLabel) {
                std::string label = m_chapterName.empty() ?
                    "Chapter " + getChapterDisplayNumber() : m_chapterName;
                chapterLabel->setText(label);
            }
            if (chapterProgress) {
                chapterProgress->setText("Ch. " + getChapterDisplayNumber() +
                                         " of " + std::to_string(m_totalChapters));
            }

            // Handle webtoon mode vs single-page mode
            if (m_continuousScrollMode && webtoonScroll) {
                // Update webtoon scroll view with new pages and scroll to start
                float viewW = webtoonScroll->getWidth();
                if (viewW <= 0) viewW = container ? container->getWidth() : 960.0f;
                webtoonScroll->setPages(m_pages, viewW, m_currentPage);
                initWebtoonSegments();
                setupWebtoonTransitionText();
            } else {
                loadPage(m_currentPage);
            }

            updatePageDisplay();

            // Start preloading adjacent chapters
            preloadNextChapter();
            preloadPrevChapter();
        } else {
            m_startPage = 0;
            m_pages.clear();
            loadPages();
        }
    }
}

void ReaderActivity::previousChapter() {
    brls::Logger::info("PREVCHAPTER: chapterPos={}/{} prevPreloaded={}", m_chapterPosition, m_totalChapters, m_prevChapterLoaded);
    if (m_chapterPosition > 0 &&
        m_chapterPosition - 1 < static_cast<int>(m_chapters.size())) {
        m_chapterPosition--;
        m_chapterIndex = m_chapters[m_chapterPosition].id;
        m_chapterName = m_chapters[m_chapterPosition].name;
        // Reset next chapter preload since we're changing position
        m_nextChapterLoaded = false;
        m_nextChapterPages.clear();
        m_currentPage = 0;

        // Check if we should land at the end of the chapter
        AppSettings& appSettings = Application::getInstance().getSettings();
        m_goToEndAfterLoad = appSettings.goToEndOnPrevChapter;

        // Use preloaded pages if available for instant transition (avoids memory spike)
        if (m_prevChapterLoaded && !m_prevChapterPages.empty()) {
            m_pages = std::move(m_prevChapterPages);
            m_prevChapterLoaded = false;
            m_prevChapterPages.clear();

            // Insert fake transition pages at chapter boundaries
            m_realPageCount = static_cast<int>(m_pages.size());
            m_startPage = 0;
            insertTransitionPages();

            // Land at end or beginning depending on setting
            if (m_goToEndAfterLoad) {
                m_goToEndAfterLoad = false;
                m_currentPage = static_cast<int>(m_pages.size()) - 1;
                while (m_currentPage > 0 && isTransitionPage(m_currentPage)) {
                    m_currentPage--;
                }
            } else {
                m_currentPage = isTransitionPage(0) ? 1 : 0;
            }

            // Update UI
            if (chapterLabel) {
                std::string label = m_chapterName.empty() ?
                    "Chapter " + getChapterDisplayNumber() : m_chapterName;
                chapterLabel->setText(label);
            }
            if (chapterProgress) {
                chapterProgress->setText("Ch. " + getChapterDisplayNumber() +
                                         " of " + std::to_string(m_totalChapters));
            }

            // Handle webtoon mode vs single-page mode
            if (m_continuousScrollMode && webtoonScroll) {
                float viewW = webtoonScroll->getWidth();
                if (viewW <= 0) viewW = container ? container->getWidth() : 960.0f;
                // Pass m_currentPage as startPage so the scroll position and
                // image loading begin at the target page, avoiding a visible
                // snap from position 0 to the end of the chapter.
                webtoonScroll->setPages(m_pages, viewW, m_currentPage);
                initWebtoonSegments();
                setupWebtoonTransitionText();
            } else {
                loadPage(m_currentPage);
            }

            updatePageDisplay();

            // Start preloading adjacent chapters
            preloadNextChapter();
            preloadPrevChapter();
        } else {
            m_prevChapterLoaded = false;
            m_prevChapterPages.clear();
            m_startPage = 0;
            m_pages.clear();

            // Don't call clearPages() here — let loadPages()'s callback handle
            // it via setPages() so the old content stays visible during the
            // async fetch, avoiding a blank-screen flash / snap.

            loadPages();
        }
    }
}

void ReaderActivity::markChapterAsRead() {
    int mangaId = m_mangaId;
    int chapterId = m_chapterIndex;  // This is the chapter ID
    int chapterPos = m_chapterPosition;
    int totalChapters = m_totalChapters;

    // Track every chapter marked read so the detail view can update all of them
    if (std::find(m_readChapterIds.begin(), m_readChapterIds.end(), chapterId) == m_readChapterIds.end()) {
        m_readChapterIds.push_back(chapterId);
    }

    // Always mark locally in downloads manager (works offline)
    DownloadsManager::getInstance().markChapterReadLocally(mangaId, chapterId);

    if (!Application::getInstance().isConnected()) {
        brls::Logger::info("ReaderActivity: offline, marked chapter read locally (will sync later)");
        // Still update reading statistics locally
        bool mangaCompleted = (chapterPos >= 0 && chapterPos == totalChapters - 1);
        Application::getInstance().updateReadingStatistics(true, mangaCompleted);
        return;
    }

    vitasuwayomi::asyncRun([mangaId, chapterId, chapterPos, totalChapters]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        if (client.markChapterRead(mangaId, chapterId)) {
            // Update reading statistics on the main thread
            brls::sync([chapterPos, totalChapters]() {
                // Check if this was the last chapter (manga completed)
                bool mangaCompleted = (chapterPos == totalChapters - 1);
                Application::getInstance().updateReadingStatistics(true, mangaCompleted);
            });

            // Delete downloaded chapter if deleteAfterRead is enabled
            if (Application::getInstance().getSettings().deleteAfterRead) {
                brls::Logger::info("ReaderActivity: deleteAfterRead enabled, removing chapter download");

                // Delete from local downloads
                DownloadsManager& dm = DownloadsManager::getInstance();
                if (dm.deleteChapterDownload(mangaId, chapterId)) {
                    brls::Logger::info("ReaderActivity: Deleted local chapter download (manga={}, chapter={})",
                                      mangaId, chapterId);
                }

                // Also delete from server download queue
                std::vector<int> chapterIds = {chapterId};
                std::vector<int> chapterIndexes = {chapterId};
                client.deleteChapterDownloads(chapterIds, mangaId, chapterIndexes);
                brls::Logger::info("ReaderActivity: Requested server to delete chapter download (id={})", chapterId);
            }
        }
    });
}

void ReaderActivity::toggleControls() {
    if (m_errorOverlay) return;  // Don't toggle controls while error overlay is shown

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

    // Disable focus on the full-screen content views so D-pad doesn't
    // land on them (focus highlight would appear at the corner)
    if (pageImage) pageImage->setFocusable(false);
    if (webtoonScroll) webtoonScroll->setFocusable(false);

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

    // Re-enable focus on content views
    if (pageImage) pageImage->setFocusable(true);
    if (webtoonScroll) webtoonScroll->setFocusable(true);

    // Return focus to content
    if (m_continuousScrollMode && webtoonScroll) {
        brls::Application::giveFocus(webtoonScroll);
    } else if (pageImage) {
        brls::Application::giveFocus(pageImage);
    }

    // Show page counter when controls are hidden
    showPageCounter();
    m_controlsVisible = false;
}

void ReaderActivity::showPageCounter() {
    if (pageCounter) {
        // Respect the showPageNumber setting
        AppSettings& appSettings = Application::getInstance().getSettings();
        if (!appSettings.showPageNumber) {
            pageCounter->setAlpha(0.0f);
            pageCounter->setVisibility(brls::Visibility::GONE);
            return;
        }
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
    // Auto-hide page counter after 2 seconds (NOBORU style)
    int generation = ++m_pageCounterHideGeneration;
    std::weak_ptr<bool> aliveWeak = m_alive;

    vitasuwayomi::asyncRun([this, aliveWeak, generation]() {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        brls::sync([this, aliveWeak, generation]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            // Only hide if no newer show/page-turn happened since we were scheduled
            if (generation == m_pageCounterHideGeneration && !m_controlsVisible) {
                hidePageCounter();
            }
        });
    });
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

        // Give focus to the first settings button
        if (settingsFormatBtn) {
            brls::Application::giveFocus(settingsFormatBtn);
        }

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

        // Return focus to the content view
        if (m_continuousScrollMode && webtoonScroll) {
            brls::Application::giveFocus(webtoonScroll);
        } else if (pageImage) {
            brls::Application::giveFocus(pageImage);
        }

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

    // Update scale label
    if (settingsScaleLabel) {
        std::string scaleText;
        switch (m_settings.scaleMode) {
            case ReaderScaleMode::FIT_SCREEN: scaleText = "Fit Screen"; break;
            case ReaderScaleMode::FIT_WIDTH: scaleText = "Fit Width"; break;
            case ReaderScaleMode::FIT_HEIGHT: scaleText = "Fit Height"; break;
            case ReaderScaleMode::ORIGINAL: scaleText = "Original (1:1)"; break;
        }
        settingsScaleLabel->setText(scaleText);
    }
}

void ReaderActivity::applySettings() {
    if (!pageImage) return;

    // Map reader scale mode to image scale mode
    vitasuwayomi::ImageScaleMode imgMode = vitasuwayomi::ImageScaleMode::FIT_SCREEN;
    switch (m_settings.scaleMode) {
        case ReaderScaleMode::FIT_SCREEN:
            imgMode = vitasuwayomi::ImageScaleMode::FIT_SCREEN;
            break;
        case ReaderScaleMode::FIT_WIDTH:
            imgMode = vitasuwayomi::ImageScaleMode::FIT_WIDTH;
            break;
        case ReaderScaleMode::FIT_HEIGHT:
            imgMode = vitasuwayomi::ImageScaleMode::FIT_HEIGHT;
            break;
        case ReaderScaleMode::ORIGINAL:
            imgMode = vitasuwayomi::ImageScaleMode::ORIGINAL;
            break;
    }

    // Apply scaling to both main and preview images
    pageImage->setScaleMode(imgMode);
    if (previewImage) {
        previewImage->setScaleMode(imgMode);
    }

    // Apply rotation to both main and preview images
    float rotation = static_cast<float>(m_settings.rotation);
    pageImage->setRotation(rotation);
    if (previewImage) {
        previewImage->setRotation(rotation);
    }
    if (previewImageB) {
        previewImageB->setRotation(rotation);
    }

    // Apply rotation to webtoon scroll view if in continuous mode
    if (webtoonScroll) {
        webtoonScroll->setRotation(rotation);
    }

    // Apply rotation to transition page
    if (transitionBox) {
        transitionBox->setRotation(rotation);
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

// Touch handling is now implemented inline in gesture recognizers (onContentAvailable)

int ReaderActivity::getTapZone(brls::Point position) const {
    // Map physical tap position to user's perceived left/center/right zone,
    // accounting for how the device is held at each rotation.
    //   0°:   user's left = low X,  right = high X  (screen width 960)
    //   90°:  user's left = low Y,  right = high Y  (screen height 544)
    //   180°: user's left = high X, right = low X   (screen width 960, inverted)
    //   270°: user's left = high Y, right = low Y   (screen height 544, inverted)
    float normalized;
    int rotation = static_cast<int>(m_settings.rotation);
    if (rotation == 90) {
        normalized = position.y / 544.0f;
    } else if (rotation == 270) {
        normalized = 1.0f - (position.y / 544.0f);
    } else if (rotation == 180) {
        normalized = 1.0f - (position.x / 960.0f);
    } else {
        normalized = position.x / 960.0f;
    }

    if (normalized < 1.0f / 3.0f) return -1;  // left zone
    if (normalized > 2.0f / 3.0f) return +1;   // right zone
    return 0;                                    // center zone
}

void ReaderActivity::handleDoubleTap(brls::Point position) {
    if (m_isZoomed) {
        // Zoomed in - reset to normal view
        resetZoom();
    }
    // Double-tap only resets zoom; pinch-to-zoom is the only way to zoom in
}

void ReaderActivity::resetZoom() {
    m_isZoomed = false;
    m_zoomLevel = 1.0f;
    m_zoomOffset = {0, 0};

    if (pageImage) {
        pageImage->resetZoom();
        // Restore scale mode settings
        applySettings();
    }
}

void ReaderActivity::zoomTo(float level, brls::Point center) {
    m_isZoomed = true;
    m_zoomLevel = level;

    if (pageImage) {
        pageImage->setZoomLevel(level);

        // Calculate zoom offset to center on the tap position
        // The NVG transform is: P' = imgCenter + z*(P - imgCenter + offset)
        // For the tapped point to stay fixed: offset = (1-z)/z * (tap - imgCenter)
        brls::Rect frame = pageImage->getFrame();
        float relX = (center.x - frame.getMinX()) / frame.getWidth();
        float relY = (center.y - frame.getMinY()) / frame.getHeight();

        // Focal point zoom offset (divide by level for correct transform math)
        brls::Point offset = {
            -(relX - 0.5f) * frame.getWidth() * (level - 1.0f) / level,
            -(relY - 0.5f) * frame.getHeight() * (level - 1.0f) / level
        };

        m_zoomOffset = offset;
        pageImage->setZoomOffset(offset);

        std::string zoomText = "Zoom: " + std::to_string(static_cast<int>(level * 100)) + "%";
        brls::Application::notify(zoomText);
    }
}

void ReaderActivity::handlePinchZoom(float scaleFactor) {
    // Pinch-to-zoom - scale by the pinch factor
    float newZoom = m_zoomLevel * scaleFactor;

    // Clamp zoom between 1.0x and 4.0x (no zoom-out past fit)
    newZoom = std::max(1.0f, std::min(4.0f, newZoom));

    if (newZoom != m_zoomLevel) {
        if (newZoom <= 1.05f && newZoom >= 0.95f) {
            // Near 1x - snap to fit
            resetZoom();
        } else {
            m_zoomLevel = newZoom;
            m_isZoomed = (newZoom > 1.0f);

            if (pageImage) {
                pageImage->setZoomLevel(newZoom);

                // Preserve current zoom offset (user's pan position)
                pageImage->setZoomOffset(m_zoomOffset);
            }
        }
    }
}

void ReaderActivity::showPageError(const std::string& message) {
    if (!container) return;

    hidePageError();  // Remove any existing error overlay

    m_errorOverlay = new brls::Box();
    m_errorOverlay->setAxis(brls::Axis::COLUMN);
    m_errorOverlay->setJustifyContent(brls::JustifyContent::CENTER);
    m_errorOverlay->setAlignItems(brls::AlignItems::CENTER);
    m_errorOverlay->setWidth(960);
    m_errorOverlay->setHeight(544);
    m_errorOverlay->setPositionType(brls::PositionType::ABSOLUTE);
    m_errorOverlay->setPositionTop(0);
    m_errorOverlay->setPositionLeft(0);
    m_errorOverlay->setBackgroundColor(Application::getInstance().getErrorOverlayBg());

    m_errorLabel = new brls::Label();
    m_errorLabel->setText(message);
    m_errorLabel->setFontSize(18);
    m_errorLabel->setTextColor(nvgRGB(200, 200, 200));
    m_errorLabel->setMarginBottom(20);
    m_errorOverlay->addView(m_errorLabel);

    m_retryButton = new brls::Button();
    m_retryButton->setText("Retry");
    m_retryButton->setWidth(160);
    m_retryButton->setHeight(44);
    m_retryButton->setCornerRadius(22);
    m_retryButton->setBackgroundColor(Application::getInstance().getCtaButtonColor());
    std::weak_ptr<bool> retryAlive = m_alive;
    m_retryButton->registerClickAction([this, retryAlive](brls::View* view) {
        if (!Application::getInstance().isConnected() && !m_loadedFromLocal) {
            brls::Application::notify("App is offline");
            return true;
        }
        // Defer to next frame so the click event finishes processing before
        // the button is removed from the view hierarchy. Without this,
        // hidePageError() destroys the button mid-callback, and borealis's
        // hover/focus state machine accesses the freed view.
        brls::sync([this, retryAlive]() {
            auto alive = retryAlive.lock();
            if (!alive || !*alive) return;
            hidePageError();
            if (m_pages.empty()) {
                loadPages();
            } else {
                loadPage(m_currentPage);
            }
        });
        return true;
    });
    m_retryButton->addGestureRecognizer(new brls::TapGestureRecognizer(m_retryButton));
    m_errorOverlay->addView(m_retryButton);

    // Intercept all touch events on the overlay so they don't pass through
    // to the container/pageImage behind it (prevents hover transfer errors)
    m_errorOverlay->addGestureRecognizer(new brls::TapGestureRecognizer(
        [](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
            // Consume the tap - do nothing
        }));
    m_errorOverlay->addGestureRecognizer(new brls::PanGestureRecognizer(
        [](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
            // Consume the swipe - do nothing
        }, brls::PanAxis::ANY));
    m_errorOverlay->setFocusable(false);

    container->addView(m_errorOverlay);
    brls::Application::giveFocus(m_retryButton);
}

void ReaderActivity::hidePageError() {
    if (m_errorOverlay && container) {
        container->removeView(m_errorOverlay);
        m_errorOverlay = nullptr;
        m_errorLabel = nullptr;
        m_retryButton = nullptr;

        // Restore focus to the appropriate content view
        if (m_continuousScrollMode && webtoonScroll) {
            brls::Application::giveFocus(webtoonScroll);
        } else if (pageImage) {
            brls::Application::giveFocus(pageImage);
        }
    }
}

bool ReaderActivity::isTransitionPage(int index) const {
    if (index < 0 || index >= static_cast<int>(m_pages.size())) return false;
    const std::string& url = m_pages[index].imageUrl;
    return url == TRANSITION_NEXT || url == TRANSITION_PREV || url == TRANSITION_END;
}

void ReaderActivity::insertTransitionPages() {
    // Insert a fake "previous chapter" page at the beginning
    if (m_chapterPosition > 0) {
        Page prevPage;
        prevPage.imageUrl = TRANSITION_PREV;
        prevPage.index = -1;
        m_pages.insert(m_pages.begin(), prevPage);
        // Shift start page to account for inserted page
        m_startPage++;
    }

    // Insert a fake "next chapter" or "end" page at the end
    Page endPage;
    if (m_chapterPosition >= 0 && m_chapterPosition < m_totalChapters - 1) {
        endPage.imageUrl = TRANSITION_NEXT;
    } else {
        endPage.imageUrl = TRANSITION_END;
    }
    endPage.index = -1;
    m_pages.push_back(endPage);

    m_realPageCount = static_cast<int>(m_pages.size());
    // Subtract the fake pages from the real count
    if (m_chapterPosition > 0) m_realPageCount--;
    m_realPageCount--;  // End page is always inserted
}

void ReaderActivity::renderTransitionPage(int index) {
    if (!transitionBox) return;

    // Hide the manga page image, show the transition page
    if (pageImage) {
        pageImage->setVisibility(brls::Visibility::GONE);
    }

    const std::string& url = m_pages[index].imageUrl;

    std::string currentChapterDisplay = m_chapterName.empty() ?
        "Chapter " + getChapterDisplayNumber() : m_chapterName;

    std::string line1;
    std::string line2;

    if (url == TRANSITION_NEXT) {
        line1 = "End of: " + currentChapterDisplay;
        if (m_chapterPosition >= 0 && m_chapterPosition < m_totalChapters - 1) {
            const Chapter& nextCh = m_chapters[m_chapterPosition + 1];
            std::string nextName = nextCh.name;
            if (nextName.empty()) {
                float num = nextCh.chapterNumber;
                if (num == static_cast<int>(num))
                    nextName = "Chapter " + std::to_string(static_cast<int>(num));
                else {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "Chapter %.1f", num);
                    nextName = buf;
                }
            }
            line2 = "Next: " + nextName;
        }
    } else if (url == TRANSITION_PREV) {
        line1 = "Beginning of: " + currentChapterDisplay;
        if (m_chapterPosition > 0) {
            const Chapter& prevCh = m_chapters[m_chapterPosition - 1];
            std::string prevName = prevCh.name;
            if (prevName.empty()) {
                float num = prevCh.chapterNumber;
                if (num == static_cast<int>(num))
                    prevName = "Chapter " + std::to_string(static_cast<int>(num));
                else {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "Chapter %.1f", num);
                    prevName = buf;
                }
            }
            line2 = "Previous: " + prevName;
        }
    } else if (url == TRANSITION_END) {
        line1 = "End of: " + currentChapterDisplay;
        line2 = "You've reached the end!";
    }

    // Update the permanent transition page labels and show it
    if (transitionLine1) transitionLine1->setText(line1);
    if (transitionLine2) transitionLine2->setText(line2);
    transitionBox->setVisibility(brls::Visibility::VISIBLE);

    // Hide the preview thumbnail — not needed on the transition page
    if (transitionPreview) {
        transitionPreview->setVisibility(brls::Visibility::GONE);
    }

    // Transfer focus to transitionBox so dpad registerAction callbacks fire
    brls::Application::giveFocus(transitionBox);
}

void ReaderActivity::setupWebtoonTransitionText() {
    if (!webtoonScroll) return;

    std::string currentChapterDisplay = m_chapterName.empty() ?
        "Chapter " + getChapterDisplayNumber() : m_chapterName;

    for (int i = 0; i < static_cast<int>(m_pages.size()); i++) {
        if (!isTransitionPage(i)) continue;

        const std::string& url = m_pages[i].imageUrl;
        std::string line1, line2;

        if (url == TRANSITION_NEXT) {
            line1 = "End of: " + currentChapterDisplay;
            if (m_chapterPosition >= 0 && m_chapterPosition < m_totalChapters - 1) {
                const Chapter& nextCh = m_chapters[m_chapterPosition + 1];
                std::string nextName = nextCh.name;
                if (nextName.empty()) {
                    float num = nextCh.chapterNumber;
                    if (num == static_cast<int>(num))
                        nextName = "Chapter " + std::to_string(static_cast<int>(num));
                    else {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "Chapter %.1f", num);
                        nextName = buf;
                    }
                }
                line2 = "Next: " + nextName;
            }
        } else if (url == TRANSITION_PREV) {
            line1 = "Beginning of: " + currentChapterDisplay;
            if (m_chapterPosition > 0) {
                const Chapter& prevCh = m_chapters[m_chapterPosition - 1];
                std::string prevName = prevCh.name;
                if (prevName.empty()) {
                    float num = prevCh.chapterNumber;
                    if (num == static_cast<int>(num))
                        prevName = "Chapter " + std::to_string(static_cast<int>(num));
                    else {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "Chapter %.1f", num);
                        prevName = buf;
                    }
                }
                line2 = "Previous: " + prevName;
            }
        } else if (url == TRANSITION_END) {
            line1 = "End of: " + currentChapterDisplay;
            line2 = "You've reached the end!";
        }

        webtoonScroll->setTransitionText(i, line1, line2);
    }
}

// ---------------------------------------------------------------------------
// Webtoon chapter-boundary tracking
// ---------------------------------------------------------------------------

void ReaderActivity::initWebtoonSegments() {
    m_webtoonSegments.clear();
    if (!webtoonScroll) return;

    // Count real (non-transition) pages and find the first one
    int firstReal = -1;
    int realCount = 0;
    for (int i = 0; i < webtoonScroll->getPageCount(); i++) {
        if (!webtoonScroll->isTransitionPage(i)) {
            if (firstReal < 0) firstReal = i;
            realCount++;
        }
    }

    if (firstReal >= 0 && realCount > 0) {
        WebtoonChapterSegment seg;
        seg.firstPage = firstReal;
        seg.pageCount = realCount;
        seg.chapterId = m_chapterIndex;
        seg.chapterPos = m_chapterPosition;
        m_webtoonSegments.push_back(seg);
    }
}

void ReaderActivity::getWebtoonProgress(int rawPageIndex, int& outChapterId,
                                         int& outChapterPos, int& outPageInChapter,
                                         int& outChapterPageCount) const {
    // Default to current chapter info
    outChapterId = m_chapterIndex;
    outChapterPos = m_chapterPosition;
    outPageInChapter = 0;
    outChapterPageCount = m_realPageCount;

    if (m_webtoonSegments.empty()) return;

    // Find which segment this page belongs to
    for (const auto& seg : m_webtoonSegments) {
        int lastPage = seg.firstPage + seg.pageCount - 1;
        if (rawPageIndex >= seg.firstPage && rawPageIndex <= lastPage) {
            outChapterId = seg.chapterId;
            outChapterPos = seg.chapterPos;
            outPageInChapter = rawPageIndex - seg.firstPage;
            outChapterPageCount = seg.pageCount;
            return;
        }
    }

    // If between segments (on a transition page), find the nearest segment
    // Prefer the segment that starts after this index (user is scrolling toward it)
    for (const auto& seg : m_webtoonSegments) {
        if (rawPageIndex < seg.firstPage) {
            outChapterId = seg.chapterId;
            outChapterPos = seg.chapterPos;
            outPageInChapter = 0;
            outChapterPageCount = seg.pageCount;
            return;
        }
    }

    // Past all segments — use the last one
    const auto& last = m_webtoonSegments.back();
    outChapterId = last.chapterId;
    outChapterPos = last.chapterPos;
    outPageInChapter = last.pageCount - 1;
    outChapterPageCount = last.pageCount;
}

void ReaderActivity::webtoonExtendChapter(bool next) {
    if (!webtoonScroll) {
        // Fallback if webtoon scroll not available
        if (next) nextChapter(); else previousChapter();
        return;
    }

    if (next) {
        brls::Logger::info("WEBTOON_EXTEND: next chapter, chapterPos={}/{} preloaded={}",
                           m_chapterPosition, m_totalChapters, m_nextChapterLoaded);

        if (m_chapterPosition < 0 || m_chapterPosition >= m_totalChapters - 1 ||
            m_chapterPosition + 1 >= static_cast<int>(m_chapters.size())) {
            return;  // No next chapter
        }

        // Get next chapter pages (preloaded or fetch)
        std::vector<Page> newPages;
        if (m_nextChapterLoaded && !m_nextChapterPages.empty()) {
            newPages = std::move(m_nextChapterPages);
            m_nextChapterLoaded = false;
            m_nextChapterPages.clear();
        } else {
            // Not preloaded - fall back to full chapter load
            nextChapter();
            return;
        }

        // Mark current chapter as read and advance position
        markChapterAsRead();
        int nextPos = m_chapterPosition + 1;
        int nextChapterId = m_chapters[nextPos].id;
        std::string nextChapterName = m_chapters[nextPos].name;

        m_prevChapterLoaded = false;
        m_prevChapterPages.clear();

        // Count real pages in newPages (exclude transitions)
        int realNewPages = 0;
        for (const auto& p : newPages) {
            if (p.imageUrl.compare(0, 14, "__transition:") != 0) realNewPages++;
        }

        // Build the pages to append:
        // - The new chapter's real pages
        // - A transition page at the end (next or end)
        std::vector<Page> appendList = std::move(newPages);

        // Add end transition
        Page endPage;
        if (nextPos < m_totalChapters - 1) {
            endPage.imageUrl = TRANSITION_NEXT;
        } else {
            endPage.imageUrl = TRANSITION_END;
        }
        endPage.index = -1;
        appendList.push_back(endPage);

        // Remember where new pages start in the webtoon view.
        // The old trailing transition page is kept as a chapter separator,
        // so new real pages start right after it.
        int newStartIdx = webtoonScroll->getPageCount();

        // Append to webtoon scroll view (transition page kept as separator)
        webtoonScroll->appendPages(appendList);

        // Track the new chapter segment (real pages start at newStartIdx)
        {
            WebtoonChapterSegment seg;
            seg.firstPage = newStartIdx;
            seg.pageCount = realNewPages;
            seg.chapterId = nextChapterId;
            seg.chapterPos = nextPos;
            m_webtoonSegments.push_back(seg);
        }

        // Now advance position/metadata for UI labels
        m_chapterPosition = nextPos;
        m_chapterIndex = nextChapterId;
        m_chapterName = nextChapterName;

        // Set transition text for the newly appended pages
        std::string currentChapterDisplay = m_chapterName.empty() ?
            "Chapter " + getChapterDisplayNumber() : m_chapterName;

        for (int i = newStartIdx; i < webtoonScroll->getPageCount(); i++) {
            // Only set text for transition pages in the new section
            // Check by attempting to set - the view will ignore non-transition pages
            if (i == webtoonScroll->getPageCount() - 1) {
                // Last page is the end/next transition
                std::string line1 = "End of: " + currentChapterDisplay;
                std::string line2;
                if (m_chapterPosition < m_totalChapters - 1) {
                    const Chapter& nextCh = m_chapters[m_chapterPosition + 1];
                    std::string nextName = nextCh.name;
                    if (nextName.empty()) {
                        float num = nextCh.chapterNumber;
                        if (num == static_cast<int>(num))
                            nextName = "Chapter " + std::to_string(static_cast<int>(num));
                        else {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "Chapter %.1f", num);
                            nextName = buf;
                        }
                    }
                    line2 = "Next: " + nextName;
                } else {
                    line2 = "You've reached the end!";
                }
                webtoonScroll->setTransitionText(i, line1, line2);
            }
        }

        // Update UI labels
        if (chapterLabel) {
            std::string label = m_chapterName.empty() ?
                "Chapter " + getChapterDisplayNumber() : m_chapterName;
            chapterLabel->setText(label);
        }
        if (chapterProgress) {
            chapterProgress->setText("Ch. " + getChapterDisplayNumber() +
                                     " of " + std::to_string(m_totalChapters));
        }

        // Trim distant chapters to limit memory usage (keep max 3 segments)
        if (m_webtoonSegments.size() > 3 && webtoonScroll) {
            // Remove the oldest (first) segment from the start
            const auto& oldSeg = m_webtoonSegments.front();
            // Pages to remove: everything from start up to (but not including) the second segment's first page
            // The second segment's firstPage includes any transition page before it
            int trimCount = 0;
            if (m_webtoonSegments.size() >= 2) {
                // Find where the second segment starts (minus its transition separator)
                trimCount = m_webtoonSegments[1].firstPage;
                // Include the transition page before the second segment if it exists
                // Actually, trimCount = secondSeg.firstPage already includes everything before it
            } else {
                trimCount = oldSeg.firstPage + oldSeg.pageCount;
            }

            if (trimCount > 0) {
                brls::Logger::info("WEBTOON_TRIM: removing first segment ({} pages, chapterPos={}), trimCount={}",
                                    oldSeg.pageCount, oldSeg.chapterPos, trimCount);
                webtoonScroll->trimPagesFromStart(trimCount);

                // Shift all remaining segments
                for (auto& seg : m_webtoonSegments) {
                    seg.firstPage -= trimCount;
                }
                // Remove the trimmed segment
                m_webtoonSegments.erase(m_webtoonSegments.begin());
            }
        }

        // Start preloading next adjacent chapters
        preloadNextChapter();
        preloadPrevChapter();

    } else {
        brls::Logger::info("WEBTOON_EXTEND: prev chapter, chapterPos={}/{} preloaded={}",
                           m_chapterPosition, m_totalChapters, m_prevChapterLoaded);

        if (m_chapterPosition <= 0 ||
            m_chapterPosition - 1 >= static_cast<int>(m_chapters.size())) {
            return;  // No previous chapter
        }

        // Get prev chapter pages (preloaded or fetch)
        std::vector<Page> newPages;
        if (m_prevChapterLoaded && !m_prevChapterPages.empty()) {
            newPages = std::move(m_prevChapterPages);
            m_prevChapterLoaded = false;
            m_prevChapterPages.clear();
        } else {
            // Not preloaded - fall back to full chapter load
            previousChapter();
            return;
        }

        int prevPos = m_chapterPosition - 1;
        int prevChapterId = m_chapters[prevPos].id;
        std::string prevChapterName = m_chapters[prevPos].name;

        m_nextChapterLoaded = false;
        m_nextChapterPages.clear();

        // Count real pages in newPages (exclude transitions)
        int realNewPages = 0;
        for (const auto& p : newPages) {
            if (p.imageUrl.compare(0, 14, "__transition:") != 0) realNewPages++;
        }

        // Build the pages to prepend:
        // - A transition page at the start (prev) if there's a further previous chapter
        // - The new chapter's real pages
        std::vector<Page> prependList;

        bool hasPrevTransition = (prevPos > 0);
        if (hasPrevTransition) {
            Page prevPage;
            prevPage.imageUrl = TRANSITION_PREV;
            prevPage.index = -1;
            prependList.push_back(prevPage);
        }

        // Append the chapter's real pages
        for (auto& p : newPages) {
            prependList.push_back(p);
        }

        // Calculate how many pages will be added.
        // prependPages keeps the old leading transition as a chapter separator.
        int netAdded = static_cast<int>(prependList.size());

        // Shift all existing segment firstPage indices by the net-added count
        for (auto& seg : m_webtoonSegments) {
            seg.firstPage += netAdded;
        }

        // Insert new segment at the beginning
        {
            WebtoonChapterSegment seg;
            seg.firstPage = hasPrevTransition ? 1 : 0;  // After optional leading transition
            seg.pageCount = realNewPages;
            seg.chapterId = prevChapterId;
            seg.chapterPos = prevPos;
            m_webtoonSegments.insert(m_webtoonSegments.begin(), seg);
        }

        // Prepend to webtoon scroll view (old leading transition kept as separator)
        webtoonScroll->prependPages(prependList);

        // Now advance position/metadata for UI labels
        m_chapterPosition = prevPos;
        m_chapterIndex = prevChapterId;
        m_chapterName = prevChapterName;

        // Set transition text for the newly prepended pages
        std::string currentChapterDisplay = m_chapterName.empty() ?
            "Chapter " + getChapterDisplayNumber() : m_chapterName;

        if (hasPrevTransition) {
            // First page (index 0) is the new leading transition
            std::string line1 = "Beginning of: " + currentChapterDisplay;
            std::string line2;
            const Chapter& prevCh = m_chapters[m_chapterPosition - 1];
            std::string prevName = prevCh.name;
            if (prevName.empty()) {
                float num = prevCh.chapterNumber;
                if (num == static_cast<int>(num))
                    prevName = "Chapter " + std::to_string(static_cast<int>(num));
                else {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "Chapter %.1f", num);
                    prevName = buf;
                }
            }
            line2 = "Previous: " + prevName;
            webtoonScroll->setTransitionText(0, line1, line2);
        }

        // Update the old transition page (now a separator between prev and current chapter).
        // It was at index 0 before prepend, now shifted by netAdded.
        {
            int sepIdx = netAdded;  // Where the old leading transition now sits
            // Find the current chapter name (the one after this separator)
            int origChapterPos = prevPos + 1;  // The chapter that was originally being read
            if (origChapterPos < static_cast<int>(m_chapters.size())) {
                const Chapter& origCh = m_chapters[origChapterPos];
                std::string origName = origCh.name;
                if (origName.empty()) {
                    float num = origCh.chapterNumber;
                    if (num == static_cast<int>(num))
                        origName = "Chapter " + std::to_string(static_cast<int>(num));
                    else {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "Chapter %.1f", num);
                        origName = buf;
                    }
                }
                std::string sepLine1 = "End of: " + currentChapterDisplay;
                std::string sepLine2 = "Next: " + origName;
                webtoonScroll->setTransitionText(sepIdx, sepLine1, sepLine2);
            }
        }

        // Update UI labels
        if (chapterLabel) {
            std::string label = m_chapterName.empty() ?
                "Chapter " + getChapterDisplayNumber() : m_chapterName;
            chapterLabel->setText(label);
        }
        if (chapterProgress) {
            chapterProgress->setText("Ch. " + getChapterDisplayNumber() +
                                     " of " + std::to_string(m_totalChapters));
        }

        // Trim distant chapters to limit memory usage (keep max 3 segments)
        if (m_webtoonSegments.size() > 3 && webtoonScroll) {
            // Remove the last (newest-at-the-end) segment from the end
            const auto& lastSeg = m_webtoonSegments.back();
            // Pages to remove: from lastSeg.firstPage to the end (including trailing transition)
            int totalPages = webtoonScroll->getPageCount();
            // Find where to start trimming: include the transition page before this segment
            int trimStart = lastSeg.firstPage;
            // Check if there's a transition page right before this segment
            if (trimStart > 0 && webtoonScroll->isTransitionPage(trimStart - 1)) {
                trimStart--;
            }
            int trimCount = totalPages - trimStart;

            if (trimCount > 0) {
                brls::Logger::info("WEBTOON_TRIM: removing last segment ({} pages, chapterPos={}), trimCount={}",
                                    lastSeg.pageCount, lastSeg.chapterPos, trimCount);
                webtoonScroll->trimPagesFromEnd(trimCount);
                m_webtoonSegments.pop_back();
            }
        }

        // Start preloading adjacent chapters
        preloadNextChapter();
        preloadPrevChapter();
    }
}

// NOBORU-style swipe methods

void ReaderActivity::updateSwipePreview(float offset) {
    // 3-page carousel: positive side + current + negative side
    // Uses NanoVG slide offsets (not borealis setTranslationX/Y) to position
    // views, because borealis translations don't reliably move NanoVG
    // rendering on PS Vita's GXM backend. Each view's draw() method applies
    // the slide offset via nvgTranslate inside a scissor clip, so pages
    // slide off screen without overlapping.
    //
    // Use actual view dimensions (internal rendering coords), NOT physical
    // screen dimensions (960x544). The borealis framework renders at a higher
    // internal resolution (~1280x726) that gets scaled to the physical display.
    // Using physical screen dimensions causes pages to be spaced too close
    // together, resulting in overlap during swipe transitions.
    auto [viewWidth, viewHeight] = getSwipeViewSize();

    bool useVerticalSwipe = (m_settings.rotation == ImageRotation::ROTATE_90 ||
                             m_settings.rotation == ImageRotation::ROTATE_270);
    float screenExtent = useVerticalSwipe ? viewHeight : viewWidth;
    offset = std::max(-screenExtent, std::min(screenExtent, offset));

    // Calculate positions for the 3-page strip
    auto makeSlide = [&](float val) -> std::pair<float,float> {
        return useVerticalSwipe ? std::make_pair(0.0f, val) : std::make_pair(val, 0.0f);
    };

    auto [curX, curY] = makeSlide(offset);
    auto [posX, posY] = makeSlide(offset - screenExtent);  // positive side (left/above)
    auto [negX, negY] = makeSlide(offset + screenExtent);  // negative side (right/below)

    // Debug: log slide positions once when swipe first starts or direction changes
    static int s_lastLoggedOffset = -99999;
    int roundedOffset = static_cast<int>(offset / 50) * 50;  // log every ~50px
    if (roundedOffset != s_lastLoggedOffset) {
        s_lastLoggedOffset = roundedOffset;
        brls::Logger::info("SWIPE offset={:.0f} rot={} vert={} extent={:.0f} | "
            "cur=({:.0f},{:.0f}) pos=({:.0f},{:.0f}) neg=({:.0f},{:.0f}) | "
            "posIdx={} negIdx={} posIsTrans={} negIsTrans={}",
            offset, static_cast<int>(m_settings.rotation), useVerticalSwipe, screenExtent,
            curX, curY, posX, posY, negX, negY,
            m_posPreviewIdx, m_negPreviewIdx, m_posIsTransition, m_negIsTransition);
    }

    // Current page
    bool onTransition = transitionBox &&
                        transitionBox->getVisibility() == brls::Visibility::VISIBLE &&
                        isTransitionPage(m_currentPage);
    if (onTransition) {
        transitionBox->setSlideOffset(curX, curY);
    } else if (pageImage) {
        pageImage->setSlideOffset(curX, curY);
    }

    // Positive side preview
    // Show preview if it has a valid in-bounds index OR a cross-chapter image was preloaded
    bool posInBounds = m_posPreviewIdx >= 0 && m_posPreviewIdx < static_cast<int>(m_pages.size());
    bool posCrossLoaded = !posInBounds && onTransition && previewImage && previewImage->hasImage();
    if (m_posIsTransition && transitionBox && !onTransition) {
        transitionBox->setSlideOffset(posX, posY);
    } else if (previewImage && (posInBounds || posCrossLoaded)) {
        previewImage->setVisibility(brls::Visibility::VISIBLE);
        previewImage->setSlideOffset(posX, posY);
    }

    // Negative side preview
    bool negInBounds = m_negPreviewIdx >= 0 && m_negPreviewIdx < static_cast<int>(m_pages.size());
    bool negCrossLoaded = !negInBounds && onTransition && previewImageB && previewImageB->hasImage();
    if (m_negIsTransition && transitionBox && !onTransition && !m_posIsTransition) {
        transitionBox->setSlideOffset(negX, negY);
    } else if (previewImageB && (negInBounds || negCrossLoaded)) {
        previewImageB->setVisibility(brls::Visibility::VISIBLE);
        previewImageB->setSlideOffset(negX, negY);
    }
}

void ReaderActivity::loadPreviewPage(int index) {
    if (index < 0 || index >= static_cast<int>(m_pages.size())) {
        return;
    }

    if (!previewImage) return;

    m_swipeToChapter = false;
    m_previewIsTransition = false;

    // Case 1: Preview target is a transition page — show the transitionBox as
    // the incoming view (populate its labels/thumbnail and slide it in)
    if (isTransitionPage(index)) {
        if (!transitionBox) return;

        // Populate transition box content without changing pageImage visibility
        const std::string& url = m_pages[index].imageUrl;

        std::string currentChapterDisplay = m_chapterName.empty() ?
            "Chapter " + getChapterDisplayNumber() : m_chapterName;

        std::string line1, line2;
        if (url == TRANSITION_NEXT) {
            line1 = "End of: " + currentChapterDisplay;
            if (m_chapterPosition >= 0 && m_chapterPosition < m_totalChapters - 1) {
                const Chapter& nextCh = m_chapters[m_chapterPosition + 1];
                std::string nextName = nextCh.name;
                if (nextName.empty()) {
                    float num = nextCh.chapterNumber;
                    if (num == static_cast<int>(num))
                        nextName = "Chapter " + std::to_string(static_cast<int>(num));
                    else {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "Chapter %.1f", num);
                        nextName = buf;
                    }
                }
                line2 = "Next: " + nextName;
            }
        } else if (url == TRANSITION_PREV) {
            line1 = "Beginning of: " + currentChapterDisplay;
            if (m_chapterPosition > 0) {
                const Chapter& prevCh = m_chapters[m_chapterPosition - 1];
                std::string prevName = prevCh.name;
                if (prevName.empty()) {
                    float num = prevCh.chapterNumber;
                    if (num == static_cast<int>(num))
                        prevName = "Chapter " + std::to_string(static_cast<int>(num));
                    else {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "Chapter %.1f", num);
                        prevName = buf;
                    }
                }
                line2 = "Previous: " + prevName;
            }
        } else if (url == TRANSITION_END) {
            line1 = "End of: " + currentChapterDisplay;
            line2 = "You've reached the end!";
        }

        if (transitionLine1) transitionLine1->setText(line1);
        if (transitionLine2) transitionLine2->setText(line2);

        // Hide preview thumbnail on transition swipe preview
        if (transitionPreview) {
            transitionPreview->setVisibility(brls::Visibility::GONE);
        }

        // Pre-position transitionBox far off-screen BEFORE making it visible,
        // so its opaque background never covers pageImage even for a single frame.
        // updateSwipePreview() will set the correct slide offset right after.
        transitionBox->setSlideOffset(10000.0f, 0.0f);
        transitionBox->setVisibility(brls::Visibility::VISIBLE);
        m_previewIsTransition = true;

        brls::Logger::debug("Loading transition preview for page {}", index);
        return;
    }

    // Case 2: Normal page preview
    const Page& page = m_pages[index];
    std::string imageUrl = page.imageUrl;

    previewImage->setRotation(static_cast<float>(m_settings.rotation));

    brls::Logger::debug("Loading preview page {}", index);

    std::weak_ptr<bool> aliveWeak = m_alive;
    auto previewAlive = m_previewLoadAlive ? m_previewLoadAlive : m_alive;
    ImageLoader::loadAsyncFullSize(imageUrl, [aliveWeak, index](RotatableImage* img) {
        auto alive = aliveWeak.lock();
        if (!alive || !*alive) return;
        brls::Logger::debug("Preview page {} loaded", index);
    }, previewImage, previewAlive);
}

void ReaderActivity::loadPreviewInto(RotatableImage* target, int index) {
    if (!target || index < 0 || index >= static_cast<int>(m_pages.size())) return;
    if (isTransitionPage(index)) return;  // Transition pages use transitionBox, not images

    const Page& page = m_pages[index];
    std::string imageUrl = page.imageUrl;

    target->setRotation(static_cast<float>(m_settings.rotation));

    std::weak_ptr<bool> aliveWeak = m_alive;
    // Use m_previewLoadAlive so stale preview loads are cancelled when
    // the user moves to a new page and preloadAdjacentPreviews runs again.
    auto previewAlive = m_previewLoadAlive ? m_previewLoadAlive : m_alive;
    ImageLoader::loadAsyncFullSize(imageUrl, [aliveWeak, index](RotatableImage* img) {
        auto alive = aliveWeak.lock();
        if (!alive || !*alive) return;
    }, target, previewAlive);
}

std::pair<float, float> ReaderActivity::getSwipeViewSize() {
    // Use whichever view is currently visible for accurate dimensions.
    // When on a transition page, pageImage is GONE (dims == 0), so
    // we fall back to transitionBox, then to the physical screen size.
    float w = 0, h = 0;
    if (pageImage && pageImage->getVisibility() == brls::Visibility::VISIBLE) {
        w = pageImage->getWidth();
        h = pageImage->getHeight();
    }
    if ((w <= 0 || h <= 0) && transitionBox &&
        transitionBox->getVisibility() == brls::Visibility::VISIBLE) {
        w = transitionBox->getWidth();
        h = transitionBox->getHeight();
    }
    if (w <= 0) w = 960.0f;
    if (h <= 0) h = 544.0f;
    return {w, h};
}

void ReaderActivity::preloadAdjacentPreviews() {
    // Invalidate any in-flight preview loads from a previous page so stale
    // downloads (e.g. stuck behind a 401 token refresh) don't create GPU
    // textures for pages the user has already swiped past.
    if (m_previewLoadAlive) *m_previewLoadAlive = false;
    m_previewLoadAlive = std::make_shared<bool>(true);

    // Determine which page goes on the positive side (swiping right/down reveals it)
    // and which goes on the negative side (swiping left/up reveals it).
    bool invertDirection = (m_settings.rotation == ImageRotation::ROTATE_180 ||
                            m_settings.rotation == ImageRotation::ROTATE_270);
    bool isRTL = (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT);
    // In RTL at 0° rotation, swiping positive (right) → next page
    bool positiveIsNext = (isRTL != invertDirection);

    int posIdx = positiveIsNext ? m_currentPage + 1 : m_currentPage - 1;
    int negIdx = positiveIsNext ? m_currentPage - 1 : m_currentPage + 1;

    // Helper: when on a transition page and the adjacent slot is out of bounds,
    // preload the cross-chapter page so the preview is ready before the user swipes.
    auto preloadCrossChapter = [&](int idx, RotatableImage* target, bool isNext) -> bool {
        if (!target || !isTransitionPage(m_currentPage) || m_continuousScrollMode) return false;
        if (idx >= 0 && idx < static_cast<int>(m_pages.size())) return false; // in-bounds, not needed

        // Skip if the target already has an image (e.g. from early cross-chapter preload)
        if (target->hasImage()) return true;

        const std::string& tUrl = m_pages[m_currentPage].imageUrl;
        std::string crossUrl;
        if (isNext && tUrl == TRANSITION_NEXT && m_nextChapterLoaded && !m_nextChapterPages.empty()) {
            crossUrl = m_nextChapterPages[0].imageUrl;
        } else if (!isNext && tUrl == TRANSITION_PREV && m_prevChapterLoaded && !m_prevChapterPages.empty()) {
            crossUrl = m_prevChapterPages.back().imageUrl;
        }
        if (crossUrl.empty()) return false;

        target->setRotation(static_cast<float>(m_settings.rotation));
        auto previewAlive = m_previewLoadAlive ? m_previewLoadAlive : m_alive;
        std::weak_ptr<bool> aliveWeak = m_alive;
        ImageLoader::loadAsyncFullSize(crossUrl, [aliveWeak](RotatableImage*) {
            auto a = aliveWeak.lock();
            if (!a || !*a) return;
        }, target, previewAlive);
        return true;
    };

    // Load positive side
    m_posIsTransition = false;
    if (posIdx >= 0 && posIdx < static_cast<int>(m_pages.size())) {
        m_posPreviewIdx = posIdx;
        if (isTransitionPage(posIdx)) {
            m_posIsTransition = true;
            // Transition uses transitionBox, not previewImage. Clear any stale
            // image so it doesn't get mistaken for a valid cross-chapter preview.
            if (previewImage) previewImage->clearImage();
        } else if (previewImage) {
            loadPreviewInto(previewImage, posIdx);
        }
    } else if (preloadCrossChapter(posIdx, previewImage, positiveIsNext)) {
        // Cross-chapter page preloaded — use a sentinel so updateSwipePreview shows it
        m_posPreviewIdx = posIdx;
    } else {
        m_posPreviewIdx = -1;
    }

    // Load negative side
    m_negIsTransition = false;
    if (negIdx >= 0 && negIdx < static_cast<int>(m_pages.size())) {
        m_negPreviewIdx = negIdx;
        if (isTransitionPage(negIdx)) {
            m_negIsTransition = true;
            // Clear stale image (same reason as positive side above).
            if (previewImageB) previewImageB->clearImage();
        } else if (previewImageB) {
            loadPreviewInto(previewImageB, negIdx);
        }
    } else if (preloadCrossChapter(negIdx, previewImageB, !positiveIsNext)) {
        m_negPreviewIdx = negIdx;
    } else {
        m_negPreviewIdx = -1;
    }

    // Early cross-chapter preload: when adjacent to a transition page, start
    // loading the cross-chapter image one page ahead so it's ready when the
    // user reaches the transition page. Uses a separate alive guard so the
    // load survives the next preloadAdjacentPreviews() call.
    if (!m_continuousScrollMode && (m_posIsTransition || m_negIsTransition)) {
        if (!m_crossChapterPreloadAlive)
            m_crossChapterPreloadAlive = std::make_shared<bool>(true);

        auto earlyCrossPreload = [&](int transIdx, RotatableImage* target, bool isNext) {
            if (!target || transIdx < 0 || transIdx >= static_cast<int>(m_pages.size())) return;
            if (target->hasImage()) return; // already loaded

            const std::string& tUrl = m_pages[transIdx].imageUrl;
            std::string crossUrl;
            if (isNext && tUrl == TRANSITION_NEXT && m_nextChapterLoaded && !m_nextChapterPages.empty()) {
                crossUrl = m_nextChapterPages[0].imageUrl;
            } else if (!isNext && tUrl == TRANSITION_PREV && m_prevChapterLoaded && !m_prevChapterPages.empty()) {
                crossUrl = m_prevChapterPages.back().imageUrl;
            }
            if (crossUrl.empty()) return;

            target->setRotation(static_cast<float>(m_settings.rotation));
            std::weak_ptr<bool> aliveWeak = m_alive;
            ImageLoader::loadAsyncFullSize(crossUrl, [aliveWeak](RotatableImage*) {
                auto a = aliveWeak.lock();
                if (!a || !*a) return;
            }, target, m_crossChapterPreloadAlive);

            brls::Logger::info("Early cross-chapter preload started for {} chapter",
                               isNext ? "next" : "prev");
        };

        // When positive side is a transition, previewImage is free (transition
        // uses transitionBox). Preload the cross-chapter image into previewImage.
        if (m_posIsTransition && previewImage) {
            earlyCrossPreload(posIdx, previewImage, positiveIsNext);
        }
        // When negative side is a transition, previewImageB is free.
        if (m_negIsTransition && previewImageB) {
            earlyCrossPreload(negIdx, previewImageB, !positiveIsNext);
        }
    } else {
        // Not near a transition — cancel any early cross-chapter preloads
        if (m_crossChapterPreloadAlive) {
            *m_crossChapterPreloadAlive = false;
            m_crossChapterPreloadAlive.reset();
        }
    }
}

void ReaderActivity::completeSwipeAnimation(bool turnPage) {
    brls::Logger::info("COMPLETE: turnPage={} swipeToChapter={} previewIsTransition={} "
        "previewIdx={} swipingToNext={} currentPage={}",
        turnPage, m_swipeToChapter, m_previewIsTransition,
        m_previewPageIndex, m_swipingToNext, m_currentPage);

    // Determine target offset for slide animation
    // Use actual view dimensions, not physical screen dimensions
    bool useVerticalSwipe = (m_settings.rotation == ImageRotation::ROTATE_90 ||
                             m_settings.rotation == ImageRotation::ROTATE_270);
    auto [viewWidth, viewHeight] = getSwipeViewSize();
    float screenExtent = useVerticalSwipe ? viewHeight : viewWidth;

    if (turnPage && m_swipeToChapter) {
        // Swiped on a transition page showing a chapter page preview —
        // animate the slide, then navigate to the chapter after animation completes.
        brls::Logger::info("COMPLETE: taking CHAPTER path, swipingToNext={}", m_swipingToNext);
        float target = (m_swipeOffset > 0) ? screenExtent : -screenExtent;
        m_completionOffset = m_swipeOffset;
        m_completionTarget = target;
        m_completionTurnPage = false;
        m_completionNavChapter = true;
        m_completionNavNext = m_swipingToNext;
        m_completionAnimating = true;
        animateSwipeCompletion();
        return;
    }

    if (turnPage && (m_previewIsTransition || (m_previewPageIndex >= 0 &&
        m_previewPageIndex < static_cast<int>(m_pages.size())))) {
        // Animate slide to full screen extent, then finalize page turn
        float target = (m_swipeOffset > 0) ? screenExtent : -screenExtent;
        m_completionOffset = m_swipeOffset;
        m_completionTarget = target;
        m_completionTurnPage = true;
        m_completionNavChapter = false;
        m_completionAnimating = true;
        animateSwipeCompletion();
        return;
    }

    if (turnPage && isTransitionPage(m_currentPage)) {
        // Swiped past a transition page boundary with no preview page available.
        // Animate the slide, then trigger chapter navigation after.
        const std::string& url = m_pages[m_currentPage].imageUrl;
        brls::Logger::info("COMPLETE: taking FALLBACK path, url={} swipingToNext={}", url, m_swipingToNext);

        // Determine if this should actually navigate to a chapter
        bool shouldNavChapter = false;
        bool navNext = false;
        if (m_swipingToNext) {
            if (url == TRANSITION_NEXT) { shouldNavChapter = true; navNext = true; }
            else if (url == TRANSITION_PREV) { shouldNavChapter = true; navNext = false; }
        } else {
            if (url == TRANSITION_PREV) { shouldNavChapter = true; navNext = false; }
            else if (url == TRANSITION_NEXT) { shouldNavChapter = true; navNext = false; }
        }

        if (shouldNavChapter) {
            float target = (m_swipeOffset > 0) ? screenExtent : -screenExtent;
            m_completionOffset = m_swipeOffset;
            m_completionTarget = target;
            m_completionTurnPage = false;
            m_completionNavChapter = true;
            m_completionNavNext = navNext;
            m_completionAnimating = true;
            animateSwipeCompletion();
            return;
        }
    }

    // Snap back — animate smoothly to 0
    m_completionOffset = m_swipeOffset;
    m_completionTarget = 0.0f;
    m_completionTurnPage = false;
    m_completionNavChapter = false;
    m_completionAnimating = true;
    animateSwipeCompletion();
}

void ReaderActivity::animateSwipeCompletion() {
    if (!m_completionAnimating) return;
    if (m_alive && !*m_alive) { m_completionAnimating = false; return; }

    float diff = m_completionTarget - m_completionOffset;
    if (std::abs(diff) < 3.0f) {
        // Close enough to target — finalize
        m_completionAnimating = false;
        if (m_completionNavChapter) {
            // Chapter navigation after slide animation
            bool navNext = m_completionNavNext;
            m_completionNavChapter = false;
            resetSwipeState();
            if (navNext) {
                nextChapter();
            } else {
                previousChapter();
            }
        } else if (m_completionTurnPage) {
            finalizePageTurn();
        } else {
            resetSwipeState();
        }
        return;
    }

    // Ease toward target (~30% of remaining distance per frame)
    m_completionOffset += diff * 0.3f;
    updateSwipePreview(m_completionOffset);

    // Schedule next frame
    std::weak_ptr<bool> aliveWeak = m_alive;
    brls::sync([this, aliveWeak]() {
        auto alive = aliveWeak.lock();
        if (!alive || !*alive) return;
        animateSwipeCompletion();
    });
}

void ReaderActivity::finalizePageTurn() {
    brls::Logger::info("FINALIZE: previewIdx={} previewIsTransition={}",
        m_previewPageIndex, m_previewIsTransition);

    if (m_previewIsTransition) {
        m_previewIsTransition = false;
        m_currentPage = m_previewPageIndex;
        updatePageDisplay();
        loadPage(m_currentPage);
    } else if (m_previewPageIndex >= 0 &&
               m_previewPageIndex < static_cast<int>(m_pages.size())) {
        // Transfer the already-loaded preview image to pageImage for a
        // seamless transition. This avoids a brief flash of the old page
        // that would occur if we hid the preview before the async load
        // into pageImage completed.
        bool swipedPositive = (m_swipeOffset > 0);
        RotatableImage* sourcePreview = swipedPositive ? previewImage : previewImageB;
        if (pageImage && sourcePreview && sourcePreview->hasImage()) {
            pageImage->takeImageFrom(sourcePreview);
        } else if (pageImage) {
            // Preview hasn't loaded yet — clear the old page so it doesn't
            // briefly flash the previous page while the new one loads
            pageImage->clearImage();
        }

        m_currentPage = m_previewPageIndex;
        updatePageDisplay();
        loadPage(m_currentPage);
        updateProgress();

        if (m_currentPage >= static_cast<int>(m_pages.size()) - 3) {
            preloadNextChapter();
        }
    }

    resetSwipeState();
}

void ReaderActivity::resetSwipeState() {
    // If transitionBox was shown as a preview for an incoming transition page,
    // hide it again since we're snapping back to the current real page
    bool wasTransitionPreview = m_previewIsTransition;
    if (wasTransitionPreview) {
        brls::Logger::info("RESET: wasTransitionPreview=true currentPage={} isTransition={}",
            m_currentPage, isTransitionPage(m_currentPage));
    }

    m_swipeToChapter = false;
    m_previewIsTransition = false;
    m_isSwipeAnimating = false;
    m_swipeOffset = 0.0f;
    m_previewPageIndex = -1;

    // Reset slide offsets on all views (NanoVG-based positioning)
    if (pageImage) {
        pageImage->resetSlideOffset();
    }

    // Reset transition box
    if (transitionBox) {
        transitionBox->resetSlideOffset();
        if (wasTransitionPreview && !isTransitionPage(m_currentPage)) {
            transitionBox->setVisibility(brls::Visibility::GONE);
        }
    }

    // Hide both preview images and reset slide offsets
    if (previewImage) {
        previewImage->setVisibility(brls::Visibility::GONE);
        previewImage->resetSlideOffset();
    }
    if (previewImageB) {
        previewImageB->setVisibility(brls::Visibility::GONE);
        previewImageB->resetSlideOffset();
    }
}

void ReaderActivity::updateMarginColors() {
    // Set background color based on reader background setting
    // This shows when the manga page doesn't fill the screen
    AppSettings& appSettings = Application::getInstance().getSettings();
    NVGcolor bgColor;
    switch (appSettings.readerBackground) {
        case ReaderBackground::WHITE:
            bgColor = nvgRGBA(240, 240, 245, 255);  // #f0f0f5
            break;
        case ReaderBackground::GRAY:
            bgColor = nvgRGBA(80, 80, 90, 255);  // Mid-gray
            break;
        case ReaderBackground::BLACK:
        default:
            bgColor = nvgRGBA(26, 26, 46, 255);  // #1a1a2e
            break;
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
    if (previewImageB) {
        previewImageB->setBackgroundFillColor(bgColor);
    }

    // Update webtoon scroll view background
    if (webtoonScroll) {
        webtoonScroll->setBackgroundColor(bgColor);
    }
}

void ReaderActivity::preloadNextChapter() {
    // Preload next chapter pages for seamless transition
    if (m_nextChapterLoaded || m_chapterPosition < 0 || m_chapterPosition >= m_totalChapters - 1) {
        return;  // Already loaded or no next chapter
    }
    if (m_chapterPosition + 1 >= static_cast<int>(m_chapters.size())) {
        return;  // Bounds check
    }

    int nextChapterId = m_chapters[m_chapterPosition + 1].id;
    brls::Logger::info("Preloading next chapter id={}", nextChapterId);

    int mangaId = m_mangaId;
    int nextChapterIndex = nextChapterId;
    auto sharedNextPages = std::make_shared<std::vector<Page>>();
    std::weak_ptr<bool> aliveWeak = m_alive;

    vitasuwayomi::asyncTask<bool>([mangaId, nextChapterIndex, sharedNextPages]() {
        // First check if next chapter is downloaded locally
        DownloadsManager& localMgr = DownloadsManager::getInstance();
        if (localMgr.isChapterDownloaded(mangaId, nextChapterIndex)) {
            std::vector<std::string> localPaths = localMgr.getChapterPages(mangaId, nextChapterIndex);
            if (!localPaths.empty()) {
                for (size_t i = 0; i < localPaths.size(); i++) {
                    Page page;
                    page.index = static_cast<int>(i);
                    page.url = localPaths[i];
                    page.imageUrl = localPaths[i];
                    page.segment = 0;
                    page.totalSegments = 1;
                    page.originalIndex = static_cast<int>(i);
                    sharedNextPages->push_back(page);
                }
                return true;
            }
        }

        // Fall back to server
        if (!Application::getInstance().isConnected()) {
            return false;
        }
        SuwayomiClient& client = SuwayomiClient::getInstance();
        return client.fetchChapterPages(mangaId, nextChapterIndex, *sharedNextPages);
    }, [this, aliveWeak, sharedNextPages](bool success) {
        auto alive = aliveWeak.lock();
        if (!alive || !*alive) return;

        if (success && !sharedNextPages->empty()) {
            m_nextChapterPages = std::move(*sharedNextPages);
            m_nextChapterLoaded = true;
            brls::Logger::info("Next chapter preloaded: {} pages", m_nextChapterPages.size());

            // Preload first few images of next chapter (full size for manga reader)
            for (size_t i = 0; i < std::min(size_t(3), m_nextChapterPages.size()); i++) {
                ImageLoader::preloadFullSize(m_nextChapterPages[i].imageUrl);
            }
        }
    });
}

void ReaderActivity::preloadPrevChapter() {
    if (m_prevChapterLoaded || m_chapterPosition <= 0) {
        return;
    }
    if (m_chapterPosition - 1 >= static_cast<int>(m_chapters.size())) {
        return;  // Bounds check
    }

    int prevChapterId = m_chapters[m_chapterPosition - 1].id;
    brls::Logger::info("Preloading prev chapter id={}", prevChapterId);

    int mangaId = m_mangaId;
    auto sharedPrevPages = std::make_shared<std::vector<Page>>();
    std::weak_ptr<bool> aliveWeak = m_alive;

    vitasuwayomi::asyncTask<bool>([mangaId, prevChapterId, sharedPrevPages]() {
        DownloadsManager& localMgr = DownloadsManager::getInstance();
        if (localMgr.isChapterDownloaded(mangaId, prevChapterId)) {
            std::vector<std::string> localPaths = localMgr.getChapterPages(mangaId, prevChapterId);
            if (!localPaths.empty()) {
                for (size_t i = 0; i < localPaths.size(); i++) {
                    Page page;
                    page.index = static_cast<int>(i);
                    page.url = localPaths[i];
                    page.imageUrl = localPaths[i];
                    page.segment = 0;
                    page.totalSegments = 1;
                    page.originalIndex = static_cast<int>(i);
                    sharedPrevPages->push_back(page);
                }
                return true;
            }
        }

        if (!Application::getInstance().isConnected()) {
            return false;
        }
        SuwayomiClient& client = SuwayomiClient::getInstance();
        return client.fetchChapterPages(mangaId, prevChapterId, *sharedPrevPages);
    }, [this, aliveWeak, sharedPrevPages](bool success) {
        auto alive = aliveWeak.lock();
        if (!alive || !*alive) return;

        if (success && !sharedPrevPages->empty()) {
            m_prevChapterPages = std::move(*sharedPrevPages);
            m_prevChapterLoaded = true;
            brls::Logger::info("Prev chapter preloaded: {} pages", m_prevChapterPages.size());

            // Preload last few images (user navigates to end when going back)
            size_t count = m_prevChapterPages.size();
            for (size_t i = (count > 3 ? count - 3 : 0); i < count; i++) {
                ImageLoader::preloadFullSize(m_prevChapterPages[i].imageUrl);
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

            // Set up progress callback with alive guard
            std::weak_ptr<bool> cbAlive = m_alive;
            webtoonScroll->setProgressCallback([this, cbAlive](int currentPage, int totalPages, float scrollPercent) {
                auto a = cbAlive.lock();
                if (!a || !*a) return;
                m_currentPage = currentPage;
                updatePageDisplay();
                updateProgress();
            });

            // Set up tap callback with alive guard
            webtoonScroll->setTapCallback([this, cbAlive]() {
                auto a = cbAlive.lock();
                if (!a || !*a) return;
                toggleControls();
            });

            // Set up chapter navigation callback for transition pages
            // Use smooth extend (append/prepend) instead of full chapter replacement
            webtoonScroll->setChapterNavigateCallback([this, cbAlive](bool next) {
                auto a = cbAlive.lock();
                if (!a || !*a) return;
                webtoonExtendChapter(next);
            });

            // Load pages into the scroll view
            if (!m_pages.empty()) {
                float viewW = webtoonScroll->getWidth();
                if (viewW <= 0) viewW = container ? container->getWidth() : 960.0f;
                webtoonScroll->setPages(m_pages, viewW);
                initWebtoonSegments();
                setupWebtoonTransitionText();
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

void ReaderActivity::willDisappear(bool resetState) {
    Activity::willDisappear(resetState);

    // Save reader state so MangaDetailView can immediately update chapters
    // without waiting for async server refresh
    Application::ReaderResult result;
    result.mangaId = m_mangaId;

    int adjustedPage = 0;
    int chapterPageCount = m_realPageCount;

    if (m_continuousScrollMode && webtoonScroll) {
        // Webtoon mode: use segment tracking for correct chapter/page
        int rawPage = webtoonScroll->getCurrentPage();
        int chapterId, chapterPos, pageInChapter, segPageCount;
        getWebtoonProgress(rawPage, chapterId, chapterPos, pageInChapter, segPageCount);
        result.chapterId = chapterId;
        adjustedPage = pageInChapter;
        chapterPageCount = segPageCount;
        // Ensure m_chapterIndex/m_chapterPosition match for markChapterAsRead()
        m_chapterIndex = chapterId;
        m_chapterPosition = chapterPos;
    } else {
        // Manga mode
        result.chapterId = m_chapterIndex;
        adjustedPage = m_currentPage;
        if (!m_pages.empty() && m_pages[0].imageUrl == TRANSITION_PREV) {
            adjustedPage = m_currentPage - 1;
        }
    }

    result.lastPageRead = std::max(0, adjustedPage);
    result.markedRead = (chapterPageCount > 0 && adjustedPage >= chapterPageCount - 1);
    result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    result.chaptersRead = m_readChapterIds;
    Application::getInstance().setLastReaderResult(result);

    // If user finished the chapter (on last page), mark it as read
    if (result.markedRead) {
        markChapterAsRead();
    }

    // Save final reading progress (both online and offline)
    bool onTransition = m_continuousScrollMode && webtoonScroll
        ? webtoonScroll->isTransitionPage(webtoonScroll->getCurrentPage())
        : (!m_pages.empty() && isTransitionPage(m_currentPage));
    if (!onTransition) {
        DownloadsManager::getInstance().updateReadingProgress(
            m_mangaId, result.chapterId, result.lastPageRead);
    }

    // Invalidate alive flag so pending async callbacks bail out safely.
    // This must happen BEFORE any cleanup so that in-flight callbacks
    // (image loader brls::sync, asyncTask completions, etc.) see the
    // flag as false and skip accessing any of our member state.
    *m_alive = false;
    if (m_pageLoadAlive) *m_pageLoadAlive = false;
    if (m_previewLoadAlive) *m_previewLoadAlive = false;
    if (m_crossChapterPreloadAlive) *m_crossChapterPreloadAlive = false;

    // Cancel all pending image loader operations to avoid their brls::sync
    // callbacks calling target->setImageFromMem() on our destroyed views.
    ImageLoader::cancelAll();

    // Clear the full-size image cache to free memory before a new reader
    // potentially starts loading. Reader images are ~4MB each in TGA format
    // and keeping them cached while opening a new chapter risks OOM on Vita.
    ImageLoader::clearCache();

    // Clear webtoon scroll view callbacks and pages so it stops referencing us
    if (webtoonScroll) {
        webtoonScroll->setProgressCallback(nullptr);
        webtoonScroll->setTapCallback(nullptr);
        webtoonScroll->clearPages();
    }

    // Restore screen dimming when leaving the reader
    if (m_settings.keepScreenOn) {
        auto* platform = brls::Application::getPlatform();
        if (platform) {
            platform->disableScreenDimming(false, "Reading manga", "VitaSuwayomi");
            brls::Logger::info("ReaderActivity: willDisappear, restored screen dimming");
        }
    }
}

} // namespace vitasuwayomi
