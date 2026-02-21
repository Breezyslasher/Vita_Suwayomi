/**
 * VitaSuwayomi - Manga Reader Activity implementation
 * NOBORU-style UI with tap to show/hide controls and touch navigation
 */

#include "activity/reader_activity.hpp"
#include "view/pinch_gesture.hpp"
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
#include <cstdio>
#include <thread>

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
    , m_startPage(-1) {  // -1 = use server's lastPageRead as resume point
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
                m_settings.isWebtoonFormat = true;  // Set webtoon format flag for page splitting
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
        brls::Application::getPlatform()->disableScreenDimming(true, "Reading manga", "VitaSuwayomi");
        brls::Logger::info("ReaderActivity: keepScreenOn enabled, disabled screen dimming");
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

    // D-pad for page navigation (rotation-aware, animated slide)
    // LEFT/RIGHT only active at 0° and 180° rotation (horizontal reading axis)
    this->registerAction("", brls::ControllerButton::BUTTON_LEFT, [this](brls::View*) {
        if (m_controlsVisible || m_settingsVisible) return false;
        if (m_settings.rotation == ImageRotation::ROTATE_90 ||
            m_settings.rotation == ImageRotation::ROTATE_270) return false;
        bool inverted = (m_settings.rotation == ImageRotation::ROTATE_180);
        bool rtl = (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT);
        // LEFT = next in RTL (XOR with inverted for 180°)
        bool forward = (rtl != inverted);
        animatePageTurn(forward);
        return true;
    });

    this->registerAction("", brls::ControllerButton::BUTTON_RIGHT, [this](brls::View*) {
        if (m_controlsVisible || m_settingsVisible) return false;
        if (m_settings.rotation == ImageRotation::ROTATE_90 ||
            m_settings.rotation == ImageRotation::ROTATE_270) return false;
        bool inverted = (m_settings.rotation == ImageRotation::ROTATE_180);
        bool rtl = (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT);
        bool forward = !(rtl != inverted);
        animatePageTurn(forward);
        return true;
    });

    // UP/DOWN only active at 90° and 270° rotation (vertical reading axis)
    this->registerAction("", brls::ControllerButton::BUTTON_UP, [this](brls::View*) {
        if (m_controlsVisible || m_settingsVisible) return false;
        if (m_settings.rotation == ImageRotation::ROTATE_0 ||
            m_settings.rotation == ImageRotation::ROTATE_180) return false;
        bool inverted = (m_settings.rotation == ImageRotation::ROTATE_270);
        animatePageTurn(inverted);
        return true;
    });

    this->registerAction("", brls::ControllerButton::BUTTON_DOWN, [this](brls::View*) {
        if (m_controlsVisible || m_settingsVisible) return false;
        if (m_settings.rotation == ImageRotation::ROTATE_0 ||
            m_settings.rotation == ImageRotation::ROTATE_180) return false;
        bool inverted = (m_settings.rotation == ImageRotation::ROTATE_270);
        animatePageTurn(!inverted);
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

        // Hide the blue focus highlight border (full-screen reader doesn't need it)
        pageImage->setHideHighlight(true);

        // Initialize double-tap tracking
        m_lastTapTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        m_lastTapPosition = {0, 0};

        // Tap gesture for controls toggle and optional tap-to-navigate zones
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
                        m_lastTapTime = now;
                        m_lastTapPosition = status.position;

                        // Tap-to-navigate: left 1/3 = prev, right 1/3 = next, center = toggle
                        // Respects reading direction (RTL: right=prev, left=next)
                        AppSettings& appSettings = Application::getInstance().getSettings();
                        if (appSettings.tapToNavigate && !m_isZoomed && !m_continuousScrollMode) {
                            const float SCREEN_WIDTH = 960.0f;
                            float tapX = status.position.x;
                            float leftZone = SCREEN_WIDTH / 3.0f;
                            float rightZone = SCREEN_WIDTH * 2.0f / 3.0f;

                            if (tapX < leftZone) {
                                // Left zone
                                if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) {
                                    nextPage();
                                } else {
                                    previousPage();
                                }
                            } else if (tapX > rightZone) {
                                // Right zone
                                if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) {
                                    previousPage();
                                } else {
                                    nextPage();
                                }
                            } else {
                                // Center zone - toggle controls
                                toggleControls();
                            }
                        } else {
                            // Tap-to-navigate disabled - toggle controls
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
                    // Suppress page navigation while pinch-to-zoom is active
                    if (m_isPinching) return;

                    // Real-time swipe tracking (STAY = finger still on screen)
                    m_touchCurrent = status.position;
                    float dx = m_touchCurrent.x - m_touchStart.x;
                    float dy = m_touchCurrent.y - m_touchStart.y;

                    // If zoomed, use pan to scroll the zoomed image instead of page navigation
                    if (m_isZoomed) {
                        // Update zoom offset based on pan delta
                        brls::Point newOffset = {
                            m_zoomOffset.x + dx,
                            m_zoomOffset.y + dy
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
                        m_swipeOffset = rawDelta;  // Store raw for visual

                        // Determine which page we're swiping to based on logical direction
                        bool swipingPositive = logicalDelta > 0;
                        bool wantNextPage = (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) ? swipingPositive : !swipingPositive;

                        int previewIndex = wantNextPage ? m_currentPage + 1 : m_currentPage - 1;

                        // Always track swipe direction (needed for chapter boundary detection)
                        m_swipingToNext = wantNextPage;

                        // Load preview page if not already loaded and in bounds
                        if (previewIndex != m_previewPageIndex && previewIndex >= 0 &&
                            previewIndex < static_cast<int>(m_pages.size())) {
                            m_previewPageIndex = previewIndex;
                            loadPreviewPage(previewIndex);
                        }

                        // Update visual positions - page follows finger (use raw delta)
                        updateSwipePreview(rawDelta);
                    }
                } else if (status.state == brls::GestureState::END) {
                    // Don't turn the page if we were just pinch-zooming
                    if (m_isPinching) {
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

                        if (absSwipe >= PAGE_TURN_THRESHOLD) {
                            if (m_previewPageIndex >= 0) {
                                // Swipe was long enough and preview page loaded - turn the page
                                completeSwipeAnimation(true);
                            } else {
                                // Swiped past chapter boundary (no preview page exists)
                                resetSwipeState();
                                if (m_swipingToNext) {
                                    nextChapter();
                                } else {
                                    previousChapter(true);
                                }
                            }
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
        // Updates zoom level live on every frame while pinching
        pageImage->addGestureRecognizer(new vitasuwayomi::PinchGestureRecognizer(
            [this](vitasuwayomi::PinchGestureStatus status, brls::Sound* soundToPlay) {
                if (status.state == brls::GestureState::START) {
                    // Record zoom level at pinch start
                    m_isPinching = true;
                    m_initialPinchDistance = 0;  // not used, PinchGestureRecognizer tracks internally
                    m_initialZoomLevel = m_zoomLevel;
                } else if (status.state == brls::GestureState::STAY) {
                    // Live zoom update every frame
                    float newZoom = m_initialZoomLevel * status.scaleFactor;
                    newZoom = std::max(0.5f, std::min(4.0f, newZoom));

                    if (std::abs(newZoom - m_zoomLevel) > 0.01f) {
                        m_zoomLevel = newZoom;
                        m_isZoomed = (newZoom > 1.05f);

                        if (pageImage) {
                            pageImage->setZoomLevel(newZoom);
                            // Zoom towards the pinch center
                            brls::Rect frame = pageImage->getFrame();
                            float relX = (status.center.x - frame.getMinX()) / frame.getWidth();
                            float relY = (status.center.y - frame.getMinY()) / frame.getHeight();
                            brls::Point offset = {
                                -(relX - 0.5f) * frame.getWidth() * (newZoom - 1.0f),
                                -(relY - 0.5f) * frame.getHeight() * (newZoom - 1.0f)
                            };
                            m_zoomOffset = offset;
                            pageImage->setZoomOffset(offset);
                        }
                    }
                } else if (status.state == brls::GestureState::END) {
                    m_isPinching = false;
                    // Snap to 1x if near normal
                    if (m_zoomLevel <= 1.05f && m_zoomLevel >= 0.95f) {
                        resetZoom();
                    }
                }
            }));
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

                        // Same tap-to-navigate logic as pageImage
                        AppSettings& appSettings = Application::getInstance().getSettings();
                        if (appSettings.tapToNavigate && !m_isZoomed && !m_continuousScrollMode) {
                            const float SCREEN_WIDTH = 960.0f;
                            float tapX = status.position.x;
                            float leftZone = SCREEN_WIDTH / 3.0f;
                            float rightZone = SCREEN_WIDTH * 2.0f / 3.0f;

                            if (tapX < leftZone) {
                                if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) {
                                    nextPage();
                                } else {
                                    previousPage();
                                }
                            } else if (tapX > rightZone) {
                                if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) {
                                    previousPage();
                                } else {
                                    nextPage();
                                }
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

    // Page slider in bottom bar
    if (pageSlider) {
        pageSlider->getProgressEvent()->subscribe([this](float progress) {
            if (m_pages.empty()) return;
            int targetPage = static_cast<int>(progress * (m_pages.size() - 1));
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
    auto sharedLastPageRead = std::make_shared<int>(0);

    vitasuwayomi::asyncTask<bool>([isWebtoonMode, mangaId, chapterIndex,
                                    sharedPages, sharedChapterName,
                                    sharedChapters, sharedTotalChapters,
                                    sharedLoadedFromLocal, sharedLastPageRead]() {
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
                    rawPages.push_back(page);
                }
                loadedFromLocal = true;
                *sharedLoadedFromLocal = true;
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

        // Use pages as-is (no splitting - webtoon images load whole)
        *sharedPages = std::move(rawPages);

        // Fetch chapter details and navigation from server (skip when offline)
        if (Application::getInstance().isConnected()) {
            Chapter chapter;
            if (client.fetchChapter(mangaId, chapterIndex, chapter)) {
                *sharedChapterName = chapter.name;
                *sharedLastPageRead = chapter.lastPageRead;
            }
            client.fetchChapters(mangaId, *sharedChapters);
            *sharedTotalChapters = static_cast<int>(sharedChapters->size());
        }

        return true;
    }, [this, isWebtoonMode, aliveWeak,
        sharedPages, sharedChapterName,
        sharedChapters, sharedTotalChapters,
        sharedLoadedFromLocal, sharedLastPageRead](bool success) {
        auto alive = aliveWeak.lock();
        if (!alive || !*alive) return;

        // Copy results from shared containers into our members (safe - we're alive)
        m_pages = std::move(*sharedPages);
        m_chapterName = std::move(*sharedChapterName);
        m_chapters = std::move(*sharedChapters);
        m_totalChapters = *sharedTotalChapters;
        m_loadedFromLocal = *sharedLoadedFromLocal;

        if (!success || m_pages.empty()) {
            brls::Logger::error("No pages to display");

            if (chapterLabel) {
                chapterLabel->setText("Failed to load chapter");
            }
            showPageError("Failed to load chapter pages");
            return;
        }

        brls::Logger::info("Loaded {} pages", m_pages.size());

        // Find current chapter position in the chapter list for correct display/navigation
        m_chapterListPosition = -1;
        for (int i = 0; i < static_cast<int>(m_chapters.size()); i++) {
            if (m_chapters[i].id == m_chapterIndex) {
                m_chapterListPosition = i;
                break;
            }
        }

        // Update UI labels using chapter data
        updateChapterDisplay();

        // Determine starting page:
        // Priority: scrollToEnd (prev chapter nav) > explicit startPage > server lastPageRead > 0
        int lastPage = static_cast<int>(m_pages.size()) - 1;
        if (m_scrollToEndOnLoad) {
            // Going to previous chapter - start at end
            m_currentPage = lastPage;
            m_scrollToEndOnLoad = false;
            brls::Logger::info("ReaderActivity: Starting at end of chapter (scroll to end)");
        } else if (m_startPage > 0) {
            // Explicit start page from constructor or caller
            m_currentPage = std::min(m_startPage, lastPage);
            brls::Logger::info("ReaderActivity: Starting at explicit page {}", m_currentPage);
        } else if (m_startPage == -1 && *sharedLastPageRead > 0) {
            // Resume from server's saved progress (first constructor, no explicit page)
            m_currentPage = std::min(*sharedLastPageRead, lastPage);
            brls::Logger::info("ReaderActivity: Resuming from server progress page {}", m_currentPage);
        } else {
            m_currentPage = 0;
        }
        // Reset m_startPage so subsequent loadPages() calls (from chapter nav) start fresh
        m_startPage = 0;

        // Set up the correct reader mode (webtoon scroll vs single page)
        setupReaderMode(isWebtoonMode);

        updatePageDisplay();
    });
}

void ReaderActivity::loadPage(int index) {
    if (index < 0 || index >= static_cast<int>(m_pages.size())) {
        return;
    }

    hidePageError();  // Clear any previous error

    const Page& page = m_pages[index];
    std::string imageUrl = page.imageUrl;

    brls::Logger::debug("Loading page {} from: {}", index, imageUrl);

    // Show page counter when navigating
    showPageCounter();
    schedulePageCounterHide();

    // Track this load generation for timeout detection
    int loadGen = ++m_pageLoadGeneration;
    m_pageLoadSucceeded = false;

    // Load image using RotatableImage
    if (pageImage) {
        std::weak_ptr<bool> aliveWeak = m_alive;

        auto onLoaded = [this, aliveWeak, index, loadGen](RotatableImage* img, bool success) {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            if (m_pageLoadGeneration == loadGen && success) {
                // This is still the current page load and it succeeded
                brls::Logger::debug("ReaderActivity: Page {} loaded", index);
                m_pageLoadSucceeded = true;
            } else if (m_pageLoadGeneration == loadGen && !success) {
                // Current page load failed - show error immediately
                brls::Logger::error("ReaderActivity: Page {} failed to load", index);
                showPageError("Failed to load page " + std::to_string(index + 1));
            } else if (success) {
                // Stale page load completed and overwrote the current page's image.
                // Reload the correct current page (should be instant from cache).
                brls::Logger::debug("ReaderActivity: Stale page {} loaded (current={}), reloading correct page",
                                    index, m_currentPage);
                int correctPage = m_currentPage;
                if (correctPage >= 0 && correctPage < static_cast<int>(m_pages.size()) && img) {
                    ImageLoader::loadAsyncFullSize(m_pages[correctPage].imageUrl, nullptr, img, alive);
                }
            }
        };

        ImageLoader::loadAsyncFullSize(imageUrl, onLoaded, pageImage, m_alive);

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
        pageCounter->setText(std::to_string(m_currentPage + 1) + "/" +
                          std::to_string(m_pages.size()));
    }

    // Update slider page label (in bottom bar)
    if (sliderPageLabel) {
        sliderPageLabel->setText("Page " + std::to_string(m_currentPage + 1) +
                                 " of " + std::to_string(m_pages.size()));
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

void ReaderActivity::updateChapterDisplay() {
    if (!m_chapters.empty() && m_chapterListPosition >= 0 &&
        m_chapterListPosition < static_cast<int>(m_chapters.size())) {
        const Chapter& ch = m_chapters[m_chapterListPosition];

        if (chapterLabel) {
            std::string label;
            if (!ch.name.empty()) {
                label = ch.name;
            } else if (ch.chapterNumber > 0) {
                if (ch.chapterNumber == static_cast<float>(static_cast<int>(ch.chapterNumber))) {
                    label = "Chapter " + std::to_string(static_cast<int>(ch.chapterNumber));
                } else {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "Chapter %.1f", ch.chapterNumber);
                    label = buf;
                }
            } else {
                label = "Chapter " + std::to_string(m_chapterListPosition + 1);
            }
            chapterLabel->setText(label);
        }

        if (chapterProgress) {
            chapterProgress->setText("Ch. " + std::to_string(m_chapterListPosition + 1) +
                                     " of " + std::to_string(m_totalChapters));
        }
    } else {
        // Fallback when chapter list is not available (offline)
        if (chapterLabel) {
            chapterLabel->setText(!m_chapterName.empty() ? m_chapterName : "Chapter");
        }
        if (chapterProgress) {
            chapterProgress->setText("");
        }
    }
}

void ReaderActivity::updateProgress() {
    // Throttle progress saves in webtoon mode to avoid excessive network/disk I/O
    // (page boundary changes fire rapidly during continuous scrolling)
    if (m_continuousScrollMode) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastProgressSaveTime).count();
        if (elapsed < 2) return;
        m_lastProgressSaveTime = now;
    }

    int mangaId = m_mangaId;
    int chapterIndex = m_chapterIndex;
    int currentPage = m_currentPage;

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
    if (m_currentPage < static_cast<int>(m_pages.size()) - 1) {
        m_currentPage++;
        updatePageDisplay();
        loadPage(m_currentPage);
        updateProgress();
    } else {
        // End of chapter - nextChapter() handles marking as read and end-of-manga
        nextChapter();
    }
}

void ReaderActivity::previousPage() {
    if (m_currentPage > 0) {
        m_currentPage--;
        updatePageDisplay();
        loadPage(m_currentPage);
        updateProgress();
    } else {
        // Beginning of chapter - go to last page of previous chapter
        previousChapter(true);
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
    // Use chapter list for navigation (m_chapterIndex is a DB ID, not sequential)
    bool hasNext = !m_chapters.empty() && m_chapterListPosition >= 0 &&
        m_chapterListPosition < static_cast<int>(m_chapters.size()) - 1;

    if (!hasNext) {
        markChapterAsRead();
        brls::Application::notify("End of manga");
        return;
    }

    markChapterAsRead();

    // Navigate to next chapter by list position
    m_chapterListPosition++;
    m_chapterIndex = m_chapters[m_chapterListPosition].id;
    m_chapterName = m_chapters[m_chapterListPosition].name;
    m_currentPage = 0;

    // Use preloaded pages if available for instant transition
    if (m_nextChapterLoaded && !m_nextChapterPages.empty()) {
        m_pages = std::move(m_nextChapterPages);
        m_nextChapterLoaded = false;
        m_nextChapterPages.clear();

        // Update UI with correct chapter info
        updateChapterDisplay();

        // Handle webtoon mode vs single-page mode
        if (m_continuousScrollMode && webtoonScroll) {
            webtoonScroll->setPages(m_pages, 960.0f);
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

void ReaderActivity::previousChapter(bool scrollToEnd) {
    // Use chapter list for navigation (m_chapterIndex is a DB ID, not sequential)
    bool hasPrev = !m_chapters.empty() && m_chapterListPosition > 0;

    if (!hasPrev) {
        return;
    }

    // Navigate to previous chapter by list position
    m_chapterListPosition--;
    m_chapterIndex = m_chapters[m_chapterListPosition].id;
    m_chapterName = m_chapters[m_chapterListPosition].name;

    // Reset preloaded chapter since we're going backwards
    m_nextChapterLoaded = false;
    m_nextChapterPages.clear();
    m_currentPage = 0;
    m_pages.clear();

    // When scrolling back from webtoon start, go to end of previous chapter
    if (scrollToEnd) {
        m_scrollToEndOnLoad = true;
    }

    // Clear webtoon scroll view to reset scroll position for new chapter
    if (m_continuousScrollMode && webtoonScroll) {
        webtoonScroll->clearPages();
    }

    loadPages();
}

void ReaderActivity::markChapterAsRead() {
    // Skip server call when offline
    if (!Application::getInstance().isConnected()) {
        brls::Logger::info("ReaderActivity: offline, skipping mark as read");
        return;
    }

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

            // Delete downloaded chapter if deleteAfterRead is enabled
            if (Application::getInstance().getSettings().deleteAfterRead) {
                brls::Logger::info("ReaderActivity: deleteAfterRead enabled, removing chapter download");

                // Delete from local downloads
                DownloadsManager& dm = DownloadsManager::getInstance();
                if (dm.deleteChapterDownload(mangaId, chapterIndex)) {
                    brls::Logger::info("ReaderActivity: Deleted local chapter download (manga={}, chapter={})",
                                      mangaId, chapterIndex);
                }

                // Also delete from server download queue if applicable
                // Get chapter ID from pages if available
                std::vector<Chapter> chapters;
                if (client.fetchChapters(mangaId, chapters)) {
                    for (const auto& ch : chapters) {
                        if (ch.chapterNumber == chapterIndex || ch.index == chapterIndex) {
                            std::vector<int> chapterIds = {ch.id};
                            std::vector<int> chapterIndexes = {chapterIndex};
                            client.deleteChapterDownloads(chapterIds, mangaId, chapterIndexes);
                            brls::Logger::info("ReaderActivity: Requested server to delete chapter download (id={})",
                                              ch.id);
                            break;
                        }
                    }
                }
            }
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

// Touch handling is now implemented inline in gesture recognizers (onContentAvailable)

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
        // Transform center point to image space and zoom around it
        brls::Rect frame = pageImage->getFrame();
        float relX = (center.x - frame.getMinX()) / frame.getWidth();
        float relY = (center.y - frame.getMinY()) / frame.getHeight();

        // Offset to keep the tapped point in place
        brls::Point offset = {
            -(relX - 0.5f) * frame.getWidth() * (level - 1.0f),
            -(relY - 0.5f) * frame.getHeight() * (level - 1.0f)
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
    m_errorOverlay->setBackgroundColor(nvgRGBA(26, 26, 46, 200));

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
    m_retryButton->setBackgroundColor(nvgRGBA(0, 150, 136, 255));
    m_retryButton->registerClickAction([this](brls::View* view) {
        if (!Application::getInstance().isConnected() && !m_loadedFromLocal) {
            brls::Application::notify("App is offline");
            return true;
        }
        // Defer to next frame: hidePageError() (called directly and inside
        // loadPage) destroys this button, so we cannot run it from within
        // the button's own click handler without a use-after-free crash.
        std::weak_ptr<bool> aliveWeak = m_alive;
        brls::sync([this, aliveWeak]() {
            auto alive = aliveWeak.lock();
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

    container->addView(m_errorOverlay);
    brls::Application::giveFocus(m_retryButton);
}

void ReaderActivity::hidePageError() {
    if (m_errorOverlay && container) {
        container->removeView(m_errorOverlay);
        m_errorOverlay = nullptr;
        m_errorLabel = nullptr;
        m_retryButton = nullptr;
    }
}

// NOBORU-style swipe methods

void ReaderActivity::updateSwipePreview(float offset) {
    // Update visual positions during swipe - PUSH EFFECT
    // Current page is pushed off screen, preview page follows behind pushing it
    const float SCREEN_WIDTH = 960.0f;
    const float SCREEN_HEIGHT = 544.0f;

    if (!pageImage || !previewImage) return;

    // Only show preview image if a valid preview page was loaded.
    // At chapter boundaries (first/last page), there's no preview page,
    // so hide it to avoid showing stale content from a previous swipe.
    if (m_previewPageIndex >= 0) {
        previewImage->setVisibility(brls::Visibility::VISIBLE);
    } else {
        previewImage->setVisibility(brls::Visibility::GONE);
    }

    // At chapter boundaries (no preview page), apply resistance/dampening
    // so the page feels like it's at the edge rather than sliding freely
    if (m_previewPageIndex < 0) {
        offset *= 0.3f;  // 30% of finger movement for rubber-band feel
    }

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
        if (m_previewPageIndex >= 0) {
            if (offset > 0) {
                // Swiping down - preview pushes from top (above current page)
                previewImage->setTranslationY(offset - SCREEN_HEIGHT);
            } else {
                // Swiping up - preview pushes from bottom (below current page)
                previewImage->setTranslationY(offset + SCREEN_HEIGHT);
            }
            previewImage->setTranslationX(0.0f);
        }
    } else {
        // Horizontal swipe for 0/180 rotation
        // Clamp offset to screen width
        offset = std::max(-SCREEN_WIDTH, std::min(SCREEN_WIDTH, offset));

        // Move current page with finger (horizontally)
        pageImage->setTranslationX(offset);
        pageImage->setTranslationY(0.0f);

        // PUSH EFFECT: Preview page is always one screen behind current page
        // When current page is pushed off, preview slides in from behind
        if (m_previewPageIndex >= 0) {
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
    std::weak_ptr<bool> aliveWeak = m_alive;

    brls::Logger::debug("Loading preview page {}", index);

    // Load the preview image (full size for manga reader)
    ImageLoader::loadAsyncFullSize(imageUrl, [aliveWeak, index](RotatableImage* img, bool success) {
        auto alive = aliveWeak.lock();
        if (!alive || !*alive) return;
        brls::Logger::debug("Preview page {} {}", index, success ? "loaded" : "failed");
    }, previewImage, m_alive);
}

void ReaderActivity::completeSwipeAnimation(bool turnPage) {
    if (turnPage && m_previewPageIndex >= 0) {
        // Instantly transfer the preview texture to the main page image so the
        // user sees the new page the moment positions reset to zero.
        // Without this, resetSwipeState() snaps the old image back to center
        // while the async loadPage() hasn't finished yet, causing a visual
        // "snap-back" to the previous page.
        if (pageImage && previewImage && previewImage->hasImage()) {
            pageImage->takeImageFrom(previewImage);
        }

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

void ReaderActivity::animatePageTurn(bool forward) {
    // Animated page slide for d-pad navigation (same push effect as touch swipe)
    if (m_isDpadAnimating || m_isSwipeAnimating || m_continuousScrollMode) return;

    int targetPage = forward ? m_currentPage + 1 : m_currentPage - 1;

    // If out of bounds, go to next/previous chapter (no animation needed)
    if (targetPage < 0 || targetPage >= static_cast<int>(m_pages.size())) {
        if (forward)
            nextChapter();
        else
            previousChapter(true);
        return;
    }

    m_isDpadAnimating = true;
    m_previewPageIndex = targetPage;
    m_swipingToNext = forward;
    loadPreviewPage(targetPage);

    // Determine animation direction based on rotation
    bool useVerticalSwipe = (m_settings.rotation == ImageRotation::ROTATE_90 ||
                             m_settings.rotation == ImageRotation::ROTATE_270);
    bool invertDirection = (m_settings.rotation == ImageRotation::ROTATE_180 ||
                            m_settings.rotation == ImageRotation::ROTATE_270);
    bool rtl = (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT);

    // Compute the sign of the slide: which way the current page moves off-screen
    // For forward page turn in LTR at 0°: slide left (negative offset)
    float sign;
    if (useVerticalSwipe) {
        // Vertical axis: forward = slide up (negative), backward = slide down (positive)
        sign = forward ? -1.0f : 1.0f;
        if (invertDirection) sign = -sign;
    } else {
        // Horizontal axis: account for RTL and rotation inversion
        bool effectiveForward = (rtl != invertDirection) ? !forward : forward;
        sign = effectiveForward ? 1.0f : -1.0f;
    }

    float totalDistance = useVerticalSwipe ? 544.0f : 960.0f;
    float targetOffset = sign * totalDistance;

    // Animate over ~200ms using async thread posting sync updates
    const int ANIM_DURATION_MS = 200;
    const int FRAME_INTERVAL_MS = 16;  // ~60fps
    std::weak_ptr<bool> aliveWeak = m_alive;

    vitasuwayomi::asyncRun([this, aliveWeak, targetOffset, ANIM_DURATION_MS, FRAME_INTERVAL_MS]() {
        auto startTime = std::chrono::steady_clock::now();

        while (true) {
            auto now = std::chrono::steady_clock::now();
            int elapsed = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count());

            float t = std::min(1.0f, static_cast<float>(elapsed) / ANIM_DURATION_MS);
            // Ease-out cubic for smooth deceleration
            float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
            float currentOffset = targetOffset * eased;
            bool done = (t >= 1.0f);

            brls::sync([this, aliveWeak, currentOffset, done]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                if (done) {
                    // Animation complete - finalize page turn
                    completeSwipeAnimation(true);
                    m_isDpadAnimating = false;
                } else {
                    updateSwipePreview(currentOffset);
                }
            });

            if (done) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(FRAME_INTERVAL_MS));
        }
    });
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

    // Update webtoon scroll view background
    if (webtoonScroll) {
        webtoonScroll->setBackgroundColor(bgColor);
    }
}

void ReaderActivity::preloadNextChapter() {
    // Preload next chapter pages for seamless transition
    if (m_nextChapterLoaded) return;

    // Use chapter list to find next chapter ID
    if (m_chapters.empty() || m_chapterListPosition < 0 ||
        m_chapterListPosition >= static_cast<int>(m_chapters.size()) - 1) {
        return;  // No next chapter available
    }

    int nextChapterId = m_chapters[m_chapterListPosition + 1].id;
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

void ReaderActivity::updateReaderMode() {
    bool shouldUseContinuousScroll = m_settings.isWebtoonFormat;

    if (shouldUseContinuousScroll == m_continuousScrollMode) {
        return;  // Mode hasn't changed
    }

    brls::Logger::info("ReaderActivity: Switching to {} mode",
                       shouldUseContinuousScroll ? "continuous scroll" : "single page");

    setupReaderMode(shouldUseContinuousScroll);
}

void ReaderActivity::setupReaderMode(bool webtoonMode) {
    m_continuousScrollMode = webtoonMode;

    if (webtoonMode) {
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

            // Set up callbacks with alive guard
            std::weak_ptr<bool> cbAlive = m_alive;
            webtoonScroll->setProgressCallback([this, cbAlive](int currentPage, int totalPages, float scrollPercent) {
                auto a = cbAlive.lock();
                if (!a || !*a) return;
                m_currentPage = currentPage;
                updatePageDisplay();
                updateProgress();

                if (currentPage >= totalPages - 3) {
                    preloadNextChapter();
                }
            });

            webtoonScroll->setTapCallback([this, cbAlive]() {
                auto a = cbAlive.lock();
                if (!a || !*a) return;
                toggleControls();
            });

            webtoonScroll->setEndReachedCallback([this, cbAlive]() {
                auto a = cbAlive.lock();
                if (!a || !*a) return;
                nextChapter();
            });

            webtoonScroll->setStartReachedCallback([this, cbAlive]() {
                auto a = cbAlive.lock();
                if (!a || !*a) return;
                previousChapter(true);
            });

            // Load pages into the scroll view
            if (!m_pages.empty()) {
                webtoonScroll->setPages(m_pages, 960.0f);  // PS Vita screen width
                webtoonScroll->scrollToPage(m_currentPage);
            }
        }
    } else {
        // Single page mode
        if (webtoonScroll) {
            webtoonScroll->setVisibility(brls::Visibility::GONE);
            webtoonScroll->clearPages();
        }
        if (pageImage) {
            pageImage->setVisibility(brls::Visibility::VISIBLE);
        }

        if (!m_pages.empty()) {
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
    result.chapterId = m_chapterIndex;  // This is actually the chapter ID passed to reader
    result.lastPageRead = m_currentPage;
    result.markedRead = (m_currentPage >= static_cast<int>(m_pages.size()) - 1 && !m_pages.empty());
    result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    Application::getInstance().setLastReaderResult(result);

    // Invalidate alive flag so pending async callbacks bail out safely.
    // This must happen BEFORE any cleanup so that in-flight callbacks
    // (image loader brls::sync, asyncTask completions, etc.) see the
    // flag as false and skip accessing any of our member state.
    *m_alive = false;

    // Cancel all pending image loader operations to avoid their brls::sync
    // callbacks calling target->setImageFromMem() on our destroyed views.
    ImageLoader::cancelAll();

    // Clear webtoon scroll view callbacks and pages so it stops referencing us
    if (webtoonScroll) {
        webtoonScroll->setProgressCallback(nullptr);
        webtoonScroll->setTapCallback(nullptr);
        webtoonScroll->setEndReachedCallback(nullptr);
        webtoonScroll->setStartReachedCallback(nullptr);
        webtoonScroll->clearPages();
    }

    // Restore screen dimming when leaving the reader
    if (m_settings.keepScreenOn) {
        brls::Application::getPlatform()->disableScreenDimming(false, "Reading manga", "VitaSuwayomi");
        brls::Logger::info("ReaderActivity: willDisappear, restored screen dimming");
    }
}

} // namespace vitasuwayomi
