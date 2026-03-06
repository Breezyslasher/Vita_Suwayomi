/**
 * WebtoonScrollView implementation
 * Continuous vertical scrolling for webtoon reading
 */

#include "view/webtoon_scroll_view.hpp"
#include "app/application.hpp"
#include "utils/image_loader.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace vitasuwayomi {

// Transition page URL prefixes (must match reader_activity constants)
static const std::string TRANSITION_PREFIX = "__transition:";

WebtoonScrollView::WebtoonScrollView() {
    // Set up as a full-size container
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setGrow(1.0f);

    // Initialize double-tap, pinch cooldown, and frame timing
    m_lastTapTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    m_pinchEndTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    m_lastFrameTime = std::chrono::steady_clock::now();

    // Setup touch gestures
    setupGestures();
}

WebtoonScrollView::~WebtoonScrollView() {
    *m_alive = false;
    clearPages();
}

void WebtoonScrollView::resetZoom() {
    m_isZoomed = false;
    m_zoomLevel = 1.0f;
    m_zoomOffset = {0, 0};
}

void WebtoonScrollView::setupGestures() {
    this->setFocusable(true);

    // Tap gesture for double-tap zoom reset and controls toggle
    this->addGestureRecognizer(new brls::TapGestureRecognizer(
        [this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
            if (m_isPinching) return;

            // Ignore taps shortly after a pinch ends (finger-lift artifacts)
            auto timeSincePinch = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_pinchEndTime).count();
            if (timeSincePinch < 300) return;

            if (status.state == brls::GestureState::END) {
                auto now = std::chrono::steady_clock::now();
                auto timeSinceLastTap = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_lastTapTime).count();
                float tapDistance = std::sqrt(
                    std::pow(status.position.x - m_lastTapPosition.x, 2) +
                    std::pow(status.position.y - m_lastTapPosition.y, 2));

                if (timeSinceLastTap < DOUBLE_TAP_THRESHOLD_MS &&
                    tapDistance < DOUBLE_TAP_DISTANCE) {
                    // Double-tap detected - reset zoom if zoomed
                    if (m_isZoomed) {
                        resetZoom();
                    }
                    // Prevent triple-tap
                    m_lastTapTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                } else {
                    // Single tap
                    m_lastTapTime = now;
                    m_lastTapPosition = status.position;

                    // Scale tap position from physical to view coords
                    float scaleX = (m_viewWidth > 0) ? (m_viewWidth / 960.0f) : 1.0f;
                    float scaleY = (m_viewHeight > 0) ? (m_viewHeight / 544.0f) : 1.0f;
                    float tapX = status.position.x * scaleX;
                    float tapY = status.position.y * scaleY;

                    int tappedPage = getPageAtPosition(tapX, tapY);

                    if (tappedPage >= 0 && isFailedPage(tappedPage)) {
                        // Tapped on a failed page - retry loading
                        m_failedPages.erase(tappedPage);
                        m_loadedPages.erase(tappedPage);
                        m_loadingPages.erase(tappedPage);
                        updateVisibleImages();
                    } else {
                        // Regular tap - toggle controls
                        if (m_tapCallback) {
                            m_tapCallback();
                        }
                    }
                }
            }
        }));

    // Pan gesture for scrolling (ANY axis to support rotated views)
    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
            // Suppress scrolling while pinch-to-zoom is active
            if (m_isPinching) return;

            if (status.state == brls::GestureState::START) {
                m_isTouching = true;
                m_touchStart = status.position;
                m_touchLast = status.position;
                m_scrollAtTouchStart = m_scrollY;
                m_scrollVelocity = 0.0f;
                m_lastTouchTime = std::chrono::steady_clock::now();
            } else if (status.state == brls::GestureState::STAY) {
                // Calculate raw deltas
                float rawDx = status.position.x - m_touchLast.x;
                float rawDy = status.position.y - m_touchLast.y;

                // If zoomed, pan the zoomed view instead of normal scrolling
                // Divide by zoom level since offset is in pre-scale space;
                // without this, panning moves too fast at higher zoom levels.
                if (m_isZoomed) {
                    float scaleX = (m_viewWidth > 0) ? (m_viewWidth / 960.0f) : 1.0f;
                    float scaleY = (m_viewHeight > 0) ? (m_viewHeight / 544.0f) : 1.0f;
                    m_zoomOffset.x += rawDx * scaleX / m_zoomLevel;
                    m_zoomOffset.y += rawDy * scaleY / m_zoomLevel;
                    m_touchLast = status.position;
                    m_lastTouchTime = std::chrono::steady_clock::now();
                    return;
                }

                // Calculate scroll delta based on rotation
                // scrollY: 0 = beginning, negative = scrolled towards end
                // Content follows finger (natural scrolling) for all rotations.
                // 0°: swipe up to scroll forward
                // 90° (RTL layout): swipe up (user's +X) to scroll forward
                // 180° (BTT layout): swipe up (user's +Y) to scroll forward
                // 270° (LTR layout): swipe up (user's -X) to scroll forward
                //
                // Touch deltas are in physical screen coords (960x544) but scroll
                // position is in view coords (~1280x726). Scale for 1:1 tracking.
                float scrollDelta = 0.0f;
                int rotation = static_cast<int>(m_rotationDegrees);
                float scaleY = (m_viewHeight > 0) ? (m_viewHeight / 544.0f) : 1.0f;
                float scaleX = (m_viewWidth > 0) ? (m_viewWidth / 960.0f) : 1.0f;

                if (rotation == 0) {
                    scrollDelta = rawDy * scaleY;
                } else if (rotation == 90) {
                    // 90° CW: user holds device CCW, their "up" = +X on screen
                    // RTL layout: -scrollY in formula, so negate dx for natural tracking
                    scrollDelta = -rawDx * scaleX;
                } else if (rotation == 180) {
                    scrollDelta = -rawDy * scaleY;
                } else if (rotation == 270) {
                    // 270° CW: user holds device CW, their "up" = -X on screen
                    // LTR layout: +scrollY in formula, so use dx directly for natural tracking
                    scrollDelta = rawDx * scaleX;
                }

                // Update scroll position
                float prevScrollY = m_scrollY;
                m_scrollY += scrollDelta;

                // Clamp scroll position based on layout direction
                float maxScroll = 0.0f;
                bool horizontal = (rotation == 90 || rotation == 270);
                float viewSize = horizontal ? m_viewWidth : m_viewHeight;
                float totalContentSize = getTotalContentSize();
                float minScroll = -(totalContentSize - viewSize);
                if (minScroll > maxScroll) minScroll = maxScroll;

                m_scrollY = std::max(minScroll, std::min(maxScroll, m_scrollY));

                // Mark that the user has started scrolling (enables auto-extend)
                if (!m_userHasScrolled) m_userHasScrolled = true;

                // Don't update anchor here — it must stay fixed at the value
                // set by prependPages/setPages so that ALL prepended pages
                // always get scroll compensation when their images load with
                // different-than-estimated heights.  Tracking anchor to
                // m_currentPage caused pages the user just scrolled past to
                // lose compensation, resulting in visible content jumps.

                // Calculate velocity for momentum
                auto now = std::chrono::steady_clock::now();
                auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastTouchTime).count();
                if (dt > 0) {
                    m_scrollVelocity = scrollDelta / (dt / 16.67f);  // Normalize to ~60fps
                }

                m_touchLast = status.position;
                m_lastTouchTime = now;

                // Update visible images and current page
                updateVisibleImages();
                updateCurrentPage();
            } else if (status.state == brls::GestureState::END) {
                m_isTouching = false;

                // If zoomed, just stop panning (no momentum when zoomed)
                if (m_isZoomed) {
                    m_scrollVelocity = 0.0f;
                    return;
                }

                // If it was a tap (minimal movement), kill momentum
                // (tap handling is done by the TapGestureRecognizer above)
                float dx = status.position.x - m_touchStart.x;
                float dy = status.position.y - m_touchStart.y;
                float distance = std::sqrt(dx * dx + dy * dy);
                if (distance < TAP_THRESHOLD) {
                    m_scrollVelocity = 0.0f;
                }

                // Otherwise, momentum will be applied in onFrame()
            }
        }, brls::PanAxis::ANY));

    // Pinch-to-zoom gesture (Vita two-finger touch)
    this->addGestureRecognizer(new vitasuwayomi::PinchGestureRecognizer(
        [this](vitasuwayomi::PinchGestureStatus status, brls::Sound* soundToPlay) {
            if (status.state == brls::GestureState::START) {
                m_isPinching = true;
                m_initialZoomLevel = m_zoomLevel;
                m_initialZoomOffset = m_zoomOffset;
                // Invalidate pending double-tap so lifting fingers after
                // pinch can't accidentally trigger resetZoom
                m_lastTapTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                // Convert physical pinch center to view coords
                float scaleX = (m_viewWidth > 0) ? (m_viewWidth / 960.0f) : 1.0f;
                float scaleY = (m_viewHeight > 0) ? (m_viewHeight / 544.0f) : 1.0f;
                m_initialPinchCenter = {status.center.x * scaleX, status.center.y * scaleY};
                m_scrollVelocity = 0.0f;
            } else if (status.state == brls::GestureState::STAY) {
                float newZoom = m_initialZoomLevel * status.scaleFactor;
                newZoom = std::max(1.0f, std::min(4.0f, newZoom));

                if (std::abs(newZoom - m_zoomLevel) > 0.01f) {
                    // Convert current pinch center to view coords
                    float scaleX = (m_viewWidth > 0) ? (m_viewWidth / 960.0f) : 1.0f;
                    float scaleY = (m_viewHeight > 0) ? (m_viewHeight / 544.0f) : 1.0f;
                    brls::Point currentCenter = {status.center.x * scaleX, status.center.y * scaleY};

                    // Focal-point zoom formula:
                    // NVG transform: P' = C + z*(P - C + off), C = view center
                    // To keep image point P at screen pos S:
                    //   off = (S-C)/z - (S0-C)/z0 + off0
                    float cx = m_viewWidth / 2.0f;
                    float cy = m_viewHeight / 2.0f;

                    m_zoomOffset.x = (currentCenter.x - cx) / newZoom - (m_initialPinchCenter.x - cx) / m_initialZoomLevel + m_initialZoomOffset.x;
                    m_zoomOffset.y = (currentCenter.y - cy) / newZoom - (m_initialPinchCenter.y - cy) / m_initialZoomLevel + m_initialZoomOffset.y;

                    m_zoomLevel = newZoom;
                    m_isZoomed = (newZoom > 1.01f);
                }
            } else if (status.state == brls::GestureState::END) {
                m_isPinching = false;
                m_pinchEndTime = std::chrono::steady_clock::now();

                // If user pinched back to 1.0x, clear zoomed state so scrolling works
                if (m_zoomLevel <= 1.0f) {
                    resetZoom();
                }
            }
        }));
}

bool WebtoonScrollView::isTransitionPage(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size())) return false;
    const std::string& url = m_pages[pageIndex].imageUrl;
    return url.compare(0, TRANSITION_PREFIX.size(), TRANSITION_PREFIX) == 0;
}

bool WebtoonScrollView::isFailedPage(int pageIndex) const {
    return m_failedPages.count(pageIndex) > 0;
}

void WebtoonScrollView::setTransitionText(int pageIndex, const std::string& line1, const std::string& line2) {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size())) return;
    TransitionInfo info;
    info.line1 = line1;
    info.line2 = line2;
    // Determine direction from URL
    const std::string& url = m_pages[pageIndex].imageUrl;
    info.isNext = (url.find("next") != std::string::npos);
    m_transitionInfo[pageIndex] = info;
}

int WebtoonScrollView::getPageAtPosition(float tapX, float tapY) const {
    // Convert tap position to content-space coordinate
    bool horizontal = isHorizontalLayout();
    int rotation = static_cast<int>(m_rotationDegrees);
    float contentPos;
    if (horizontal) {
        if (rotation == 90) {
            // Right-to-left layout: content start is at the right edge
            contentPos = m_viewWidth - m_scrollY - tapX;
        } else {
            contentPos = tapX - m_scrollY;  // scrollY acts as scrollX
        }
    } else {
        if (rotation == 180) {
            // Bottom-to-top layout: content start is at the bottom edge
            contentPos = m_viewHeight - m_scrollY - tapY;
        } else {
            contentPos = tapY - m_scrollY;
        }
    }

    // Use binary search to find which page contains the tap position
    rebuildOffsetCache();
    int page = findPageAtOffset(contentPos);
    if (page >= 0 && page < static_cast<int>(m_pageHeights.size())) {
        float pageStart = m_offsetCache[page];
        float pageSize = getEffectivePageSize(page);
        if (contentPos >= pageStart && contentPos < pageStart + pageSize) {
            return page;
        }
    }
    return -1;
}

void WebtoonScrollView::drawTransitionPage(NVGcontext* vg, int pageIndex,
                                             float x, float y, float width, float height) {
    // Dark background for transition (drawn in screen space)
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, Application::getInstance().getReaderBackground());
    nvgFill(vg);

    // Apply rotation so text matches page content orientation
    int rotation = static_cast<int>(m_rotationDegrees);
    float drawX = x, drawY = y, drawW = width, drawH = height;

    nvgSave(vg);

    if (rotation != 0) {
        float cx = x + width * 0.5f;
        float cy = y + height * 0.5f;
        nvgTranslate(vg, cx, cy);
        nvgRotate(vg, nvgDegToRad(static_cast<float>(rotation)));

        if (rotation == 90 || rotation == 270) {
            // In the rotated coordinate space, width and height are swapped
            drawX = -height * 0.5f;
            drawY = -width * 0.5f;
            drawW = height;
            drawH = width;
        } else {
            // 180°
            drawX = -width * 0.5f;
            drawY = -height * 0.5f;
            drawW = width;
            drawH = height;
        }
    }

    // Horizontal divider line
    float lineY = drawY + drawH * 0.35f;
    nvgBeginPath(vg);
    nvgRect(vg, drawX + drawW * 0.2f, lineY, drawW * 0.6f, 1.0f);
    nvgFillColor(vg, nvgRGBA(150, 150, 150, 100));
    nvgFill(vg);

    // Set font face for text rendering (required by nanoVG)
    nvgFontFace(vg, "regular");

    auto it = m_transitionInfo.find(pageIndex);
    if (it != m_transitionInfo.end()) {
        float fontSize1 = 20.0f;
        float fontSize2 = 16.0f;

        // Line 1 (above divider)
        if (!it->second.line1.empty()) {
            nvgFontSize(vg, fontSize1);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
            nvgFillColor(vg, nvgRGBA(220, 220, 220, 255));
            nvgText(vg, drawX + drawW * 0.5f, lineY - 8.0f, it->second.line1.c_str(), nullptr);
        }

        // Line 2 (below divider)
        if (!it->second.line2.empty()) {
            nvgFontSize(vg, fontSize2);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            nvgFillColor(vg, nvgRGBA(180, 180, 180, 255));
            nvgText(vg, drawX + drawW * 0.5f, lineY + 12.0f, it->second.line2.c_str(), nullptr);
        }

        // Scroll hint
        nvgFontSize(vg, 14.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
        nvgFillColor(vg, nvgRGBA(120, 120, 120, 200));
        nvgText(vg, drawX + drawW * 0.5f, drawY + drawH - 12.0f, "Keep scrolling to continue", nullptr);
    } else {
        // Fallback text if no transition info set
        nvgFontSize(vg, 18.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGBA(180, 180, 180, 255));
        nvgText(vg, drawX + drawW * 0.5f, drawY + drawH * 0.5f, "Chapter transition", nullptr);
    }

    nvgRestore(vg);
}

void WebtoonScrollView::drawFailedPage(NVGcontext* vg, int pageIndex,
                                        float x, float y, float width, float height) {
    // Dark red-tinted background (drawn in screen space)
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGBA(40, 20, 20, 255));
    nvgFill(vg);

    // Apply rotation so text matches page content orientation
    int rotation = static_cast<int>(m_rotationDegrees);
    float drawX = x, drawY = y, drawW = width, drawH = height;

    nvgSave(vg);

    if (rotation != 0) {
        float cx = x + width * 0.5f;
        float cy = y + height * 0.5f;
        nvgTranslate(vg, cx, cy);
        nvgRotate(vg, nvgDegToRad(static_cast<float>(rotation)));

        if (rotation == 90 || rotation == 270) {
            drawX = -height * 0.5f;
            drawY = -width * 0.5f;
            drawW = height;
            drawH = width;
        } else {
            drawX = -width * 0.5f;
            drawY = -height * 0.5f;
            drawW = width;
            drawH = height;
        }
    }

    float centerX = drawX + drawW * 0.5f;
    float centerY = drawY + drawH * 0.5f;

    // Set font face for text rendering (required by nanoVG)
    nvgFontFace(vg, "regular");

    // Error message
    nvgFontSize(vg, 16.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgFillColor(vg, nvgRGBA(200, 120, 120, 255));
    nvgText(vg, centerX, centerY - 4.0f, "Failed to load page", nullptr);

    // Retry prompt
    nvgFontSize(vg, 14.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(vg, Application::getInstance().getCtaButtonColor());
    nvgText(vg, centerX, centerY + 4.0f, "Tap to retry", nullptr);

    nvgRestore(vg);
}

void WebtoonScrollView::setPages(const std::vector<Page>& pages, float screenWidth, int startPage) {
    clearPages();

    m_pages = pages;
    m_viewWidth = screenWidth;
    // m_viewHeight will be updated from actual view dimensions in draw().
    // Use a reasonable default until then; the draw() dimensions (~726 internal)
    // differ from the physical screen (544) due to borealis DPI scaling.
    if (m_viewHeight <= 0.0f) {
        m_viewHeight = screenWidth * (544.0f / 960.0f);  // Maintain screen aspect ratio
    }

    // Calculate available width after padding
    float availableWidth = screenWidth - (m_sidePadding * 2);

    // For webtoons, we'll estimate heights based on typical aspect ratios
    // Images will be FIT_WIDTH, so height depends on aspect ratio
    // Default to 2:3 aspect ratio if unknown (will adjust when images load)
    float defaultHeight = availableWidth * 1.5f;  // 2:3 aspect ratio

    m_totalHeight = 0.0f;
    m_pageHeights.clear();
    m_pageHeights.reserve(pages.size());

    // Create image containers for each page
    for (size_t i = 0; i < pages.size(); i++) {
        float pageHeight;

        if (isTransitionPage(static_cast<int>(i))) {
            // Transition pages use a fixed smaller height
            pageHeight = TRANSITION_PAGE_HEIGHT;
        } else {
            pageHeight = defaultHeight;
        }

        auto pageImg = std::make_shared<RotatableImage>();
        pageImg->setWidth(availableWidth);
        pageImg->setHeight(pageHeight);
        pageImg->setScalingType(brls::ImageScalingType::FIT);
        pageImg->setBackgroundFillColor(m_bgColor);
        pageImg->setRotation(m_rotationDegrees);

        m_pageImages.push_back(pageImg);
        m_pageHeights.push_back(pageHeight);
        m_totalHeight += pageHeight + m_pageGap;
    }

    // Remove trailing gap
    if (!pages.empty() && m_pageGap > 0) {
        m_totalHeight -= m_pageGap;
    }

    invalidateOffsetCache();

    // Reset state
    m_scrollY = 0.0f;
    m_scrollVelocity = 0.0f;
    m_overscrollAmount = 0.0f;
    m_overscrollTriggered = false;
    m_currentPage = 0;
    m_loadedPages.clear();
    m_loadingPages.clear();
    m_failedPages.clear();
    m_transitionInfo.clear();

    // Reset auto-extend flags (user must scroll before auto-extend activates)
    m_trailingExtendTriggered = false;
    m_leadingExtendTriggered = false;
    m_userHasScrolled = false;
    m_extendingChapter = false;
    m_anchorPage = -1;

    // Scroll to start page BEFORE loading images so we load from the
    // resume position, not from page 0
    if (startPage > 0 && startPage < static_cast<int>(m_pages.size())) {
        float targetScroll = -getPageOffset(startPage);
        float maxScroll = 0.0f;
        float viewSize = isHorizontalLayout() ? m_viewWidth : m_viewHeight;
        float totalContentSize = getTotalContentSize();
        float minScroll = -(totalContentSize - viewSize);
        if (minScroll > maxScroll) minScroll = maxScroll;
        m_scrollY = std::max(minScroll, std::min(maxScroll, targetScroll));
        m_currentPage = startPage;

        // Set anchor so only pages before startPage get scroll compensation
        // when images load with different-than-estimated heights. Without this,
        // the cascading drift pushes visibleTop past the start page.
        m_anchorPage = startPage;
    }

    // Load visible images starting from current scroll position
    updateVisibleImages();

    brls::Logger::info("WebtoonScrollView: Set {} pages, startPage={}, totalHeight={}", pages.size(), startPage, m_totalHeight);
}

void WebtoonScrollView::clearPages() {
    // Invalidate alive flag so pending async callbacks don't mutate our state.
    // Image objects stay alive via shared_ptr until all async callbacks complete.
    *m_alive = false;
    m_alive = std::make_shared<bool>(true);

    m_pageImages.clear();
    m_pageHeights.clear();
    m_pages.clear();
    m_loadedPages.clear();
    m_loadingPages.clear();
    m_failedPages.clear();
    m_transitionInfo.clear();
    m_totalHeight = 0.0f;
    invalidateOffsetCache();
    m_scrollY = 0.0f;
    m_scrollVelocity = 0.0f;
    m_overscrollAmount = 0.0f;
    m_overscrollTriggered = false;
    m_currentPage = 0;
    m_trailingExtendTriggered = false;
    m_leadingExtendTriggered = false;
    m_userHasScrolled = false;
    m_extendingChapter = false;
    m_anchorPage = -1;

    // Reset zoom state
    resetZoom();
}

void WebtoonScrollView::appendPages(const std::vector<Page>& pages) {
    if (pages.empty()) return;

    float availableWidth = m_viewWidth - (m_sidePadding * 2);
    float defaultHeight = availableWidth * 1.5f;

    brls::Logger::info("WEBTOON_APPEND: start, adding {} pages, current total={}, scrollY={:.1f}, totalH={:.0f}",
                        pages.size(), m_pages.size(), m_scrollY, m_totalHeight);

    // Keep the existing trailing transition page as a chapter separator.
    // New pages are appended after it. No scroll adjustment needed since
    // all new content is below the current viewport.

    int startIdx = static_cast<int>(m_pages.size());

    for (size_t i = 0; i < pages.size(); i++) {
        const std::string& url = pages[i].imageUrl;
        bool isTransition = url.compare(0, TRANSITION_PREFIX.size(), TRANSITION_PREFIX) == 0;
        float pageHeight = isTransition ? TRANSITION_PAGE_HEIGHT : defaultHeight;

        auto pageImg = std::make_shared<RotatableImage>();
        pageImg->setWidth(availableWidth);
        pageImg->setHeight(pageHeight);
        pageImg->setScalingType(brls::ImageScalingType::FIT);
        pageImg->setBackgroundFillColor(m_bgColor);
        pageImg->setRotation(m_rotationDegrees);

        m_pages.push_back(pages[i]);
        m_pageImages.push_back(pageImg);
        m_pageHeights.push_back(pageHeight);

        if (startIdx + static_cast<int>(i) > 0) {
            m_totalHeight += m_pageGap;
        }
        m_totalHeight += pageHeight;
    }

    invalidateOffsetCache();

    // Kill momentum — prevent the user from flying through placeholder pages
    // at high speed, which causes jarring jumps when images load with different heights.
    m_scrollVelocity = 0.0f;

    // Reset overscroll since we just added content
    m_overscrollAmount = 0.0f;
    m_overscrollTriggered = false;

    // Allow next trailing auto-extend (new transition page at end)
    m_trailingExtendTriggered = false;

    // Load newly visible images
    updateVisibleImages();
    updateCurrentPage();

    brls::Logger::info("WEBTOON_APPEND: done, total={}, scrollY={:.1f}, totalH={:.0f}",
                        m_pages.size(), m_scrollY, m_totalHeight);
}

void WebtoonScrollView::prependPages(const std::vector<Page>& pages) {
    if (pages.empty()) return;

    float availableWidth = m_viewWidth - (m_sidePadding * 2);
    float defaultHeight = availableWidth * 1.5f;

    brls::Logger::info("WEBTOON_PREPEND: start, adding {} pages, current total={}, scrollY={:.1f}, totalH={:.0f}",
                        pages.size(), m_pages.size(), m_scrollY, m_totalHeight);

    // Keep the existing leading transition page as a chapter separator.
    // New pages are inserted before it so the user sees the boundary.

    // Build new page data
    std::vector<Page> newPages;
    std::vector<std::shared_ptr<RotatableImage>> newImages;
    std::vector<float> newHeights;

    for (size_t i = 0; i < pages.size(); i++) {
        const std::string& url = pages[i].imageUrl;
        bool isTransition = url.compare(0, TRANSITION_PREFIX.size(), TRANSITION_PREFIX) == 0;
        float pageHeight = isTransition ? TRANSITION_PAGE_HEIGHT : defaultHeight;

        auto pageImg = std::make_shared<RotatableImage>();
        pageImg->setWidth(availableWidth);
        pageImg->setHeight(pageHeight);
        pageImg->setScalingType(brls::ImageScalingType::FIT);
        pageImg->setBackgroundFillColor(m_bgColor);
        pageImg->setRotation(m_rotationDegrees);

        newPages.push_back(pages[i]);
        newImages.push_back(pageImg);
        newHeights.push_back(pageHeight);
    }

    // Append ALL existing pages (keeping the leading transition as chapter separator)
    for (size_t i = 0; i < m_pages.size(); i++) {
        newPages.push_back(m_pages[i]);
        newImages.push_back(m_pageImages[i]);
        newHeights.push_back(m_pageHeights[i]);
    }

    // Rebuild tracking sets with shifted indices
    int shift = static_cast<int>(pages.size());
    std::set<int> newLoaded, newLoading, newFailed;
    for (int idx : m_loadedPages) {
        int newIdx = idx + shift;
        if (newIdx >= 0 && newIdx < static_cast<int>(newPages.size())) {
            newLoaded.insert(newIdx);
        }
    }
    for (int idx : m_loadingPages) {
        int newIdx = idx + shift;
        if (newIdx >= 0 && newIdx < static_cast<int>(newPages.size())) {
            newLoading.insert(newIdx);
        }
    }
    for (int idx : m_failedPages) {
        int newIdx = idx + shift;
        if (newIdx >= 0 && newIdx < static_cast<int>(newPages.size())) {
            newFailed.insert(newIdx);
        }
    }

    // Shift transition info
    std::map<int, TransitionInfo> newTransitionInfo;
    for (auto& pair : m_transitionInfo) {
        int newIdx = pair.first + shift;
        if (newIdx >= 0 && newIdx < static_cast<int>(newPages.size())) {
            newTransitionInfo[newIdx] = pair.second;
        }
    }

    // Replace all data
    m_pages = std::move(newPages);
    m_pageImages = std::move(newImages);
    m_pageHeights = std::move(newHeights);
    m_loadedPages = std::move(newLoaded);
    m_loadingPages = std::move(newLoading);
    m_failedPages = std::move(newFailed);
    m_transitionInfo = std::move(newTransitionInfo);

    // Recalculate total height from scratch for accuracy
    m_totalHeight = 0.0f;
    for (size_t i = 0; i < m_pageHeights.size(); i++) {
        m_totalHeight += m_pageHeights[i];
        if (i > 0) m_totalHeight += m_pageGap;
    }

    invalidateOffsetCache();

    // Adjust scroll position so existing content stays at the exact same screen position.
    // Use getPageOffset(shift) which returns the offset in scroll-axis space,
    // correctly handling horizontal layout where effective page size differs from height.
    float scrollAdjust = getPageOffset(shift);
    float prevScrollY = m_scrollY;
    m_scrollY -= scrollAdjust;

    // Kill any momentum - prevents the velocity from causing drift after the adjustment
    m_scrollVelocity = 0.0f;

    // Adjust current page index
    m_currentPage += shift;

    // Set anchor page so that only pages before this index get scroll
    // compensation when their image heights change. This prevents the
    // cascading drift where each compensation shifts visibleTop and
    // causes further pages to appear above the viewport.
    m_anchorPage = m_currentPage;

    brls::Logger::info("WEBTOON_PREPEND: scrollAdjust={:.1f}, scrollY {:.1f} -> {:.1f}, shift={}, anchor={}",
                        scrollAdjust, prevScrollY, m_scrollY, shift, m_anchorPage);

    // Reset overscroll
    m_overscrollAmount = 0.0f;
    m_overscrollTriggered = false;

    // Allow next leading auto-extend (new transition page at start)
    m_leadingExtendTriggered = false;

    // Load newly visible images
    updateVisibleImages();
    updateCurrentPage();

    brls::Logger::info("WEBTOON_PREPEND: done, total={}, scrollY={:.1f}, totalH={:.0f}",
                        m_pages.size(), m_scrollY, m_totalHeight);
}

void WebtoonScrollView::trimPagesFromStart(int count) {
    if (count <= 0 || count >= static_cast<int>(m_pages.size())) return;

    brls::Logger::info("WEBTOON_TRIM_START: removing {} pages from start, total={}, scrollY={:.1f}",
                        count, m_pages.size(), m_scrollY);

    // Calculate effective size of pages being removed in scroll-axis space.
    // Must be computed BEFORE removing from arrays. getPageOffset handles
    // horizontal layout correctly (uses effective page sizes, not raw heights).
    float removedEffective = getPageOffset(count);

    // Remove from vectors
    m_pages.erase(m_pages.begin(), m_pages.begin() + count);
    m_pageImages.erase(m_pageImages.begin(), m_pageImages.begin() + count);
    m_pageHeights.erase(m_pageHeights.begin(), m_pageHeights.begin() + count);

    // Shift tracking sets down by count
    std::set<int> newLoaded, newLoading, newFailed;
    for (int idx : m_loadedPages) {
        int newIdx = idx - count;
        if (newIdx >= 0 && newIdx < static_cast<int>(m_pages.size())) {
            newLoaded.insert(newIdx);
        }
    }
    for (int idx : m_loadingPages) {
        int newIdx = idx - count;
        if (newIdx >= 0 && newIdx < static_cast<int>(m_pages.size())) {
            newLoading.insert(newIdx);
        }
    }
    for (int idx : m_failedPages) {
        int newIdx = idx - count;
        if (newIdx >= 0 && newIdx < static_cast<int>(m_pages.size())) {
            newFailed.insert(newIdx);
        }
    }
    m_loadedPages = std::move(newLoaded);
    m_loadingPages = std::move(newLoading);
    m_failedPages = std::move(newFailed);

    // Shift transition info
    std::map<int, TransitionInfo> newTransitionInfo;
    for (auto& pair : m_transitionInfo) {
        int newIdx = pair.first - count;
        if (newIdx >= 0 && newIdx < static_cast<int>(m_pages.size())) {
            newTransitionInfo[newIdx] = pair.second;
        }
    }
    m_transitionInfo = std::move(newTransitionInfo);

    // Recalculate total height
    m_totalHeight = 0.0f;
    for (size_t i = 0; i < m_pageHeights.size(); i++) {
        m_totalHeight += m_pageHeights[i];
        if (i > 0) m_totalHeight += m_pageGap;
    }

    invalidateOffsetCache();

    // Adjust scroll: content above was removed, so shift scroll up
    m_scrollY += removedEffective;

    // Adjust current page
    m_currentPage = std::max(0, m_currentPage - count);

    // Adjust anchor page (shift by trimmed count, disable if trimmed away)
    if (m_anchorPage >= 0) {
        m_anchorPage = std::max(-1, m_anchorPage - count);
    }

    brls::Logger::info("WEBTOON_TRIM_START: done, total={}, scrollY={:.1f}, removedEffective={:.0f}, anchor={}",
                        m_pages.size(), m_scrollY, removedEffective, m_anchorPage);
}

void WebtoonScrollView::trimPagesFromEnd(int count) {
    if (count <= 0 || count >= static_cast<int>(m_pages.size())) return;

    brls::Logger::info("WEBTOON_TRIM_END: removing {} pages from end, total={}", count, m_pages.size());

    int newSize = static_cast<int>(m_pages.size()) - count;

    // Remove from tracking sets
    for (int i = newSize; i < static_cast<int>(m_pages.size()); i++) {
        m_loadedPages.erase(i);
        m_loadingPages.erase(i);
        m_failedPages.erase(i);
        m_transitionInfo.erase(i);
    }

    // Remove from vectors
    m_pages.resize(newSize);
    m_pageImages.resize(newSize);
    m_pageHeights.resize(newSize);

    // Recalculate total height
    m_totalHeight = 0.0f;
    for (size_t i = 0; i < m_pageHeights.size(); i++) {
        m_totalHeight += m_pageHeights[i];
        if (i > 0) m_totalHeight += m_pageGap;
    }

    invalidateOffsetCache();

    // Clamp scroll position in case the user was scrolled near the end
    float viewSize = isHorizontalLayout() ? m_viewWidth : m_viewHeight;
    float totalContent = getTotalContentSize();
    float minScroll = -(totalContent - viewSize);
    if (minScroll > 0.0f) minScroll = 0.0f;
    if (m_scrollY < minScroll) {
        m_scrollY = minScroll;
        m_scrollVelocity = 0.0f;
    }

    // Clamp current page if needed
    if (m_currentPage >= newSize) {
        m_currentPage = std::max(0, newSize - 1);
    }

    brls::Logger::info("WEBTOON_TRIM_END: done, total={}, totalHeight={:.0f}, scrollY={:.1f}",
                        m_pages.size(), m_totalHeight, m_scrollY);
}

int WebtoonScrollView::getRealPageCount() const {
    int count = 0;
    for (int i = 0; i < static_cast<int>(m_pages.size()); i++) {
        if (!isTransitionPage(i)) count++;
    }
    return count;
}

int WebtoonScrollView::displayPageFromIndex(int pageIndex) const {
    int display = 0;
    for (int i = 0; i < pageIndex && i < static_cast<int>(m_pages.size()); i++) {
        if (!isTransitionPage(i)) display++;
    }
    return display;
}

int WebtoonScrollView::pageIndexFromDisplayPage(int displayPage) const {
    int realCount = 0;
    for (int i = 0; i < static_cast<int>(m_pages.size()); i++) {
        if (!isTransitionPage(i)) {
            if (realCount == displayPage) return i;
            realCount++;
        }
    }
    // Fallback: return last real page
    for (int i = static_cast<int>(m_pages.size()) - 1; i >= 0; i--) {
        if (!isTransitionPage(i)) return i;
    }
    return 0;
}

void WebtoonScrollView::scrollToPage(int pageIndex) {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size())) {
        return;
    }

    // Calculate scroll position for this page
    float targetScroll = -getPageOffset(pageIndex);

    // Clamp scroll position based on layout direction
    float maxScroll = 0.0f;
    float viewSize = isHorizontalLayout() ? m_viewWidth : m_viewHeight;
    float totalContentSize = getTotalContentSize();
    float minScroll = -(totalContentSize - viewSize);
    if (minScroll > maxScroll) minScroll = maxScroll;

    m_scrollY = std::max(minScroll, std::min(maxScroll, targetScroll));
    m_scrollVelocity = 0.0f;
    m_anchorPage = -1;  // Clear anchor - explicit scroll sets a new viewport

    updateVisibleImages();
    updateCurrentPage();
}

float WebtoonScrollView::getScrollProgress() const {
    float viewSize = isHorizontalLayout() ? m_viewWidth : m_viewHeight;
    float totalContentSize = getTotalContentSize();
    if (totalContentSize <= viewSize) {
        return 0.0f;
    }
    float scrollable = totalContentSize - viewSize;
    return std::min(1.0f, std::max(0.0f, -m_scrollY / scrollable));
}

void WebtoonScrollView::setBackgroundColor(NVGcolor color) {
    m_bgColor = color;

    // Update background on all existing page images
    for (auto& img : m_pageImages) {
        if (img) {
            img->setBackgroundFillColor(color);
        }
    }
}

void WebtoonScrollView::setSidePadding(int percent) {
    if (percent < 0 || percent > 20) {
        percent = 0;
    }
    float paddingRatio = percent / 100.0f;
    m_sidePadding = m_viewWidth * paddingRatio;
    invalidateOffsetCache();
}

void WebtoonScrollView::setRotation(float degrees) {
    // Normalize to 0, 90, 180, 270
    int normalized = static_cast<int>(degrees) % 360;
    if (normalized < 0) normalized += 360;

    // Snap to nearest 90 degree increment
    if (normalized < 45) {
        m_rotationDegrees = 0.0f;
    } else if (normalized < 135) {
        m_rotationDegrees = 90.0f;
    } else if (normalized < 225) {
        m_rotationDegrees = 180.0f;
    } else if (normalized < 315) {
        m_rotationDegrees = 270.0f;
    } else {
        m_rotationDegrees = 0.0f;
    }

    // Apply rotation directly to all existing page images
    // RotatableImage handles rotation rendering correctly on its own
    for (auto& img : m_pageImages) {
        if (img) {
            img->setRotation(m_rotationDegrees);
        }
    }

    invalidateOffsetCache();
    brls::Logger::debug("WebtoonScrollView: setRotation({}) -> {} degrees", degrees, m_rotationDegrees);
}

bool WebtoonScrollView::isHorizontalLayout() const {
    int rotation = static_cast<int>(m_rotationDegrees);
    return (rotation == 90 || rotation == 270);
}

float WebtoonScrollView::getEffectivePageSize(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pageHeights.size())) {
        return 0.0f;
    }

    if (!isHorizontalLayout()) {
        // Vertical layout: use stored page height directly
        return m_pageHeights[pageIndex];
    }

    // Horizontal layout: calculate page width from stored height
    float availableWidth = m_viewWidth - (m_sidePadding * 2);
    float availableHeight = m_viewHeight - (m_sidePadding * 2);

    if (availableWidth > 0 && m_pageHeights[pageIndex] > 0) {
        return m_pageHeights[pageIndex] * (availableHeight / availableWidth);
    }

    // Fallback
    return availableHeight * 1.5f;
}

float WebtoonScrollView::getTotalContentSize() const {
    if (!isHorizontalLayout()) {
        // Vertical layout: use stored total height
        return m_totalHeight;
    }

    // Horizontal layout: use offset cache (last entry = total size + trailing gap)
    rebuildOffsetCache();
    if (m_offsetCache.size() > 1) {
        // Subtract trailing m_pageGap since there's no gap after the last page
        return m_offsetCache.back() - m_pageGap;
    }
    return 0.0f;
}

void WebtoonScrollView::rebuildOffsetCache() const {
    if (!m_offsetCacheDirty) return;

    int n = static_cast<int>(m_pageHeights.size());
    m_offsetCache.resize(n + 1);
    m_offsetCache[0] = 0.0f;
    // Match getPageOffset semantics: each page followed by m_pageGap
    for (int i = 0; i < n; i++) {
        m_offsetCache[i + 1] = m_offsetCache[i] + getEffectivePageSize(i) + m_pageGap;
    }
    m_offsetCacheDirty = false;
}

void WebtoonScrollView::invalidateOffsetCache() {
    m_offsetCacheDirty = true;
}

int WebtoonScrollView::findPageAtOffset(float targetOffset) const {
    rebuildOffsetCache();
    int n = static_cast<int>(m_pageHeights.size());
    if (n == 0) return 0;

    // Binary search: find the last page whose start offset <= targetOffset
    int lo = 0, hi = n - 1;
    int result = 0;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (m_offsetCache[mid] <= targetOffset) {
            result = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return result;
}

void WebtoonScrollView::onFrame() {
    // Calculate frame delta time for frame-rate independent physics.
    // Velocity is stored in units of "pixels per frame at 60fps" (~16.67ms).
    // When the Vita drops frames, dtFrames > 1.0 and we apply proportionally
    // more scroll and more friction, so deceleration feels the same at any fps.
    auto now = std::chrono::steady_clock::now();
    float dtMs = std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastFrameTime).count() / 1000.0f;
    m_lastFrameTime = now;
    // Clamp to avoid spiral on very long frames (e.g. loading stalls)
    float dtFrames = std::min(dtMs / 16.667f, 4.0f);

    // Apply momentum scrolling when not touching
    if (!m_isTouching && std::abs(m_scrollVelocity) > MOMENTUM_MIN_VELOCITY) {
        m_scrollY += m_scrollVelocity * dtFrames;
        m_scrollVelocity *= std::pow(MOMENTUM_FRICTION, dtFrames);

        // Clamp scroll position based on layout direction
        float maxScroll = 0.0f;
        float viewSize = isHorizontalLayout() ? m_viewWidth : m_viewHeight;
        float totalContentSize = getTotalContentSize();
        float minScroll = -(totalContentSize - viewSize);
        if (minScroll > maxScroll) minScroll = maxScroll;

        // Bounce back if out of bounds
        if (m_scrollY > maxScroll) {
            m_scrollY = maxScroll;
            m_scrollVelocity = 0.0f;
        } else if (m_scrollY < minScroll) {
            m_scrollY = minScroll;
            m_scrollVelocity = 0.0f;
        }

        updateVisibleImages();
        updateCurrentPage();

        // Don't update anchor during momentum — keep it fixed so
        // prepended pages always get scroll compensation (see STAY handler).
    }
}

float WebtoonScrollView::getPageOffset(int pageIndex) const {
    if (pageIndex <= 0 || pageIndex >= static_cast<int>(m_pageHeights.size())) {
        return 0.0f;
    }

    rebuildOffsetCache();
    return m_offsetCache[pageIndex];
}

bool WebtoonScrollView::isPageVisible(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pageHeights.size())) {
        return false;
    }

    rebuildOffsetCache();
    float pageStart = m_offsetCache[pageIndex];
    float pageSize = getEffectivePageSize(pageIndex);
    float pageEnd = pageStart + pageSize;

    float visibleStart = -m_scrollY;
    float viewSize = isHorizontalLayout() ? m_viewWidth : m_viewHeight;
    float visibleEnd = visibleStart + viewSize;
    return (pageEnd > visibleStart && pageStart < visibleEnd);
}

void WebtoonScrollView::updateVisibleImages() {
    if (m_pages.empty()) return;

    // Use binary search to find visible page range (O(log n) instead of O(n))
    rebuildOffsetCache();
    int n = static_cast<int>(m_pages.size());

    float visibleStart = -m_scrollY;
    float viewSize = isHorizontalLayout() ? m_viewWidth : m_viewHeight;
    float visibleEnd = visibleStart + viewSize;

    // Find first page that overlaps the visible area
    // findPageAtOffset returns the last page whose start <= visibleStart,
    // which is exactly the topmost visible (or partially visible) page.
    int firstVisible = findPageAtOffset(visibleStart);
    // Clamp to valid range
    firstVisible = std::max(0, std::min(firstVisible, n - 1));

    // Find last visible page by scanning forward from firstVisible
    int lastVisible = firstVisible;
    for (int i = firstVisible; i < n; i++) {
        float pageStart = m_offsetCache[i];
        if (pageStart > visibleEnd) break;
        lastVisible = i;
    }

    if (firstVisible < 0 || firstVisible >= n) {
        return;  // No visible pages
    }

    // Expand range for preloading
    int loadStart = std::max(0, firstVisible - PRELOAD_PAGES);
    int loadEnd = std::min(static_cast<int>(m_pages.size()) - 1, lastVisible + PRELOAD_PAGES);

    // Capture alive flag for async callbacks
    std::weak_ptr<bool> aliveWeak = m_alive;

    // Load pages in range
    for (int i = loadStart; i <= loadEnd; i++) {
        if (m_loadedPages.count(i) > 0 || m_loadingPages.count(i) > 0) {
            continue;  // Already loaded or loading
        }

        // Skip transition pages - they don't have real images to load
        if (isTransitionPage(i)) {
            m_loadedPages.insert(i);  // Mark as "loaded" so we don't retry
            continue;
        }

        // Skip failed pages (user must tap to retry)
        if (m_failedPages.count(i) > 0) {
            continue;
        }

        m_loadingPages.insert(i);

        const Page& page = m_pages[i];
        std::shared_ptr<RotatableImage> imgPtr = m_pageImages[i];
        RotatableImage* img = imgPtr.get();

        // Load the image
        // Capture the index at queue time for O(1) lookup in the common case.
        // After prepend/append, indices shift, so verify imgPtr still matches
        // and fall back to linear scan only when it doesn't.
        ImageLoader::loadAsyncFullSize(page.imageUrl,
            [this, aliveWeak, imgPtr, capturedIdx = i](RotatableImage* loadedImg) {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                // Fast path: check if the image is still at the captured index
                int pageIndex = capturedIdx;
                if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pageImages.size()) ||
                    m_pageImages[pageIndex] != imgPtr) {
                    // Index shifted (prepend/append happened) — fall back to linear scan
                    pageIndex = -1;
                    for (int idx = 0; idx < static_cast<int>(m_pageImages.size()); idx++) {
                        if (m_pageImages[idx] == imgPtr) {
                            pageIndex = idx;
                            break;
                        }
                    }
                }
                if (pageIndex < 0) {
                    // Image was removed (trimmed) while loading — discard silently
                    return;
                }

                m_loadingPages.erase(pageIndex);

                if (imgPtr->hasImage()) {
                    m_loadedPages.insert(pageIndex);

                    float imageWidth = static_cast<float>(imgPtr->getImageWidth());
                    float imageHeight = static_cast<float>(imgPtr->getImageHeight());

                    if (imageWidth > 0 && imageHeight > 0) {
                        float availableWidth = m_viewWidth - (m_sidePadding * 2);
                        float aspectRatio = imageHeight / imageWidth;
                        float newHeight = availableWidth * aspectRatio;

                        float oldHeight = m_pageHeights[pageIndex];
                        float heightDelta = newHeight - oldHeight;

                        if (std::abs(heightDelta) > 0.5f) {
                            float pageStart = getPageOffset(pageIndex);
                            float visibleTop = -m_scrollY;
                            float viewSize = isHorizontalLayout() ? m_viewWidth : m_viewHeight;
                            float visibleBottom = visibleTop + viewSize;

                            m_totalHeight += heightDelta;
                            m_pageHeights[pageIndex] = newHeight;
                            invalidateOffsetCache();

                            // Compensate scroll for pages above the user's reading
                            // position so visible content stays pinned in place.
                            //
                            // Use anchor-page check when available (after prepend/append)
                            // to avoid cascading drift: only pages before the anchor
                            // get compensation. Otherwise fall back to position check.
                            bool shouldAdjust = false;
                            if (m_anchorPage >= 0) {
                                shouldAdjust = (pageIndex < m_anchorPage);
                            } else {
                                shouldAdjust = (pageStart < visibleTop);
                            }

                            if (shouldAdjust) {
                                // In horizontal layout, scroll is in effective-size
                                // space (height * availableH/availableW), not raw height.
                                float scrollDelta = heightDelta;
                                if (isHorizontalLayout()) {
                                    float aw = m_viewWidth - (m_sidePadding * 2);
                                    float ah = m_viewHeight - (m_sidePadding * 2);
                                    if (aw > 0) scrollDelta = heightDelta * (ah / aw);
                                }
                                m_scrollY -= scrollDelta;
                                brls::Logger::info("WEBTOON_HEIGHT: page {} (anchor={}, start={:.0f}, visTop={:.0f}), scroll adjusted by {:.1f} (hDelta={:.1f})",
                                                    pageIndex, m_anchorPage, pageStart, visibleTop, -scrollDelta, heightDelta);
                            } else {
                                brls::Logger::info("WEBTOON_HEIGHT: page {} below anchor/viewport (anchor={}, start={:.0f} visTop={:.0f}), delta={:.1f}, no adjust",
                                                    pageIndex, m_anchorPage, pageStart, visibleTop, heightDelta);
                            }
                        }
                    }
                } else {
                    // Image load failed - mark as failed for retry
                    m_failedPages.insert(pageIndex);

                    float oldHeight = m_pageHeights[pageIndex];
                    if (oldHeight > FAILED_PAGE_HEIGHT * 2) {
                        float pageStart = getPageOffset(pageIndex);
                        float visibleTop = -m_scrollY;

                        float heightDelta = FAILED_PAGE_HEIGHT - oldHeight;
                        m_totalHeight += heightDelta;
                        m_pageHeights[pageIndex] = FAILED_PAGE_HEIGHT;
                        invalidateOffsetCache();

                        bool shouldAdjust = (m_anchorPage >= 0)
                            ? (pageIndex < m_anchorPage)
                            : (pageStart < visibleTop);
                        if (shouldAdjust) {
                            float scrollDelta = heightDelta;
                            if (isHorizontalLayout()) {
                                float aw = m_viewWidth - (m_sidePadding * 2);
                                float ah = m_viewHeight - (m_sidePadding * 2);
                                if (aw > 0) scrollDelta = heightDelta * (ah / aw);
                            }
                            m_scrollY -= scrollDelta;
                        }
                    }

                    brls::Logger::warning("WebtoonScrollView: Failed to load page {}", pageIndex);
                }

                brls::Logger::debug("WebtoonScrollView: Loaded page {}", pageIndex);
            }, img, m_alive);
    }

    // Unload images that are far from the visible area to free GPU memory
    // This is important when multiple chapters accumulate via append/prepend
    // Iterate m_loadedPages (small set, typically ~10 entries) instead of all
    // pages to avoid O(n) per-frame cost with 170+ pages across chapters.
    int keepStart = std::max(0, firstVisible - UNLOAD_PAGES);
    int keepEnd = std::min(static_cast<int>(m_pages.size()) - 1, lastVisible + UNLOAD_PAGES);

    // Collect pages to unload (can't erase from set while iterating)
    std::vector<int> toUnload;
    for (int i : m_loadedPages) {
        if (i < keepStart || i > keepEnd) {
            if (!isTransitionPage(i) && i < static_cast<int>(m_pageImages.size()) &&
                m_pageImages[i] && m_pageImages[i]->hasImage()) {
                toUnload.push_back(i);
            }
        }
    }
    for (int i : toUnload) {
        m_pageImages[i]->clearImage();
        m_loadedPages.erase(i);
    }

    // Auto-extend: seamlessly load next/prev chapter when approaching transition pages.
    // Only trigger when the entire transition page is visible on screen so the user
    // can read the chapter separator text. The next/prev chapter loads behind the
    // transition page and only appears when the user keeps scrolling past it.
    if (!m_extendingChapter && m_userHasScrolled && m_chapterNavigateCallback && firstVisible >= 0) {
        float visibleStart = -m_scrollY;
        float viewSize = isHorizontalLayout() ? m_viewWidth : m_viewHeight;
        float visibleEnd = visibleStart + viewSize;

        // Check trailing transition page (next chapter)
        int lastIdx = static_cast<int>(m_pages.size()) - 1;
        if (lastIdx >= 0 && isTransitionPage(lastIdx) && !m_trailingExtendTriggered && lastVisible >= 0) {
            float pageStart = getPageOffset(lastIdx);
            float pageSize = getEffectivePageSize(lastIdx);
            float pageEnd = pageStart + pageSize;
            // Extend once the entire transition page is on screen
            if (pageEnd <= visibleEnd) {
                brls::Logger::info("WEBTOON_EXTEND: TRAILING trigger! scrollY={:.1f}, visEnd={:.1f}, pageEnd={:.1f}, pages={}, vel={:.1f}",
                                    m_scrollY, visibleEnd, pageEnd, m_pages.size(), m_scrollVelocity);
                m_extendingChapter = true;
                m_trailingExtendTriggered = true;
                float preScrollY = m_scrollY;
                auto it = m_transitionInfo.find(lastIdx);
                bool isNext = (it != m_transitionInfo.end()) ? it->second.isNext : true;
                m_chapterNavigateCallback(isNext);
                m_extendingChapter = false;
                brls::Logger::info("WEBTOON_EXTEND: TRAILING done, scrollY {:.1f} -> {:.1f}, pages {}",
                                    preScrollY, m_scrollY, m_pages.size());
            }
        }

        // Check leading transition page (previous chapter)
        if (!m_pages.empty() && isTransitionPage(0) && !m_leadingExtendTriggered) {
            // Page 0 starts at offset 0; extend once its top edge is visible
            // (i.e. the user has scrolled up enough to see the full page)
            if (visibleStart <= 0.0f) {
                brls::Logger::info("WEBTOON_EXTEND: LEADING trigger! scrollY={:.1f}, visStart={:.1f}, pages={}, vel={:.1f}",
                                    m_scrollY, visibleStart, m_pages.size(), m_scrollVelocity);
                m_extendingChapter = true;
                m_leadingExtendTriggered = true;
                float preScrollY = m_scrollY;
                auto it = m_transitionInfo.find(0);
                bool isNext = (it != m_transitionInfo.end()) ? it->second.isNext : false;
                m_chapterNavigateCallback(isNext);
                m_extendingChapter = false;
                brls::Logger::info("WEBTOON_EXTEND: LEADING done, scrollY {:.1f} -> {:.1f}, pages {}",
                                    preScrollY, m_scrollY, m_pages.size());
            }
        }
    }
}

void WebtoonScrollView::updateCurrentPage() {
    if (m_pages.empty()) return;

    // Use binary search to find the page at the start of the visible area (O(log n))
    rebuildOffsetCache();
    float visibleStart = -m_scrollY;

    int pageAtTop = findPageAtOffset(visibleStart);
    int n = static_cast<int>(m_pages.size());
    pageAtTop = std::max(0, std::min(pageAtTop, n - 1));

    // Check if the page end extends past the visible start
    float pageEnd = m_offsetCache[pageAtTop] + getEffectivePageSize(pageAtTop);
    int newCurrentPage = 0;

    if (pageEnd > visibleStart) {
        // Skip transition pages for progress reporting
        if (!isTransitionPage(pageAtTop)) {
            newCurrentPage = pageAtTop;
        } else if (pageAtTop + 1 < n) {
            newCurrentPage = pageAtTop + 1;
        }
    } else if (pageAtTop + 1 < n) {
        newCurrentPage = pageAtTop + 1;
    }

    if (newCurrentPage != m_currentPage) {
        m_currentPage = newCurrentPage;

        // Notify progress callback
        if (m_progressCallback) {
            m_progressCallback(m_currentPage, static_cast<int>(m_pages.size()), getScrollProgress());
        }
    }
}

void WebtoonScrollView::draw(NVGcontext* vg, float x, float y, float width, float height,
                              brls::Style style, brls::FrameContext* ctx) {
    // Update view dimensions
    m_viewWidth = width;
    m_viewHeight = height;

    // Draw background
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, m_bgColor);
    nvgFill(vg);

    // Save state and set scissor for clipping
    nvgSave(vg);
    nvgScissor(vg, x, y, width, height);

    // Apply zoom transform if zoomed
    if (m_zoomLevel != 1.0f) {
        float centerX = x + width / 2.0f;
        float centerY = y + height / 2.0f;
        nvgTranslate(vg, centerX, centerY);
        nvgScale(vg, m_zoomLevel, m_zoomLevel);
        nvgTranslate(vg, m_zoomOffset.x, m_zoomOffset.y);
        nvgTranslate(vg, -centerX, -centerY);
    }

    bool horizontal = isHorizontalLayout();

    // Rebuild offset cache once for O(1) lookups in the draw loop
    rebuildOffsetCache();
    int pageCount = static_cast<int>(m_pageImages.size());

    // Use binary search to find the range of pages to draw (with 100px margin)
    // This avoids iterating all pages every frame - critical for chapters with 90+ pages
    float margin = 100.0f;
    float visibleStart = -m_scrollY - margin;
    float visibleEnd = -m_scrollY + (horizontal ? width : height) + margin;
    int drawStart = std::max(0, findPageAtOffset(visibleStart));
    // Step back one to catch partially visible pages
    if (drawStart > 0) drawStart--;
    int drawEnd = std::min(pageCount - 1, findPageAtOffset(visibleEnd));
    // Step forward one to catch partially visible pages
    if (drawEnd < pageCount - 1) drawEnd++;

    if (horizontal) {
        // Horizontal layout (for 90/270 rotation)
        float availableHeight = height - (m_sidePadding * 2);
        float pageY = y + m_sidePadding;
        int rotation = static_cast<int>(m_rotationDegrees);

        // 90° CW: original top maps to RIGHT side, so pages flow right-to-left
        // 270° CW: original top maps to LEFT side, so pages flow left-to-right
        bool rightToLeft = (rotation == 90);

        for (int i = drawStart; i <= drawEnd && i < pageCount; i++) {
            RotatableImage* img = m_pageImages[i].get();

            float pageWidth;
            if (isTransitionPage(i)) {
                pageWidth = TRANSITION_PAGE_HEIGHT;
            } else if (isFailedPage(i)) {
                pageWidth = FAILED_PAGE_HEIGHT;
            } else if (img && img->hasImage() && img->getImageWidth() > 0 && img->getImageHeight() > 0) {
                float rotatedAspect = static_cast<float>(img->getImageHeight()) / static_cast<float>(img->getImageWidth());
                pageWidth = availableHeight * rotatedAspect;
            } else {
                float availableWidth = width - (m_sidePadding * 2);
                if (availableWidth > 0) {
                    pageWidth = m_pageHeights[i] * (availableHeight / availableWidth);
                } else {
                    pageWidth = availableHeight * 1.5f;
                }
            }

            // Calculate screen X position based on layout direction (O(1) offset lookup)
            float contentOffset = m_offsetCache[i];
            float pageX_screen;
            if (rightToLeft) {
                // RTL: content start at right edge, pages flow leftward
                pageX_screen = x + width - m_scrollY - contentOffset - pageWidth;
            } else {
                // LTR: content start at left edge, pages flow rightward
                pageX_screen = x + m_scrollY + contentOffset;
            }

            if (isTransitionPage(i)) {
                drawTransitionPage(vg, i, pageX_screen, pageY, pageWidth, availableHeight);
            } else if (isFailedPage(i)) {
                drawFailedPage(vg, i, pageX_screen, pageY, pageWidth, availableHeight);
            } else if (img) {
                img->setWidth(pageWidth);
                img->setHeight(availableHeight);
                img->draw(vg, pageX_screen, pageY, pageWidth, availableHeight, style, ctx);
            }
        }
    } else {
        // Vertical layout (for 0/180 rotation)
        float availableWidth = width - (m_sidePadding * 2);
        float pageX = x + m_sidePadding;
        int rotation = static_cast<int>(m_rotationDegrees);

        // 180°: original top maps to BOTTOM, so pages flow bottom-to-top
        // 0°: normal top-to-bottom
        bool bottomToTop = (rotation == 180);

        for (int i = drawStart; i <= drawEnd && i < pageCount; i++) {
            float pageHeight = m_pageHeights[i];

            // Calculate screen Y position based on layout direction (O(1) offset lookup)
            float contentOffset = m_offsetCache[i];
            float pageY_screen;
            if (bottomToTop) {
                // BTT: content start at bottom edge, pages flow upward
                pageY_screen = y + height - m_scrollY - contentOffset - pageHeight;
            } else {
                // TTB: content start at top edge, pages flow downward
                pageY_screen = y + m_scrollY + contentOffset;
            }

            if (isTransitionPage(i)) {
                drawTransitionPage(vg, i, pageX, pageY_screen, availableWidth, pageHeight);
            } else if (isFailedPage(i)) {
                drawFailedPage(vg, i, pageX, pageY_screen, availableWidth, pageHeight);
            } else {
                RotatableImage* img = m_pageImages[i].get();
                if (img) {
                    img->setWidth(availableWidth);
                    img->draw(vg, pageX, pageY_screen, availableWidth, pageHeight, style, ctx);
                }
            }
        }
    }

    // Restore state
    nvgRestore(vg);

    // Call onFrame for momentum updates
    onFrame();
}

brls::View* WebtoonScrollView::create() {
    return new WebtoonScrollView();
}

} // namespace vitasuwayomi
