/**
 * WebtoonScrollView implementation
 * Continuous vertical scrolling for webtoon reading
 */

#include "view/webtoon_scroll_view.hpp"
#include "utils/image_loader.hpp"
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

    // Setup touch gestures
    setupGestures();
}

WebtoonScrollView::~WebtoonScrollView() {
    *m_alive = false;
    clearPages();
}

void WebtoonScrollView::setupGestures() {
    this->setFocusable(true);

    // Pan gesture for scrolling (ANY axis to support rotated views)
    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
            if (status.state == brls::GestureState::START) {
                m_isTouching = true;
                m_touchStart = status.position;
                m_touchLast = status.position;
                m_scrollAtTouchStart = m_scrollY;
                m_scrollVelocity = 0.0f;
                m_overscrollAmount = 0.0f;
                m_overscrollTriggered = false;
                m_lastTouchTime = std::chrono::steady_clock::now();
            } else if (status.state == brls::GestureState::STAY) {
                // Calculate raw deltas
                float rawDx = status.position.x - m_touchLast.x;
                float rawDy = status.position.y - m_touchLast.y;

                // Calculate scroll delta based on rotation
                // scrollY: 0 = beginning (top/left), negative = scrolled towards end
                // 0°: Normal vertical scrolling - swipe up (negative dy) to scroll down
                // 90°: Horizontal scrolling - swipe left (negative dx) to scroll down (forward)
                // 180°: Inverted vertical scrolling - swipe down (positive dy) to scroll down
                // 270°: Horizontal scrolling - swipe right (positive dx) to scroll down (forward)
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
                    // Swipe left (negative dx) should scroll forward (decrease scrollY)
                    scrollDelta = rawDx * scaleX;
                } else if (rotation == 180) {
                    scrollDelta = -rawDy * scaleY;
                } else if (rotation == 270) {
                    // Swipe right (positive dx) should scroll forward (decrease scrollY)
                    scrollDelta = -rawDx * scaleX;
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

                // Track overscroll for chapter navigation
                // If user is scrolling past the boundary, accumulate overscroll
                float unclamped = prevScrollY + scrollDelta;
                if (unclamped > maxScroll && scrollDelta > 0) {
                    // Overscrolling at the beginning (scrolling up past start)
                    m_overscrollAmount += scrollDelta;
                    if (m_overscrollAmount > OVERSCROLL_THRESHOLD && !m_overscrollTriggered) {
                        // Check if first page is a transition page
                        if (!m_pages.empty() && isTransitionPage(0)) {
                            m_overscrollTriggered = true;
                            auto it = m_transitionInfo.find(0);
                            bool isNext = (it != m_transitionInfo.end()) ? it->second.isNext : false;
                            if (m_chapterNavigateCallback) {
                                m_chapterNavigateCallback(isNext);
                            }
                        }
                    }
                } else if (unclamped < minScroll && scrollDelta < 0) {
                    // Overscrolling at the end (scrolling down past end)
                    m_overscrollAmount += -scrollDelta;
                    int lastIdx = static_cast<int>(m_pages.size()) - 1;
                    if (m_overscrollAmount > OVERSCROLL_THRESHOLD && !m_overscrollTriggered) {
                        // Check if last page is a transition page
                        if (lastIdx >= 0 && isTransitionPage(lastIdx)) {
                            m_overscrollTriggered = true;
                            auto it = m_transitionInfo.find(lastIdx);
                            bool isNext = (it != m_transitionInfo.end()) ? it->second.isNext : true;
                            if (m_chapterNavigateCallback) {
                                m_chapterNavigateCallback(isNext);
                            }
                        }
                    }
                } else {
                    // Not overscrolling - reset
                    m_overscrollAmount = 0.0f;
                    m_overscrollTriggered = false;
                }

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

                // Check if it was a tap (minimal movement)
                float dx = status.position.x - m_touchStart.x;
                float dy = status.position.y - m_touchStart.y;
                float distance = std::sqrt(dx * dx + dy * dy);

                if (distance < TAP_THRESHOLD) {
                    m_scrollVelocity = 0.0f;

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
                        // Regular tap (including on transition pages) - toggle controls
                        if (m_tapCallback) {
                            m_tapCallback();
                        }
                    }
                }

                // Reset overscroll state when finger lifts
                m_overscrollAmount = 0.0f;
                m_overscrollTriggered = false;
                // Otherwise, momentum will be applied in onFrame()
            }
        }, brls::PanAxis::ANY));
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
        contentPos = tapY - m_scrollY;
    }

    // Subtract the view origin (since tap coords start from view origin)
    // The draw function uses x + m_scrollY or y + m_scrollY as starting offset

    float offset = 0.0f;
    for (int i = 0; i < static_cast<int>(m_pageHeights.size()); i++) {
        float pageSize = getEffectivePageSize(i);
        if (contentPos >= offset && contentPos < offset + pageSize) {
            return i;
        }
        offset += pageSize + m_pageGap;
    }
    return -1;
}

void WebtoonScrollView::drawTransitionPage(NVGcontext* vg, int pageIndex,
                                             float x, float y, float width, float height) {
    // Dark background for transition
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGBA(20, 20, 30, 255));
    nvgFill(vg);

    // Horizontal divider line
    float lineY = y + height * 0.35f;
    nvgBeginPath(vg);
    nvgRect(vg, x + width * 0.2f, lineY, width * 0.6f, 1.0f);
    nvgFillColor(vg, nvgRGBA(150, 150, 150, 100));
    nvgFill(vg);

    auto it = m_transitionInfo.find(pageIndex);
    if (it != m_transitionInfo.end()) {
        float fontSize1 = 20.0f;
        float fontSize2 = 16.0f;

        // Line 1 (above divider)
        if (!it->second.line1.empty()) {
            nvgFontSize(vg, fontSize1);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
            nvgFillColor(vg, nvgRGBA(220, 220, 220, 255));
            nvgText(vg, x + width * 0.5f, lineY - 8.0f, it->second.line1.c_str(), nullptr);
        }

        // Line 2 (below divider)
        if (!it->second.line2.empty()) {
            nvgFontSize(vg, fontSize2);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            nvgFillColor(vg, nvgRGBA(180, 180, 180, 255));
            nvgText(vg, x + width * 0.5f, lineY + 12.0f, it->second.line2.c_str(), nullptr);
        }

        // Scroll hint
        nvgFontSize(vg, 14.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
        nvgFillColor(vg, nvgRGBA(120, 120, 120, 200));
        nvgText(vg, x + width * 0.5f, y + height - 12.0f, "Keep scrolling to continue", nullptr);
    } else {
        // Fallback text if no transition info set
        nvgFontSize(vg, 18.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGBA(180, 180, 180, 255));
        nvgText(vg, x + width * 0.5f, y + height * 0.5f, "Chapter transition", nullptr);
    }
}

void WebtoonScrollView::drawFailedPage(NVGcontext* vg, int pageIndex,
                                        float x, float y, float width, float height) {
    // Dark red-tinted background
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGBA(40, 20, 20, 255));
    nvgFill(vg);

    float centerX = x + width * 0.5f;
    float centerY = y + height * 0.5f;

    // Error message
    nvgFontSize(vg, 16.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgFillColor(vg, nvgRGBA(200, 120, 120, 255));
    nvgText(vg, centerX, centerY - 4.0f, "Failed to load page", nullptr);

    // Retry prompt
    nvgFontSize(vg, 14.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(100, 200, 180, 255));
    nvgText(vg, centerX, centerY + 4.0f, "Tap to retry", nullptr);
}

void WebtoonScrollView::setPages(const std::vector<Page>& pages, float screenWidth) {
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

    // Reset scroll
    m_scrollY = 0.0f;
    m_scrollVelocity = 0.0f;
    m_overscrollAmount = 0.0f;
    m_overscrollTriggered = false;
    m_currentPage = 0;
    m_loadedPages.clear();
    m_loadingPages.clear();
    m_failedPages.clear();
    m_transitionInfo.clear();

    // Load initial visible images
    updateVisibleImages();

    brls::Logger::info("WebtoonScrollView: Set {} pages, totalHeight={}", pages.size(), m_totalHeight);
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
    m_scrollY = 0.0f;
    m_scrollVelocity = 0.0f;
    m_overscrollAmount = 0.0f;
    m_overscrollTriggered = false;
    m_currentPage = 0;
}

void WebtoonScrollView::appendPages(const std::vector<Page>& pages) {
    if (pages.empty()) return;

    float availableWidth = m_viewWidth - (m_sidePadding * 2);
    float defaultHeight = availableWidth * 1.5f;

    // Remove trailing transition page if present
    if (!m_pages.empty() && isTransitionPage(static_cast<int>(m_pages.size()) - 1)) {
        int lastIdx = static_cast<int>(m_pages.size()) - 1;
        float removedSize = getEffectivePageSize(lastIdx);
        m_totalHeight -= removedSize;
        if (lastIdx > 0) m_totalHeight -= m_pageGap;  // Remove gap before removed page

        m_pages.pop_back();
        m_pageImages.pop_back();
        m_pageHeights.pop_back();
        m_loadedPages.erase(lastIdx);
        m_loadingPages.erase(lastIdx);
        m_transitionInfo.erase(lastIdx);
    }

    int startIdx = static_cast<int>(m_pages.size());

    // Append new pages
    for (size_t i = 0; i < pages.size(); i++) {
        float pageHeight;

        m_pages.push_back(pages[i]);

        // Check if new page is a transition page
        const std::string& url = pages[i].imageUrl;
        bool isTransition = url.compare(0, TRANSITION_PREFIX.size(), TRANSITION_PREFIX) == 0;

        if (isTransition) {
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

        // Add gap + page height to total
        if (startIdx + static_cast<int>(i) > 0) {
            m_totalHeight += m_pageGap;
        }
        m_totalHeight += pageHeight;
    }

    // Reset overscroll since we just added content
    m_overscrollAmount = 0.0f;
    m_overscrollTriggered = false;

    // Load newly visible images
    updateVisibleImages();
    updateCurrentPage();

    brls::Logger::info("WebtoonScrollView: Appended {} pages (total now {})", pages.size(), m_pages.size());
}

void WebtoonScrollView::prependPages(const std::vector<Page>& pages) {
    if (pages.empty()) return;

    float availableWidth = m_viewWidth - (m_sidePadding * 2);
    float defaultHeight = availableWidth * 1.5f;

    // Calculate the size of the leading transition page we're about to remove
    float removedSize = 0.0f;
    bool hadLeadingTransition = false;
    if (!m_pages.empty() && isTransitionPage(0)) {
        removedSize = getEffectivePageSize(0);
        if (m_pages.size() > 1) removedSize += m_pageGap;
        hadLeadingTransition = true;
    }

    // Build new page data
    std::vector<Page> newPages;
    std::vector<std::shared_ptr<RotatableImage>> newImages;
    std::vector<float> newHeights;
    float addedHeight = 0.0f;

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
        addedHeight += pageHeight;
        if (i > 0) addedHeight += m_pageGap;
    }

    // Skip old leading transition page
    int skipOld = hadLeadingTransition ? 1 : 0;

    // Append existing pages (minus the removed transition) to new lists
    for (size_t i = skipOld; i < m_pages.size(); i++) {
        newPages.push_back(m_pages[i]);
        newImages.push_back(m_pageImages[i]);
        newHeights.push_back(m_pageHeights[i]);
    }

    // Rebuild tracking sets with shifted indices
    int shift = static_cast<int>(pages.size()) - skipOld;
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

    // Recalculate total height
    m_totalHeight = 0.0f;
    for (size_t i = 0; i < m_pageHeights.size(); i++) {
        m_totalHeight += m_pageHeights[i];
        if (i > 0) m_totalHeight += m_pageGap;
    }

    // Adjust scroll position: user's view stays in the same visual spot
    // We added content before the current view and possibly removed a transition page
    float scrollAdjust = addedHeight + (pages.empty() ? 0 : m_pageGap) - removedSize;
    m_scrollY -= scrollAdjust;

    // Adjust current page index
    m_currentPage += shift;

    // Reset overscroll
    m_overscrollAmount = 0.0f;
    m_overscrollTriggered = false;

    // Load newly visible images
    updateVisibleImages();
    updateCurrentPage();

    brls::Logger::info("WebtoonScrollView: Prepended {} pages (total now {}, scrollAdjust={:.0f})",
                        pages.size(), m_pages.size(), scrollAdjust);
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

    // Horizontal layout: recalculate total width
    float totalWidth = 0.0f;
    for (size_t i = 0; i < m_pageHeights.size(); i++) {
        totalWidth += getEffectivePageSize(static_cast<int>(i));
        if (i < m_pageHeights.size() - 1) {
            totalWidth += m_pageGap;
        }
    }
    return totalWidth;
}

void WebtoonScrollView::onFrame() {
    // Apply momentum scrolling when not touching
    if (!m_isTouching && std::abs(m_scrollVelocity) > MOMENTUM_MIN_VELOCITY) {
        m_scrollY += m_scrollVelocity;
        m_scrollVelocity *= MOMENTUM_FRICTION;

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
    }
}

float WebtoonScrollView::getPageOffset(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pageHeights.size())) {
        return 0.0f;
    }

    float offset = 0.0f;
    for (int i = 0; i < pageIndex; i++) {
        offset += getEffectivePageSize(i) + m_pageGap;
    }
    return offset;
}

bool WebtoonScrollView::isPageVisible(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pageHeights.size())) {
        return false;
    }

    float pageStart = getPageOffset(pageIndex);
    float pageSize = getEffectivePageSize(pageIndex);
    float pageEnd = pageStart + pageSize;

    if (isHorizontalLayout()) {
        float visibleLeft = -m_scrollY;
        float visibleRight = visibleLeft + m_viewWidth;
        return (pageEnd > visibleLeft && pageStart < visibleRight);
    } else {
        float visibleTop = -m_scrollY;
        float visibleBottom = visibleTop + m_viewHeight;
        return (pageEnd > visibleTop && pageStart < visibleBottom);
    }
}

void WebtoonScrollView::updateVisibleImages() {
    // Find visible pages and preload nearby pages
    int firstVisible = -1;
    int lastVisible = -1;

    for (int i = 0; i < static_cast<int>(m_pages.size()); i++) {
        if (isPageVisible(i)) {
            if (firstVisible < 0) firstVisible = i;
            lastVisible = i;
        }
    }

    if (firstVisible < 0) {
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
        int pageIndex = i;

        // Load the image
        ImageLoader::loadAsyncFullSize(page.imageUrl,
            [this, aliveWeak, pageIndex, imgPtr](RotatableImage* loadedImg) {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

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
                        m_totalHeight += (newHeight - oldHeight);
                        m_pageHeights[pageIndex] = newHeight;
                        imgPtr->setHeight(newHeight);
                    }
                } else {
                    // Image load failed - mark as failed for retry
                    m_failedPages.insert(pageIndex);

                    // Set a reasonable height for the failed page placeholder
                    float oldHeight = m_pageHeights[pageIndex];
                    if (oldHeight > FAILED_PAGE_HEIGHT * 2) {
                        m_totalHeight += (FAILED_PAGE_HEIGHT - oldHeight);
                        m_pageHeights[pageIndex] = FAILED_PAGE_HEIGHT;
                    }

                    brls::Logger::warning("WebtoonScrollView: Failed to load page {}", pageIndex);
                }

                brls::Logger::debug("WebtoonScrollView: Loaded page {}", pageIndex);
            }, img, m_alive);
    }

    // Unload images that are far from the visible area to free GPU memory
    // This is important when multiple chapters accumulate via append/prepend
    int unloadStart = std::max(0, firstVisible - UNLOAD_PAGES);
    int unloadEnd = std::min(static_cast<int>(m_pages.size()) - 1, lastVisible + UNLOAD_PAGES);

    for (int i = 0; i < static_cast<int>(m_pages.size()); i++) {
        if (i >= unloadStart && i <= unloadEnd) continue;  // Within keep range
        if (isTransitionPage(i)) continue;  // Transition pages have no images

        if (m_loadedPages.count(i) > 0 && m_pageImages[i] && m_pageImages[i]->hasImage()) {
            m_pageImages[i]->clearImage();
            m_loadedPages.erase(i);
            // Don't erase from m_loadingPages or add to m_failedPages -
            // the page will be re-loaded when it comes back into range
        }
    }
}

void WebtoonScrollView::updateCurrentPage() {
    // Find the page at the start of the visible area
    float visibleStart = -m_scrollY;

    int newCurrentPage = 0;
    float offset = 0.0f;

    for (int i = 0; i < static_cast<int>(m_pageHeights.size()); i++) {
        float pageSize = getEffectivePageSize(i);
        float pageEnd = offset + pageSize;

        if (pageEnd > visibleStart) {
            // Skip transition pages for progress reporting
            if (!isTransitionPage(i)) {
                newCurrentPage = i;
            } else if (i + 1 < static_cast<int>(m_pageHeights.size())) {
                newCurrentPage = i + 1;  // Report next real page
            }
            break;
        }

        offset = pageEnd + m_pageGap;
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

    bool horizontal = isHorizontalLayout();

    if (horizontal) {
        // Horizontal layout (for 90/270 rotation)
        float availableHeight = height - (m_sidePadding * 2);
        float pageY = y + m_sidePadding;
        int rotation = static_cast<int>(m_rotationDegrees);

        // 90° CW: original top maps to RIGHT side, so pages flow right-to-left
        // 270° CW: original top maps to LEFT side, so pages flow left-to-right
        bool rightToLeft = (rotation == 90);

        for (int i = 0; i < static_cast<int>(m_pageImages.size()); i++) {
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

            // Calculate screen X position based on layout direction
            float contentOffset = getPageOffset(i);
            float pageX_screen;
            if (rightToLeft) {
                // RTL: content start at right edge, pages flow leftward
                // screenX = x + width - scrollY - contentOffset - pageWidth
                pageX_screen = x + width - m_scrollY - contentOffset - pageWidth;
            } else {
                // LTR: content start at left edge, pages flow rightward
                pageX_screen = x + m_scrollY + contentOffset;
            }
            float pageRight = pageX_screen + pageWidth;

            // Check if page is in visible area
            if (pageRight >= x - 100 && pageX_screen <= x + width + 100) {
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
        }
    } else {
        // Vertical layout (for 0/180 rotation)
        float availableWidth = width - (m_sidePadding * 2);
        float pageX = x + m_sidePadding;
        float currentY = y + m_scrollY;

        for (int i = 0; i < static_cast<int>(m_pageImages.size()); i++) {
            float pageHeight = m_pageHeights[i];
            float pageBottom = currentY + pageHeight;

            // Check if page is in visible area
            if (pageBottom >= y - 100 && currentY <= y + height + 100) {
                if (isTransitionPage(i)) {
                    drawTransitionPage(vg, i, pageX, currentY, availableWidth, pageHeight);
                } else if (isFailedPage(i)) {
                    drawFailedPage(vg, i, pageX, currentY, availableWidth, pageHeight);
                } else {
                    RotatableImage* img = m_pageImages[i].get();
                    if (img) {
                        img->setWidth(availableWidth);
                        img->draw(vg, pageX, currentY, availableWidth, pageHeight, style, ctx);
                    }
                }
            }

            currentY = pageBottom + m_pageGap;
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
