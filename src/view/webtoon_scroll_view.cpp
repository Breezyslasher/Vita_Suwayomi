/**
 * WebtoonScrollView implementation
 * Continuous vertical scrolling for webtoon reading
 */

#include "view/webtoon_scroll_view.hpp"
#include "utils/image_loader.hpp"
#include <cmath>

namespace vitasuwayomi {

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
    this->setHideHighlight(true);  // No focus border in full-screen reader

    // Pan gesture for scrolling (ANY axis to support rotated views)
    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
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

                // Calculate scroll delta based on rotation
                // scrollY: 0 = beginning (top/left), negative = scrolled towards end
                // 0°: Normal vertical scrolling - swipe up (negative dy) to scroll down
                // 90°: Horizontal scrolling - swipe left (negative dx) to scroll down (forward)
                // 180°: Inverted vertical scrolling - swipe down (positive dy) to scroll down
                // 270°: Horizontal scrolling - swipe right (positive dx) to scroll down (forward)
                float scrollDelta = 0.0f;
                int rotation = static_cast<int>(m_rotationDegrees);

                if (rotation == 0) {
                    scrollDelta = rawDy;
                } else if (rotation == 90) {
                    // Swipe left (negative dx) should scroll forward (decrease scrollY)
                    scrollDelta = rawDx;
                } else if (rotation == 180) {
                    scrollDelta = -rawDy;
                } else if (rotation == 270) {
                    // Swipe right (positive dx) should scroll forward (decrease scrollY)
                    scrollDelta = -rawDx;
                }

                // Update scroll position
                float desiredScrollY = m_scrollY + scrollDelta;

                // Clamp scroll position based on layout direction
                float maxScroll = 0.0f;
                bool horizontal = (rotation == 90 || rotation == 270);
                float viewSize = horizontal ? m_viewWidth : m_viewHeight;
                float totalContentSize = getTotalContentSize();
                float minScroll = -(totalContentSize - viewSize);
                if (minScroll > maxScroll) minScroll = maxScroll;

                m_scrollY = std::max(minScroll, std::min(maxScroll, desiredScrollY));

                // Track overscroll for chapter navigation (user dragging past boundary)
                if (desiredScrollY < minScroll && scrollDelta < 0) {
                    m_endOvershoot += std::abs(scrollDelta);
                } else {
                    m_endOvershoot = 0.0f;
                }
                if (desiredScrollY > maxScroll && scrollDelta > 0) {
                    m_startOvershoot += std::abs(scrollDelta);
                } else {
                    m_startOvershoot = 0.0f;
                }

                // Trigger chapter navigation on sufficient overscroll
                if (m_endOvershoot > OVERSCROLL_TRIGGER && !m_endReached) {
                    bool atLastPages = m_currentPage >= static_cast<int>(m_pages.size()) - 2;
                    bool lastPageLoaded = !m_pages.empty() &&
                        m_loadedPages.count(static_cast<int>(m_pages.size()) - 1) > 0;
                    if (atLastPages && lastPageLoaded && m_endReachedCallback) {
                        m_endReached = true;
                        m_endReachedCallback();
                    }
                }
                if (m_startOvershoot > OVERSCROLL_TRIGGER && !m_startReached) {
                    bool atFirstPages = m_currentPage <= 1;
                    bool firstPageLoaded = !m_pages.empty() && m_loadedPages.count(0) > 0;
                    if (atFirstPages && firstPageLoaded && m_startReachedCallback) {
                        m_startReached = true;
                        m_startReachedCallback();
                    }
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
                m_endOvershoot = 0.0f;
                m_startOvershoot = 0.0f;

                // Check if it was a tap (minimal movement)
                float dx = status.position.x - m_touchStart.x;
                float dy = status.position.y - m_touchStart.y;
                float distance = std::sqrt(dx * dx + dy * dy);

                if (distance < TAP_THRESHOLD) {
                    // It's a tap - toggle controls via callback
                    m_scrollVelocity = 0.0f;
                    if (m_tapCallback) {
                        m_tapCallback();
                    }
                }
                // Otherwise, momentum will be applied in onFrame()
            }
        }, brls::PanAxis::ANY));
}

void WebtoonScrollView::setPages(const std::vector<Page>& pages, float screenWidth) {
    clearPages();

    m_pages = pages;
    m_viewWidth = screenWidth;
    m_viewHeight = 544.0f;  // PS Vita screen height

    // Calculate available width after padding
    float availableWidth = screenWidth - (m_sidePadding * 2);

    // For webtoons, we'll estimate heights based on typical aspect ratios
    // Images will be FIT_WIDTH, so height depends on aspect ratio
    // Default to 2:3 aspect ratio if unknown (will adjust when images load)
    float defaultHeight = availableWidth * 1.5f;  // 2:3 aspect ratio

    m_totalHeight = 0.0f;
    m_pageHeights.clear();
    m_pageHeights.reserve(pages.size());

    // Calculate image rotation (swap 90/270 for webtoon mode)
    float imageRotation = m_rotationDegrees;
    if (m_rotationDegrees == 90.0f) {
        imageRotation = 270.0f;
    } else if (m_rotationDegrees == 270.0f) {
        imageRotation = 90.0f;
    }

    // Create image containers for each page
    for (size_t i = 0; i < pages.size(); i++) {
        auto pageImg = std::make_shared<RotatableImage>();
        pageImg->setWidth(availableWidth);
        pageImg->setHeight(defaultHeight);  // Will be adjusted when image loads
        pageImg->setScalingType(brls::ImageScalingType::FIT);
        pageImg->setBackgroundFillColor(m_bgColor);
        pageImg->setRotation(imageRotation);  // Apply corrected rotation for webtoon mode

        m_pageImages.push_back(pageImg);
        m_pageHeights.push_back(defaultHeight);
        m_totalHeight += defaultHeight + m_pageGap;
    }

    // Remove trailing gap
    if (!pages.empty() && m_pageGap > 0) {
        m_totalHeight -= m_pageGap;
    }

    // Reset scroll
    m_scrollY = 0.0f;
    m_scrollVelocity = 0.0f;
    m_currentPage = 0;
    m_loadedPages.clear();
    m_loadingPages.clear();

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
    m_totalHeight = 0.0f;
    m_scrollY = 0.0f;
    m_scrollVelocity = 0.0f;
    m_currentPage = 0;
    m_endReached = false;
    m_startReached = false;
    m_endOvershoot = 0.0f;
    m_startOvershoot = 0.0f;
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

    // For webtoon mode with horizontal layout (90°/270°), swap rotation
    float imageRotation = m_rotationDegrees;
    if (m_rotationDegrees == 90.0f) {
        imageRotation = 270.0f;
    } else if (m_rotationDegrees == 270.0f) {
        imageRotation = 90.0f;
    }

    // Apply rotation to all existing page images
    for (auto& img : m_pageImages) {
        if (img) {
            img->setRotation(imageRotation);
        }
    }

    brls::Logger::debug("WebtoonScrollView: setRotation({}) -> {} degrees (image: {} degrees)", degrees, m_rotationDegrees, imageRotation);
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
    // m_pageHeights was calculated as availableWidth * aspectRatio
    // For horizontal mode, we need availableHeight * aspectRatio
    // So: pageWidth = m_pageHeights[i] * (availableHeight / availableWidth)
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

        // Check boundary hits and trigger chapter navigation callbacks
        // Safety: only trigger if actually at the first/last pages AND those pages are loaded
        if (m_scrollY > maxScroll) {
            // Hit start boundary (top/left) - go to previous chapter
            bool atFirstPages = m_currentPage <= 1;
            bool firstPageLoaded = !m_pages.empty() && m_loadedPages.count(0) > 0;
            if (!m_startReached && m_startReachedCallback && atFirstPages && firstPageLoaded) {
                m_startReached = true;
                m_startReachedCallback();
            }
            m_scrollY = maxScroll;
            m_scrollVelocity = 0.0f;
        } else if (m_scrollY < minScroll) {
            // Hit end boundary (bottom/right) - go to next chapter
            bool atLastPages = m_currentPage >= static_cast<int>(m_pages.size()) - 2;
            bool lastPageLoaded = !m_pages.empty() &&
                m_loadedPages.count(static_cast<int>(m_pages.size()) - 1) > 0;
            if (!m_endReached && m_endReachedCallback && atLastPages && lastPageLoaded) {
                m_endReached = true;
                m_endReachedCallback();
            }
            m_scrollY = minScroll;
            m_scrollVelocity = 0.0f;
        } else {
            // Reset boundary flags when scrolled away from edges
            if (m_scrollY < maxScroll - 100.0f) {
                m_startReached = false;
            }
            if (m_scrollY > minScroll + 100.0f) {
                m_endReached = false;
            }
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
        // Horizontal layout: check X position
        float visibleLeft = -m_scrollY;  // scrollY acts as scrollX
        float visibleRight = visibleLeft + m_viewWidth;

        // Check if page overlaps with visible area
        return (pageEnd > visibleLeft && pageStart < visibleRight);
    } else {
        // Vertical layout: check Y position
        float visibleTop = -m_scrollY;
        float visibleBottom = visibleTop + m_viewHeight;

        // Check if page overlaps with visible area
        return (pageEnd > visibleTop && pageStart < visibleBottom);
    }
}

void WebtoonScrollView::updateVisibleImages() {
    int size = static_cast<int>(m_pages.size());
    if (size == 0) return;

    int firstVisible = -1;
    int lastVisible = -1;

    // Single-pass O(n) visibility check instead of calling getPageOffset() per page
    float offset = 0.0f;
    float viewStart = -m_scrollY;
    bool horizontal = isHorizontalLayout();
    float viewSize = horizontal ? m_viewWidth : m_viewHeight;
    float viewEnd = viewStart + viewSize;

    for (int i = 0; i < size; i++) {
        float pageSize = getEffectivePageSize(i);
        float pageEnd = offset + pageSize;

        if (pageEnd > viewStart && offset < viewEnd) {
            if (firstVisible < 0) firstVisible = i;
            lastVisible = i;
        }

        offset = pageEnd + m_pageGap;

        // Early exit once we've passed the visible area
        if (offset > viewEnd && firstVisible >= 0) break;
    }

    if (firstVisible < 0) {
        return;  // No visible pages
    }

    // Expand range for preloading
    int loadStart = std::max(0, firstVisible - PRELOAD_PAGES);
    int loadEnd = std::min(size - 1, lastVisible + PRELOAD_PAGES);

    // Capture alive flag for async callbacks
    std::weak_ptr<bool> aliveWeak = m_alive;

    // Load pages in range
    for (int i = loadStart; i <= loadEnd; i++) {
        if (m_loadedPages.count(i) > 0 || m_loadingPages.count(i) > 0) {
            continue;  // Already loaded or loading
        }

        m_loadingPages.insert(i);

        const Page& page = m_pages[i];
        // Capture shared_ptr so the RotatableImage stays alive until the image
        // loader's brls::sync callback completes (it calls target->setImageFromMem
        // before our callback, so the object must outlive the entire brls::sync lambda).
        std::shared_ptr<RotatableImage> imgPtr = m_pageImages[i];
        RotatableImage* img = imgPtr.get();
        int pageIndex = i;

        // Load the whole image
        ImageLoader::loadAsyncFullSize(page.imageUrl,
            [this, aliveWeak, pageIndex, imgPtr](RotatableImage* loadedImg) {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;

                m_loadingPages.erase(pageIndex);

                if (imgPtr->hasImage()) {
                    // Success - mark as loaded and update dimensions
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

                        // Clamp scroll position after height change to prevent
                        // false boundary triggers from estimated-to-actual height shifts
                        float viewSize = isHorizontalLayout() ? m_viewWidth : m_viewHeight;
                        float minScroll = -(m_totalHeight - viewSize);
                        if (minScroll > 0.0f) minScroll = 0.0f;
                        if (m_scrollY < minScroll) {
                            m_scrollY = minScroll;
                        }
                    }

                    brls::Logger::debug("WebtoonScrollView: Loaded page {}", pageIndex);
                } else {
                    // Load failed - leave out of both sets so it will be retried
                    brls::Logger::warning("WebtoonScrollView: Failed to load page {}, will retry", pageIndex);
                }
            }, img, m_alive);
    }

    // Unload textures for distant pages to save GPU memory
    unloadDistantPages(firstVisible, lastVisible);
}

void WebtoonScrollView::unloadDistantPages(int firstVisible, int lastVisible) {
    int size = static_cast<int>(m_pageImages.size());
    int keepStart = std::max(0, firstVisible - PRELOAD_PAGES - UNLOAD_DISTANCE);
    int keepEnd = std::min(size - 1, lastVisible + PRELOAD_PAGES + UNLOAD_DISTANCE);

    // Find pages to unload (outside the keep range)
    std::vector<int> toUnload;
    for (int page : m_loadedPages) {
        if (page < keepStart || page > keepEnd) {
            toUnload.push_back(page);
        }
    }

    // Unload - clear GPU texture but keep height data for scroll calculations
    for (int page : toUnload) {
        if (page >= 0 && page < size) {
            m_pageImages[page]->clearImage();
        }
        m_loadedPages.erase(page);
    }

    if (!toUnload.empty()) {
        brls::Logger::debug("WebtoonScrollView: Unloaded {} distant pages", toUnload.size());
    }
}

void WebtoonScrollView::updateCurrentPage() {
    // Find the page at the start of the visible area
    // (top for vertical layout, left for horizontal layout)
    float visibleStart = -m_scrollY;

    int newCurrentPage = 0;
    float offset = 0.0f;

    for (int i = 0; i < static_cast<int>(m_pageHeights.size()); i++) {
        float pageSize = getEffectivePageSize(i);
        float pageEnd = offset + pageSize;

        if (pageEnd > visibleStart) {
            newCurrentPage = i;
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
        // Pages are laid out left to right, scrollY acts as scrollX
        float availableHeight = height - (m_sidePadding * 2);
        float pageY = y + m_sidePadding;

        // Draw visible pages horizontally
        float currentX = x + m_scrollY;  // scrollY is used as horizontal offset

        for (int i = 0; i < static_cast<int>(m_pageImages.size()); i++) {
            RotatableImage* img = m_pageImages[i].get();

            // Calculate the correct page width for the rotated image
            // When rotated 90/270, the effective aspect ratio is imageHeight/imageWidth
            // The page width should fill the available height: availableHeight * (imageH/imageW)
            float pageWidth;
            if (img && img->hasImage() && img->getImageWidth() > 0 && img->getImageHeight() > 0) {
                // For 90/270 rotation, the rotated aspect ratio is height/width
                float rotatedAspect = static_cast<float>(img->getImageHeight()) / static_cast<float>(img->getImageWidth());
                pageWidth = availableHeight * rotatedAspect;
            } else {
                // Fallback: use stored height scaled by aspect ratio
                // m_pageHeights was calculated as availableWidth * aspectRatio
                // For horizontal, we need availableHeight * aspectRatio
                float availableWidth = width - (m_sidePadding * 2);
                if (availableWidth > 0) {
                    pageWidth = m_pageHeights[i] * (availableHeight / availableWidth);
                } else {
                    pageWidth = availableHeight * 1.5f;  // Default 2:3 aspect ratio
                }
            }

            float pageRight = currentX + pageWidth;

            // Check if page is in visible area (with some margin for smooth scrolling)
            if (pageRight >= x - 100 && currentX <= x + width + 100) {
                // Draw this page
                if (img) {
                    // Update image size for horizontal layout
                    img->setWidth(pageWidth);
                    img->setHeight(availableHeight);

                    // Draw the image
                    img->draw(vg, currentX, pageY, pageWidth, availableHeight, style, ctx);
                }
            }

            currentX = pageRight + m_pageGap;
        }
    } else {
        // Vertical layout (for 0/180 rotation)
        // Calculate the X position for centered pages
        float availableWidth = width - (m_sidePadding * 2);
        float pageX = x + m_sidePadding;

        // Draw visible pages vertically
        float currentY = y + m_scrollY;  // Start position adjusted by scroll

        for (int i = 0; i < static_cast<int>(m_pageImages.size()); i++) {
            float pageHeight = m_pageHeights[i];
            float pageBottom = currentY + pageHeight;

            // Check if page is in visible area (with some margin for smooth scrolling)
            if (pageBottom >= y - 100 && currentY <= y + height + 100) {
                // Draw this page
                RotatableImage* img = m_pageImages[i].get();
                if (img) {
                    // Update image width in case view resized
                    img->setWidth(availableWidth);

                    // Draw the image
                    img->draw(vg, pageX, currentY, availableWidth, pageHeight, style, ctx);
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
