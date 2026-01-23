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
                m_scrollY += scrollDelta;

                // Clamp scroll position based on layout direction
                float maxScroll = 0.0f;
                bool horizontal = (rotation == 90 || rotation == 270);
                float viewSize = horizontal ? m_viewWidth : m_viewHeight;
                float totalContentSize = getTotalContentSize();
                float minScroll = -(totalContentSize - viewSize);
                if (minScroll > maxScroll) minScroll = maxScroll;

                m_scrollY = std::max(minScroll, std::min(maxScroll, m_scrollY));

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

    // Calculate image rotation (flipped for 90°/270° to show beginning on left)
    float imageRotation = m_rotationDegrees;
    if (m_rotationDegrees == 90.0f) {
        imageRotation = 270.0f;
    } else if (m_rotationDegrees == 270.0f) {
        imageRotation = 90.0f;
    }

    // Create image containers for each page
    for (size_t i = 0; i < pages.size(); i++) {
        auto* pageImg = new RotatableImage();
        pageImg->setWidth(availableWidth);
        pageImg->setHeight(defaultHeight);  // Will be adjusted when image loads
        pageImg->setScalingType(brls::ImageScalingType::FIT);
        pageImg->setBackgroundFillColor(nvgRGBA(26, 26, 46, 255));
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
    for (auto* img : m_pageImages) {
        delete img;
    }
    m_pageImages.clear();
    m_pageHeights.clear();
    m_pages.clear();
    m_loadedPages.clear();
    m_loadingPages.clear();
    m_totalHeight = 0.0f;
    m_scrollY = 0.0f;
    m_scrollVelocity = 0.0f;
    m_currentPage = 0;
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

    // For webtoon mode with horizontal layout (90°/270°), we need to flip the rotation direction
    // so that the "beginning" (top) of the original vertical content appears on the left
    // where horizontal reading starts.
    // - 90° rotation: use -90° (270°) so original TOP -> LEFT
    // - 270° rotation: use -270° (90°) so original TOP -> RIGHT
    float imageRotation = m_rotationDegrees;
    if (m_rotationDegrees == 90.0f) {
        imageRotation = 270.0f;  // Flip to counter-clockwise
    } else if (m_rotationDegrees == 270.0f) {
        imageRotation = 90.0f;   // Flip to clockwise
    }

    // Apply rotation to all existing page images
    for (auto* img : m_pageImages) {
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

    // Load pages in range
    for (int i = loadStart; i <= loadEnd; i++) {
        if (m_loadedPages.count(i) > 0 || m_loadingPages.count(i) > 0) {
            continue;  // Already loaded or loading
        }

        m_loadingPages.insert(i);

        const Page& page = m_pages[i];
        RotatableImage* img = m_pageImages[i];
        int pageIndex = i;

        // Load the image
        if (page.totalSegments > 1) {
            // Segmented webtoon page
            ImageLoader::loadAsyncFullSizeSegment(
                page.imageUrl, page.segment, page.totalSegments,
                [this, pageIndex, img](RotatableImage* loadedImg) {
                    m_loadingPages.erase(pageIndex);
                    m_loadedPages.insert(pageIndex);

                    // Update page height based on actual image dimensions
                    if (img->hasImage()) {
                        float imageWidth = static_cast<float>(img->getImageWidth());
                        float imageHeight = static_cast<float>(img->getImageHeight());

                        if (imageWidth > 0 && imageHeight > 0) {
                            float availableWidth = m_viewWidth - (m_sidePadding * 2);
                            float aspectRatio = imageHeight / imageWidth;
                            float newHeight = availableWidth * aspectRatio;

                            // Update total height
                            float oldHeight = m_pageHeights[pageIndex];
                            m_totalHeight += (newHeight - oldHeight);
                            m_pageHeights[pageIndex] = newHeight;
                            img->setHeight(newHeight);
                        }
                    }

                    brls::Logger::debug("WebtoonScrollView: Loaded segment page {}", pageIndex);
                }, img);
        } else {
            // Regular page
            ImageLoader::loadAsyncFullSize(page.imageUrl,
                [this, pageIndex, img](RotatableImage* loadedImg) {
                    m_loadingPages.erase(pageIndex);
                    m_loadedPages.insert(pageIndex);

                    // Update page height based on actual image dimensions
                    if (img->hasImage()) {
                        float imageWidth = static_cast<float>(img->getImageWidth());
                        float imageHeight = static_cast<float>(img->getImageHeight());

                        if (imageWidth > 0 && imageHeight > 0) {
                            float availableWidth = m_viewWidth - (m_sidePadding * 2);
                            float aspectRatio = imageHeight / imageWidth;
                            float newHeight = availableWidth * aspectRatio;

                            // Update total height
                            float oldHeight = m_pageHeights[pageIndex];
                            m_totalHeight += (newHeight - oldHeight);
                            m_pageHeights[pageIndex] = newHeight;
                            img->setHeight(newHeight);
                        }
                    }

                    brls::Logger::debug("WebtoonScrollView: Loaded page {}", pageIndex);
                }, img);
        }
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
    nvgFillColor(vg, nvgRGBA(26, 26, 46, 255));
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
            RotatableImage* img = m_pageImages[i];

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
                RotatableImage* img = m_pageImages[i];
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
